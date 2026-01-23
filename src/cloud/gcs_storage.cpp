/**
 * @file gcs_storage.cpp
 * @brief Google Cloud Storage backend implementation
 * @version 0.1.0
 */

#include "kcenon/file_transfer/cloud/gcs_storage.h"
#include "kcenon/file_transfer/cloud/cloud_http_client.h"
#include "kcenon/file_transfer/cloud/cloud_utils.h"
#include "kcenon/file_transfer/config/feature_flags.h"

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
#include <sstream>
#include <thread>

#ifdef FILE_TRANS_ENABLE_ENCRYPTION
#include <openssl/evp.h>
#include <openssl/pem.h>
#endif

namespace kcenon::file_transfer {

// Import cloud utilities
using cloud_utils::bytes_to_hex;
using cloud_utils::base64_encode;
using cloud_utils::base64_decode;
using cloud_utils::base64url_encode;
using cloud_utils::sha256;
using cloud_utils::sha256_bytes;
using cloud_utils::url_encode;
using cloud_utils::get_rfc3339_time;
using cloud_utils::get_unix_timestamp;
using cloud_utils::get_future_rfc3339_time;
using cloud_utils::generate_random_bytes;
using cloud_utils::extract_json_value;
using cloud_utils::detect_content_type;
using cloud_utils::calculate_retry_delay;

// ============================================================================
// Real HTTP Client Adapter (uses unified cloud_http_client)
// ============================================================================

/**
 * @brief Adapter that wraps cloud_http_client to implement gcs_http_client_interface
 *
 * This adapter uses the unified cloud_http_client internally, reducing code
 * duplication while maintaining the existing gcs_http_client_interface for
 * backwards compatibility.
 */
class real_gcs_http_client : public gcs_http_client_interface {
public:
    explicit real_gcs_http_client(std::chrono::milliseconds timeout = std::chrono::milliseconds(30000))
        : client_(make_cloud_http_client(timeout)) {}

    auto get(
        const std::string& url,
        const std::map<std::string, std::string>& query,
        const std::map<std::string, std::string>& headers)
        -> result<gcs_http_response> override {
        auto response = client_->get(url, query, headers);
        if (!response.has_value()) {
            return unexpected{response.error()};
        }
        return convert_response(response.value());
    }

    auto post(
        const std::string& url,
        const std::vector<uint8_t>& body,
        const std::map<std::string, std::string>& headers)
        -> result<gcs_http_response> override {
        auto response = client_->post(url, body, headers);
        if (!response.has_value()) {
            return unexpected{response.error()};
        }
        return convert_response(response.value());
    }

    auto post(
        const std::string& url,
        const std::string& body,
        const std::map<std::string, std::string>& headers)
        -> result<gcs_http_response> override {
        auto response = client_->post(url, body, headers);
        if (!response.has_value()) {
            return unexpected{response.error()};
        }
        return convert_response(response.value());
    }

    auto del(
        const std::string& url,
        const std::map<std::string, std::string>& headers)
        -> result<gcs_http_response> override {
        auto response = client_->del(url, headers);
        if (!response.has_value()) {
            return unexpected{response.error()};
        }
        return convert_response(response.value());
    }

private:
    static auto convert_response(const http_response_base& resp) -> gcs_http_response {
        gcs_http_response result;
        result.status_code = resp.status_code;
        result.headers = resp.headers;
        result.body = resp.body;
        return result;
    }

    std::shared_ptr<cloud_http_client> client_;
};

namespace {

// ============================================================================
// GCS-Specific Utilities
// ============================================================================

/**
 * @brief Generate upload ID for resumable uploads
 */
auto generate_upload_id() -> std::string {
    return cloud_utils::generate_random_hex(16);
}

#ifdef FILE_TRANS_ENABLE_ENCRYPTION
/**
 * @brief Sign data with RSA-SHA256 (RS256 for JWT)
 */
auto rsa_sha256_sign(const std::string& data, const std::string& private_key_pem)
    -> std::optional<std::vector<uint8_t>> {
    // Parse PEM private key
    BIO* bio = BIO_new_mem_buf(private_key_pem.data(),
                                static_cast<int>(private_key_pem.size()));
    if (!bio) {
        return std::nullopt;
    }

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!pkey) {
        return std::nullopt;
    }

