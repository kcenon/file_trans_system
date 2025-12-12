/**
 * @file types.h
 * @brief Core type definitions for file_trans_system
 */

#ifndef KCENON_FILE_TRANSFER_CORE_TYPES_H
#define KCENON_FILE_TRANSFER_CORE_TYPES_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace kcenon::file_transfer {

/**
 * @brief Error codes for file transfer operations
 */
enum class error_code {
    success = 0,

    // File errors (-100 to -119)
    file_not_found = -100,
    file_access_denied = -101,
    file_already_exists = -102,
    file_too_large = -103,
    invalid_file_path = -104,
    file_read_error = -105,
    file_write_error = -106,

    // Chunk errors (-120 to -139)
    chunk_checksum_error = -120,
    chunk_sequence_error = -121,
    chunk_size_error = -122,
    file_hash_mismatch = -123,
    invalid_chunk_index = -124,
    missing_chunks = -125,

    // Configuration errors (-140 to -159)
    invalid_chunk_size = -140,
    invalid_configuration = -141,

    // Network errors (-160 to -179)
    connection_failed = -160,
    connection_timeout = -161,
    connection_refused = -162,
    connection_lost = -163,
    server_not_running = -164,

    // Quota errors (-180 to -199)
    quota_exceeded = -180,
    storage_full = -181,

    // Internal errors (-200 to -219)
    internal_error = -200,
    not_initialized = -201,
    already_initialized = -202,
};

/**
 * @brief Convert error code to string
 */
[[nodiscard]] constexpr auto to_string(error_code code) -> const char* {
    switch (code) {
        case error_code::success:
            return "success";
        case error_code::file_not_found:
            return "file not found";
        case error_code::file_access_denied:
            return "file access denied";
        case error_code::file_already_exists:
            return "file already exists";
        case error_code::file_too_large:
            return "file too large";
        case error_code::invalid_file_path:
            return "invalid file path";
        case error_code::file_read_error:
            return "file read error";
        case error_code::file_write_error:
            return "file write error";
        case error_code::chunk_checksum_error:
            return "chunk checksum error";
        case error_code::chunk_sequence_error:
            return "chunk sequence error";
        case error_code::chunk_size_error:
            return "chunk size error";
        case error_code::file_hash_mismatch:
            return "file hash mismatch";
        case error_code::invalid_chunk_index:
            return "invalid chunk index";
        case error_code::missing_chunks:
            return "missing chunks";
        case error_code::invalid_chunk_size:
            return "invalid chunk size";
        case error_code::invalid_configuration:
            return "invalid configuration";
        case error_code::connection_failed:
            return "connection failed";
        case error_code::connection_timeout:
            return "connection timeout";
        case error_code::connection_refused:
            return "connection refused";
        case error_code::connection_lost:
            return "connection lost";
        case error_code::server_not_running:
            return "server not running";
        case error_code::quota_exceeded:
            return "quota exceeded";
        case error_code::storage_full:
            return "storage full";
        case error_code::internal_error:
            return "internal error";
        case error_code::not_initialized:
            return "not initialized";
        case error_code::already_initialized:
            return "already initialized";
        default:
            return "unknown error";
    }
}

/**
 * @brief Error type with code and optional message
 */
struct error {
    error_code code;
    std::string message;

    error() : code(error_code::success) {}
    explicit error(error_code c) : code(c), message(to_string(c)) {}
    error(error_code c, std::string msg) : code(c), message(std::move(msg)) {}

    [[nodiscard]] explicit operator bool() const noexcept {
        return code != error_code::success;
    }
};

/**
 * @brief Wrapper for unexpected error (used with result<T>)
 */
struct unexpected {
    error err;

    explicit unexpected(error e) : err(std::move(e)) {}
};

/**
 * @brief Result type for operations that can fail
 *
 * A simple Result type similar to std::expected (C++23).
 * Contains either a value of type T or an error.
 */
template <typename T>
class result {
public:
    result() : value_(std::nullopt), error_{} {}

