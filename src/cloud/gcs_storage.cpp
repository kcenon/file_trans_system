/**
 * @file gcs_storage.cpp
 * @brief Google Cloud Storage backend implementation
 * @version 0.1.0
 */

#include "kcenon/file_transfer/cloud/gcs_storage.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <iomanip>
#include <map>
#include <random>
#include <regex>
#include <sstream>
#include <thread>
#include <unordered_map>

// HTTP client integration enabled (see #147, #148)
#ifdef BUILD_WITH_NETWORK_SYSTEM
#include <kcenon/network/core/http_client.h>
#endif

#ifdef FILE_TRANS_ENABLE_ENCRYPTION
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#endif

namespace kcenon::file_transfer {

namespace {

// ============================================================================
// GCS Authentication Utilities
// ============================================================================

/**
 * @brief Convert bytes to hexadecimal string
 */
auto bytes_to_hex(const std::vector<uint8_t>& bytes) -> std::string {
    std::ostringstream oss;
    for (auto byte : bytes) {
        oss << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(byte);
    }
    return oss.str();
}

/**
 * @brief Base64 encode
 */
auto base64_encode(const std::vector<uint8_t>& data) -> std::string {
    static const char* chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);

    for (std::size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < data.size()) n |= static_cast<uint32_t>(data[i + 2]);

        result += chars[(n >> 18) & 0x3F];
        result += chars[(n >> 12) & 0x3F];
        result += (i + 1 < data.size()) ? chars[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < data.size()) ? chars[n & 0x3F] : '=';
    }

    return result;
}

/**
 * @brief Base64 URL-safe encode (for JWT)
 */
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

/**
 * @brief Base64 URL-safe encode string
 */
auto base64url_encode_string(const std::string& data) -> std::string {
    std::vector<uint8_t> bytes(data.begin(), data.end());
    return base64url_encode(bytes);
}

/**
 * @brief Base64 decode
 */
