/**
 * @file error_codes.h
 * @brief Error codes for file_trans_system (-700 to -799 range)
 * @version 0.2.0
 *
 * This file defines all error codes used in the file transfer system.
 * Error codes follow the range -700 to -799 as per ecosystem convention.
 */

#ifndef KCENON_FILE_TRANSFER_CORE_ERROR_CODES_H
#define KCENON_FILE_TRANSFER_CORE_ERROR_CODES_H

#include <cstdint>
#include <string_view>

namespace kcenon::file_transfer {

/**
 * @brief Error codes for file transfer operations (-700 to -799)
 *
 * Error code ranges:
 * - -700 to -709: Connection Errors
 * - -710 to -719: Transfer Errors
 * - -720 to -739: Chunk Errors
 * - -740 to -749: Storage Errors
 * - -750 to -759: File I/O Errors
 * - -760 to -779: Resume Errors
 * - -780 to -789: Compression Errors
 * - -790 to -799: Configuration Errors
 */
enum class transfer_error_code : int32_t {
    success = 0,

    // Connection Errors (-700 to -709)
    connection_failed = -700,
    connection_timeout = -701,
    connection_refused = -702,
    connection_lost = -703,
    reconnect_failed = -704,
    session_expired = -705,
    server_busy = -706,
    protocol_mismatch = -707,

    // Transfer Errors (-710 to -719)
    transfer_init_failed = -710,
    transfer_cancelled = -711,
    transfer_timeout = -712,
    upload_rejected = -713,
    download_rejected = -714,
    transfer_already_exists = -715,
    transfer_not_found = -716,
    transfer_in_progress = -717,

    // Chunk Errors (-720 to -739)
    chunk_checksum_error = -720,
    chunk_sequence_error = -721,
    chunk_size_error = -722,
    file_hash_mismatch = -723,
    chunk_timeout = -724,
    chunk_duplicate = -725,

    // Storage Errors (-740 to -749)
    storage_error = -740,
    storage_unavailable = -741,
    storage_quota_exceeded = -742,
    max_file_size_exceeded = -743,
    file_already_exists = -744,
    storage_full = -745,
    file_not_found_on_server = -746,
    access_denied = -747,
    invalid_filename = -748,
    client_quota_exceeded = -749,

    // File I/O Errors (-750 to -759)
    file_read_error = -750,
    file_write_error = -751,
    file_permission_error = -752,
    file_not_found = -753,
    disk_full = -754,
    directory_not_found = -755,
    file_locked = -756,

    // Resume Errors (-760 to -779)
    resume_state_invalid = -760,
    resume_file_changed = -761,
    resume_state_corrupted = -762,
    resume_not_supported = -763,
    resume_transfer_not_found = -764,
    resume_session_mismatch = -765,

    // Compression Errors (-780 to -789)
    compression_failed = -780,
    decompression_failed = -781,
    compression_buffer_error = -782,
    invalid_compression_data = -783,

