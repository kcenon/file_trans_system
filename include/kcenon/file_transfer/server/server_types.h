/**
 * @file server_types.h
 * @brief Server-related type definitions for file_trans_system
 */

#ifndef KCENON_FILE_TRANSFER_SERVER_SERVER_TYPES_H
#define KCENON_FILE_TRANSFER_SERVER_SERVER_TYPES_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kcenon::file_transfer {

/**
 * @brief Server state enumeration
 */
enum class server_state {
    stopped,
    starting,
    running,
    stopping
};

/**
 * @brief Convert server_state to string
 */
[[nodiscard]] constexpr auto to_string(server_state state) -> const char* {
    switch (state) {
        case server_state::stopped: return "stopped";
        case server_state::starting: return "starting";
        case server_state::running: return "running";
        case server_state::stopping: return "stopping";
        default: return "unknown";
    }
}

/**
 * @brief Endpoint for server address
 */
struct endpoint {
    std::string host;
    uint16_t port;

    endpoint() : port(0) {}
    endpoint(std::string h, uint16_t p) : host(std::move(h)), port(p) {}
    explicit endpoint(uint16_t p) : host("0.0.0.0"), port(p) {}
};

/**
 * @brief Unique identifier for a client connection
 */
struct client_id {
    uint64_t value;

    client_id() : value(0) {}
    explicit client_id(uint64_t v) : value(v) {}

    [[nodiscard]] auto operator==(const client_id& other) const -> bool = default;
    [[nodiscard]] auto operator<(const client_id& other) const -> bool {
        return value < other.value;
    }
};

/**
 * @brief Client information
 */
struct client_info {
    client_id id;
    std::string address;
    uint16_t port;
};

/**
 * @brief Server configuration
 */
struct server_config {
    std::filesystem::path storage_directory;
    std::size_t max_connections = 100;
    uint64_t max_file_size = 10ULL * 1024 * 1024 * 1024;      // 10GB
    uint64_t storage_quota = 100ULL * 1024 * 1024 * 1024;     // 100GB
    std::size_t chunk_size = 256 * 1024;                       // 256KB

    [[nodiscard]] auto is_valid() const -> bool {
        return !storage_directory.empty() && max_connections > 0;
    }
};

/**
 * @brief Server statistics
 */
struct server_statistics {
    uint64_t total_bytes_received = 0;
    uint64_t total_bytes_sent = 0;
    uint64_t total_files_uploaded = 0;
    uint64_t total_files_downloaded = 0;
    std::size_t active_connections = 0;
    std::size_t active_transfers = 0;
};

/**
 * @brief Storage statistics
 */
struct storage_stats {
    uint64_t total_capacity;
    uint64_t used_size;
    uint64_t available_size;
    std::size_t file_count;

    [[nodiscard]] auto usage_percent() const -> double {
        if (total_capacity == 0) return 0.0;
        return static_cast<double>(used_size) / static_cast<double>(total_capacity) * 100.0;
    }
};

/**
 * @brief Upload request from client
 */
struct upload_request {
    std::string filename;
    uint64_t file_size;
    uint64_t total_chunks;
    std::string sha256_hash;
    client_id client;
};

/**
 * @brief Download request from client
 */
struct download_request {
    std::string filename;
    client_id client;
};

/**
 * @brief Transfer result information
 */
struct transfer_result {
    bool success;
    std::string filename;
    uint64_t bytes_transferred;
    std::string error_message;
};

/**
 * @brief Transfer progress information
 */
struct transfer_progress {
    std::string filename;
    uint64_t bytes_transferred;
    uint64_t total_bytes;
    double percentage;
};

}  // namespace kcenon::file_transfer

// Hash support for client_id
template <>
struct std::hash<kcenon::file_transfer::client_id> {
    auto operator()(const kcenon::file_transfer::client_id& id) const noexcept -> std::size_t {
        return std::hash<uint64_t>{}(id.value);
    }
};

#endif  // KCENON_FILE_TRANSFER_SERVER_SERVER_TYPES_H