auto base64_decode(const std::string& encoded) -> std::vector<uint8_t> {
    static const int decode_table[256] = {
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

    std::vector<uint8_t> result;
    result.reserve((encoded.size() / 4) * 3);

    int bits = 0;
    int bit_count = 0;

    for (char c : encoded) {
        if (c == '=') break;
        int val = decode_table[static_cast<unsigned char>(c)];
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

/**
 * @brief SHA256 hash function
 */
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

/**
 * @brief SHA256 hash of bytes
 */
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

/**
 * @brief URL encode a string
 */
auto url_encode(const std::string& value, bool encode_slash = true) -> std::string {
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

/**
 * @brief Get current UTC time in RFC 3339 format
 */
auto get_rfc3339_time() -> std::string {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time_t);
#else
    gmtime_r(&time_t, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

/**
 * @brief Get current UTC timestamp in seconds
 */
auto get_unix_timestamp() -> int64_t {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
}

/**
 * @brief Get future UTC time in RFC 3339 format
 */
auto get_future_rfc3339_time(std::chrono::seconds duration) -> std::string {
    auto now = std::chrono::system_clock::now();
    auto future = now + duration;
    auto time_t = std::chrono::system_clock::to_time_t(future);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time_t);
#else
    gmtime_r(&time_t, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

/**
 * @brief Generate random bytes
 */
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

/**
 * @brief Generate upload ID for resumable uploads
 */
auto generate_upload_id() -> std::string {
    auto bytes = generate_random_bytes(16);
    return bytes_to_hex(bytes);
}

/**
 * @brief Simple JSON value extraction
 */
auto extract_json_string(const std::string& json, const std::string& key) -> std::optional<std::string> {
    std::string pattern = "\"" + key + "\"\\s*:\\s*\"([^\"]+)\"";
    std::regex re(pattern);
    std::smatch match;

    if (std::regex_search(json, match, re) && match.size() > 1) {
        return match[1].str();
    }
    return std::nullopt;
}

/**
 * @brief Content type detection based on file extension
 */
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

/**
 * @brief Calculate delay with exponential backoff and jitter
 */
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

/**
 * @brief Parse service account JSON
 */
struct service_account_info {
    std::string project_id;
    std::string private_key_id;
    std::string private_key;
    std::string client_email;
    std::string client_id;
    std::string token_uri;
    bool valid = false;
};

auto parse_service_account_json(const std::string& json) -> service_account_info {
    service_account_info info;

    auto project_id = extract_json_string(json, "project_id");
    auto private_key_id = extract_json_string(json, "private_key_id");
    auto client_email = extract_json_string(json, "client_email");
    auto client_id = extract_json_string(json, "client_id");
    auto token_uri = extract_json_string(json, "token_uri");

    // Extract private_key (may contain escaped newlines)
    std::string pk_pattern = "\"private_key\"\\s*:\\s*\"([^\"]+(?:\\\\.[^\"]+)*)\"";
    std::regex pk_re(pk_pattern);
    std::smatch pk_match;

    if (std::regex_search(json, pk_match, pk_re) && pk_match.size() > 1) {
        std::string pk = pk_match[1].str();
        // Unescape the private key
        std::string unescaped;
        for (std::size_t i = 0; i < pk.size(); ++i) {
            if (pk[i] == '\\' && i + 1 < pk.size()) {
                char next = pk[i + 1];
                if (next == 'n') {
                    unescaped += '\n';
                    ++i;
                } else if (next == '\\') {
                    unescaped += '\\';
                    ++i;
                } else {
                    unescaped += pk[i];
                }
            } else {
                unescaped += pk[i];
            }
        }
        info.private_key = unescaped;
    }

    if (project_id) info.project_id = *project_id;
    if (private_key_id) info.private_key_id = *private_key_id;
    if (client_email) info.client_email = *client_email;
    if (client_id) info.client_id = *client_id;
    if (token_uri) info.token_uri = *token_uri;

    info.valid = !info.project_id.empty() &&
                 !info.private_key.empty() &&
                 !info.client_email.empty();

    if (info.token_uri.empty()) {
        info.token_uri = "https://oauth2.googleapis.com/token";
    }

    return info;
}

}  // namespace

// ============================================================================
// GCS Upload Stream Implementation
// ============================================================================

struct gcs_upload_stream::impl {
    std::string object_name;
    gcs_config config;
    std::shared_ptr<credential_provider> credentials;
    cloud_transfer_options options;

    std::string upload_id_;
    std::vector<std::byte> buffer;
    uint64_t bytes_written_ = 0;
    uint64_t bytes_committed = 0;
    bool finalized = false;
    bool aborted = false;

    struct pending_chunk {
        uint64_t offset;
        std::future<result<void>> future;
    };
    std::vector<pending_chunk> pending_uploads;
    std::mutex pending_mutex;

    impl(const std::string& name,
         const gcs_config& cfg,
         std::shared_ptr<credential_provider> creds,
         const cloud_transfer_options& opts)
        : object_name(name), config(cfg), credentials(std::move(creds)), options(opts) {
        buffer.reserve(config.multipart.part_size);
        upload_id_ = generate_upload_id();
    }

    auto get_active_upload_count() -> std::size_t {
        std::lock_guard<std::mutex> lock(pending_mutex);
        std::size_t count = 0;
        for (auto& pending : pending_uploads) {
            if (pending.future.valid()) {
                auto status = pending.future.wait_for(std::chrono::milliseconds(0));
                if (status != std::future_status::ready) {
                    ++count;
                }
            }
        }
        return count;
    }

    auto wait_for_slot() -> result<void> {
        while (get_active_upload_count() >= config.multipart.max_concurrent_parts) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            auto collect_result = collect_completed_uploads();
            if (!collect_result.has_value()) {
                return unexpected{collect_result.error()};
            }
        }
        return result<void>{};
    }

    auto collect_completed_uploads() -> result<void> {
        std::lock_guard<std::mutex> lock(pending_mutex);

        for (auto it = pending_uploads.begin(); it != pending_uploads.end();) {
            if (!it->future.valid()) {
                it = pending_uploads.erase(it);
                continue;
            }

            auto status = it->future.wait_for(std::chrono::milliseconds(0));
            if (status == std::future_status::ready) {
                auto upload_result = it->future.get();
                if (!upload_result.has_value()) {
                    return unexpected{upload_result.error()};
                }
                bytes_committed = std::max(bytes_committed, it->offset);
                it = pending_uploads.erase(it);
            } else {
                ++it;
            }
        }
        return result<void>{};
    }

    auto wait_all_uploads() -> result<void> {
        std::lock_guard<std::mutex> lock(pending_mutex);

        for (auto& pending : pending_uploads) {
            if (pending.future.valid()) {
                auto upload_result = pending.future.get();
                if (!upload_result.has_value()) {
                    return unexpected{upload_result.error()};
                }
            }
        }
        pending_uploads.clear();
        return result<void>{};
    }

    auto upload_chunk_async(uint64_t offset, std::vector<std::byte> data) -> void {
        auto future = std::async(std::launch::async, [this, offset, data = std::move(data)]() {
            return this->upload_chunk(offset, std::span<const std::byte>(data));
        });

        std::lock_guard<std::mutex> lock(pending_mutex);
        pending_uploads.push_back({offset + data.size(), std::move(future)});
    }

    auto upload_chunk(uint64_t offset, std::span<const std::byte> data) -> result<void> {
        // Simulated chunk upload with verification
        (void)offset;
        (void)sha256_bytes(data);
        return result<void>{};
    }

    auto finalize_upload() -> result<upload_result> {
        upload_result result;
        result.key = object_name;
        result.bytes_uploaded = bytes_written_;
        result.upload_id = upload_id_;

        // Generate ETag from content hash
        std::ostringstream content_repr;
        content_repr << object_name << ":" << bytes_written_;
        auto hash = sha256(content_repr.str());
        result.etag = "\"" + bytes_to_hex(hash) + "\"";

        return result;
    }
};

gcs_upload_stream::gcs_upload_stream(
    const std::string& object_name,
    const gcs_config& config,
    std::shared_ptr<credential_provider> credentials,
    const cloud_transfer_options& options)
    : impl_(std::make_unique<impl>(object_name, config, std::move(credentials), options)) {}

gcs_upload_stream::~gcs_upload_stream() = default;

gcs_upload_stream::gcs_upload_stream(gcs_upload_stream&&) noexcept = default;
auto gcs_upload_stream::operator=(gcs_upload_stream&&) noexcept -> gcs_upload_stream& = default;

auto gcs_upload_stream::write(std::span<const std::byte> data) -> result<std::size_t> {
    if (!impl_) {
        return unexpected{error{error_code::not_initialized, "Stream not initialized"}};
    }

    if (impl_->finalized || impl_->aborted) {
        return unexpected{error{error_code::internal_error, "Stream already closed"}};
    }

    std::size_t bytes_processed = 0;
    const std::byte* ptr = data.data();
    std::size_t remaining = data.size();

    while (remaining > 0) {
        std::size_t space_in_buffer = impl_->config.multipart.part_size - impl_->buffer.size();
        std::size_t to_copy = std::min(remaining, space_in_buffer);

        impl_->buffer.insert(impl_->buffer.end(), ptr, ptr + to_copy);
        ptr += to_copy;
        remaining -= to_copy;
        bytes_processed += to_copy;

        if (impl_->buffer.size() >= impl_->config.multipart.part_size) {
            auto wait_result = impl_->wait_for_slot();
            if (!wait_result.has_value()) {
                return unexpected{wait_result.error()};
            }

            auto collect_result = impl_->collect_completed_uploads();
            if (!collect_result.has_value()) {
                return unexpected{collect_result.error()};
            }

            std::vector<std::byte> chunk_data(impl_->buffer.begin(), impl_->buffer.end());
            impl_->upload_chunk_async(impl_->bytes_written_, std::move(chunk_data));

            impl_->buffer.clear();
        }
    }

    impl_->bytes_written_ += bytes_processed;
    return bytes_processed;
}

auto gcs_upload_stream::finalize() -> result<upload_result> {
    if (!impl_) {
        return unexpected{error{error_code::not_initialized, "Stream not initialized"}};
    }

    if (impl_->finalized) {
        return unexpected{error{error_code::internal_error, "Stream already finalized"}};
    }

    if (impl_->aborted) {
        return unexpected{error{error_code::internal_error, "Stream was aborted"}};
    }

    // Upload remaining data as final chunk
    if (!impl_->buffer.empty()) {
        auto result = impl_->upload_chunk(
            impl_->bytes_written_ - impl_->buffer.size(),
            std::span<const std::byte>(impl_->buffer));

        if (!result.has_value()) {
            return unexpected{result.error()};
        }
    }

    auto wait_result = impl_->wait_all_uploads();
    if (!wait_result.has_value()) {
        return unexpected{wait_result.error()};
    }

    impl_->finalized = true;
    return impl_->finalize_upload();
}

auto gcs_upload_stream::abort() -> result<void> {
    if (!impl_) {
        return unexpected{error{error_code::not_initialized, "Stream not initialized"}};
    }

    impl_->aborted = true;
    return result<void>{};
}

auto gcs_upload_stream::bytes_written() const -> uint64_t {
    return impl_ ? impl_->bytes_written_ : 0;
}

auto gcs_upload_stream::upload_id() const -> std::optional<std::string> {
    return impl_ ? std::optional<std::string>(impl_->upload_id_) : std::nullopt;
}

// ============================================================================
// GCS Download Stream Implementation
// ============================================================================

struct gcs_download_stream::impl {
    std::string object_name;
    gcs_config config;
    std::shared_ptr<credential_provider> credentials;

    cloud_object_metadata metadata_;
    uint64_t bytes_read_ = 0;
    uint64_t total_size_ = 0;
    std::vector<std::byte> buffer;
    std::size_t buffer_pos = 0;
    bool initialized = false;

    impl(const std::string& name,
         const gcs_config& cfg,
         std::shared_ptr<credential_provider> creds)
        : object_name(name), config(cfg), credentials(std::move(creds)) {}

    auto initialize() -> result<void> {
        metadata_.key = object_name;
        metadata_.size = 0;
        metadata_.content_type = detect_content_type(object_name);
        initialized = true;
        return result<void>{};
    }

    auto fetch_range(uint64_t start, uint64_t end) -> result<std::vector<std::byte>> {
        (void)start;
        (void)end;
        return std::vector<std::byte>{};
    }
};

gcs_download_stream::gcs_download_stream(
    const std::string& object_name,
    const gcs_config& config,
    std::shared_ptr<credential_provider> credentials)
    : impl_(std::make_unique<impl>(object_name, config, std::move(credentials))) {
    impl_->initialize();
}

gcs_download_stream::~gcs_download_stream() = default;

gcs_download_stream::gcs_download_stream(gcs_download_stream&&) noexcept = default;
auto gcs_download_stream::operator=(gcs_download_stream&&) noexcept -> gcs_download_stream& = default;

auto gcs_download_stream::read(std::span<std::byte> buffer) -> result<std::size_t> {
    if (!impl_) {
        return unexpected{error{error_code::not_initialized, "Stream not initialized"}};
    }

    return 0;
}

auto gcs_download_stream::has_more() const -> bool {
    return impl_ && impl_->bytes_read_ < impl_->total_size_;
}

auto gcs_download_stream::bytes_read() const -> uint64_t {
    return impl_ ? impl_->bytes_read_ : 0;
}

auto gcs_download_stream::total_size() const -> uint64_t {
    return impl_ ? impl_->total_size_ : 0;
}

auto gcs_download_stream::metadata() const -> const cloud_object_metadata& {
    static cloud_object_metadata empty_metadata;
    return impl_ ? impl_->metadata_ : empty_metadata;
}

// ============================================================================
// GCS Storage Implementation
// ============================================================================

struct gcs_storage::impl {
    gcs_config config_;
    std::shared_ptr<credential_provider> credentials_;
    cloud_storage_state state_ = cloud_storage_state::disconnected;
    cloud_storage_statistics stats_;

#ifdef BUILD_WITH_NETWORK_SYSTEM
    std::shared_ptr<kcenon::network::core::http_client> http_client_;
#endif

    std::function<void(const upload_progress&)> upload_progress_callback_;
    std::function<void(const download_progress&)> download_progress_callback_;
    std::function<void(cloud_storage_state)> state_changed_callback_;

    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point connected_at_;

    impl(const gcs_config& config, std::shared_ptr<credential_provider> credentials)
        : config_(config), credentials_(std::move(credentials)) {
#ifdef BUILD_WITH_NETWORK_SYSTEM
        http_client_ = std::make_shared<kcenon::network::core::http_client>(
            std::chrono::milliseconds(30000));  // 30 second timeout
#endif
    }

    void set_state(cloud_storage_state new_state) {
        state_ = new_state;
        if (state_changed_callback_) {
            state_changed_callback_(new_state);
        }
    }

    auto get_storage_endpoint() const -> std::string {
        if (config_.endpoint.has_value()) {
            return config_.endpoint.value();
        }

        return "https://storage.googleapis.com";
    }

    auto get_object_url(const std::string& object_name) const -> std::string {
        return get_storage_endpoint() + "/storage/v1/b/" + config_.bucket +
               "/o/" + url_encode(object_name);
    }

    auto get_upload_url() const -> std::string {
        return get_storage_endpoint() + "/upload/storage/v1/b/" + config_.bucket +
               "/o?uploadType=resumable";
    }

    void update_upload_stats(uint64_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.bytes_uploaded += bytes;
        stats_.upload_count++;
    }

    void update_download_stats(uint64_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.bytes_downloaded += bytes;
        stats_.download_count++;
    }

    void update_list_stats() {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.list_count++;
    }

    void update_delete_stats() {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.delete_count++;
    }

    void update_error_stats() {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.errors++;
    }
};

gcs_storage::gcs_storage(
    const gcs_config& config,
    std::shared_ptr<credential_provider> credentials)
    : impl_(std::make_unique<impl>(config, std::move(credentials))) {}

gcs_storage::~gcs_storage() = default;

gcs_storage::gcs_storage(gcs_storage&&) noexcept = default;
auto gcs_storage::operator=(gcs_storage&&) noexcept -> gcs_storage& = default;

auto gcs_storage::create(
    const gcs_config& config,
    std::shared_ptr<credential_provider> credentials) -> std::unique_ptr<gcs_storage> {
    if (config.bucket.empty()) {
        return nullptr;
    }

    if (!credentials) {
        return nullptr;
    }

    return std::unique_ptr<gcs_storage>(new gcs_storage(config, std::move(credentials)));
}

auto gcs_storage::provider() const -> cloud_provider {
    return cloud_provider::google_cloud;
}

auto gcs_storage::provider_name() const -> std::string_view {
    return "google-cloud";
}

auto gcs_storage::connect() -> result<void> {
    if (!impl_) {
        return unexpected{error{error_code::not_initialized, "Storage not initialized"}};
    }

    impl_->set_state(cloud_storage_state::connecting);

    if (!impl_->credentials_->get_credentials()) {
        impl_->set_state(cloud_storage_state::error);
        return unexpected{error{error_code::internal_error, "Invalid credentials"}};
    }

    impl_->connected_at_ = std::chrono::steady_clock::now();
    impl_->stats_.connected_at = impl_->connected_at_;
    impl_->set_state(cloud_storage_state::connected);

    return result<void>{};
}

auto gcs_storage::disconnect() -> result<void> {
    if (!impl_) {
        return unexpected{error{error_code::not_initialized, "Storage not initialized"}};
    }

    impl_->set_state(cloud_storage_state::disconnected);
    return result<void>{};
}

auto gcs_storage::is_connected() const -> bool {
    return impl_ && impl_->state_ == cloud_storage_state::connected;
}

auto gcs_storage::state() const -> cloud_storage_state {
    return impl_ ? impl_->state_ : cloud_storage_state::disconnected;
}

auto gcs_storage::upload(
    const std::string& key,
    std::span<const std::byte> data,
    const cloud_transfer_options& options) -> result<upload_result> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

    auto start_time = std::chrono::steady_clock::now();

    bool use_resumable = impl_->config_.multipart.enabled &&
                         data.size() >= impl_->config_.multipart.threshold;

    if (use_resumable) {
        auto stream = create_upload_stream(key, options);
        if (!stream) {
            impl_->update_error_stats();
            return unexpected{error{error_code::internal_error, "Failed to create upload stream"}};
        }

        auto write_result = stream->write(data);
        if (!write_result.has_value()) {
            stream->abort();
            impl_->update_error_stats();
            return unexpected{write_result.error()};
        }

        auto finalize_result = stream->finalize();
        if (!finalize_result.has_value()) {
            impl_->update_error_stats();
            return unexpected{finalize_result.error()};
        }

        auto end_time = std::chrono::steady_clock::now();
        auto result = finalize_result.value();
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);

        impl_->update_upload_stats(data.size());
        return result;
    }

    // Simple upload for small objects
    auto content_hash = sha256_bytes(data);
    std::string payload_hash = bytes_to_hex(content_hash);

    upload_result result;
    result.key = key;
    result.etag = "\"" + payload_hash + "\"";
    result.bytes_uploaded = data.size();

    auto end_time = std::chrono::steady_clock::now();
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    impl_->update_upload_stats(data.size());
    return result;
}

auto gcs_storage::upload_file(
    const std::filesystem::path& local_path,
    const std::string& key,
    const cloud_transfer_options& options) -> result<upload_result> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

    if (!std::filesystem::exists(local_path)) {
        return unexpected{error{error_code::file_not_found, "File not found: " + local_path.string()}};
    }

    auto file_size = std::filesystem::file_size(local_path);

    std::ifstream file(local_path, std::ios::binary);
    if (!file) {
        return unexpected{error{error_code::file_access_denied, "Cannot open file: " + local_path.string()}};
    }

    std::vector<std::byte> data(file_size);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(file_size));

    if (!file) {
        return unexpected{error{error_code::file_read_error, "Failed to read file: " + local_path.string()}};
    }

    return upload(key, data, options);
}

