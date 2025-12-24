/**
 * @file cloud_error.h
 * @brief Error codes for cloud storage operations (-800 to -899 range)
 * @version 0.1.0
 *
 * This file defines all error codes used in cloud storage operations.
 * Error codes follow the range -800 to -899 as per ecosystem convention.
 */

#ifndef KCENON_FILE_TRANSFER_CLOUD_CLOUD_ERROR_H
#define KCENON_FILE_TRANSFER_CLOUD_CLOUD_ERROR_H

#include <cstdint>
#include <string_view>

namespace kcenon::file_transfer {

/**
 * @brief Error codes for cloud storage operations (-800 to -899)
 *
 * Error code ranges:
 * - -800 to -809: Authentication Errors
 * - -810 to -819: Authorization Errors
 * - -820 to -829: Connection/Network Errors
 * - -830 to -839: Bucket/Container Errors
 * - -840 to -849: Object/Blob Errors
 * - -850 to -859: Transfer Errors
 * - -860 to -869: Quota/Limit Errors
 * - -870 to -879: Provider-specific Errors
 * - -880 to -889: Configuration Errors
 * - -890 to -899: Internal Errors
 */
enum class cloud_error_code : int32_t {
    success = 0,

    // Authentication Errors (-800 to -809)
    auth_failed = -800,              ///< Authentication failed
    auth_expired = -801,             ///< Authentication token expired
    auth_invalid_credentials = -802, ///< Invalid credentials provided
    auth_missing_credentials = -803, ///< Credentials not provided
    auth_token_refresh_failed = -804,///< Failed to refresh authentication token
    auth_mfa_required = -805,        ///< Multi-factor authentication required

    // Authorization Errors (-810 to -819)
    access_denied = -810,            ///< Access denied to resource
    permission_denied = -811,        ///< Permission denied for operation
    resource_forbidden = -812,       ///< Resource access forbidden
    policy_violation = -813,         ///< Policy violation

    // Connection/Network Errors (-820 to -829)
    connection_failed = -820,        ///< Failed to connect to cloud provider
    connection_timeout = -821,       ///< Connection timeout
    network_error = -822,            ///< Network error occurred
    dns_resolution_failed = -823,    ///< DNS resolution failed
    ssl_handshake_failed = -824,     ///< SSL/TLS handshake failed
    connection_reset = -825,         ///< Connection reset by peer
    service_unavailable = -826,      ///< Cloud service temporarily unavailable
    rate_limited = -827,             ///< Request rate limited

    // Bucket/Container Errors (-830 to -839)
    bucket_not_found = -830,         ///< Bucket/container not found
    bucket_already_exists = -831,    ///< Bucket/container already exists
    bucket_not_empty = -832,         ///< Bucket/container is not empty
    invalid_bucket_name = -833,      ///< Invalid bucket/container name
    bucket_access_denied = -834,     ///< Access denied to bucket/container
    bucket_quota_exceeded = -835,    ///< Bucket quota exceeded

    // Object/Blob Errors (-840 to -849)
    object_not_found = -840,         ///< Object/blob not found
    object_already_exists = -841,    ///< Object/blob already exists
    invalid_object_key = -842,       ///< Invalid object key/path
    object_too_large = -843,         ///< Object exceeds maximum size
    object_corrupted = -844,         ///< Object data corrupted
    checksum_mismatch = -845,        ///< Checksum verification failed
    invalid_metadata = -846,         ///< Invalid object metadata
    version_not_found = -847,        ///< Object version not found

    // Transfer Errors (-850 to -859)
    upload_failed = -850,            ///< Upload operation failed
    download_failed = -851,          ///< Download operation failed
    multipart_init_failed = -852,    ///< Multipart upload initialization failed
    multipart_upload_failed = -853,  ///< Multipart upload part failed
    multipart_complete_failed = -854,///< Multipart upload completion failed
    multipart_abort_failed = -855,   ///< Multipart upload abort failed
    transfer_cancelled = -856,       ///< Transfer was cancelled
    transfer_timeout = -857,         ///< Transfer operation timeout

    // Quota/Limit Errors (-860 to -869)
    storage_quota_exceeded = -860,   ///< Storage quota exceeded
    bandwidth_limit_exceeded = -861, ///< Bandwidth limit exceeded
    request_limit_exceeded = -862,   ///< Request limit exceeded
    object_count_exceeded = -863,    ///< Maximum object count exceeded
    file_size_limit_exceeded = -864, ///< File size limit exceeded

    // Provider-specific Errors (-870 to -879)
    provider_error = -870,           ///< Provider-specific error
    s3_error = -871,                 ///< AWS S3 specific error
    azure_error = -872,              ///< Azure Blob Storage specific error
    gcs_error = -873,                ///< Google Cloud Storage specific error
    unsupported_operation = -874,    ///< Operation not supported by provider
    region_not_available = -875,     ///< Region not available