    // Configuration Errors (-790 to -799)
    config_invalid = -790,
    config_chunk_size_error = -791,
    config_transport_error = -792,
    config_storage_path_error = -793,
    config_quota_error = -794,
    config_reconnect_error = -795,
};

/**
 * @brief Convert transfer_error_code to string
 */
[[nodiscard]] constexpr auto to_string(transfer_error_code code) noexcept
    -> std::string_view {
    switch (code) {
        case transfer_error_code::success:
            return "success";

        // Connection Errors
        case transfer_error_code::connection_failed:
            return "connection failed";
        case transfer_error_code::connection_timeout:
            return "connection timeout";
        case transfer_error_code::connection_refused:
            return "connection refused";
        case transfer_error_code::connection_lost:
            return "connection lost";
        case transfer_error_code::reconnect_failed:
            return "reconnect failed after max attempts";
        case transfer_error_code::session_expired:
            return "session expired";
        case transfer_error_code::server_busy:
            return "server at maximum connections";
        case transfer_error_code::protocol_mismatch:
            return "protocol version incompatible";

        // Transfer Errors
        case transfer_error_code::transfer_init_failed:
            return "transfer initialization failed";
        case transfer_error_code::transfer_cancelled:
            return "transfer cancelled by user";
        case transfer_error_code::transfer_timeout:
            return "transfer timeout";
        case transfer_error_code::upload_rejected:
            return "upload rejected by server";
        case transfer_error_code::download_rejected:
            return "download rejected by server";
        case transfer_error_code::transfer_already_exists:
            return "transfer ID already in use";
        case transfer_error_code::transfer_not_found:
            return "transfer ID not found";
        case transfer_error_code::transfer_in_progress:
            return "transfer already in progress";

        // Chunk Errors
        case transfer_error_code::chunk_checksum_error:
            return "chunk CRC32 verification failed";
        case transfer_error_code::chunk_sequence_error:
            return "chunk sequence error";
        case transfer_error_code::chunk_size_error:
            return "chunk size exceeds maximum";
        case transfer_error_code::file_hash_mismatch:
            return "SHA-256 verification failed";
        case transfer_error_code::chunk_timeout:
            return "chunk acknowledgment timeout";
        case transfer_error_code::chunk_duplicate:
            return "duplicate chunk received";

        // Storage Errors
        case transfer_error_code::storage_error:
            return "storage error";
        case transfer_error_code::storage_unavailable:
            return "storage temporarily unavailable";
        case transfer_error_code::storage_quota_exceeded:
            return "storage quota exceeded";
        case transfer_error_code::max_file_size_exceeded:
            return "file exceeds maximum allowed size";
        case transfer_error_code::file_already_exists:
            return "file already exists on server";
        case transfer_error_code::storage_full:
            return "server storage full";
        case transfer_error_code::file_not_found_on_server:
            return "file not found on server";
        case transfer_error_code::access_denied:
            return "access denied";
        case transfer_error_code::invalid_filename:
            return "invalid filename";
        case transfer_error_code::client_quota_exceeded:
            return "per-client quota exceeded";

        // File I/O Errors
        case transfer_error_code::file_read_error:
            return "file read error";
        case transfer_error_code::file_write_error:
            return "file write error";
        case transfer_error_code::file_permission_error:
            return "file permission error";
        case transfer_error_code::file_not_found:
            return "local file not found";
        case transfer_error_code::disk_full:
            return "local disk full";
        case transfer_error_code::directory_not_found:
            return "directory not found";
        case transfer_error_code::file_locked:
            return "file locked by another process";

        // Resume Errors
        case transfer_error_code::resume_state_invalid:
            return "resume state invalid";
        case transfer_error_code::resume_file_changed:
            return "source file changed since last checkpoint";
        case transfer_error_code::resume_state_corrupted:
            return "resume state corrupted";
        case transfer_error_code::resume_not_supported:
            return "resume not supported for this transfer";
        case transfer_error_code::resume_transfer_not_found:
            return "transfer ID not found for resume";
        case transfer_error_code::resume_session_mismatch:
            return "resume session mismatch";

        // Compression Errors
        case transfer_error_code::compression_failed:
            return "compression failed";
        case transfer_error_code::decompression_failed:
            return "decompression failed";
        case transfer_error_code::compression_buffer_error:
            return "compression buffer error";
        case transfer_error_code::invalid_compression_data:
            return "invalid compression data";

        // Configuration Errors
        case transfer_error_code::config_invalid:
            return "invalid configuration";
        case transfer_error_code::config_chunk_size_error:
            return "chunk size out of valid range";
        case transfer_error_code::config_transport_error:
            return "transport configuration error";
        case transfer_error_code::config_storage_path_error:
            return "invalid storage directory";
        case transfer_error_code::config_quota_error:
            return "invalid quota configuration";
        case transfer_error_code::config_reconnect_error:
            return "invalid reconnect policy";

        default:
            return "unknown error";
    }
}

/**
 * @brief Get error message for a numeric error code
 */
[[nodiscard]] inline auto error_message(int32_t code) noexcept
    -> std::string_view {
    return to_string(static_cast<transfer_error_code>(code));
}

/**
 * @brief Check if error code is in connection error range
 */
[[nodiscard]] constexpr auto is_connection_error(int32_t code) noexcept
    -> bool {
    return code <= -700 && code >= -709;
}

/**
 * @brief Check if error code is in transfer error range
 */
[[nodiscard]] constexpr auto is_transfer_error(int32_t code) noexcept -> bool {
    return code <= -710 && code >= -719;
}

/**
 * @brief Check if error code is in chunk error range
 */
[[nodiscard]] constexpr auto is_chunk_error(int32_t code) noexcept -> bool {
    return code <= -720 && code >= -739;
}

/**
 * @brief Check if error code is in storage error range
 */
[[nodiscard]] constexpr auto is_storage_error(int32_t code) noexcept -> bool {
    return code <= -740 && code >= -749;
}

/**
 * @brief Check if error code is in I/O error range
 */
[[nodiscard]] constexpr auto is_io_error(int32_t code) noexcept -> bool {
    return code <= -750 && code >= -759;
}

/**
 * @brief Check if error code is in resume error range
 */
[[nodiscard]] constexpr auto is_resume_error(int32_t code) noexcept -> bool {
    return code <= -760 && code >= -779;
}

/**
 * @brief Check if error code is in compression error range
 */
[[nodiscard]] constexpr auto is_compression_error(int32_t code) noexcept
    -> bool {
    return code <= -780 && code >= -789;
}

/**
 * @brief Check if error code is in configuration error range
 */
[[nodiscard]] constexpr auto is_config_error(int32_t code) noexcept -> bool {
    return code <= -790 && code >= -799;
}

/**
 * @brief Check if the error is retryable
 */
[[nodiscard]] constexpr auto is_retryable(int32_t code) noexcept -> bool {
    switch (static_cast<transfer_error_code>(code)) {
        case transfer_error_code::connection_failed:
        case transfer_error_code::connection_timeout:
        case transfer_error_code::connection_refused:
        case transfer_error_code::connection_lost:
        case transfer_error_code::transfer_init_failed:
        case transfer_error_code::transfer_timeout:
        case transfer_error_code::chunk_checksum_error:
        case transfer_error_code::chunk_timeout:
        case transfer_error_code::file_hash_mismatch:
        case transfer_error_code::compression_failed:
        case transfer_error_code::decompression_failed:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Check if error is a client-side issue
 */
[[nodiscard]] constexpr auto is_client_error(int32_t code) noexcept -> bool {
    return is_io_error(code) || is_config_error(code);
}

/**
 * @brief Check if error is a server-side issue
 */
[[nodiscard]] constexpr auto is_server_error(int32_t code) noexcept -> bool {
    return is_storage_error(code);
}

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CORE_ERROR_CODES_H
