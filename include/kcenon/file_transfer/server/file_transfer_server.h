/**
 * @file file_transfer_server.h
 * @brief File transfer server implementation
 */

#ifndef KCENON_FILE_TRANSFER_SERVER_FILE_TRANSFER_SERVER_H
#define KCENON_FILE_TRANSFER_SERVER_FILE_TRANSFER_SERVER_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "kcenon/file_transfer/core/types.h"
#include "kcenon/file_transfer/server/quota_manager.h"
#include "kcenon/file_transfer/server/server_types.h"

namespace kcenon::file_transfer {

/**
 * @brief File transfer server
 *
 * Manages file storage, accepts client connections, and handles
 * upload/download/list requests.
 *
 * @code
 * auto server_result = file_transfer_server::builder()
 *     .with_storage_directory("/data/files")
 *     .with_max_connections(100)
 *     .build();
 *
 * if (server_result.has_value()) {
 *     auto& server = server_result.value();
 *     server.start(endpoint{8080});
 * }
 * @endcode
 */
class file_transfer_server {
public:
    /**
     * @brief Builder for file_transfer_server
     */
    class builder {
    public:
        builder();

        /**
         * @brief Set storage directory for uploaded files
         * @param dir Path to storage directory
         * @return Reference to builder for chaining
         */
        auto with_storage_directory(const std::filesystem::path& dir) -> builder&;

        /**
         * @brief Set maximum number of concurrent connections
         * @param max_count Maximum connections (default: 100)
         * @return Reference to builder for chaining
         */
        auto with_max_connections(std::size_t max_count) -> builder&;

        /**
         * @brief Set maximum file size limit
         * @param max_bytes Maximum file size in bytes (default: 10GB)
         * @return Reference to builder for chaining
         */
        auto with_max_file_size(uint64_t max_bytes) -> builder&;

        /**
         * @brief Set storage quota
         * @param max_bytes Maximum total storage in bytes (default: 100GB)
         * @return Reference to builder for chaining
         */
        auto with_storage_quota(uint64_t max_bytes) -> builder&;

        /**
         * @brief Set chunk size for file transfers
         * @param size Chunk size in bytes (default: 256KB)
         * @return Reference to builder for chaining
         */
        auto with_chunk_size(std::size_t size) -> builder&;

        /**
         * @brief Set storage mode
         * @param mode Storage mode (local_only, cloud_only, hybrid)
         * @return Reference to builder for chaining
         */
        auto with_storage_mode(storage_mode mode) -> builder&;

        /**
         * @brief Set cloud storage backend
         * @param cloud_storage Cloud storage interface instance
         * @return Reference to builder for chaining
         */
        auto with_cloud_storage(
            std::shared_ptr<cloud_storage_interface> cloud_storage) -> builder&;

        /**
         * @brief Set cloud storage credentials
         * @param credentials Credential provider instance
         * @return Reference to builder for chaining
         */
        auto with_cloud_credentials(
            std::shared_ptr<credential_provider> credentials) -> builder&;

        /**
         * @brief Set cloud key prefix
         * @param prefix Key prefix for all cloud objects
         * @return Reference to builder for chaining
         */
        auto with_cloud_key_prefix(std::string prefix) -> builder&;

        /**
         * @brief Enable cloud write replication
         * @param enable Enable replication (default: true)
         * @return Reference to builder for chaining
         */
        auto with_cloud_replication(bool enable) -> builder&;

        /**
         * @brief Enable cloud read fallback
         * @param enable Enable fallback (default: true)
         * @return Reference to builder for chaining
         */
        auto with_cloud_fallback(bool enable) -> builder&;

        /**
         * @brief Enable local caching for cloud objects
         * @param enable Enable caching (default: true)
         * @param max_size Maximum cache size in bytes (default: 1GB)
         * @return Reference to builder for chaining
         */
        auto with_cloud_cache(bool enable, uint64_t max_size = 1ULL * 1024 * 1024 * 1024) -> builder&;

        /**
         * @brief Set cloud cache directory
         * @param dir Cache directory path
         * @return Reference to builder for chaining
         */
        auto with_cloud_cache_directory(const std::filesystem::path& dir) -> builder&;

