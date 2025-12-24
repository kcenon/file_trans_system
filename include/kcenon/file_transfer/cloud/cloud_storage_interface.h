/**
 * @file cloud_storage_interface.h
 * @brief Cloud storage abstraction layer interface
 * @version 0.1.0
 *
 * This file defines the cloud storage abstraction interface that supports
 * multiple cloud providers (AWS S3, Azure Blob, Google Cloud Storage).
 */

#ifndef KCENON_FILE_TRANSFER_CLOUD_CLOUD_STORAGE_INTERFACE_H
#define KCENON_FILE_TRANSFER_CLOUD_CLOUD_STORAGE_INTERFACE_H

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "cloud_config.h"
#include "cloud_credentials.h"
#include "cloud_error.h"
#include "kcenon/file_transfer/core/types.h"

namespace kcenon::file_transfer {

/**
 * @brief Cloud storage state enumeration
 */
enum class cloud_storage_state {
    disconnected,   ///< Not connected to cloud storage
    connecting,     ///< Connection in progress
    connected,      ///< Connected and ready
    error           ///< Error state
};

/**
 * @brief Convert cloud_storage_state to string
 */
[[nodiscard]] constexpr auto to_string(cloud_storage_state state) -> const char* {
    switch (state) {
        case cloud_storage_state::disconnected: return "disconnected";
        case cloud_storage_state::connecting: return "connecting";
        case cloud_storage_state::connected: return "connected";
        case cloud_storage_state::error: return "error";
        default: return "unknown";
    }
}

/**
 * @brief Cloud object metadata
 */
struct cloud_object_metadata {
    /// Object key (path)
    std::string key;

    /// Object size in bytes
    uint64_t size = 0;

    /// Last modified timestamp
    std::chrono::system_clock::time_point last_modified;

    /// ETag (entity tag)
    std::string etag;

    /// Content type
    std::string content_type;

    /// Content encoding
    std::optional<std::string> content_encoding;

    /// Storage class
    std::optional<std::string> storage_class;

    /// Version ID (if versioning enabled)
    std::optional<std::string> version_id;

    /// MD5 checksum
    std::optional<std::string> md5;

    /// Custom metadata
    std::vector<std::pair<std::string, std::string>> custom_metadata;

    /// Is this a directory marker
    bool is_directory = false;
};

/**
 * @brief List objects result
 */
struct list_objects_result {
    /// Objects in the result
    std::vector<cloud_object_metadata> objects;

    /// Common prefixes (for directory-like listing)
    std::vector<std::string> common_prefixes;

    /// Is the result truncated
    bool is_truncated = false;

    /// Continuation token for pagination
    std::optional<std::string> continuation_token;

    /// Total objects count (if available)
    std::optional<uint64_t> total_count;
};

/**
 * @brief List objects options
 */
struct list_objects_options {
    /// Prefix to filter objects
    std::optional<std::string> prefix;

    /// Delimiter for grouping (typically '/')
    std::optional<std::string> delimiter = "/";

    /// Maximum keys to return
    std::size_t max_keys = 1000;

    /// Continuation token for pagination
    std::optional<std::string> continuation_token;

    /// Start after this key (for pagination)
    std::optional<std::string> start_after;

    /// Fetch owner information
    bool fetch_owner = false;
};

/**
 * @brief Upload progress information
 */
struct upload_progress {
    /// Bytes uploaded so far
    uint64_t bytes_transferred = 0;

    /// Total bytes to upload
    uint64_t total_bytes = 0;

    /// Current upload speed (bytes per second)
    uint64_t speed_bps = 0;

    /// Upload ID (for multipart uploads)
    std::optional<std::string> upload_id;

    /// Current part number (for multipart uploads)
    std::optional<std::size_t> current_part;

    /// Total parts (for multipart uploads)
    std::optional<std::size_t> total_parts;

