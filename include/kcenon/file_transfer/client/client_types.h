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

namespace kcenon::file_transfer {

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
 * @brief Transfer handle for tracking ongoing transfers
 */
struct transfer_handle {
    uint64_t id;

    transfer_handle() : id(0) {}
    explicit transfer_handle(uint64_t v) : id(v) {}

    [[nodiscard]] auto is_valid() const -> bool { return id != 0; }
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
