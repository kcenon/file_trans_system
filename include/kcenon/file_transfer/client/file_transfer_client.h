/**
 * @file file_transfer_client.h
 * @brief File transfer client implementation
 */

#ifndef KCENON_FILE_TRANSFER_CLIENT_FILE_TRANSFER_CLIENT_H
#define KCENON_FILE_TRANSFER_CLIENT_FILE_TRANSFER_CLIENT_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "kcenon/file_transfer/core/types.h"
#include "kcenon/file_transfer/client/client_types.h"
#include "kcenon/file_transfer/server/server_types.h"

namespace kcenon::file_transfer {

/**
 * @brief File transfer client
 *
 * Connects to a file transfer server to upload and download files.
 *
 * @code
 * auto client_result = file_transfer_client::builder()
 *     .with_compression(compression_mode::adaptive)
 *     .with_auto_reconnect(true)
 *     .build();
 *
 * if (client_result.has_value()) {
 *     auto& client = client_result.value();
 *     client.connect(endpoint{"localhost", 8080});
 * }
 * @endcode
 */
class file_transfer_client {
public:
    /**
     * @brief Builder for file_transfer_client
     */
    class builder {
    public:
        builder();

        /**
         * @brief Set compression mode
         * @param mode Compression mode (default: adaptive)
         * @return Reference to builder for chaining
         */
        auto with_compression(compression_mode mode) -> builder&;

        /**
         * @brief Set compression level
         * @param level Compression level (default: fast)
         * @return Reference to builder for chaining
         */
        auto with_compression_level(compression_level level) -> builder&;

        /**
         * @brief Set chunk size for transfers
         * @param size Chunk size in bytes (default: 256KB)
         * @return Reference to builder for chaining
         */
        auto with_chunk_size(std::size_t size) -> builder&;

        /**
         * @brief Enable or disable auto-reconnection
         * @param enable Enable auto-reconnect (default: true)
         * @param policy Reconnection policy
         * @return Reference to builder for chaining
         */
        auto with_auto_reconnect(bool enable, reconnect_policy policy = {}) -> builder&;

        /**
         * @brief Set upload bandwidth limit
         * @param bytes_per_second Maximum upload speed
         * @return Reference to builder for chaining
         */
        auto with_upload_bandwidth_limit(std::size_t bytes_per_second) -> builder&;

        /**
         * @brief Set download bandwidth limit
         * @param bytes_per_second Maximum download speed
         * @return Reference to builder for chaining
         */
        auto with_download_bandwidth_limit(std::size_t bytes_per_second) -> builder&;

        /**
         * @brief Set connection timeout
         * @param timeout Connection timeout duration
         * @return Reference to builder for chaining
         */
        auto with_connect_timeout(std::chrono::milliseconds timeout) -> builder&;

        /**
         * @brief Build the client instance
         * @return Result containing the client or an error
         */
        [[nodiscard]] auto build() -> result<file_transfer_client>;

    private:
        client_config config_;
    };

    // Non-copyable, movable
    file_transfer_client(const file_transfer_client&) = delete;
    auto operator=(const file_transfer_client&) -> file_transfer_client& = delete;
    file_transfer_client(file_transfer_client&&) noexcept;
    auto operator=(file_transfer_client&&) noexcept -> file_transfer_client&;
    ~file_transfer_client();

    /**
     * @brief Connect to a server
     * @param server_addr Server endpoint
     * @return Result indicating success or failure
     */
    [[nodiscard]] auto connect(const endpoint& server_addr) -> result<void>;

    /**
     * @brief Disconnect from the server
     * @return Result indicating success or failure
     */
    [[nodiscard]] auto disconnect() -> result<void>;

    /**
     * @brief Check if connected to server
     * @return true if connected, false otherwise
     */
    [[nodiscard]] auto is_connected() const -> bool;

    /**
     * @brief Get current connection state
     * @return Current state
     */
    [[nodiscard]] auto state() const -> connection_state;

    // File operations
    /**
     * @brief Upload a file to the server
     * @param local_path Local file path
     * @param remote_name Remote filename
     * @param options Upload options
     * @return Result containing transfer handle or error
     */
    [[nodiscard]] auto upload_file(
        const std::filesystem::path& local_path,
        const std::string& remote_name,
        const upload_options& options = {}
    ) -> result<transfer_handle>;

    /**
     * @brief Download a file from the server
     * @param remote_name Remote filename
     * @param local_path Local file path
     * @param options Download options
     * @return Result containing transfer handle or error
     */
    [[nodiscard]] auto download_file(
        const std::string& remote_name,
        const std::filesystem::path& local_path,
        const download_options& options = {}
    ) -> result<transfer_handle>;

    /**
     * @brief List files on the server
     * @param options List options
     * @return Result containing file list or error
     */
    [[nodiscard]] auto list_files(
        const list_options& options = {}
    ) -> result<std::vector<file_info>>;

    // Callbacks
    /**
     * @brief Set callback for transfer progress updates
     * @param callback Function called with progress updates
     */
    void on_progress(std::function<void(const transfer_progress&)> callback);

    /**
     * @brief Set callback for transfer completion events
     * @param callback Function called when transfer completes
     */
    void on_complete(std::function<void(const transfer_result&)> callback);

    /**
     * @brief Set callback for connection state changes
     * @param callback Function called when state changes
     */
    void on_connection_state_changed(std::function<void(connection_state)> callback);

    // Statistics
    /**
     * @brief Get client statistics
     * @return Current statistics
     */
    [[nodiscard]] auto get_statistics() const -> client_statistics;

    /**
     * @brief Get compression statistics
     * @return Compression stats
     */
    [[nodiscard]] auto get_compression_stats() const -> compression_statistics;

    /**
     * @brief Get client configuration
     * @return Client configuration
     */
    [[nodiscard]] auto config() const -> const client_config&;

private:
    explicit file_transfer_client(client_config config);

    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CLIENT_FILE_TRANSFER_CLIENT_H