    // Create signing context
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return std::nullopt;
    }

    std::vector<uint8_t> signature;
    size_t sig_len = 0;

    if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return std::nullopt;
    }

    if (EVP_DigestSignUpdate(ctx, data.data(), data.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return std::nullopt;
    }

    // Get required signature length
    if (EVP_DigestSignFinal(ctx, nullptr, &sig_len) != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return std::nullopt;
    }

    signature.resize(sig_len);

    // Sign
    if (EVP_DigestSignFinal(ctx, signature.data(), &sig_len) != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return std::nullopt;
    }

    signature.resize(sig_len);
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);

    return signature;
}
#endif

/**
 * @brief Parse GCS object metadata from JSON response
 */
auto parse_object_metadata(const std::string& json) -> cloud_object_metadata {
    cloud_object_metadata metadata;

    auto name = extract_json_value(json, "name");
    if (name) {
        metadata.key = *name;
    }

    auto size_str = extract_json_value(json, "size");
    if (size_str) {
        try {
            metadata.size = std::stoull(*size_str);
        } catch (...) {
            metadata.size = 0;
        }
    }

    auto content_type = extract_json_value(json, "contentType");
    if (content_type) {
        metadata.content_type = *content_type;
    }

    auto etag = extract_json_value(json, "etag");
    if (etag) {
        metadata.etag = *etag;
    }

    auto updated = extract_json_value(json, "updated");
    if (updated) {
        // Parse RFC 3339 timestamp
        std::tm tm = {};
        std::istringstream ss(*updated);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (!ss.fail()) {
            metadata.last_modified = std::chrono::system_clock::from_time_t(
                std::mktime(&tm));
        }
    }

    auto storage_class = extract_json_value(json, "storageClass");
    if (storage_class) {
        metadata.storage_class = *storage_class;
    }

    return metadata;
}

/**
 * @brief Parse list objects response
 */
auto parse_list_response(const std::string& json)
    -> std::pair<std::vector<cloud_object_metadata>, std::optional<std::string>> {
    std::vector<cloud_object_metadata> objects;
    std::optional<std::string> next_page_token;

    // Extract nextPageToken if present
    auto token = extract_json_value(json, "nextPageToken");
    if (token && !token->empty()) {
        next_page_token = *token;
    }

    // Find items array
    auto items_pos = json.find("\"items\"");
    if (items_pos == std::string::npos) {
        return {objects, next_page_token};
    }

    auto array_start = json.find('[', items_pos);
    if (array_start == std::string::npos) {
        return {objects, next_page_token};
    }

    // Parse each object in the array
    size_t pos = array_start + 1;
    while (pos < json.size()) {
        // Find object start
        auto obj_start = json.find('{', pos);
        if (obj_start == std::string::npos) {
            break;
        }

        // Find matching closing brace
        int brace_count = 1;
        auto obj_end = obj_start + 1;
        while (obj_end < json.size() && brace_count > 0) {
            if (json[obj_end] == '{') ++brace_count;
            else if (json[obj_end] == '}') --brace_count;
            ++obj_end;
        }

        if (brace_count == 0) {
            std::string obj_json = json.substr(obj_start, obj_end - obj_start);
            objects.push_back(parse_object_metadata(obj_json));
        }

        pos = obj_end;

        // Check for array end
        auto next_comma = json.find(',', pos);
        auto array_end = json.find(']', pos);
        if (array_end != std::string::npos &&
            (next_comma == std::string::npos || array_end < next_comma)) {
            break;
        }
    }

    return {objects, next_page_token};
}

/**
 * @brief Simple JSON value extraction (regex-free for MSVC compatibility)
 */