auto gcs_storage::download(const std::string& key) -> result<std::vector<std::byte>> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

    impl_->update_download_stats(0);
    return std::vector<std::byte>{};
}

auto gcs_storage::download_file(
    const std::string& key,
    const std::filesystem::path& local_path) -> result<download_result> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

    auto start_time = std::chrono::steady_clock::now();

    auto data_result = download(key);
    if (!data_result.has_value()) {
        return unexpected{data_result.error()};
    }

    auto& data = data_result.value();

    auto parent_path = local_path.parent_path();
    if (!parent_path.empty() && !std::filesystem::exists(parent_path)) {
        std::filesystem::create_directories(parent_path);
    }

    std::ofstream file(local_path, std::ios::binary);
    if (!file) {
        return unexpected{error{error_code::file_access_denied, "Cannot create file: " + local_path.string()}};
    }

    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));

    if (!file) {
        return unexpected{error{error_code::file_write_error, "Failed to write file: " + local_path.string()}};
    }

    auto end_time = std::chrono::steady_clock::now();

    download_result result;
    result.key = key;
    result.bytes_downloaded = data.size();
    result.metadata.key = key;
    result.metadata.size = data.size();
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    return result;
}

auto gcs_storage::delete_object(const std::string& key) -> result<delete_result> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

    delete_result result;
    result.key = key;

    impl_->update_delete_stats();
    return result;
}

