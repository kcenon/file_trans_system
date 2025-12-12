/**
 * @file client_types.h
 * @brief Client-related type definitions for file_trans_system
 */

#ifndef KCENON_FILE_TRANSFER_CLIENT_CLIENT_TYPES_H
#define KCENON_FILE_TRANSFER_CLIENT_CLIENT_TYPES_H

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "kcenon/file_transfer/core/types.h"

namespace kcenon::file_transfer {

// Forward declaration
class file_transfer_client;

/**
 * @brief Connection state enumeration
 */
enum class connection_state {
    disconnected,
    connecting,
    connected,
    reconnecting
};

/**
 * @brief Convert connection_state to string
 */
[[nodiscard]] constexpr auto to_string(connection_state state) -> const char* {
    switch (state) {
        case connection_state::disconnected: return "disconnected";
        case connection_state::connecting: return "connecting";
        case connection_state::connected: return "connected";
        case connection_state::reconnecting: return "reconnecting";
        default: return "unknown";
    }
}

/**
 * @brief Reconnection policy configuration
 */
struct reconnect_policy {
    std::size_t max_attempts = 5;
    std::chrono::milliseconds initial_delay{1000};
    std::chrono::milliseconds max_delay{30000};
    double backoff_multiplier = 2.0;
};

/**
 * @brief Compression mode for transfers
 */
enum class compression_mode {
    none,
    always,
    adaptive
};

/**
 * @brief Compression level
 */
enum class compression_level {
    fast,
    balanced,
    best
};

/**
 * @brief Client configuration
 */
struct client_config {
    compression_mode compression = compression_mode::adaptive;
    compression_level comp_level = compression_level::fast;
    std::size_t chunk_size = 256 * 1024;  // 256KB
    bool auto_reconnect = true;
    reconnect_policy reconnect;
    std::optional<std::size_t> upload_bandwidth_limit;
    std::optional<std::size_t> download_bandwidth_limit;
    std::chrono::milliseconds connect_timeout{30000};
};

/**
 * @brief Upload options
 */
struct upload_options {
    std::optional<compression_mode> compression;
    bool overwrite = false;
};

/**
 * @brief Download options
 */
struct download_options {
    bool overwrite = false;
    bool verify_hash = true;
};

/**
 * @brief List options
 */
struct list_options {
    std::string pattern = "*";
    std::size_t offset = 0;
    std::size_t limit = 1000;
};

/**
 * @brief File information from server
 */
struct file_info {
    std::string filename;
    uint64_t size;
    std::string sha256_hash;
    std::chrono::system_clock::time_point modified_time;
};

/**
 * @brief Transfer status for transfer control
 *
 * Maps to transfer_state but with simpler naming for public API.
 */
enum class transfer_status {
    pending,       ///< Waiting to start
    in_progress,   ///< Transfer in progress
    paused,        ///< Transfer paused
    completing,    ///< Finalizing transfer
    completed,     ///< Transfer completed successfully
    failed,        ///< Transfer failed
    cancelled      ///< Transfer cancelled by user
};

/**
 * @brief Convert transfer_status to string
 */
[[nodiscard]] constexpr auto to_string(transfer_status status) noexcept
    -> const char* {
    switch (status) {
        case transfer_status::pending: return "pending";
        case transfer_status::in_progress: return "in_progress";
        case transfer_status::paused: return "paused";
        case transfer_status::completing: return "completing";
        case transfer_status::completed: return "completed";
        case transfer_status::failed: return "failed";
        case transfer_status::cancelled: return "cancelled";
        default: return "unknown";
    }
}

/**
 * @brief Check if status is terminal (final)
 */
[[nodiscard]] constexpr auto is_terminal_status(transfer_status status) noexcept
    -> bool {
    return status == transfer_status::completed ||
           status == transfer_status::failed ||
           status == transfer_status::cancelled;
}

/**
 * @brief Progress information for a transfer
 */
struct transfer_progress_info {
    uint64_t bytes_transferred = 0;    ///< Bytes transferred so far
    uint64_t total_bytes = 0;          ///< Total file size
    uint64_t chunks_transferred = 0;   ///< Chunks transferred
    uint64_t total_chunks = 0;         ///< Total number of chunks
    double transfer_rate = 0.0;        ///< Bytes per second
    std::chrono::milliseconds elapsed{0};  ///< Time elapsed