    [[nodiscard]] auto percentage() const -> double {
        if (total_bytes == 0) return 0.0;
        return static_cast<double>(bytes_transferred) /
               static_cast<double>(total_bytes) * 100.0;
    }
};

/**
 * @brief Download progress information
 */
struct download_progress {
    /// Bytes downloaded so far
    uint64_t bytes_transferred = 0;

    /// Total bytes to download
    uint64_t total_bytes = 0;

    /// Current download speed (bytes per second)
    uint64_t speed_bps = 0;

    [[nodiscard]] auto percentage() const -> double {
        if (total_bytes == 0) return 0.0;
        return static_cast<double>(bytes_transferred) /
               static_cast<double>(total_bytes) * 100.0;
    }
};

/**
 * @brief Upload result
 */
struct upload_result {
    /// Object key
    std::string key;

    /// ETag of uploaded object
    std::string etag;

    /// Version ID (if versioning enabled)
    std::optional<std::string> version_id;

    /// Upload ID (for multipart uploads)
    std::optional<std::string> upload_id;

    /// Total bytes uploaded
    uint64_t bytes_uploaded = 0;

    /// Time taken for upload
    std::chrono::milliseconds duration{0};
};

/**
 * @brief Download result
 */
struct download_result {
    /// Object key
    std::string key;

    /// Total bytes downloaded
    uint64_t bytes_downloaded = 0;

    /// Object metadata
    cloud_object_metadata metadata;

    /// Time taken for download
    std::chrono::milliseconds duration{0};
};

/**
 * @brief Delete result
 */
struct delete_result {
    /// Object key
    std::string key;

    /// Version ID (if versioning enabled)
    std::optional<std::string> version_id;

    /// Delete marker (for versioned buckets)
    bool delete_marker = false;
};

/**
 * @brief Presigned URL options
 */
struct presigned_url_options {
    /// URL expiration duration
    std::chrono::seconds expiration{3600};

    /// HTTP method (GET, PUT)
    std::string method = "GET";

    /// Content type (for PUT)
    std::optional<std::string> content_type;

    /// Content MD5 (for PUT)
    std::optional<std::string> content_md5;
};

/**
 * @brief Cloud storage statistics
 */
struct cloud_storage_statistics {
    uint64_t bytes_uploaded = 0;       ///< Total bytes uploaded
    uint64_t bytes_downloaded = 0;     ///< Total bytes downloaded
    uint64_t upload_count = 0;         ///< Number of upload operations
    uint64_t download_count = 0;       ///< Number of download operations
    uint64_t list_count = 0;           ///< Number of list operations
    uint64_t delete_count = 0;         ///< Number of delete operations
    uint64_t errors = 0;               ///< Total errors
    std::chrono::steady_clock::time_point connected_at;  ///< Connection time
};

/**
 * @brief Streaming upload context for large file processing
 *
 * Allows uploading large files in chunks without loading
 * the entire file into memory.
 */
class cloud_upload_stream {
public:
    virtual ~cloud_upload_stream() = default;

    /**
     * @brief Write data chunk to the stream
     * @param data Data chunk to write
     * @return Result indicating success or error
     */
    [[nodiscard]] virtual auto write(
        std::span<const std::byte> data) -> result<std::size_t> = 0;

    /**
     * @brief Finalize the upload
     * @return Result containing upload result or error
     */
    [[nodiscard]] virtual auto finalize() -> result<upload_result> = 0;

    /**
     * @brief Abort the upload
     * @return Result indicating success or error
     */
    [[nodiscard]] virtual auto abort() -> result<void> = 0;

    /**
     * @brief Get bytes written so far
     */
    [[nodiscard]] virtual auto bytes_written() const -> uint64_t = 0;

    /**
     * @brief Get upload ID (for multipart uploads)
     */
    [[nodiscard]] virtual auto upload_id() const -> std::optional<std::string> = 0;
};

/**
 * @brief Streaming download context for large file processing
 *
 * Allows downloading large files in chunks without loading
 * the entire file into memory.
 */
class cloud_download_stream {
public:
    virtual ~cloud_download_stream() = default;