auto extract_json_string(const std::string& json, const std::string& key) -> std::optional<std::string> {
    // Find "key" in the JSON
    std::string search_key = "\"" + key + "\"";
    auto key_pos = json.find(search_key);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    // Skip past the key and any whitespace/colon
    auto pos = key_pos + search_key.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' ||
                                    json[pos] == '\n' || json[pos] == '\r' ||
                                    json[pos] == ':')) {
        ++pos;
    }

    // Expect opening quote
    if (pos >= json.length() || json[pos] != '"') {
        return std::nullopt;
    }
    ++pos;  // Skip opening quote

    // Find closing quote (not escaped)
    std::string value;
    while (pos < json.length() && json[pos] != '"') {
        value += json[pos];
        ++pos;
    }

    if (pos >= json.length()) {
        return std::nullopt;
    }

    return value;
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

    // Extract private_key (may contain escaped newlines) - regex-free for MSVC compatibility
    std::string pk_search = "\"private_key\"";
    auto pk_key_pos = json.find(pk_search);
    if (pk_key_pos != std::string::npos) {
        // Skip past the key and any whitespace/colon
        auto pk_pos = pk_key_pos + pk_search.length();
        while (pk_pos < json.length() && (json[pk_pos] == ' ' || json[pk_pos] == '\t' ||
                                           json[pk_pos] == '\n' || json[pk_pos] == '\r' ||
                                           json[pk_pos] == ':')) {
            ++pk_pos;
        }

        // Expect opening quote
        if (pk_pos < json.length() && json[pk_pos] == '"') {
            ++pk_pos;  // Skip opening quote

            // Extract until closing quote (handling escaped chars)
            std::string pk;
            while (pk_pos < json.length()) {
                if (json[pk_pos] == '\\' && pk_pos + 1 < json.length()) {
                    // Escape sequence
                    char next = json[pk_pos + 1];
                    if (next == '"') {
                        pk += '"';
                        pk_pos += 2;
                    } else if (next == 'n') {
                        pk += '\n';
                        pk_pos += 2;
                    } else if (next == '\\') {
                        pk += '\\';
                        pk_pos += 2;
                    } else if (next == 't') {
                        pk += '\t';
                        pk_pos += 2;
                    } else if (next == 'r') {
                        pk += '\r';
                        pk_pos += 2;
                    } else {
                        pk += json[pk_pos];
                        ++pk_pos;
                    }
                } else if (json[pk_pos] == '"') {
                    // End of string
                    break;
                } else {
                    pk += json[pk_pos];
                    ++pk_pos;
                }
            }
            info.private_key = pk;
        }
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

#ifdef FILE_TRANS_ENABLE_ENCRYPTION
/**
 * @brief Create JWT for GCS service account authentication
 */
auto create_jwt(const service_account_info& sa_info,
                const std::string& scope) -> std::optional<std::string> {
    auto now = get_unix_timestamp();
    auto exp = now + 3600;  // Token valid for 1 hour

    // JWT Header
    std::string header = R"({"alg":"RS256","typ":"JWT"})";

    // JWT Claim
    std::ostringstream claim_builder;
    claim_builder << "{";
    claim_builder << "\"iss\":\"" << sa_info.client_email << "\",";
    claim_builder << "\"scope\":\"" << scope << "\",";
    claim_builder << "\"aud\":\"" << sa_info.token_uri << "\",";
    claim_builder << "\"iat\":" << now << ",";
    claim_builder << "\"exp\":" << exp;
    claim_builder << "}";
    std::string claim = claim_builder.str();

    // Encode header and claim
    std::string header_encoded = base64url_encode(header);
    std::string claim_encoded = base64url_encode(claim);

    // Create signing input
    std::string signing_input = header_encoded + "." + claim_encoded;

    // Sign with RSA-SHA256
    auto signature = rsa_sha256_sign(signing_input, sa_info.private_key);
    if (!signature) {
        return std::nullopt;
    }

    std::string signature_encoded = base64url_encode(signature.value());

    return signing_input + "." + signature_encoded;
}
#endif

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

    std::shared_ptr<gcs_http_client_interface> http_client_;

    // OAuth2 token cache
    std::string access_token_;
    std::chrono::system_clock::time_point token_expiry_;
    mutable std::mutex token_mutex_;

    std::function<void(const upload_progress&)> upload_progress_callback_;
    std::function<void(const download_progress&)> download_progress_callback_;
    std::function<void(cloud_storage_state)> state_changed_callback_;

    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point connected_at_;

    impl(const gcs_config& config,
         std::shared_ptr<credential_provider> credentials,
         std::shared_ptr<gcs_http_client_interface> http_client = nullptr)
        : config_(config), credentials_(std::move(credentials)), http_client_(std::move(http_client)) {
#if KCENON_WITH_NETWORK_SYSTEM
        if (!http_client_) {
            http_client_ = std::make_shared<real_gcs_http_client>(
                std::chrono::milliseconds(30000));  // 30 second timeout
        }
#endif
    }

    /**
     * @brief Get valid access token, refreshing if needed
     */
    auto get_access_token() -> result<std::string> {
#if KCENON_WITH_NETWORK_SYSTEM && defined(FILE_TRANS_ENABLE_ENCRYPTION)
        std::lock_guard<std::mutex> lock(token_mutex_);

        auto now = std::chrono::system_clock::now();
        // Return cached token if still valid (with 60 second buffer)
        if (!access_token_.empty() &&
            now + std::chrono::seconds(60) < token_expiry_) {
            return access_token_;
        }

        // Get credentials and parse service account
        auto creds = credentials_->get_credentials();
        if (!creds) {
            return unexpected{error{error_code::internal_error, "No credentials available"}};
        }

        auto gcs_creds = std::dynamic_pointer_cast<const gcs_credentials>(creds);
        if (!gcs_creds || !gcs_creds->service_account_json.has_value()) {
            return unexpected{error{error_code::internal_error, "Invalid GCS credentials"}};
        }

        auto sa_info = parse_service_account_json(gcs_creds->service_account_json.value());
        if (!sa_info.valid) {
            return unexpected{error{error_code::internal_error, "Failed to parse service account JSON"}};
        }

        // Create JWT
        std::string scope = "https://www.googleapis.com/auth/devstorage.full_control";
        auto jwt = create_jwt(sa_info, scope);
        if (!jwt) {
            return unexpected{error{error_code::internal_error, "Failed to create JWT"}};
        }

        // Exchange JWT for access token
        std::string token_url = sa_info.token_uri;
        std::string body = "grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=" + *jwt;

        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/x-www-form-urlencoded"}
        };

        auto response = http_client_->post(token_url, body, headers);
        if (!response) {
            return unexpected{error{error_code::internal_error, "Token request failed"}};
        }

        auto& resp = response.value();
        if (resp.status_code != 200) {
            return unexpected{error{error_code::internal_error,
                "Token request returned status " + std::to_string(resp.status_code)}};
        }

        std::string response_body = resp.get_body_string();
        auto token = extract_json_value(response_body, "access_token");
        if (!token) {
            return unexpected{error{error_code::internal_error, "No access_token in response"}};
        }

        auto expires_in = extract_json_value(response_body, "expires_in");
        int64_t expires_seconds = 3600;  // Default 1 hour
        if (expires_in) {
            try {
                expires_seconds = std::stoll(*expires_in);
            } catch (...) {}
        }

        access_token_ = *token;
        token_expiry_ = std::chrono::system_clock::now() +
                        std::chrono::seconds(expires_seconds);

        return access_token_;
#else
        return unexpected{error{error_code::internal_error,
            "OAuth2 requires KCENON_WITH_NETWORK_SYSTEM and FILE_TRANS_ENABLE_ENCRYPTION"}};
#endif
    }

    /**
     * @brief Create authorization headers for GCS API requests
     */
    auto get_auth_headers() -> result<std::map<std::string, std::string>> {
        auto token_result = get_access_token();
        if (!token_result.has_value()) {
            return unexpected{token_result.error()};
        }

        return std::map<std::string, std::string>{
            {"Authorization", "Bearer " + token_result.value()}
        };
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

gcs_storage::gcs_storage(
    const gcs_config& config,
    std::shared_ptr<credential_provider> credentials,
    std::shared_ptr<gcs_http_client_interface> http_client)
    : impl_(std::make_unique<impl>(config, std::move(credentials), std::move(http_client))) {}

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

auto gcs_storage::create(
    const gcs_config& config,
    std::shared_ptr<credential_provider> credentials,
    std::shared_ptr<gcs_http_client_interface> http_client) -> std::unique_ptr<gcs_storage> {
    if (config.bucket.empty()) {
        return nullptr;
    }

    if (!credentials) {
        return nullptr;
    }

    return std::unique_ptr<gcs_storage>(new gcs_storage(config, std::move(credentials), std::move(http_client)));
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

#if KCENON_WITH_NETWORK_SYSTEM
    // Simple upload using media upload endpoint
    auto auth_headers = impl_->get_auth_headers();
    if (!auth_headers.has_value()) {
        impl_->update_error_stats();
        return unexpected{auth_headers.error()};
    }

    std::string content_type = options.content_type.value_or(detect_content_type(key));

    std::map<std::string, std::string> headers = auth_headers.value();
    headers["Content-Type"] = content_type;
    headers["Content-Length"] = std::to_string(data.size());

    // Build upload URL
    // POST https://storage.googleapis.com/upload/storage/v1/b/{bucket}/o?uploadType=media&name={object}
    std::string upload_url = impl_->get_storage_endpoint() +
                             "/upload/storage/v1/b/" + impl_->config_.bucket +
                             "/o?uploadType=media&name=" + url_encode(key);

    // Convert span to vector for HTTP client
    std::vector<uint8_t> body_data(data.size());
    std::memcpy(body_data.data(), data.data(), data.size());

    auto response = impl_->http_client_->post(upload_url, body_data, headers);
    if (!response) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error, "Upload request failed"}};
    }

    auto& resp = response.value();
    if (resp.status_code != 200) {
        impl_->update_error_stats();
        std::string error_msg = "Upload failed with status " +
                                std::to_string(resp.status_code);
        auto error_body = resp.get_body_string();
        if (!error_body.empty()) {
            auto error_message = extract_json_value(error_body, "message");
            if (error_message) {
                error_msg += ": " + *error_message;
            }
        }
        return unexpected{error{error_code::internal_error, error_msg}};
    }

    // Parse response
    std::string response_body = resp.get_body_string();
    auto metadata = parse_object_metadata(response_body);

    upload_result result;
    result.key = key;
    result.etag = metadata.etag;
    result.bytes_uploaded = data.size();

    auto end_time = std::chrono::steady_clock::now();
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    impl_->update_upload_stats(data.size());
    return result;
