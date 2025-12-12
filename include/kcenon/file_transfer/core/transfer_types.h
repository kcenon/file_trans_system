/**
 * @file transfer_types.h
 * @brief Transfer-related data structures for file_trans_system
 * @version 0.2.0
 *
 * This file defines data structures related to file transfer operations,
 * including progress tracking, results, and state management.
 */

#ifndef KCENON_FILE_TRANSFER_CORE_TRANSFER_TYPES_H
#define KCENON_FILE_TRANSFER_CORE_TRANSFER_TYPES_H

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "chunk_types.h"
#include "error_codes.h"

namespace kcenon::file_transfer {

/**
 * @brief Transfer direction
 */
enum class transfer_direction {
    upload,    // Client -> Server
    download,  // Server -> Client
};

[[nodiscard]] constexpr auto to_string(transfer_direction dir) noexcept
    -> std::string_view {
    switch (dir) {
        case transfer_direction::upload:
            return "upload";
        case transfer_direction::download:
            return "download";
        default:
            return "unknown";
    }
}

/**
 * @brief Transfer state enumeration
 */
enum class transfer_state {
    idle,           // Not started
    initializing,   // Negotiating with server
    transferring,   // Data transfer in progress
    paused,         // Transfer paused
    verifying,      // Verifying file hash
    completing,     // Finalizing transfer
    completed,      // Transfer completed successfully
    failed,         // Transfer failed
    cancelled,      // Transfer cancelled by user
};

[[nodiscard]] constexpr auto to_string(transfer_state state) noexcept
    -> std::string_view {
    switch (state) {
        case transfer_state::idle:
            return "idle";
        case transfer_state::initializing:
            return "initializing";
        case transfer_state::transferring:
            return "transferring";
        case transfer_state::paused:
            return "paused";
        case transfer_state::verifying:
            return "verifying";
        case transfer_state::completing:
            return "completing";
        case transfer_state::completed:
            return "completed";
        case transfer_state::failed:
            return "failed";
        case transfer_state::cancelled:
            return "cancelled";
        default:
            return "unknown";
    }
}

/**
 * @brief Check if transfer state is terminal (final)
 */
[[nodiscard]] constexpr auto is_terminal_state(transfer_state state) noexcept
    -> bool {
    return state == transfer_state::completed ||
           state == transfer_state::failed ||
           state == transfer_state::cancelled;
}

/**
 * @brief Check if transfer is active
 */
[[nodiscard]] constexpr auto is_active_state(transfer_state state) noexcept
    -> bool {
    return state == transfer_state::initializing ||
           state == transfer_state::transferring ||
           state == transfer_state::verifying ||
           state == transfer_state::completing;
}

/**
 * @brief Duration type for time measurements
 */
using duration = std::chrono::milliseconds;
using time_point = std::chrono::system_clock::time_point;

/**
 * @brief File information structure
 */
struct detailed_file_info {
    std::string name;                    // Filename
    uint64_t size;                       // File size in bytes
    std::string sha256_hash;             // SHA-256 hash (hex string)
    time_point created_time;             // Creation timestamp
    time_point modified_time;            // Last modification timestamp
    std::filesystem::perms permissions;  // File permissions
    bool compressible_hint;              // Hint for compression

    detailed_file_info()
        : size(0)
        , permissions(std::filesystem::perms::none)
        , compressible_hint(true) {}
};

/**
 * @brief Transfer progress information
 */
struct detailed_transfer_progress {
    transfer_id id;                      // Transfer identifier
    transfer_direction direction;        // Upload or download
    transfer_state state;                // Current state
    uint64_t bytes_transferred;          // Bytes transferred so far
    uint64_t bytes_on_wire;              // Compressed bytes on wire
    uint64_t total_bytes;                // Total file size
    uint64_t chunks_transferred;         // Chunks transferred
    uint64_t total_chunks;               // Total number of chunks
    double transfer_rate;                // Bytes per second
    double compression_ratio;            // Compressed/Original ratio
    duration elapsed_time;               // Time elapsed
    duration estimated_remaining;        // Estimated time remaining

    detailed_transfer_progress()
        : direction(transfer_direction::upload)
        , state(transfer_state::idle)
        , bytes_transferred(0)
        , bytes_on_wire(0)
        , total_bytes(0)
        , chunks_transferred(0)
        , total_chunks(0)
        , transfer_rate(0.0)
        , compression_ratio(1.0)
        , elapsed_time(0)
        , estimated_remaining(0) {}

    [[nodiscard]] auto completion_percentage() const noexcept -> double {
        if (total_bytes == 0) return 0.0;
        return static_cast<double>(bytes_transferred) /
               static_cast<double>(total_bytes) * 100.0;
    }