    result(T value) : value_(std::move(value)), error_{} {}

    result(unexpected u) : value_(std::nullopt), error_(std::move(u.err)) {}

    result(const result&) = default;
    result(result&&) noexcept = default;
    auto operator=(const result&) -> result& = default;
    auto operator=(result&&) noexcept -> result& = default;

    [[nodiscard]] auto has_value() const noexcept -> bool { return value_.has_value(); }
    [[nodiscard]] explicit operator bool() const noexcept { return value_.has_value(); }

    [[nodiscard]] auto value() & -> T& { return *value_; }
    [[nodiscard]] auto value() const& -> const T& { return *value_; }
    [[nodiscard]] auto value() && -> T&& { return std::move(*value_); }

    [[nodiscard]] auto error() const -> const struct error& { return error_; }

private:
    std::optional<T> value_;
    struct error error_;
};

/**
 * @brief Specialization of result for void return type
 */
template <>
class result<void> {
public:
    result() : has_value_(true) {}

    result(unexpected u) : has_value_(false), error_(std::move(u.err)) {}

    result(const result&) = default;
    result(result&&) noexcept = default;
    auto operator=(const result&) -> result& = default;
    auto operator=(result&&) noexcept -> result& = default;

    [[nodiscard]] auto has_value() const noexcept -> bool { return has_value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return has_value_; }

    [[nodiscard]] auto error() const -> const struct error& { return error_; }

private:
    bool has_value_;
    struct error error_;
};

/**
 * @brief Unique identifier for a transfer session
 */
struct transfer_id {
    uint64_t value;

    transfer_id() : value(0) {}
    explicit transfer_id(uint64_t v) : value(v) {}

    [[nodiscard]] auto operator==(const transfer_id& other) const -> bool = default;
    [[nodiscard]] auto operator<(const transfer_id& other) const -> bool {
        return value < other.value;
    }
};

/**
 * @brief Chunk flags
 */
enum class chunk_flags : uint8_t {
    none = 0,
    compressed = 1 << 0,
    last_chunk = 1 << 1,
    encrypted = 1 << 2,
};

[[nodiscard]] constexpr auto operator|(chunk_flags a, chunk_flags b) -> chunk_flags {
    return static_cast<chunk_flags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

[[nodiscard]] constexpr auto operator&(chunk_flags a, chunk_flags b) -> chunk_flags {
    return static_cast<chunk_flags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

[[nodiscard]] constexpr auto has_flag(chunk_flags flags, chunk_flags flag) -> bool {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

/**
 * @brief Chunk data structure
 */
struct chunk {
    transfer_id id;
    uint64_t index;
    uint64_t total_chunks;
    uint64_t offset;
    uint32_t checksum;
    chunk_flags flags;
    std::vector<std::byte> data;

    chunk() : index(0), total_chunks(0), offset(0), checksum(0), flags(chunk_flags::none) {}
};

/**
 * @brief File metadata
 */
struct file_metadata {
    std::string filename;
    uint64_t file_size;
    uint64_t total_chunks;
    std::size_t chunk_size;
    std::string sha256_hash;

    file_metadata()
        : file_size(0), total_chunks(0), chunk_size(0) {}
};

/**
 * @brief Assembly progress information
 */
struct assembly_progress {
    transfer_id id;
    uint64_t total_chunks;
    uint64_t received_chunks;
    uint64_t bytes_written;

    [[nodiscard]] auto completion_percentage() const -> double {
        if (total_chunks == 0) return 0.0;
        return static_cast<double>(received_chunks) / static_cast<double>(total_chunks) * 100.0;
    }
};

}  // namespace kcenon::file_transfer

// Hash support for transfer_id
template <>
struct std::hash<kcenon::file_transfer::transfer_id> {
    auto operator()(const kcenon::file_transfer::transfer_id& id) const noexcept -> std::size_t {
        return std::hash<uint64_t>{}(id.value);
    }
};

#endif  // KCENON_FILE_TRANSFER_CORE_TYPES_H