#else
    // Fallback: stub implementation when network system is not available
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
#endif
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

#if KCENON_WITH_NETWORK_SYSTEM
    auto auth_headers = impl_->get_auth_headers();
    if (!auth_headers.has_value()) {
        impl_->update_error_stats();
        return unexpected{auth_headers.error()};
    }

    // GET https://storage.googleapis.com/storage/v1/b/{bucket}/o/{object}?alt=media
    std::string download_url = impl_->get_storage_endpoint() +
                               "/storage/v1/b/" + impl_->config_.bucket +
                               "/o/" + url_encode(key) + "?alt=media";

    auto response = impl_->http_client_->get(download_url, {}, auth_headers.value());
    if (!response) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error, "Download request failed"}};
    }

    auto& resp = response.value();
    if (resp.status_code == 404) {
        impl_->update_error_stats();
        return unexpected{error{error_code::file_not_found, "Object not found: " + key}};
    }

    if (resp.status_code != 200) {
        impl_->update_error_stats();
        std::string error_msg = "Download failed with status " +
                                std::to_string(resp.status_code);
        auto error_body = resp.get_body_string();
        if (!error_body.empty()) {
            auto error_message = extract_json_value(error_body, "message");
            if (error_message) {
                error_msg += ": " + *error_message;
            }
        }
        return unexpected{error{error_code::internal_error, error_msg}};
    }

    // Convert response body to vector<byte>
    std::vector<std::byte> data(resp.body.size());
    std::memcpy(data.data(), resp.body.data(), resp.body.size());

    impl_->update_download_stats(data.size());
    return data;