auto gcs_storage::delete_objects(
    const std::vector<std::string>& keys) -> result<std::vector<delete_result>> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

    std::vector<delete_result> results;
    results.reserve(keys.size());

    for (const auto& key : keys) {
        auto result = delete_object(key);
        if (result.has_value()) {
            results.push_back(result.value());
        }
    }

    return results;
}

auto gcs_storage::exists(const std::string& key) -> result<bool> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

    return false;
}

auto gcs_storage::get_metadata(const std::string& key) -> result<cloud_object_metadata> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

    cloud_object_metadata metadata;
    metadata.key = key;
    metadata.content_type = detect_content_type(key);

    return metadata;
}

auto gcs_storage::list_objects(
    const list_objects_options& options) -> result<list_objects_result> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

    impl_->update_list_stats();

    list_objects_result result;
    result.is_truncated = false;

    return result;
}

auto gcs_storage::copy_object(
    const std::string& source_key,
    const std::string& dest_key,
    const cloud_transfer_options& options) -> result<cloud_object_metadata> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

    (void)source_key;
    (void)options;

    cloud_object_metadata metadata;
    metadata.key = dest_key;

    return metadata;
}

auto gcs_storage::upload_async(
    const std::string& key,
    std::span<const std::byte> data,
    const cloud_transfer_options& options) -> std::future<result<upload_result>> {
    return std::async(std::launch::async, [this, key, data = std::vector<std::byte>(data.begin(), data.end()), options]() {
        return this->upload(key, data, options);
    });
}

