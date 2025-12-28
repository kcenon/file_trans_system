/**
 * @file cloud_utils.cpp
 * @brief Common utility functions for cloud storage implementations
 * @version 0.1.0
 */

#include "kcenon/file_transfer/cloud/cloud_utils.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <unordered_map>

#ifdef FILE_TRANS_ENABLE_ENCRYPTION
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#endif

namespace kcenon::file_transfer::cloud_utils {

// ============================================================================
// Encoding Utilities
// ============================================================================

auto bytes_to_hex(const std::vector<uint8_t>& bytes) -> std::string {
    std::ostringstream oss;
    for (auto byte : bytes) {
        oss << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(byte);
    }
    return oss.str();
}

namespace {
constexpr const char* BASE64_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr int BASE64_DECODE_TABLE[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};
}  // namespace

auto base64_encode(const std::vector<uint8_t>& data) -> std::string {
    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);

    for (std::size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < data.size()) n |= static_cast<uint32_t>(data[i + 2]);

        result += BASE64_CHARS[(n >> 18) & 0x3F];
        result += BASE64_CHARS[(n >> 12) & 0x3F];
        result += (i + 1 < data.size()) ? BASE64_CHARS[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < data.size()) ? BASE64_CHARS[n & 0x3F] : '=';
    }

    return result;
}

auto base64_encode(const std::string& data) -> std::string {
    std::vector<uint8_t> bytes(data.begin(), data.end());
    return base64_encode(bytes);
}

auto base64_decode(const std::string& encoded) -> std::vector<uint8_t> {
    std::vector<uint8_t> result;
    result.reserve((encoded.size() / 4) * 3);

    int bits = 0;
    int bit_count = 0;

    for (char c : encoded) {
        if (c == '=') break;
        int val = BASE64_DECODE_TABLE[static_cast<unsigned char>(c)];
        if (val < 0) continue;

        bits = (bits << 6) | val;
        bit_count += 6;

        if (bit_count >= 8) {
            bit_count -= 8;
            result.push_back(static_cast<uint8_t>((bits >> bit_count) & 0xFF));
        }
    }

    return result;
}

auto base64url_encode(const std::vector<uint8_t>& data) -> std::string {
    std::string result = base64_encode(data);

    // Replace + with -, / with _, and remove padding
    for (auto& c : result) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }

    // Remove padding
    while (!result.empty() && result.back() == '=') {
        result.pop_back();
    }

    return result;
}

auto base64url_encode(const std::string& data) -> std::string {
    std::vector<uint8_t> bytes(data.begin(), data.end());
    return base64url_encode(bytes);
}

auto url_encode(const std::string& value, bool encode_slash) -> std::string {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else if (c == '/' && !encode_slash) {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << std::uppercase
                    << static_cast<int>(static_cast<unsigned char>(c));
        }
    }

    return escaped.str();
}

// ============================================================================
// Cryptographic Utilities
// ============================================================================

auto sha256(const std::string& data) -> std::vector<uint8_t> {
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const unsigned char*>(data.data()),
           data.size(),
           hash.data());
    return hash;
#else
    return std::vector<uint8_t>(32, 0);
#endif
}

auto sha256_bytes(std::span<const std::byte> data) -> std::vector<uint8_t> {
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const unsigned char*>(data.data()),
           data.size(),
           hash.data());
    return hash;
#else
    return std::vector<uint8_t>(32, 0);
#endif
}

auto hmac_sha256(const std::vector<uint8_t>& key,
                 const std::string& data) -> std::vector<uint8_t> {
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    std::vector<uint8_t> result(EVP_MAX_MD_SIZE);
    unsigned int len = 0;

    HMAC(EVP_sha256(),
         key.data(),
         static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()),
         data.size(),
         result.data(),
         &len);

    result.resize(len);
    return result;
#else
    return std::vector<uint8_t>(32, 0);
#endif
}

auto hmac_sha256(const std::string& key,
                 const std::string& data) -> std::vector<uint8_t> {
    std::vector<uint8_t> key_bytes(key.begin(), key.end());
    return hmac_sha256(key_bytes, data);
}

// ============================================================================
// Time Utilities
// ============================================================================

namespace {
auto get_current_tm() -> std::tm {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time_t);
#else
    gmtime_r(&time_t, &tm);
#endif
    return tm;
}

auto get_future_tm(std::chrono::seconds duration) -> std::tm {
    auto now = std::chrono::system_clock::now();
    auto future = now + duration;
    auto time_t = std::chrono::system_clock::to_time_t(future);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time_t);
#else
    gmtime_r(&time_t, &tm);
#endif
    return tm;
}
}  // namespace

auto get_iso8601_time() -> std::string {
    auto tm = get_current_tm();
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
    return oss.str();
}

auto get_date_stamp() -> std::string {
    auto tm = get_current_tm();
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d");
    return oss.str();
}