    [[nodiscard]] auto is_complete() const noexcept -> bool {
        return state == transfer_state::completed;
    }

    [[nodiscard]] auto is_active() const noexcept -> bool {
        return is_active_state(state);
    }
};

/**
 * @brief Transfer error information
 */
struct transfer_error {
    transfer_error_code code;
    std::string message;

    transfer_error()
        : code(transfer_error_code::success) {}

    explicit transfer_error(transfer_error_code c)
        : code(c), message(std::string(to_string(c))) {}

    transfer_error(transfer_error_code c, std::string msg)
        : code(c), message(std::move(msg)) {}

    [[nodiscard]] explicit operator bool() const noexcept {
        return code != transfer_error_code::success;
    }

    [[nodiscard]] auto is_retryable() const noexcept -> bool {
        return kcenon::file_transfer::is_retryable(
            static_cast<int32_t>(code));
    }
};

/**
 * @brief Transfer result information
 */
struct detailed_transfer_result {
    transfer_id id;                       // Transfer identifier
    transfer_direction direction;         // Upload or download
    std::filesystem::path local_path;     // Local file path
    std::string remote_name;              // Remote filename
    uint64_t bytes_transferred;           // Total bytes transferred
    uint64_t bytes_on_wire;               // Compressed bytes on wire
    bool verified;                        // SHA-256 verification passed
    std::optional<transfer_error> error;  // Error if failed
    duration elapsed_time;                // Total transfer time
    time_point completed_at;              // Completion timestamp

    detailed_transfer_result()
        : direction(transfer_direction::upload)
        , bytes_transferred(0)
        , bytes_on_wire(0)
        , verified(false)
        , elapsed_time(0) {}

    [[nodiscard]] auto success() const noexcept -> bool {
        return !error.has_value();
    }

    [[nodiscard]] auto compression_ratio() const noexcept -> double {
        if (bytes_transferred == 0) return 1.0;
        return static_cast<double>(bytes_on_wire) /
               static_cast<double>(bytes_transferred);
    }

    [[nodiscard]] auto average_speed() const noexcept -> double {
        auto ms = elapsed_time.count();
        if (ms == 0) return 0.0;
        return static_cast<double>(bytes_transferred) /
               (static_cast<double>(ms) / 1000.0);
    }
};

/**
 * @brief Network endpoint
 */
struct endpoint {
    std::string host;
    uint16_t port;

    endpoint() : port(0) {}
    endpoint(std::string h, uint16_t p) : host(std::move(h)), port(p) {}
    explicit endpoint(uint16_t p) : host("0.0.0.0"), port(p) {}

    [[nodiscard]] auto to_string() const -> std::string {
        return host + ":" + std::to_string(port);
    }

    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return !host.empty() && port != 0;
    }

    [[nodiscard]] auto operator==(const endpoint& other) const noexcept
        -> bool = default;
};

/**
 * @brief Session information
 */
struct session_info {
    transfer_id session_id;              // Session UUID
    endpoint server_endpoint;            // Server address
    time_point connected_at;             // Connection timestamp
    uint64_t bytes_uploaded;             // Total bytes uploaded this session
    uint64_t bytes_downloaded;           // Total bytes downloaded this session
    uint32_t files_uploaded;             // Files uploaded this session
    uint32_t files_downloaded;           // Files downloaded this session

    session_info()
        : bytes_uploaded(0)
        , bytes_downloaded(0)
        , files_uploaded(0)
        , files_downloaded(0) {}
};

/**
 * @brief Resume state for interrupted transfers
 */
struct resume_state {
    transfer_id id;                      // Transfer identifier
    transfer_direction direction;        // Upload or download
    std::filesystem::path local_path;    // Local file path
    std::string remote_name;             // Remote filename
    std::string file_hash;               // SHA-256 hash of file
    uint64_t file_size;                  // Total file size
    uint64_t last_chunk_index;           // Last successfully transferred chunk
    uint64_t last_offset;                // Last byte offset
    time_point saved_at;                 // When state was saved
    std::vector<uint64_t> missing_chunks;// Missing chunk indices

    resume_state()
        : direction(transfer_direction::upload)
        , file_size(0)
        , last_chunk_index(0)
        , last_offset(0) {}

    [[nodiscard]] auto can_resume() const noexcept -> bool {
        return !id.is_null() && file_size > 0;
    }

    [[nodiscard]] auto bytes_remaining() const noexcept -> uint64_t {
        return file_size > last_offset ? file_size - last_offset : 0;
    }
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CORE_TRANSFER_TYPES_H
