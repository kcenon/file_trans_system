/**
 * @file cloud_stream_base.h
 * @brief Base classes for cloud storage upload/download streams
 * @version 0.1.0
 *
 * This file provides abstract base classes for upload and download streams
 * used across S3, GCS, and Azure cloud storage implementations to reduce
 * code duplication.
 */

#ifndef KCENON_FILE_TRANSFER_CLOUD_CLOUD_STREAM_BASE_H
#define KCENON_FILE_TRANSFER_CLOUD_CLOUD_STREAM_BASE_H

#include "cloud_config.h"
#include "cloud_credentials.h"
#include "cloud_error.h"
#include "kcenon/file_transfer/core/types.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace kcenon::file_transfer {

// ============================================================================
// Upload Stream Base
// ============================================================================

/**
 * @brief Base implementation for multipart upload streams
 *
 * This class provides common functionality for managing concurrent multipart
 * uploads across different cloud providers. Each provider-specific stream
 * inherits from this base and implements the abstract methods.
 */
template <typename PartResult>
class upload_stream_base {
public:
    /**
     * @brief Pending part upload information
     */
    struct pending_part {
        int part_number;
        std::future<result<PartResult>> future;
    };

    virtual ~upload_stream_base() = default;

    /**
     * @brief Get total bytes written to stream
     */
    [[nodiscard]] auto bytes_written() const noexcept -> uint64_t {
        return bytes_written_;
    }

    /**
     * @brief Check if stream has been finalized
     */
    [[nodiscard]] auto is_finalized() const noexcept -> bool {
        return finalized_;
    }

    /**
     * @brief Check if stream has been aborted
     */
    [[nodiscard]] auto is_aborted() const noexcept -> bool {
        return aborted_;
    }

protected:
    upload_stream_base() = default;

    /**
     * @brief Initialize with buffer size
     */
    void init_buffer(std::size_t size) {
        part_buffer_.reserve(size);
    }

    /**
     * @brief Get number of active (in-progress) uploads
     */
    [[nodiscard]] auto get_active_upload_count() -> std::size_t {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        std::size_t count = 0;
        for (auto& pending : pending_uploads_) {
            if (pending.future.valid()) {
                auto status = pending.future.wait_for(std::chrono::milliseconds(0));
                if (status != std::future_status::ready) {
                    ++count;
                }
            }
        }
        return count;
    }

    /**
     * @brief Wait for an upload slot to become available
     * @param max_concurrent Maximum concurrent uploads allowed
     */
    auto wait_for_slot(std::size_t max_concurrent) -> result<void> {
        while (get_active_upload_count() >= max_concurrent) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            auto collect_result = collect_completed_uploads();
            if (!collect_result.has_value()) {
                return unexpected{collect_result.error()};
            }
        }
        return result<void>{};
    }

    /**
     * @brief Collect results from completed uploads
     *
     * This method must be overridden to handle provider-specific
     * result processing (e.g., storing ETags for S3).
     */
    virtual auto collect_completed_uploads() -> result<void> = 0;

    /**
     * @brief Wait for all pending uploads to complete
     */
    virtual auto wait_all_uploads() -> result<void> = 0;

    /**
     * @brief Add a pending upload to track
     */
    void add_pending_upload(int part_number, std::future<result<PartResult>>&& future) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_uploads_.push_back({part_number, std::move(future)});
    }

    // Member variables accessible to derived classes
    std::vector<std::byte> part_buffer_;
    std::vector<pending_part> pending_uploads_;
    std::mutex pending_mutex_;

    uint64_t bytes_written_ = 0;
    bool finalized_ = false;
    bool aborted_ = false;
    bool initialized_ = false;
};

// ============================================================================
// Download Stream Base
// ============================================================================

/**
 * @brief Base implementation for download streams
 *
 * This class provides common functionality for managing streaming downloads
 * across different cloud providers.
 */
class download_stream_base {
public:
    virtual ~download_stream_base() = default;

    /**
     * @brief Get total bytes read from stream
     */
    [[nodiscard]] auto bytes_read() const noexcept -> uint64_t {
        return bytes_read_;
    }