    /**
     * @brief Read data chunk from the stream
     * @param buffer Buffer to read into
     * @return Result containing bytes read or error
     */
    [[nodiscard]] virtual auto read(
        std::span<std::byte> buffer) -> result<std::size_t> = 0;

    /**
     * @brief Check if stream has more data
     */
    [[nodiscard]] virtual auto has_more() const -> bool = 0;

    /**
     * @brief Get bytes read so far
     */
    [[nodiscard]] virtual auto bytes_read() const -> uint64_t = 0;

    /**
     * @brief Get total size to download
     */
    [[nodiscard]] virtual auto total_size() const -> uint64_t = 0;

    /**
     * @brief Get object metadata
     */
    [[nodiscard]] virtual auto metadata() const -> const cloud_object_metadata& = 0;
};

/**
 * @brief Cloud storage interface base class
 *
 * Provides an abstraction layer for different cloud storage providers
 * (AWS S3, Azure Blob, Google Cloud Storage).
 * All implementations must support both synchronous and asynchronous operations.
 *
 * @code
 * // Example usage with S3
 * auto storage = s3_storage::create(s3_config{}, credentials);
 * if (storage) {
 *     auto result = storage->connect();
 *     if (result.has_value()) {
 *         // Upload a file
 *         auto upload = storage->upload_file("local/file.txt", "remote/file.txt");
 *         if (upload.has_value()) {
 *             // File uploaded successfully
 *         }
 *     }
 * }
 * @endcode
 */
class cloud_storage_interface {
public:
    virtual ~cloud_storage_interface() = default;

    // Non-copyable
    cloud_storage_interface(const cloud_storage_interface&) = delete;
    auto operator=(const cloud_storage_interface&) -> cloud_storage_interface& = delete;

    // Movable
    cloud_storage_interface(cloud_storage_interface&&) noexcept = default;
    auto operator=(cloud_storage_interface&&) noexcept -> cloud_storage_interface& = default;

    /**
     * @brief Get the cloud provider type
     * @return Cloud provider type
     */
    [[nodiscard]] virtual auto provider() const -> cloud_provider = 0;

    /**
     * @brief Get the provider name as string
     * @return Provider name (e.g., "aws-s3", "azure-blob")
     */
    [[nodiscard]] virtual auto provider_name() const -> std::string_view = 0;

    // ========================================================================
    // Connection Management
    // ========================================================================

    /**
     * @brief Connect to cloud storage (validate credentials and configuration)
     * @return Result indicating success or error
     */
    [[nodiscard]] virtual auto connect() -> result<void> = 0;

    /**
     * @brief Disconnect from cloud storage
     * @return Result indicating success or error
     */
    [[nodiscard]] virtual auto disconnect() -> result<void> = 0;

    /**
     * @brief Check if connected to cloud storage
     * @return true if connected, false otherwise
     */
    [[nodiscard]] virtual auto is_connected() const -> bool = 0;

    /**
     * @brief Get current storage state
     * @return Current state
     */
    [[nodiscard]] virtual auto state() const -> cloud_storage_state = 0;

    // ========================================================================
    // Object Operations - Synchronous
    // ========================================================================

    /**
     * @brief Upload data to cloud storage
     * @param key Object key (path)
     * @param data Data to upload
     * @param options Transfer options
     * @return Result containing upload result or error
     */
    [[nodiscard]] virtual auto upload(
        const std::string& key,
        std::span<const std::byte> data,
        const cloud_transfer_options& options = {}) -> result<upload_result> = 0;

    /**
     * @brief Upload file to cloud storage
     * @param local_path Local file path
     * @param key Object key (path)
     * @param options Transfer options
     * @return Result containing upload result or error
     */
    [[nodiscard]] virtual auto upload_file(
        const std::filesystem::path& local_path,
        const std::string& key,
        const cloud_transfer_options& options = {}) -> result<upload_result> = 0;

    /**
     * @brief Download data from cloud storage
     * @param key Object key (path)
     * @return Result containing downloaded data or error
     */
    [[nodiscard]] virtual auto download(
        const std::string& key) -> result<std::vector<std::byte>> = 0;