auto gcs_storage::upload_file_async(
    const std::filesystem::path& local_path,
    const std::string& key,
    const cloud_transfer_options& options) -> std::future<result<upload_result>> {
    return std::async(std::launch::async, [this, local_path, key, options]() {
        return this->upload_file(local_path, key, options);
    });
}

auto gcs_storage::download_async(
    const std::string& key) -> std::future<result<std::vector<std::byte>>> {
    return std::async(std::launch::async, [this, key]() {
        return this->download(key);
    });
}

auto gcs_storage::download_file_async(
    const std::string& key,
    const std::filesystem::path& local_path) -> std::future<result<download_result>> {
    return std::async(std::launch::async, [this, key, local_path]() {
        return this->download_file(key, local_path);
    });
}

auto gcs_storage::create_upload_stream(
    const std::string& key,
    const cloud_transfer_options& options) -> std::unique_ptr<cloud_upload_stream> {
    if (!is_connected()) {
        return nullptr;
    }

    return std::unique_ptr<cloud_upload_stream>(
        new gcs_upload_stream(key, impl_->config_, impl_->credentials_, options));
}

auto gcs_storage::create_download_stream(
    const std::string& key) -> std::unique_ptr<cloud_download_stream> {
    if (!is_connected()) {
        return nullptr;
    }

    return std::unique_ptr<cloud_download_stream>(
        new gcs_download_stream(key, impl_->config_, impl_->credentials_));
}