    [[nodiscard]] auto completion_percentage() const noexcept -> double {
        if (total_bytes == 0) return 0.0;
        return static_cast<double>(bytes_transferred) /
               static_cast<double>(total_bytes) * 100.0;
    }
};

/**
 * @brief Result of a completed transfer
 */
struct transfer_result_info {
    bool success = false;              ///< Whether transfer succeeded
    uint64_t bytes_transferred = 0;    ///< Total bytes transferred
    std::chrono::milliseconds elapsed{0};  ///< Total time taken
    std::optional<std::string> error_message;  ///< Error message if failed
};

/**
 * @brief Transfer handle for tracking and controlling ongoing transfers
 *
 * Provides methods to pause, resume, cancel, and wait for transfers.
 * The handle maintains a reference to the client that owns the transfer.
 *
 * @code
 * auto handle_result = client.upload_file("local.txt", "remote.txt");
 * if (handle_result.has_value()) {
 *     auto& handle = handle_result.value();
 *
 *     // Check status
 *     auto status = handle.get_status();
 *
 *     // Pause transfer
 *     handle.pause();
 *
 *     // Resume transfer
 *     handle.resume();
 *
 *     // Wait for completion
 *     auto result = handle.wait();
 * }
 * @endcode
 */
class transfer_handle {
public:
    /**
     * @brief Default constructor (invalid handle)
     */
    transfer_handle();

    /**
     * @brief Construct with ID and client reference
     * @param handle_id Unique handle identifier
     * @param client Pointer to owning client
     */
    transfer_handle(uint64_t handle_id, file_transfer_client* client);

    // Copyable and movable
    transfer_handle(const transfer_handle&) = default;
    transfer_handle(transfer_handle&&) noexcept = default;
    auto operator=(const transfer_handle&) -> transfer_handle& = default;
    auto operator=(transfer_handle&&) noexcept -> transfer_handle& = default;

    /**
     * @brief Get handle ID
     * @return Handle identifier
     */
    [[nodiscard]] auto get_id() const noexcept -> uint64_t;

    /**
     * @brief Check if handle is valid
     * @return true if handle is valid
     */
    [[nodiscard]] auto is_valid() const noexcept -> bool;

    /**
     * @brief Get current transfer status
     * @return Current status
     */
    [[nodiscard]] auto get_status() const -> transfer_status;

    /**
     * @brief Get current transfer progress
     * @return Progress information
     */
    [[nodiscard]] auto get_progress() const -> transfer_progress_info;

    /**
     * @brief Pause the transfer
     * @return Success or error
     *
     * Valid state transitions:
     * - in_progress -> paused
     */
    [[nodiscard]] auto pause() -> result<void>;

    /**
     * @brief Resume a paused transfer
     * @return Success or error
     *
     * Valid state transitions:
     * - paused -> in_progress
     */
    [[nodiscard]] auto resume() -> result<void>;

    /**
     * @brief Cancel the transfer
     * @return Success or error
     *
     * Can be called from any non-terminal state.
     * Cleans up temporary files.
     */
    [[nodiscard]] auto cancel() -> result<void>;

    /**
     * @brief Wait for transfer completion
     * @return Transfer result or error
     *
     * Blocks until transfer completes, fails, or is cancelled.
     */
    [[nodiscard]] auto wait() -> result<transfer_result_info>;

    /**
     * @brief Wait for transfer completion with timeout
     * @param timeout Maximum time to wait
     * @return Transfer result or error
     *
     * Returns error if timeout expires before completion.
     */
    [[nodiscard]] auto wait_for(std::chrono::milliseconds timeout)
        -> result<transfer_result_info>;

private:
    uint64_t id_ = 0;
    file_transfer_client* client_ = nullptr;
};

/**
 * @brief Client statistics
 */
struct client_statistics {
    uint64_t total_bytes_uploaded = 0;
    uint64_t total_bytes_downloaded = 0;
    uint64_t total_files_uploaded = 0;
    uint64_t total_files_downloaded = 0;
    std::size_t active_transfers = 0;
};

/**
 * @brief Compression statistics
 */
struct compression_statistics {
    uint64_t total_compressed_bytes = 0;
    uint64_t total_uncompressed_bytes = 0;

    [[nodiscard]] auto compression_ratio() const -> double {
        if (total_uncompressed_bytes == 0) return 1.0;
        return static_cast<double>(total_compressed_bytes) /
               static_cast<double>(total_uncompressed_bytes);
    }
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CLIENT_CLIENT_TYPES_H