#else
    // Stub implementation when network system is not available
    impl_->update_download_stats(0);
    return std::vector<std::byte>{};
#endif
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

#if KCENON_WITH_NETWORK_SYSTEM
    auto auth_headers = impl_->get_auth_headers();
    if (!auth_headers.has_value()) {
        impl_->update_error_stats();
        return unexpected{auth_headers.error()};
    }

    // DELETE https://storage.googleapis.com/storage/v1/b/{bucket}/o/{object}
    std::string delete_url = impl_->get_storage_endpoint() +
                             "/storage/v1/b/" + impl_->config_.bucket +
                             "/o/" + url_encode(key);

    auto response = impl_->http_client_->del(delete_url, auth_headers.value());
    if (!response) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error, "Delete request failed"}};
    }

    auto& resp = response.value();
    // 204 No Content is success, 404 means object doesn't exist
    if (resp.status_code == 404) {
        impl_->update_error_stats();
        return unexpected{error{error_code::file_not_found, "Object not found: " + key}};
    }

    if (resp.status_code != 204 && resp.status_code != 200) {
        impl_->update_error_stats();
        std::string error_msg = "Delete failed with status " +
                                std::to_string(resp.status_code);
        auto error_body = resp.get_body_string();
        if (!error_body.empty()) {
            auto error_message = extract_json_value(error_body, "message");
            if (error_message) {
                error_msg += ": " + *error_message;
            }
        }
        return unexpected{error{error_code::internal_error, error_msg}};
    }

    delete_result result;
    result.key = key;

    impl_->update_delete_stats();
    return result;