    /**
     * @brief Download file from cloud storage
     * @param key Object key (path)
     * @param local_path Local file path
     * @return Result containing download result or error
     */
    [[nodiscard]] virtual auto download_file(
        const std::string& key,
        const std::filesystem::path& local_path) -> result<download_result> = 0;

    /**
     * @brief Delete object from cloud storage
     * @param key Object key (path)
     * @return Result containing delete result or error
     */
    [[nodiscard]] virtual auto delete_object(
        const std::string& key) -> result<delete_result> = 0;

    /**
     * @brief Delete multiple objects from cloud storage
     * @param keys Object keys to delete
     * @return Result containing vector of delete results or error
     */
    [[nodiscard]] virtual auto delete_objects(
        const std::vector<std::string>& keys) -> result<std::vector<delete_result>> = 0;

    /**
     * @brief Check if object exists
     * @param key Object key (path)
     * @return Result containing true if exists, false otherwise
     */
    [[nodiscard]] virtual auto exists(
        const std::string& key) -> result<bool> = 0;

    /**
     * @brief Get object metadata
     * @param key Object key (path)
     * @return Result containing metadata or error
     */
    [[nodiscard]] virtual auto get_metadata(
        const std::string& key) -> result<cloud_object_metadata> = 0;

    /**
     * @brief List objects in cloud storage
     * @param options List options
     * @return Result containing list result or error
     */
    [[nodiscard]] virtual auto list_objects(
        const list_objects_options& options = {}) -> result<list_objects_result> = 0;

    /**
     * @brief Copy object within cloud storage
     * @param source_key Source object key
     * @param dest_key Destination object key
     * @param options Transfer options for destination
     * @return Result indicating success or error
     */
    [[nodiscard]] virtual auto copy_object(
        const std::string& source_key,
        const std::string& dest_key,
        const cloud_transfer_options& options = {}) -> result<cloud_object_metadata> = 0;

    // ========================================================================
    // Object Operations - Asynchronous
    // ========================================================================

    /**
     * @brief Upload data to cloud storage (asynchronous)
     * @param key Object key (path)
     * @param data Data to upload
     * @param options Transfer options
     * @return Future containing upload result or error
     */
    [[nodiscard]] virtual auto upload_async(
        const std::string& key,
        std::span<const std::byte> data,
        const cloud_transfer_options& options = {}) -> std::future<result<upload_result>> = 0;

    /**
     * @brief Upload file to cloud storage (asynchronous)
     * @param local_path Local file path
     * @param key Object key (path)
     * @param options Transfer options
     * @return Future containing upload result or error
     */
    [[nodiscard]] virtual auto upload_file_async(
        const std::filesystem::path& local_path,
        const std::string& key,
        const cloud_transfer_options& options = {}) -> std::future<result<upload_result>> = 0;

    /**
     * @brief Download data from cloud storage (asynchronous)
     * @param key Object key (path)
     * @return Future containing downloaded data or error
     */
    [[nodiscard]] virtual auto download_async(
        const std::string& key) -> std::future<result<std::vector<std::byte>>> = 0;

    /**
     * @brief Download file from cloud storage (asynchronous)
     * @param key Object key (path)
     * @param local_path Local file path
     * @return Future containing download result or error
     */
    [[nodiscard]] virtual auto download_file_async(
        const std::string& key,
        const std::filesystem::path& local_path) -> std::future<result<download_result>> = 0;

    // ========================================================================
    // Streaming Operations
    // ========================================================================

    /**
     * @brief Create streaming upload context
     * @param key Object key (path)
     * @param options Transfer options
     * @return Upload stream or nullptr on failure
     */
    [[nodiscard]] virtual auto create_upload_stream(
        const std::string& key,
        const cloud_transfer_options& options = {}) -> std::unique_ptr<cloud_upload_stream> = 0;