auto get_rfc3339_time() -> std::string {
    auto tm = get_current_tm();
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

auto get_rfc1123_time() -> std::string {
    auto tm = get_current_tm();
    std::ostringstream oss;
    oss << std::put_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");
    return oss.str();
}

auto get_unix_timestamp() -> int64_t {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
}

auto get_future_rfc3339_time(std::chrono::seconds duration) -> std::string {
    auto tm = get_future_tm(duration);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

auto get_future_iso8601_time(std::chrono::seconds duration) -> std::string {
    auto tm = get_future_tm(duration);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

// ============================================================================
// Random Utilities
// ============================================================================

auto generate_random_bytes(std::size_t count) -> std::vector<uint8_t> {
    std::vector<uint8_t> bytes(count);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    for (std::size_t i = 0; i < count; ++i) {
        bytes[i] = static_cast<uint8_t>(dis(gen));
    }

    return bytes;
}

auto generate_random_hex(std::size_t byte_count) -> std::string {
    auto bytes = generate_random_bytes(byte_count);
    return bytes_to_hex(bytes);
}

// ============================================================================
// XML Utilities
// ============================================================================

auto extract_xml_element(const std::string& xml,
                         const std::string& tag) -> std::optional<std::string> {
    std::string open_tag = "<" + tag + ">";
    std::string close_tag = "</" + tag + ">";

    auto start_pos = xml.find(open_tag);
    if (start_pos == std::string::npos) {
        return std::nullopt;
    }
    start_pos += open_tag.length();

    auto end_pos = xml.find(close_tag, start_pos);
    if (end_pos == std::string::npos) {
        return std::nullopt;
    }

    return xml.substr(start_pos, end_pos - start_pos);
}

// ============================================================================
// JSON Utilities
// ============================================================================

auto extract_json_value(const std::string& json,
                        const std::string& key) -> std::optional<std::string> {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        return std::nullopt;
    }

    pos = json.find(':', pos + search.length());
    if (pos == std::string::npos) {
        return std::nullopt;
    }

    // Skip whitespace
    pos = json.find_first_not_of(" \t\n\r", pos + 1);
    if (pos == std::string::npos) {
        return std::nullopt;
    }

    // Check if value is a string (starts with quote)
    if (json[pos] == '"') {
        auto end_pos = pos + 1;
        while (end_pos < json.size()) {
            if (json[end_pos] == '"' && json[end_pos - 1] != '\\') {
                break;
            }
            ++end_pos;
        }
        return json.substr(pos + 1, end_pos - pos - 1);
    }

    // Check if value is a number or other non-string value
    auto end_pos = json.find_first_of(",}\n", pos);
    if (end_pos == std::string::npos) {
        end_pos = json.size();
    }
    return json.substr(pos, end_pos - pos);
}

// ============================================================================
// Content Type Detection
// ============================================================================

auto detect_content_type(const std::string& key) -> std::string {
    static const std::unordered_map<std::string, std::string> mime_types = {
        {".txt", "text/plain"},
        {".html", "text/html"},
        {".htm", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".json", "application/json"},
        {".xml", "application/xml"},
        {".pdf", "application/pdf"},
        {".zip", "application/zip"},
        {".gz", "application/gzip"},
        {".tar", "application/x-tar"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".svg", "image/svg+xml"},
        {".webp", "image/webp"},
        {".ico", "image/x-icon"},
        {".mp3", "audio/mpeg"},
        {".mp4", "video/mp4"},
        {".webm", "video/webm"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},
        {".otf", "font/otf"},
    };

    auto dot_pos = key.rfind('.');
    if (dot_pos == std::string::npos) {
        return "application/octet-stream";
    }

    std::string ext = key.substr(dot_pos);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    auto it = mime_types.find(ext);
    if (it != mime_types.end()) {
        return it->second;
    }

    return "application/octet-stream";
}

// ============================================================================
// Retry Policy Utilities
// ============================================================================

auto calculate_retry_delay(const cloud_retry_policy& policy,
                           std::size_t attempt) -> std::chrono::milliseconds {
    auto delay = static_cast<double>(policy.initial_delay.count());

    for (std::size_t i = 1; i < attempt; ++i) {
        delay *= policy.backoff_multiplier;
    }

    delay = std::min(delay, static_cast<double>(policy.max_delay.count()));

    if (policy.use_jitter) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0.5, 1.5);
        delay *= dis(gen);
    }

    return std::chrono::milliseconds(static_cast<int64_t>(delay));
}

auto is_retryable_status(int status_code,
                         const cloud_retry_policy& policy) -> bool {
    // Rate limiting
    if (policy.retry_on_rate_limit && (status_code == 429 || status_code == 503)) {
        return true;
    }

    // Server errors
    if (policy.retry_on_server_error && status_code >= 500 && status_code < 600) {
        return true;
    }

    return false;
}

}  // namespace kcenon::file_transfer::cloud_utils