#else
    delete_result result;
    result.key = key;

    impl_->update_delete_stats();
    return result;
#endif
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

#if KCENON_WITH_NETWORK_SYSTEM
    auto auth_headers = impl_->get_auth_headers();
    if (!auth_headers.has_value()) {
        impl_->update_error_stats();
        return unexpected{auth_headers.error()};
    }

    // HEAD-like request using GET with fields parameter to minimize data transfer
    // GET https://storage.googleapis.com/storage/v1/b/{bucket}/o/{object}?fields=name
    std::string url = impl_->get_storage_endpoint() +
                      "/storage/v1/b/" + impl_->config_.bucket +
                      "/o/" + url_encode(key) + "?fields=name";

    auto response = impl_->http_client_->get(url, {}, auth_headers.value());
    if (!response) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error, "Exists check failed"}};
    }

    auto& resp = response.value();
    if (resp.status_code == 404) {
        return false;
    }

    if (resp.status_code == 200) {
        return true;
    }

    impl_->update_error_stats();
    return unexpected{error{error_code::internal_error,
        "Exists check returned status " + std::to_string(resp.status_code)}};
#else
    return false;
#endif
}

auto gcs_storage::get_metadata(const std::string& key) -> result<cloud_object_metadata> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

#if KCENON_WITH_NETWORK_SYSTEM
    auto auth_headers = impl_->get_auth_headers();
    if (!auth_headers.has_value()) {
        impl_->update_error_stats();
        return unexpected{auth_headers.error()};
    }

    // GET https://storage.googleapis.com/storage/v1/b/{bucket}/o/{object}
    std::string url = impl_->get_storage_endpoint() +
                      "/storage/v1/b/" + impl_->config_.bucket +
                      "/o/" + url_encode(key);

    auto response = impl_->http_client_->get(url, {}, auth_headers.value());
    if (!response) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error, "Get metadata request failed"}};
    }

    auto& resp = response.value();
    if (resp.status_code == 404) {
        impl_->update_error_stats();
        return unexpected{error{error_code::file_not_found, "Object not found: " + key}};
    }

    if (resp.status_code != 200) {
        impl_->update_error_stats();
        std::string error_msg = "Get metadata failed with status " +
                                std::to_string(resp.status_code);
        return unexpected{error{error_code::internal_error, error_msg}};
    }

    std::string response_body = resp.get_body_string();
    return parse_object_metadata(response_body);