    /**
     * @brief Create streaming download context
     * @param key Object key (path)
     * @return Download stream or nullptr on failure
     */
    [[nodiscard]] virtual auto create_download_stream(
        const std::string& key) -> std::unique_ptr<cloud_download_stream> = 0;

    // ========================================================================
    // Presigned URLs
    // ========================================================================

    /**
     * @brief Generate presigned URL for object access
     * @param key Object key (path)
     * @param options Presigned URL options
     * @return Result containing presigned URL or error
     */
    [[nodiscard]] virtual auto generate_presigned_url(
        const std::string& key,
        const presigned_url_options& options = {}) -> result<std::string> = 0;

    // ========================================================================
    // Progress Callbacks
    // ========================================================================

    /**
     * @brief Set callback for upload progress
     * @param callback Function called during uploads
     */
    virtual void on_upload_progress(
        std::function<void(const upload_progress&)> callback) = 0;

    /**
     * @brief Set callback for download progress
     * @param callback Function called during downloads
     */
    virtual void on_download_progress(
        std::function<void(const download_progress&)> callback) = 0;

    /**
     * @brief Set callback for state changes
     * @param callback Function called when state changes
     */
    virtual void on_state_changed(
        std::function<void(cloud_storage_state)> callback) = 0;

    // ========================================================================
    // Statistics and Configuration
    // ========================================================================

    /**
     * @brief Get cloud storage statistics
     * @return Current statistics
     */
    [[nodiscard]] virtual auto get_statistics() const -> cloud_storage_statistics = 0;

    /**
     * @brief Reset statistics counters
     */
    virtual void reset_statistics() = 0;

    /**
     * @brief Get storage configuration
     * @return Configuration reference
     */
    [[nodiscard]] virtual auto config() const -> const cloud_storage_config& = 0;

    /**
     * @brief Get current bucket name
     * @return Bucket name
     */
    [[nodiscard]] virtual auto bucket() const -> std::string_view = 0;

    /**
     * @brief Get current region
     * @return Region name
     */
    [[nodiscard]] virtual auto region() const -> std::string_view = 0;

protected:
    cloud_storage_interface() = default;
};

/**
 * @brief Cloud storage factory interface
 *
 * Creates cloud storage instances based on configuration.
 */
class cloud_storage_factory {
public:
    virtual ~cloud_storage_factory() = default;

    /**
     * @brief Create an AWS S3 storage instance
     * @param config S3 configuration
     * @param credentials Credential provider
     * @return Storage instance or nullptr on failure
     */
    [[nodiscard]] virtual auto create_s3(
        const s3_config& config,
        std::shared_ptr<credential_provider> credentials) -> std::unique_ptr<cloud_storage_interface> = 0;

    /**
     * @brief Create an Azure Blob storage instance
     * @param config Azure Blob configuration
     * @param credentials Credential provider
     * @return Storage instance or nullptr on failure
     */
    [[nodiscard]] virtual auto create_azure_blob(
        const azure_blob_config& config,
        std::shared_ptr<credential_provider> credentials) -> std::unique_ptr<cloud_storage_interface> = 0;

    /**
     * @brief Create a Google Cloud Storage instance
     * @param config GCS configuration
     * @param credentials Credential provider
     * @return Storage instance or nullptr on failure
     */
    [[nodiscard]] virtual auto create_gcs(
        const gcs_config& config,
        std::shared_ptr<credential_provider> credentials) -> std::unique_ptr<cloud_storage_interface> = 0;

    /**
     * @brief Create a storage instance from configuration
     * @param config Storage configuration
     * @param credentials Credential provider
     * @return Storage instance or nullptr on failure
     */
    [[nodiscard]] virtual auto create(
        const cloud_storage_config& config,
        std::shared_ptr<credential_provider> credentials) -> std::unique_ptr<cloud_storage_interface> = 0;

    /**
     * @brief Get supported cloud providers
     * @return Vector of supported providers
     */
    [[nodiscard]] virtual auto supported_providers() const
        -> std::vector<cloud_provider> = 0;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CLOUD_CLOUD_STORAGE_INTERFACE_H