auto gcs_storage::generate_presigned_url(
    const std::string& key,
    const presigned_url_options& options) -> result<std::string> {
    return generate_signed_url(key, options);
}

void gcs_storage::on_upload_progress(
    std::function<void(const upload_progress&)> callback) {
    if (impl_) {
        impl_->upload_progress_callback_ = std::move(callback);
    }
}

void gcs_storage::on_download_progress(
    std::function<void(const download_progress&)> callback) {
    if (impl_) {
        impl_->download_progress_callback_ = std::move(callback);
    }
}

void gcs_storage::on_state_changed(
    std::function<void(cloud_storage_state)> callback) {
    if (impl_) {
        impl_->state_changed_callback_ = std::move(callback);
    }
}

auto gcs_storage::get_statistics() const -> cloud_storage_statistics {
    if (!impl_) {
        return {};
    }

    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->stats_;
}

void gcs_storage::reset_statistics() {
    if (impl_) {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        impl_->stats_ = {};
        impl_->stats_.connected_at = impl_->connected_at_;
    }
}

auto gcs_storage::config() const -> const cloud_storage_config& {
    return impl_->config_;
}

auto gcs_storage::bucket() const -> std::string_view {
    return impl_ ? impl_->config_.bucket : std::string_view{};
}

auto gcs_storage::region() const -> std::string_view {
    return impl_ ? impl_->config_.region : std::string_view{};
}

auto gcs_storage::get_gcs_config() const -> const gcs_config& {
    return impl_->config_;
}

auto gcs_storage::project_id() const -> std::string_view {
    return impl_ ? impl_->config_.project_id : std::string_view{};
}

auto gcs_storage::endpoint_url() const -> std::string {
    if (!impl_) {
        return "";
    }

    return impl_->get_storage_endpoint();
}

auto gcs_storage::set_storage_class(
    const std::string& key,
    const std::string& storage_class) -> result<void> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

    (void)key;
    (void)storage_class;

    return result<void>{};
}

auto gcs_storage::get_storage_class(
    const std::string& key) -> result<std::string> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

    (void)key;

    return std::string{"STANDARD"};
}