#else
    cloud_object_metadata metadata;
    metadata.key = key;
    metadata.content_type = detect_content_type(key);

    return metadata;
#endif
}

auto gcs_storage::list_objects(
    const list_objects_options& options) -> result<list_objects_result> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

#if KCENON_WITH_NETWORK_SYSTEM
    auto auth_headers = impl_->get_auth_headers();
    if (!auth_headers.has_value()) {
        impl_->update_error_stats();
        return unexpected{auth_headers.error()};
    }

    // Build URL with query parameters
    // GET https://storage.googleapis.com/storage/v1/b/{bucket}/o
    std::ostringstream url_builder;
    url_builder << impl_->get_storage_endpoint()
                << "/storage/v1/b/" << impl_->config_.bucket << "/o?";

    bool first_param = true;
    auto add_param = [&](const std::string& key, const std::string& value) {
        if (!first_param) url_builder << "&";
        url_builder << key << "=" << url_encode(value);
        first_param = false;
    };

    if (options.prefix.has_value() && !options.prefix->empty()) {
        add_param("prefix", *options.prefix);
    }

    if (options.delimiter.has_value() && !options.delimiter->empty()) {
        add_param("delimiter", *options.delimiter);
    }

    if (options.max_keys > 0) {
        add_param("maxResults", std::to_string(options.max_keys));
    }

    if (options.continuation_token.has_value() && !options.continuation_token->empty()) {
        add_param("pageToken", *options.continuation_token);
    }

    std::string url = url_builder.str();

    auto response = impl_->http_client_->get(url, {}, auth_headers.value());
    if (!response) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error, "List objects request failed"}};
    }

    auto& resp = response.value();
    if (resp.status_code != 200) {
        impl_->update_error_stats();
        std::string error_msg = "List objects failed with status " +
                                std::to_string(resp.status_code);
        auto error_body = resp.get_body_string();
        if (!error_body.empty()) {
            auto error_message = extract_json_value(error_body, "message");
            if (error_message) {
                error_msg += ": " + *error_message;
            }
        }
        return unexpected{error{error_code::internal_error, error_msg}};
    }

    std::string response_body = resp.get_body_string();
    auto [objects, next_token] = parse_list_response(response_body);

    list_objects_result result;
    result.objects = std::move(objects);
    result.is_truncated = next_token.has_value();
    result.continuation_token = next_token;

    // Parse common prefixes for delimiter support
    auto prefixes_pos = response_body.find("\"prefixes\"");
    if (prefixes_pos != std::string::npos) {
        auto array_start = response_body.find('[', prefixes_pos);
        if (array_start != std::string::npos) {
            auto array_end = response_body.find(']', array_start);
            if (array_end != std::string::npos) {
                std::string prefixes_str = response_body.substr(
                    array_start + 1, array_end - array_start - 1);
                // Parse prefixes array
                size_t pos = 0;
                while (pos < prefixes_str.size()) {
                    auto quote_start = prefixes_str.find('"', pos);
                    if (quote_start == std::string::npos) break;
                    auto quote_end = prefixes_str.find('"', quote_start + 1);
                    if (quote_end == std::string::npos) break;
                    result.common_prefixes.push_back(
                        prefixes_str.substr(quote_start + 1, quote_end - quote_start - 1));
                    pos = quote_end + 1;
                }
            }
        }
    }

    impl_->update_list_stats();
    return result;
#else
    impl_->update_list_stats();

    list_objects_result result;
    result.is_truncated = false;

    return result;
#endif
}

auto gcs_storage::copy_object(
    const std::string& source_key,
    const std::string& dest_key,
    const cloud_transfer_options& options) -> result<cloud_object_metadata> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

