/**
 * @file cloud_utils.h
 * @brief Common utility functions for cloud storage implementations
 * @version 0.1.0
 *
 * This file provides shared utility functions used across S3, GCS, and Azure
 * cloud storage implementations to reduce code duplication.
 */

#ifndef KCENON_FILE_TRANSFER_CLOUD_CLOUD_UTILS_H
#define KCENON_FILE_TRANSFER_CLOUD_CLOUD_UTILS_H

#include "cloud_config.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kcenon::file_transfer::cloud_utils {

// ============================================================================
// Encoding Utilities
// ============================================================================

/**
 * @brief Convert bytes to hexadecimal string
 * @param bytes Vector of bytes to convert
 * @return Lowercase hexadecimal string representation
 */
auto bytes_to_hex(const std::vector<uint8_t>& bytes) -> std::string;

/**
 * @brief Base64 encode bytes
 * @param data Vector of bytes to encode
 * @return Base64 encoded string
 */
auto base64_encode(const std::vector<uint8_t>& data) -> std::string;

/**
 * @brief Base64 encode string
 * @param data String to encode
 * @return Base64 encoded string
 */
auto base64_encode(const std::string& data) -> std::string;

/**
 * @brief Base64 decode string
 * @param encoded Base64 encoded string
 * @return Decoded bytes
 */
auto base64_decode(const std::string& encoded) -> std::vector<uint8_t>;

/**
 * @brief Base64 URL-safe encode (for JWT)
 * @param data Vector of bytes to encode
 * @return URL-safe base64 encoded string without padding
 */
auto base64url_encode(const std::vector<uint8_t>& data) -> std::string;

/**
 * @brief Base64 URL-safe encode string
 * @param data String to encode
 * @return URL-safe base64 encoded string without padding
 */
auto base64url_encode(const std::string& data) -> std::string;

/**
 * @brief URL encode a string (RFC 3986)
 * @param value String to encode
 * @param encode_slash Whether to encode forward slashes (default: true)
 * @return URL encoded string
 */
auto url_encode(const std::string& value, bool encode_slash = true) -> std::string;

// ============================================================================
// Cryptographic Utilities
// ============================================================================

/**
 * @brief SHA256 hash function
 * @param data String to hash
 * @return SHA256 hash bytes (32 bytes), or zeros if encryption disabled
 */
auto sha256(const std::string& data) -> std::vector<uint8_t>;

/**
 * @brief SHA256 hash of bytes
 * @param data Span of bytes to hash
 * @return SHA256 hash bytes (32 bytes), or zeros if encryption disabled
 */
auto sha256_bytes(std::span<const std::byte> data) -> std::vector<uint8_t>;

/**
 * @brief HMAC-SHA256
 * @param key Key bytes
 * @param data Data to sign
 * @return HMAC-SHA256 result (32 bytes), or zeros if encryption disabled
 */
auto hmac_sha256(const std::vector<uint8_t>& key,
                 const std::string& data) -> std::vector<uint8_t>;

/**
 * @brief HMAC-SHA256 with string key
 * @param key Key string
 * @param data Data to sign
 * @return HMAC-SHA256 result (32 bytes), or zeros if encryption disabled
 */
auto hmac_sha256(const std::string& key,
                 const std::string& data) -> std::vector<uint8_t>;

// ============================================================================
// Time Utilities
// ============================================================================

/**
 * @brief Get current UTC time as ISO 8601 string (YYYYMMDD'T'HHMMSS'Z')
 * @return ISO 8601 formatted timestamp
 */
auto get_iso8601_time() -> std::string;

/**
 * @brief Get current UTC date as YYYYMMDD string
 * @return Date stamp
 */
auto get_date_stamp() -> std::string;

/**
 * @brief Get current UTC time in RFC 3339 format
 * @return RFC 3339 formatted timestamp
 */
auto get_rfc3339_time() -> std::string;

/**
 * @brief Get current UTC time in RFC 1123 format
 * @return RFC 1123 formatted timestamp
 */
auto get_rfc1123_time() -> std::string;

/**
 * @brief Get current UTC timestamp in seconds since epoch
 * @return Unix timestamp
 */
auto get_unix_timestamp() -> int64_t;

/**
 * @brief Get future UTC time in RFC 3339 format
 * @param duration Duration from now
 * @return RFC 3339 formatted future timestamp
 */
auto get_future_rfc3339_time(std::chrono::seconds duration) -> std::string;

/**
 * @brief Get future UTC time in ISO 8601 format
 * @param duration Duration from now
 * @return ISO 8601 formatted future timestamp
 */
auto get_future_iso8601_time(std::chrono::seconds duration) -> std::string;

// ============================================================================
// Random Utilities
// ============================================================================

/**
 * @brief Generate random bytes
 * @param count Number of bytes to generate
 * @return Vector of random bytes
 */
auto generate_random_bytes(std::size_t count) -> std::vector<uint8_t>;

/**
 * @brief Generate random hex string
 * @param byte_count Number of random bytes (result will be 2x this length)
 * @return Random hex string
 */
auto generate_random_hex(std::size_t byte_count) -> std::string;

// ============================================================================
// XML Utilities
// ============================================================================

/**
 * @brief Extract XML element value
 * @param xml XML string to parse
 * @param tag Tag name to extract
 * @return Element value if found, nullopt otherwise
 */
auto extract_xml_element(const std::string& xml,
                         const std::string& tag) -> std::optional<std::string>;

// ============================================================================
// JSON Utilities
// ============================================================================

/**
 * @brief Extract JSON string value (simple parser for known structure)
 * @param json JSON string to parse
 * @param key Key to extract
 * @return Value if found, nullopt otherwise
 */
auto extract_json_value(const std::string& json,
                        const std::string& key) -> std::optional<std::string>;

// ============================================================================
// Content Type Detection
// ============================================================================

/**
 * @brief Detect MIME content type from file extension
 * @param key File path or key
 * @return MIME type string (defaults to "application/octet-stream")
 */
auto detect_content_type(const std::string& key) -> std::string;

// ============================================================================
// Retry Policy Utilities
// ============================================================================

/**
 * @brief Calculate delay with exponential backoff and jitter
 * @param policy Retry policy configuration
 * @param attempt Current attempt number (1-based)
 * @return Delay to wait before next retry
 */
auto calculate_retry_delay(const cloud_retry_policy& policy,
                           std::size_t attempt) -> std::chrono::milliseconds;

/**
 * @brief Check if HTTP status code indicates a retryable error
 * @param status_code HTTP status code
 * @param policy Retry policy configuration
 * @return true if the error is retryable
 */
auto is_retryable_status(int status_code,
                         const cloud_retry_policy& policy) -> bool;

}  // namespace kcenon::file_transfer::cloud_utils

#endif  // KCENON_FILE_TRANSFER_CLOUD_CLOUD_UTILS_H