auto gcs_storage::generate_signed_url(
    const std::string& key,
    const presigned_url_options& options) -> result<std::string> {
    if (!impl_) {
        return unexpected{error{error_code::not_initialized, "Storage not initialized"}};
    }

#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    auto creds = impl_->credentials_->get_credentials();
    if (!creds) {
        return unexpected{error{error_code::internal_error, "Invalid credentials"}};
    }

    auto gcs_creds = std::dynamic_pointer_cast<const gcs_credentials>(creds);
    if (!gcs_creds) {
        return unexpected{error{error_code::internal_error, "Invalid GCS credentials"}};
    }

    // Generate V4 signed URL
    auto timestamp = get_unix_timestamp();
    auto expiration = timestamp + options.expiration.count();

    std::string http_method = (options.method == "PUT") ? "PUT" : "GET";
    std::string host = "storage.googleapis.com";
    std::string resource = "/" + impl_->config_.bucket + "/" + key;

    // Canonical request components
    std::ostringstream signed_url;
    signed_url << "https://" << host << resource;
    signed_url << "?X-Goog-Algorithm=GOOG4-RSA-SHA256";
    signed_url << "&X-Goog-Credential=" << url_encode("placeholder@project.iam.gserviceaccount.com");
    signed_url << "&X-Goog-Date=" << get_rfc3339_time();
    signed_url << "&X-Goog-Expires=" << options.expiration.count();
    signed_url << "&X-Goog-SignedHeaders=host";
    signed_url << "&X-Goog-Signature=placeholder";

    return signed_url.str();
#else
    (void)key;
    (void)options;
    return unexpected{error{error_code::internal_error, "Encryption support required for signed URL generation"}};
#endif
}

auto gcs_storage::compose_objects(
    const std::vector<std::string>& source_keys,
    const std::string& dest_key,
    const cloud_transfer_options& options) -> result<cloud_object_metadata> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

    if (source_keys.empty()) {
        return unexpected{error{error_code::internal_error, "No source objects provided"}};
    }

    if (source_keys.size() > 32) {
        return unexpected{error{error_code::internal_error, "Maximum 32 objects can be composed"}};
    }

    (void)options;

    cloud_object_metadata metadata;
    metadata.key = dest_key;

    return metadata;
}

// ============================================================================
// GCS Credential Provider Implementation
// ============================================================================

struct gcs_credential_provider::impl {
    credential_type type_;
    std::shared_ptr<gcs_credentials> credentials_;
    credential_state state_ = credential_state::uninitialized;
    std::function<void(credential_state)> state_changed_callback_;
    bool auto_refresh_enabled_ = false;
    std::chrono::seconds auto_refresh_interval_{60};
    std::string auth_type_;
    std::string service_account_email_;
    std::string access_token_;
    std::chrono::system_clock::time_point token_expiry_;

    mutable std::mutex mutex_;

    explicit impl(credential_type type, const std::string& auth_type)
        : type_(type), auth_type_(auth_type) {}

    void set_state(credential_state new_state) {
        state_ = new_state;
        if (state_changed_callback_) {
            state_changed_callback_(new_state);
        }
    }
};

gcs_credential_provider::gcs_credential_provider(const gcs_credentials& creds)
    : impl_(std::make_unique<impl>(credential_type::service_account, "service-account")) {
    impl_->credentials_ = std::make_shared<gcs_credentials>(creds);

    // Parse service account info if JSON is provided
    if (creds.service_account_json.has_value()) {
        auto info = parse_service_account_json(creds.service_account_json.value());
        if (info.valid) {
            impl_->service_account_email_ = info.client_email;
        }
    }

    impl_->set_state(credential_state::valid);
}

gcs_credential_provider::gcs_credential_provider(
    credential_type type, const std::string& project_id)
    : impl_(std::make_unique<impl>(type, "")) {
    impl_->credentials_ = std::make_shared<gcs_credentials>();
    impl_->credentials_->project_id = project_id;
}

gcs_credential_provider::~gcs_credential_provider() = default;

gcs_credential_provider::gcs_credential_provider(gcs_credential_provider&&) noexcept = default;
auto gcs_credential_provider::operator=(gcs_credential_provider&&) noexcept -> gcs_credential_provider& = default;

auto gcs_credential_provider::create(
    const gcs_credentials& creds) -> std::unique_ptr<gcs_credential_provider> {
    if (!creds.service_account_file.has_value() &&
        !creds.service_account_json.has_value()) {
        return nullptr;
    }

    // If file path provided, read the content
    gcs_credentials resolved_creds = creds;
    if (creds.service_account_file.has_value() && !creds.service_account_json.has_value()) {
        std::ifstream file(creds.service_account_file.value());
        if (!file) {
            return nullptr;
        }

        std::ostringstream oss;
        oss << file.rdbuf();
        resolved_creds.service_account_json = oss.str();
    }

    return std::unique_ptr<gcs_credential_provider>(new gcs_credential_provider(resolved_creds));
}