#if KCENON_WITH_NETWORK_SYSTEM
    auto auth_headers = impl_->get_auth_headers();
    if (!auth_headers.has_value()) {
        impl_->update_error_stats();
        return unexpected{auth_headers.error()};
    }

    // POST https://storage.googleapis.com/storage/v1/b/{srcBucket}/o/{srcObject}/copyTo/b/{destBucket}/o/{destObject}
    std::string copy_url = impl_->get_storage_endpoint() +
                           "/storage/v1/b/" + impl_->config_.bucket +
                           "/o/" + url_encode(source_key) +
                           "/copyTo/b/" + impl_->config_.bucket +
                           "/o/" + url_encode(dest_key);

    std::map<std::string, std::string> headers = auth_headers.value();
    headers["Content-Type"] = "application/json";

    // Build metadata JSON if content type specified
    std::string body = "{}";
    if (options.content_type.has_value()) {
        body = "{\"contentType\":\"" + *options.content_type + "\"}";
    }

    auto response = impl_->http_client_->post(copy_url, body, headers);
    if (!response) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error, "Copy request failed"}};
    }

    auto& resp = response.value();
    if (resp.status_code == 404) {
        impl_->update_error_stats();
        return unexpected{error{error_code::file_not_found, "Source object not found: " + source_key}};
    }

    if (resp.status_code != 200) {
        impl_->update_error_stats();
        std::string error_msg = "Copy failed with status " +
                                std::to_string(resp.status_code);
        auto error_body = resp.get_body_string();
        if (!error_body.empty()) {
            auto error_message = extract_json_value(error_body, "message");
            if (error_message) {
                error_msg += ": " + *error_message;
            }
        }
        return unexpected{error{error_code::internal_error, error_msg}};
    }

    std::string response_body = resp.get_body_string();
    return parse_object_metadata(response_body);
#else
    (void)source_key;
    (void)options;

    cloud_object_metadata metadata;
    metadata.key = dest_key;

    return metadata;
#endif
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

#if KCENON_WITH_NETWORK_SYSTEM
    auto auth_headers = impl_->get_auth_headers();
    if (!auth_headers.has_value()) {
        impl_->update_error_stats();
        return unexpected{auth_headers.error()};
    }

    // POST https://storage.googleapis.com/storage/v1/b/{bucket}/o/{dest}/compose
    std::string compose_url = impl_->get_storage_endpoint() +
                              "/storage/v1/b/" + impl_->config_.bucket +
                              "/o/" + url_encode(dest_key) + "/compose";

    // Build compose request JSON
    std::ostringstream body_builder;
    body_builder << "{\"sourceObjects\":[";
    for (size_t i = 0; i < source_keys.size(); ++i) {
        if (i > 0) body_builder << ",";
        body_builder << "{\"name\":\"" << source_keys[i] << "\"}";
    }
    body_builder << "],\"destination\":{";
    if (options.content_type.has_value()) {
        body_builder << "\"contentType\":\"" << *options.content_type << "\"";
    }
    body_builder << "}}";

    std::map<std::string, std::string> headers = auth_headers.value();
    headers["Content-Type"] = "application/json";

    auto response = impl_->http_client_->post(compose_url, body_builder.str(), headers);
    if (!response) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error, "Compose request failed"}};
    }

    auto& resp = response.value();
    if (resp.status_code != 200) {
        impl_->update_error_stats();
        std::string error_msg = "Compose failed with status " +
                                std::to_string(resp.status_code);
        auto error_body = resp.get_body_string();
        if (!error_body.empty()) {
            auto error_message = extract_json_value(error_body, "message");
            if (error_message) {
                error_msg += ": " + *error_message;
            }
        }
        return unexpected{error{error_code::internal_error, error_msg}};
    }

    std::string response_body = resp.get_body_string();
    return parse_object_metadata(response_body);
#else
    (void)options;

    cloud_object_metadata metadata;
    metadata.key = dest_key;

    return metadata;
#endif
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
