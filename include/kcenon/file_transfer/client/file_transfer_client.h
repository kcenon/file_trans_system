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
#include <span>
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

    // Batch operations
    /**
     * @brief Upload multiple files to the server
     *
     * Uploads files in parallel (up to max_concurrent), tracking progress
     * for all files collectively. Individual failures can be configured
     * to not abort the entire batch.
     *
     * @param files Files to upload
     * @param options Batch options
     * @return Result containing batch handle or error
     *
     * @code
     * std::vector<upload_entry> files{
     *     {"local1.txt", "remote1.txt"},
     *     {"local2.txt", "remote2.txt"},
     *     {"local3.txt"}  // Uses local filename
     * };
     *
     * auto result = client.upload_files(files, {.max_concurrent = 4});
     * if (result.has_value()) {
     *     auto batch_result = result.value().wait();
     *     // Check batch_result for success/failure info
     * }
     * @endcode
     */
    [[nodiscard]] auto upload_files(
        std::span<const upload_entry> files,
        const batch_options& options = {}
    ) -> result<batch_transfer_handle>;

    /**
     * @brief Download multiple files from the server
     *
     * Downloads files in parallel (up to max_concurrent), tracking progress
     * for all files collectively.
     *
     * @param files Files to download
     * @param options Batch options
     * @return Result containing batch handle or error
     *
     * @code
     * std::vector<download_entry> files{
     *     {"remote1.txt", "local1.txt"},
     *     {"remote2.txt", "local2.txt"}
     * };
     *
     * auto result = client.download_files(files, {.max_concurrent = 4});
     * if (result.has_value()) {
     *     auto batch_result = result.value().wait();
     * }
     * @endcode
     */
    [[nodiscard]] auto download_files(
        std::span<const download_entry> files,
        const batch_options& options = {}
    ) -> result<batch_transfer_handle>;

    // Batch transfer control methods
    /**
     * @brief Get batch progress
     * @param batch_id Batch ID
     * @return Current batch progress
     */
    [[nodiscard]] auto get_batch_progress(uint64_t batch_id) const
        -> batch_progress;

    /**
     * @brief Get total files in batch
     * @param batch_id Batch ID
     * @return Total file count
     */
    [[nodiscard]] auto get_batch_total_files(uint64_t batch_id) const
        -> std::size_t;

    /**
     * @brief Get completed files count in batch
     * @param batch_id Batch ID
     * @return Completed file count
     */
    [[nodiscard]] auto get_batch_completed_files(uint64_t batch_id) const
        -> std::size_t;

    /**
     * @brief Get failed files count in batch
     * @param batch_id Batch ID
     * @return Failed file count
     */
    [[nodiscard]] auto get_batch_failed_files(uint64_t batch_id) const
        -> std::size_t;

    /**
     * @brief Get individual transfer handles for a batch
     * @param batch_id Batch ID
     * @return Vector of transfer handles
     */
    [[nodiscard]] auto get_batch_individual_handles(uint64_t batch_id) const
        -> std::vector<transfer_handle>;

    /**
     * @brief Pause all transfers in a batch
     * @param batch_id Batch ID
     * @return Success or error
     */
    [[nodiscard]] auto pause_batch(uint64_t batch_id) -> result<void>;

    /**
     * @brief Resume all transfers in a batch
     * @param batch_id Batch ID
     * @return Success or error
     */
    [[nodiscard]] auto resume_batch(uint64_t batch_id) -> result<void>;

    /**
     * @brief Cancel all transfers in a batch
     * @param batch_id Batch ID
     * @return Success or error
     */
    [[nodiscard]] auto cancel_batch(uint64_t batch_id) -> result<void>;

    /**
     * @brief Wait for batch completion
     * @param batch_id Batch ID
     * @return Batch result or error
     */
    [[nodiscard]] auto wait_for_batch(uint64_t batch_id)
        -> result<batch_result>;

    /**
     * @brief Wait for batch completion with timeout
     * @param batch_id Batch ID
     * @param timeout Maximum time to wait
     * @return Batch result or error
     */
    [[nodiscard]] auto wait_for_batch(
        uint64_t batch_id,
        std::chrono::milliseconds timeout) -> result<batch_result>;

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

    // Transfer control methods
    /**
     * @brief Get transfer status
     * @param handle_id Transfer handle ID
     * @return Current transfer status
     */
    [[nodiscard]] auto get_transfer_status(uint64_t handle_id) const
        -> transfer_status;

    /**
     * @brief Get transfer progress
     * @param handle_id Transfer handle ID
     * @return Current transfer progress
     */
    [[nodiscard]] auto get_transfer_progress(uint64_t handle_id) const
        -> transfer_progress_info;

    /**
     * @brief Pause a transfer
     * @param handle_id Transfer handle ID
     * @return Success or error
     */
    [[nodiscard]] auto pause_transfer(uint64_t handle_id) -> result<void>;

    /**
     * @brief Resume a paused transfer
     * @param handle_id Transfer handle ID
     * @return Success or error
     */
    [[nodiscard]] auto resume_transfer(uint64_t handle_id) -> result<void>;

    /**
     * @brief Cancel a transfer
     * @param handle_id Transfer handle ID
     * @return Success or error
     */
    [[nodiscard]] auto cancel_transfer(uint64_t handle_id) -> result<void>;

    /**
     * @brief Wait for transfer completion
     * @param handle_id Transfer handle ID
     * @return Transfer result info or error
     */
    [[nodiscard]] auto wait_for_transfer(uint64_t handle_id)
        -> result<transfer_result_info>;

    /**
     * @brief Wait for transfer completion with timeout
     * @param handle_id Transfer handle ID
     * @param timeout Maximum time to wait
     * @return Transfer result info or error
     */
    [[nodiscard]] auto wait_for_transfer(
        uint64_t handle_id,
        std::chrono::milliseconds timeout) -> result<transfer_result_info>;

    // Download control methods (for internal and network layer use)
    /**
     * @brief Process a received download chunk
     * @param handle_id Transfer handle ID
     * @param received_chunk The chunk to process
     * @return Result indicating success or failure
     */
    [[nodiscard]] auto process_download_chunk(
        uint64_t handle_id,
        const chunk& received_chunk) -> result<void>;

    /**
     * @brief Finalize a completed download
     * @param handle_id Transfer handle ID
     * @return Result indicating success or failure
     */
    [[nodiscard]] auto finalize_download(uint64_t handle_id) -> result<void>;

    /**
     * @brief Cancel an ongoing download
     * @param handle_id Transfer handle ID
     * @return Result indicating success or failure
     */
    [[nodiscard]] auto cancel_download(uint64_t handle_id) -> result<void>;

    /**
     * @brief Set download metadata from server response
     * @param handle_id Transfer handle ID
     * @param file_size Total file size in bytes
     * @param total_chunks Total number of chunks expected
     * @param chunk_size Chunk size in bytes
     * @param sha256_hash Expected SHA-256 hash for verification
     * @return Result indicating success or failure
     */
    [[nodiscard]] auto set_download_metadata(
        uint64_t handle_id,
        uint64_t file_size,
        uint64_t total_chunks,
        uint32_t chunk_size,
        const std::string& sha256_hash) -> result<void>;

private:
    explicit file_transfer_client(client_config config);

    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CLIENT_FILE_TRANSFER_CLIENT_H