        /**
         * @brief Build the server instance
         * @return Result containing the server or an error
         */
        [[nodiscard]] auto build() -> result<file_transfer_server>;

    private:
        server_config config_;
    };

    // Non-copyable, movable
    file_transfer_server(const file_transfer_server&) = delete;
    auto operator=(const file_transfer_server&) -> file_transfer_server& = delete;
    file_transfer_server(file_transfer_server&&) noexcept;
    auto operator=(file_transfer_server&&) noexcept -> file_transfer_server&;
    ~file_transfer_server();

    /**
     * @brief Start the server on the specified endpoint
     * @param listen_addr Endpoint to listen on
     * @return Result indicating success or failure
     */
    [[nodiscard]] auto start(const endpoint& listen_addr) -> result<void>;

    /**
     * @brief Stop the server
     * @return Result indicating success or failure
     */
    [[nodiscard]] auto stop() -> result<void>;

    /**
     * @brief Check if server is running
     * @return true if running, false otherwise
     */
    [[nodiscard]] auto is_running() const -> bool;

    /**
     * @brief Get current server state
     * @return Current state
     */
    [[nodiscard]] auto state() const -> server_state;

    /**
     * @brief Get the port the server is listening on
     * @return Port number, or 0 if not running
     */
    [[nodiscard]] auto port() const -> uint16_t;

    // Request callbacks
    /**
     * @brief Set callback for upload request validation
     * @param callback Function that returns true to accept, false to reject
     */
    void on_upload_request(std::function<bool(const upload_request&)> callback);

    /**
     * @brief Set callback for download request validation
     * @param callback Function that returns true to accept, false to reject
     */
    void on_download_request(std::function<bool(const download_request&)> callback);

    // Event callbacks
    /**
     * @brief Set callback for client connection events
     * @param callback Function called when client connects
     */
    void on_client_connected(std::function<void(const client_info&)> callback);

    /**
     * @brief Set callback for client disconnection events
     * @param callback Function called when client disconnects
     */
    void on_client_disconnected(std::function<void(const client_info&)> callback);

    /**
     * @brief Set callback for transfer completion events
     * @param callback Function called when transfer completes
     */
    void on_transfer_complete(std::function<void(const transfer_result&)> callback);

    /**
     * @brief Set callback for transfer progress updates
     * @param callback Function called with progress updates
     */
    void on_progress(std::function<void(const transfer_progress&)> callback);

    // Statistics
    /**
     * @brief Get server statistics
     * @return Current statistics
     */
    [[nodiscard]] auto get_statistics() const -> server_statistics;

    /**
     * @brief Get storage statistics
     * @return Current storage stats
     */
    [[nodiscard]] auto get_storage_stats() const -> storage_stats;

    /**
     * @brief Get server configuration
     * @return Server configuration
     */
    [[nodiscard]] auto config() const -> const server_config&;

    // Quota management

    /**
     * @brief Get the quota manager instance
     * @return Reference to quota manager
     */
    [[nodiscard]] auto get_quota_manager() -> quota_manager&;

    /**
     * @brief Get the quota manager instance (const)
     * @return Const reference to quota manager
     */
    [[nodiscard]] auto get_quota_manager() const -> const quota_manager&;

    /**
     * @brief Get current quota usage
     * @return Current quota usage information
     */
    [[nodiscard]] auto get_quota_usage() const -> quota_usage;

    /**
     * @brief Check if an upload of given size is allowed
     * @param file_size Size of the file to upload
     * @return Result<void> on success, error if quota exceeded or file too large
     */
    [[nodiscard]] auto check_upload_allowed(uint64_t file_size) -> result<void>;

    /**
     * @brief Set callback for quota warning events
     * @param callback Function called when quota warning threshold is reached
     */
    void on_quota_warning(std::function<void(const quota_usage&)> callback);

    /**
     * @brief Set callback for quota exceeded events
     * @param callback Function called when quota is exceeded
     */
    void on_quota_exceeded(std::function<void(const quota_usage&)> callback);

private:
    explicit file_transfer_server(server_config config);

    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_SERVER_FILE_TRANSFER_SERVER_H