    // Configuration Errors (-880 to -889)
    config_invalid = -880,           ///< Invalid configuration
    config_missing_endpoint = -881,  ///< Missing endpoint configuration
    config_missing_region = -882,    ///< Missing region configuration
    config_missing_bucket = -883,    ///< Missing bucket configuration
    config_invalid_retry = -884,     ///< Invalid retry configuration

    // Internal Errors (-890 to -899)
    internal_error = -890,           ///< Internal error
    not_initialized = -891,          ///< Cloud storage not initialized
    already_initialized = -892,      ///< Cloud storage already initialized
    operation_in_progress = -893,    ///< Another operation is in progress
    invalid_state = -894,            ///< Invalid state for operation
    memory_allocation_failed = -895, ///< Memory allocation failed
};

/**
 * @brief Convert cloud_error_code to string
 */
[[nodiscard]] constexpr auto to_string(cloud_error_code code) noexcept
    -> std::string_view {
    switch (code) {
        case cloud_error_code::success:
            return "success";

        // Authentication Errors
        case cloud_error_code::auth_failed:
            return "authentication failed";
        case cloud_error_code::auth_expired:
            return "authentication token expired";
        case cloud_error_code::auth_invalid_credentials:
            return "invalid credentials provided";
        case cloud_error_code::auth_missing_credentials:
            return "credentials not provided";
        case cloud_error_code::auth_token_refresh_failed:
            return "failed to refresh authentication token";
        case cloud_error_code::auth_mfa_required:
            return "multi-factor authentication required";

        // Authorization Errors
        case cloud_error_code::access_denied:
            return "access denied to resource";
        case cloud_error_code::permission_denied:
            return "permission denied for operation";
        case cloud_error_code::resource_forbidden:
            return "resource access forbidden";
        case cloud_error_code::policy_violation:
            return "policy violation";

        // Connection/Network Errors
        case cloud_error_code::connection_failed:
            return "failed to connect to cloud provider";
        case cloud_error_code::connection_timeout:
            return "connection timeout";
        case cloud_error_code::network_error:
            return "network error occurred";
        case cloud_error_code::dns_resolution_failed:
            return "DNS resolution failed";
        case cloud_error_code::ssl_handshake_failed:
            return "SSL/TLS handshake failed";
        case cloud_error_code::connection_reset:
            return "connection reset by peer";
        case cloud_error_code::service_unavailable:
            return "cloud service temporarily unavailable";
        case cloud_error_code::rate_limited:
            return "request rate limited";

        // Bucket/Container Errors
        case cloud_error_code::bucket_not_found:
            return "bucket/container not found";
        case cloud_error_code::bucket_already_exists:
            return "bucket/container already exists";
        case cloud_error_code::bucket_not_empty:
            return "bucket/container is not empty";
        case cloud_error_code::invalid_bucket_name:
            return "invalid bucket/container name";
        case cloud_error_code::bucket_access_denied:
            return "access denied to bucket/container";
        case cloud_error_code::bucket_quota_exceeded:
            return "bucket quota exceeded";

        // Object/Blob Errors
        case cloud_error_code::object_not_found:
            return "object/blob not found";
        case cloud_error_code::object_already_exists:
            return "object/blob already exists";
        case cloud_error_code::invalid_object_key:
            return "invalid object key/path";
        case cloud_error_code::object_too_large:
            return "object exceeds maximum size";
        case cloud_error_code::object_corrupted:
            return "object data corrupted";
        case cloud_error_code::checksum_mismatch:
            return "checksum verification failed";
        case cloud_error_code::invalid_metadata:
            return "invalid object metadata";
        case cloud_error_code::version_not_found:
            return "object version not found";

        // Transfer Errors
        case cloud_error_code::upload_failed:
            return "upload operation failed";
        case cloud_error_code::download_failed:
            return "download operation failed";
        case cloud_error_code::multipart_init_failed:
            return "multipart upload initialization failed";
        case cloud_error_code::multipart_upload_failed:
            return "multipart upload part failed";
        case cloud_error_code::multipart_complete_failed:
            return "multipart upload completion failed";
        case cloud_error_code::multipart_abort_failed:
            return "multipart upload abort failed";
        case cloud_error_code::transfer_cancelled:
            return "transfer was cancelled";
        case cloud_error_code::transfer_timeout:
            return "transfer operation timeout";

        // Quota/Limit Errors
        case cloud_error_code::storage_quota_exceeded:
            return "storage quota exceeded";
        case cloud_error_code::bandwidth_limit_exceeded:
            return "bandwidth limit exceeded";
        case cloud_error_code::request_limit_exceeded:
            return "request limit exceeded";
        case cloud_error_code::object_count_exceeded:
            return "maximum object count exceeded";
        case cloud_error_code::file_size_limit_exceeded:
            return "file size limit exceeded";

        // Provider-specific Errors
        case cloud_error_code::provider_error:
            return "provider-specific error";
        case cloud_error_code::s3_error:
            return "AWS S3 specific error";
        case cloud_error_code::azure_error:
            return "Azure Blob Storage specific error";
        case cloud_error_code::gcs_error:
            return "Google Cloud Storage specific error";
        case cloud_error_code::unsupported_operation:
            return "operation not supported by provider";
        case cloud_error_code::region_not_available:
            return "region not available";

        // Configuration Errors
        case cloud_error_code::config_invalid:
            return "invalid configuration";
        case cloud_error_code::config_missing_endpoint:
            return "missing endpoint configuration";
        case cloud_error_code::config_missing_region:
            return "missing region configuration";
        case cloud_error_code::config_missing_bucket:
            return "missing bucket configuration";
        case cloud_error_code::config_invalid_retry:
            return "invalid retry configuration";

        // Internal Errors
        case cloud_error_code::internal_error:
            return "internal error";
        case cloud_error_code::not_initialized:
            return "cloud storage not initialized";
        case cloud_error_code::already_initialized:
            return "cloud storage already initialized";
        case cloud_error_code::operation_in_progress:
            return "another operation is in progress";
        case cloud_error_code::invalid_state:
            return "invalid state for operation";
        case cloud_error_code::memory_allocation_failed:
            return "memory allocation failed";

        default:
            return "unknown cloud error";
    }
}

/**
 * @brief Get error message for a numeric cloud error code
 */
[[nodiscard]] inline auto cloud_error_message(int32_t code) noexcept
    -> std::string_view {
    return to_string(static_cast<cloud_error_code>(code));
}

/**
 * @brief Check if error code is in authentication error range
 */
[[nodiscard]] constexpr auto is_auth_error(int32_t code) noexcept -> bool {
    return code <= -800 && code >= -809;
}

/**
 * @brief Check if error code is in authorization error range
 */
[[nodiscard]] constexpr auto is_authorization_error(int32_t code) noexcept -> bool {
    return code <= -810 && code >= -819;
}

/**
 * @brief Check if error code is in connection/network error range
 */
[[nodiscard]] constexpr auto is_cloud_connection_error(int32_t code) noexcept -> bool {
    return code <= -820 && code >= -829;
}

/**
 * @brief Check if error code is in bucket/container error range
 */
[[nodiscard]] constexpr auto is_bucket_error(int32_t code) noexcept -> bool {
    return code <= -830 && code >= -839;
}

/**
 * @brief Check if error code is in object/blob error range
 */
[[nodiscard]] constexpr auto is_object_error(int32_t code) noexcept -> bool {
    return code <= -840 && code >= -849;
}

/**
 * @brief Check if error code is in transfer error range
 */
[[nodiscard]] constexpr auto is_cloud_transfer_error(int32_t code) noexcept -> bool {
    return code <= -850 && code >= -859;
}

/**
 * @brief Check if error code is in quota/limit error range
 */
[[nodiscard]] constexpr auto is_quota_error(int32_t code) noexcept -> bool {
    return code <= -860 && code >= -869;
}

/**
 * @brief Check if error code is in provider-specific error range
 */
[[nodiscard]] constexpr auto is_provider_error(int32_t code) noexcept -> bool {
    return code <= -870 && code >= -879;
}

/**
 * @brief Check if error code is in cloud configuration error range
 */
[[nodiscard]] constexpr auto is_cloud_config_error(int32_t code) noexcept -> bool {
    return code <= -880 && code >= -889;
}

/**
 * @brief Check if error code is in internal error range
 */
[[nodiscard]] constexpr auto is_cloud_internal_error(int32_t code) noexcept -> bool {
    return code <= -890 && code >= -899;
}

/**
 * @brief Check if the cloud error is retryable
 */
[[nodiscard]] constexpr auto is_cloud_retryable(int32_t code) noexcept -> bool {
    switch (static_cast<cloud_error_code>(code)) {
        case cloud_error_code::auth_expired:
        case cloud_error_code::auth_token_refresh_failed:
        case cloud_error_code::connection_failed:
        case cloud_error_code::connection_timeout:
        case cloud_error_code::network_error:
        case cloud_error_code::connection_reset:
        case cloud_error_code::service_unavailable:
        case cloud_error_code::rate_limited:
        case cloud_error_code::upload_failed:
        case cloud_error_code::download_failed:
        case cloud_error_code::multipart_upload_failed:
        case cloud_error_code::transfer_timeout:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Check if error is a client-side issue
 */
[[nodiscard]] constexpr auto is_cloud_client_error(int32_t code) noexcept -> bool {
    return is_auth_error(code) || is_cloud_config_error(code);
}

/**
 * @brief Check if error is a server-side issue
 */
[[nodiscard]] constexpr auto is_cloud_server_error(int32_t code) noexcept -> bool {
    return code == static_cast<int32_t>(cloud_error_code::service_unavailable) ||
           code == static_cast<int32_t>(cloud_error_code::rate_limited) ||
           is_provider_error(code);
}

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CLOUD_CLOUD_ERROR_H