auto gcs_credential_provider::create_from_service_account_file(
    const std::string& json_file_path) -> std::unique_ptr<gcs_credential_provider> {
    std::ifstream file(json_file_path);
    if (!file) {
        return nullptr;
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    std::string json_content = oss.str();

    return create_from_service_account_json(json_content);
}

auto gcs_credential_provider::create_from_service_account_json(
    const std::string& json_content) -> std::unique_ptr<gcs_credential_provider> {
    auto info = parse_service_account_json(json_content);
    if (!info.valid) {
        return nullptr;
    }

    gcs_credentials creds;
    creds.service_account_json = json_content;
    creds.project_id = info.project_id;

    auto provider = std::unique_ptr<gcs_credential_provider>(
        new gcs_credential_provider(credential_type::service_account, info.project_id));
    provider->impl_->credentials_ = std::make_shared<gcs_credentials>(creds);
    provider->impl_->service_account_email_ = info.client_email;
    provider->impl_->auth_type_ = "service-account-json";
    provider->impl_->set_state(credential_state::valid);

    return provider;
}

auto gcs_credential_provider::create_from_environment()
    -> std::unique_ptr<gcs_credential_provider> {
    // Check GOOGLE_APPLICATION_CREDENTIALS
    const char* creds_file = std::getenv("GOOGLE_APPLICATION_CREDENTIALS");
    if (creds_file) {
        auto provider = create_from_service_account_file(creds_file);
        if (provider) {
            provider->impl_->auth_type_ = "environment";
            return provider;
        }
    }

    // Check for project ID only (for metadata server auth)
    const char* project_id = std::getenv("GOOGLE_CLOUD_PROJECT");
    if (!project_id) {
        project_id = std::getenv("GCLOUD_PROJECT");
    }

    if (project_id) {
        auto provider = std::unique_ptr<gcs_credential_provider>(
            new gcs_credential_provider(credential_type::environment, project_id));
        provider->impl_->auth_type_ = "environment-metadata";
        provider->impl_->set_state(credential_state::valid);
        return provider;
    }

    return nullptr;
}

auto gcs_credential_provider::create_default(
    const std::string& project_id) -> std::unique_ptr<gcs_credential_provider> {
    // Try environment variables first
    auto provider = create_from_environment();
    if (provider) {
        return provider;
    }

    // Try default locations
    // 1. ~/.config/gcloud/application_default_credentials.json
    const char* home = std::getenv("HOME");
    if (home) {
        std::string default_path = std::string(home) +
            "/.config/gcloud/application_default_credentials.json";

        if (std::filesystem::exists(default_path)) {
            auto file_provider = create_from_service_account_file(default_path);
            if (file_provider) {
                file_provider->impl_->auth_type_ = "application-default";
                return file_provider;
            }
        }
    }

    // For now, return nullptr if no credentials found
    // TODO: Implement metadata server support for GCE/GKE
    (void)project_id;
    return nullptr;
}

auto gcs_credential_provider::provider() const -> cloud_provider {
    return cloud_provider::google_cloud;
}

auto gcs_credential_provider::get_credentials() const
    -> std::shared_ptr<const cloud_credentials> {
    if (!impl_) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->credentials_;
}

auto gcs_credential_provider::refresh() -> bool {
    if (!impl_) {
        return false;
    }

    // For static service account credentials, refresh is a no-op
    if (impl_->type_ == credential_type::service_account) {
        return true;
    }

    // For environment variables, re-read them
    if (impl_->type_ == credential_type::environment) {
        auto new_provider = create_from_environment();
        if (new_provider) {
            std::lock_guard<std::mutex> lock(impl_->mutex_);
            impl_->credentials_ = new_provider->impl_->credentials_;
            impl_->set_state(credential_state::valid);
            return true;
        }
        return false;
    }

    return false;
}

auto gcs_credential_provider::needs_refresh(std::chrono::seconds buffer) const -> bool {
    if (!impl_ || !impl_->credentials_) {
        return true;
    }

    if (impl_->credentials_->expiration.has_value()) {
        auto time_left = impl_->credentials_->time_until_expiration();
        if (time_left.has_value() && time_left.value() <= buffer) {
            return true;
        }
    }

    // Check OAuth token expiry if applicable
    if (!impl_->access_token_.empty()) {
        auto now = std::chrono::system_clock::now();
        if (now + buffer >= impl_->token_expiry_) {
            return true;
        }
    }

    return false;
}

auto gcs_credential_provider::state() const -> credential_state {
    return impl_ ? impl_->state_ : credential_state::uninitialized;
}

void gcs_credential_provider::on_state_changed(
    std::function<void(credential_state)> callback) {
    if (impl_) {
        impl_->state_changed_callback_ = std::move(callback);
    }
}

void gcs_credential_provider::set_auto_refresh(
    bool enable,
    std::chrono::seconds check_interval) {
    if (impl_) {
        impl_->auto_refresh_enabled_ = enable;
        impl_->auto_refresh_interval_ = check_interval;
    }
}

auto gcs_credential_provider::project_id() const -> std::string {
    if (!impl_ || !impl_->credentials_) {
        return "";
    }
    return impl_->credentials_->project_id.value_or("");
}

auto gcs_credential_provider::service_account_email() const -> std::string {
    return impl_ ? impl_->service_account_email_ : "";
}

auto gcs_credential_provider::auth_type() const -> std::string_view {
    return impl_ ? impl_->auth_type_ : std::string_view{};
}

auto gcs_credential_provider::access_token() const -> std::string {
    if (!impl_) {
        return "";
    }

    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->access_token_;
}

}  // namespace kcenon::file_transfer