    /**
     * @brief Get total content length (if known)
     */
    [[nodiscard]] auto content_length() const noexcept -> std::optional<uint64_t> {
        return content_length_;
    }

    /**
     * @brief Check if end of stream reached
     */
    [[nodiscard]] auto is_eof() const noexcept -> bool {
        return eof_;
    }

    /**
     * @brief Check if download has been aborted
     */
    [[nodiscard]] auto is_aborted() const noexcept -> bool {
        return aborted_;
    }

protected:
    download_stream_base() = default;

    uint64_t bytes_read_ = 0;
    std::optional<uint64_t> content_length_;
    bool eof_ = false;
    bool aborted_ = false;
};

// ============================================================================
// HTTP Response Base
// ============================================================================

/**
 * @brief Base HTTP response structure used by cloud providers
 *
 * Provides a common interface for HTTP responses across different
 * cloud storage implementations.
 */
struct http_response_base {
    /// HTTP status code
    int status_code = 0;

    /// Response headers
    std::map<std::string, std::string> headers;

    /// Response body
    std::vector<uint8_t> body;

    /**
     * @brief Get body as string
     */
    [[nodiscard]] auto get_body_string() const -> std::string {
        return std::string(body.begin(), body.end());
    }

    /**
     * @brief Get header value by key (case-insensitive)
     */
    [[nodiscard]] auto get_header(const std::string& key) const
        -> std::optional<std::string> {
        // Try exact match first
        auto it = headers.find(key);
        if (it != headers.end()) {
            return it->second;
        }

        // Try case-insensitive match
        std::string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        for (const auto& [k, v] : headers) {
            std::string lower_k = k;
            std::transform(lower_k.begin(), lower_k.end(), lower_k.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lower_k == lower_key) {
                return v;
            }
        }

        return std::nullopt;
    }

    /**
     * @brief Check if response indicates success (2xx)
     */
    [[nodiscard]] auto is_success() const noexcept -> bool {
        return status_code >= 200 && status_code < 300;
    }

    /**
     * @brief Check if response indicates client error (4xx)
     */
    [[nodiscard]] auto is_client_error() const noexcept -> bool {
        return status_code >= 400 && status_code < 500;
    }

    /**
     * @brief Check if response indicates server error (5xx)
     */
    [[nodiscard]] auto is_server_error() const noexcept -> bool {
        return status_code >= 500 && status_code < 600;
    }
};

// ============================================================================
// HTTP Client Interface Base
// ============================================================================

/**
 * @brief Base interface for HTTP clients used by cloud storage
 *
 * This interface defines the common HTTP operations needed by cloud storage
 * implementations. Provider-specific clients can inherit from this.
 */
class http_client_interface_base {
public:
    virtual ~http_client_interface_base() = default;

    /**
     * @brief Execute GET request
     */
    virtual auto get(
        const std::string& url,
        const std::map<std::string, std::string>& query,
        const std::map<std::string, std::string>& headers)
        -> result<http_response_base> = 0;

    /**
     * @brief Execute POST request with string body
     */
    virtual auto post(
        const std::string& url,
        const std::string& body,
        const std::map<std::string, std::string>& headers)
        -> result<http_response_base> = 0;

    /**
     * @brief Execute POST request with binary body
     */
    virtual auto post(
        const std::string& url,
        const std::vector<uint8_t>& body,
        const std::map<std::string, std::string>& headers)
        -> result<http_response_base> = 0;

    /**
     * @brief Execute PUT request with string body
     */
    virtual auto put(
        const std::string& url,
        const std::string& body,
        const std::map<std::string, std::string>& headers)
        -> result<http_response_base> = 0;

    /**
     * @brief Execute DELETE request
     */
    virtual auto del(
        const std::string& url,
        const std::map<std::string, std::string>& headers)
        -> result<http_response_base> = 0;

    /**
     * @brief Execute HEAD request
     */
    virtual auto head(
        const std::string& url,
        const std::map<std::string, std::string>& headers)
        -> result<http_response_base> = 0;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CLOUD_CLOUD_STREAM_BASE_H
