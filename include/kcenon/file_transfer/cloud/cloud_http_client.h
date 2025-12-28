/**
 * @file cloud_http_client.h
 * @brief Unified HTTP client adapter for cloud storage implementations
 * @version 0.1.0
 *
 * This file provides a unified HTTP client implementation that wraps
 * the network_system HTTP client for use across S3, GCS, and Azure
 * cloud storage implementations.
 */

#ifndef KCENON_FILE_TRANSFER_CLOUD_CLOUD_HTTP_CLIENT_H
#define KCENON_FILE_TRANSFER_CLOUD_CLOUD_HTTP_CLIENT_H

#include "cloud_stream_base.h"
#include "cloud_config.h"
#include "kcenon/file_transfer/core/types.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Forward declaration for network_system HTTP client
namespace kcenon::network::core {
class http_client;
}

namespace kcenon::file_transfer {

/**
 * @brief Unified HTTP client for cloud storage operations
 *
 * This class wraps the network_system HTTP client and provides a common
 * interface for all cloud storage implementations. It handles:
 * - Request/response conversion
 * - Error handling standardization
 * - Timeout management
 *
 * @note This client is thread-safe for concurrent operations.
 */
class cloud_http_client : public http_client_interface_base {
public:
    /**
     * @brief Construct HTTP client with timeout
     * @param timeout Request timeout duration
     */
    explicit cloud_http_client(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));

    ~cloud_http_client() override;

    cloud_http_client(const cloud_http_client&) = delete;
    auto operator=(const cloud_http_client&) -> cloud_http_client& = delete;
    cloud_http_client(cloud_http_client&&) noexcept;
    auto operator=(cloud_http_client&&) noexcept -> cloud_http_client&;

    // ========================================================================
    // HTTP Operations
    // ========================================================================

    /**
     * @brief Execute GET request
     */
    [[nodiscard]] auto get(
        const std::string& url,
        const std::map<std::string, std::string>& query,
        const std::map<std::string, std::string>& headers)
        -> result<http_response_base> override;

    /**
     * @brief Execute POST request with string body
     */
    [[nodiscard]] auto post(
        const std::string& url,
        const std::string& body,
        const std::map<std::string, std::string>& headers)
        -> result<http_response_base> override;

    /**
     * @brief Execute POST request with binary body
     */
    [[nodiscard]] auto post(
        const std::string& url,
        const std::vector<uint8_t>& body,
        const std::map<std::string, std::string>& headers)
        -> result<http_response_base> override;

    /**
     * @brief Execute PUT request with string body
     */
    [[nodiscard]] auto put(
        const std::string& url,
        const std::string& body,
        const std::map<std::string, std::string>& headers)
        -> result<http_response_base> override;

    /**
     * @brief Execute PUT request with binary body
     */
    [[nodiscard]] auto put(
        const std::string& url,
        const std::vector<uint8_t>& body,
        const std::map<std::string, std::string>& headers)
        -> result<http_response_base>;

    /**
     * @brief Execute DELETE request
     */
    [[nodiscard]] auto del(
        const std::string& url,
        const std::map<std::string, std::string>& headers)
        -> result<http_response_base> override;

    /**
     * @brief Execute HEAD request
     */
    [[nodiscard]] auto head(
        const std::string& url,
        const std::map<std::string, std::string>& headers)
        -> result<http_response_base> override;

    // ========================================================================
    // Retry Logic
    // ========================================================================

    /**
     * @brief Execute request with retry policy
     * @tparam RequestFunc Function type for the request
     * @param request_func The request function to execute
     * @param policy Retry policy configuration
     * @return Result of the request
     */
    template <typename RequestFunc>
    [[nodiscard]] auto execute_with_retry(
        RequestFunc&& request_func,
        const cloud_retry_policy& policy) -> decltype(request_func()) {
        std::size_t attempt = 0;

        while (true) {
            ++attempt;
            auto result = request_func();

            if (result.has_value()) {
                return result;
            }

            if (attempt >= policy.max_attempts) {
                return result;
            }

            // Check if error is retryable
            // For now, retry on any error up to max_retries
            auto delay = calculate_retry_delay(policy, attempt);
            std::this_thread::sleep_for(delay);
        }
    }

    /**
     * @brief Check if the HTTP client is available
     * @return true if network system is available, false otherwise
     */
    [[nodiscard]] auto is_available() const noexcept -> bool;

private:
    struct impl;
    std::unique_ptr<impl> impl_;

    /**
     * @brief Calculate retry delay with exponential backoff
     */
    static auto calculate_retry_delay(
        const cloud_retry_policy& policy,
        std::size_t attempt) -> std::chrono::milliseconds;
};

/**
 * @brief Factory function to create cloud HTTP client
 * @param timeout Request timeout
 * @return Shared pointer to cloud HTTP client
 */
[[nodiscard]] auto make_cloud_http_client(
    std::chrono::milliseconds timeout = std::chrono::milliseconds(30000))
    -> std::shared_ptr<cloud_http_client>;

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CLOUD_CLOUD_HTTP_CLIENT_H
