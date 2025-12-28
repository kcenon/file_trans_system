/**
 * @file azure_blob_storage.cpp
 * @brief Azure Blob Storage backend implementation
 * @version 0.1.0
 */

#include "kcenon/file_transfer/cloud/azure_blob_storage.h"
#include "kcenon/file_transfer/cloud/cloud_utils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <iomanip>
#include <map>
#include <regex>
#include <sstream>
#include <thread>

// HTTP client integration enabled (see #147, #148)
#ifdef BUILD_WITH_NETWORK_SYSTEM
#include <kcenon/network/core/http_client.h>
#endif

namespace kcenon::file_transfer {

// Import cloud utilities
using cloud_utils::bytes_to_hex;
using cloud_utils::base64_encode;
using cloud_utils::base64_decode;
using cloud_utils::sha256;
using cloud_utils::sha256_bytes;
using cloud_utils::hmac_sha256;
using cloud_utils::url_encode;
using cloud_utils::get_rfc1123_time;
using cloud_utils::get_iso8601_time;
using cloud_utils::get_future_iso8601_time;
using cloud_utils::generate_random_bytes;
using cloud_utils::extract_xml_element;
using cloud_utils::detect_content_type;
using cloud_utils::calculate_retry_delay;

// ============================================================================
// Real HTTP Client Implementation
// ============================================================================

#ifdef BUILD_WITH_NETWORK_SYSTEM

/**
 * @brief Real HTTP client that uses the network system
 */
class real_azure_http_client : public azure_http_client_interface {
public:
    explicit real_azure_http_client(std::chrono::milliseconds timeout = std::chrono::milliseconds(30000))
        : client_(std::make_shared<kcenon::network::core::http_client>(timeout)) {}

    auto get(
        const std::string& url,
        const std::map<std::string, std::string>& /*query*/,
        const std::map<std::string, std::string>& headers)
        -> result<azure_http_response> override {
        auto response = client_->get(url, {}, headers);
        if (response.is_err()) {
            return unexpected{error{error_code::internal_error, "HTTP GET request failed"}};
        }
        return convert_response(response.value());
    }

    auto put(
        const std::string& url,
        const std::string& body,
        const std::map<std::string, std::string>& headers)
        -> result<azure_http_response> override {
        auto response = client_->put(url, body, headers);
        if (response.is_err()) {
            return unexpected{error{error_code::internal_error, "HTTP PUT request failed"}};
        }
        return convert_response(response.value());
    }

    auto put(
        const std::string& url,
        const std::vector<uint8_t>& body,
        const std::map<std::string, std::string>& headers)
        -> result<azure_http_response> override {
        std::string body_str(body.begin(), body.end());
        return put(url, body_str, headers);
    }

    auto del(
        const std::string& url,
        const std::map<std::string, std::string>& headers)
        -> result<azure_http_response> override {
        auto response = client_->del(url, headers);
        if (response.is_err()) {
            return unexpected{error{error_code::internal_error, "HTTP DELETE request failed"}};
        }
        return convert_response(response.value());
    }

    auto head(
        const std::string& url,
        const std::map<std::string, std::string>& headers)
        -> result<azure_http_response> override {
        auto response = client_->head(url, headers);
        if (response.is_err()) {
            return unexpected{error{error_code::internal_error, "HTTP HEAD request failed"}};
        }
        return convert_response(response.value());
    }

private:
    auto convert_response(const kcenon::network::internal::http_response& resp) -> azure_http_response {
        azure_http_response result;
        result.status_code = resp.status_code;
        for (const auto& [key, value] : resp.headers) {
            result.headers[key] = value;
        }
        if (!resp.body.empty()) {
            result.body = std::vector<uint8_t>(resp.body.begin(), resp.body.end());
        }
        return result;
    }

    std::shared_ptr<kcenon::network::core::http_client> client_;
};

#endif  // BUILD_WITH_NETWORK_SYSTEM

namespace {

// ============================================================================
// Azure-Specific Utilities
// ============================================================================

/**
 * @brief Generate block ID for Azure Block Blob
 */
auto generate_block_id(int block_number) -> std::string {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(6) << block_number;
    std::string id = oss.str();

    return cloud_utils::base64_encode(id);
}

/**
 * @brief Parse Azure connection string
 */
struct azure_connection_info {
    std::string account_name;
    std::string account_key;
    std::string endpoint_suffix;
    std::string sas_token;
    bool valid = false;
};

auto parse_connection_string(const std::string& conn_str) -> azure_connection_info {
    azure_connection_info info;

    std::string key;
    std::string value;
    std::size_t pos = 0;

    while (pos < conn_str.size()) {
        auto eq_pos = conn_str.find('=', pos);
        if (eq_pos == std::string::npos) break;

        key = conn_str.substr(pos, eq_pos - pos);

        auto semi_pos = conn_str.find(';', eq_pos);
        if (semi_pos == std::string::npos) {
            value = conn_str.substr(eq_pos + 1);
            pos = conn_str.size();
        } else {
            value = conn_str.substr(eq_pos + 1, semi_pos - eq_pos - 1);
            pos = semi_pos + 1;
        }

        if (key == "AccountName") {
            info.account_name = value;
        } else if (key == "AccountKey") {
            info.account_key = value;
        } else if (key == "EndpointSuffix") {
            info.endpoint_suffix = value;
        } else if (key == "SharedAccessSignature") {
            info.sas_token = value;
        }
    }

    info.valid = !info.account_name.empty() &&
                 (!info.account_key.empty() || !info.sas_token.empty());

    if (info.endpoint_suffix.empty()) {
        info.endpoint_suffix = "core.windows.net";
    }

    return info;
}

}  // namespace

// ============================================================================
// Azure Blob Upload Stream Implementation
// ============================================================================

struct azure_blob_upload_stream::impl {
    std::string blob_name;
    azure_blob_config config;
    std::shared_ptr<credential_provider> credentials;
    cloud_transfer_options options;
    std::shared_ptr<azure_http_client_interface> http_client_;

    std::vector<std::string> block_ids;
    int current_block_number = 0;
    std::vector<std::byte> block_buffer;
    uint64_t bytes_written_ = 0;
    bool finalized = false;
    bool aborted = false;

    struct pending_block {
        std::string block_id;
        std::future<result<void>> future;
    };
    std::vector<pending_block> pending_uploads;
    std::mutex pending_mutex;

    impl(const std::string& name,
         const azure_blob_config& cfg,
         std::shared_ptr<credential_provider> creds,
         const cloud_transfer_options& opts,
         std::shared_ptr<azure_http_client_interface> http_client = nullptr)
        : blob_name(name), config(cfg), credentials(std::move(creds)), options(opts),
          http_client_(std::move(http_client)) {
        block_buffer.reserve(config.multipart.part_size);
#ifdef BUILD_WITH_NETWORK_SYSTEM
        if (!http_client_) {
            http_client_ = std::make_shared<real_azure_http_client>(
                std::chrono::milliseconds(30000));  // 30 second timeout
        }
#endif
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

    auto upload_block_async(const std::string& block_id, std::vector<std::byte> data) -> void {
        auto future = std::async(std::launch::async, [this, block_id, data = std::move(data)]() {
            return this->upload_block(block_id, std::span<const std::byte>(data));
        });

        std::lock_guard<std::mutex> lock(pending_mutex);
        pending_uploads.push_back({block_id, std::move(future)});
    }

    auto upload_block(const std::string& block_id, std::span<const std::byte> data) -> result<void> {
#ifdef BUILD_WITH_NETWORK_SYSTEM
        if (!http_client_) {
            return unexpected{error{error_code::not_initialized, "HTTP client not initialized"}};
        }

        // Build PUT Block URL: /{container}/{blob}?comp=block&blockid={block_id}
        std::string url = get_blob_endpoint() + "/" + config.container + "/" +
                          url_encode(blob_name, false) +
                          "?comp=block&blockid=" + url_encode(block_id);

        // Build request headers
        std::map<std::string, std::string> headers;
        headers["x-ms-version"] = config.api_version;
        headers["x-ms-date"] = get_rfc1123_time();
        headers["Content-Length"] = std::to_string(data.size());
        headers["Content-Type"] = "application/octet-stream";

        // Add authorization header
        std::string resource = "/" + config.container + "/" + blob_name;
        std::string auth = create_authorization_header("PUT", resource, headers,
                                                        std::to_string(data.size()));
        if (!auth.empty()) {
            headers["Authorization"] = auth;
        }

        // Convert data to vector for HTTP client
        std::vector<uint8_t> body(data.size());
        std::memcpy(body.data(), data.data(), data.size());

        auto response = http_client_->put(url, std::string(reinterpret_cast<const char*>(body.data()), body.size()), headers);
        if (!response.has_value()) {
            return unexpected{error{error_code::internal_error, "Failed to upload block: " + response.error().message}};
        }

        if (response.value().status_code != 201) {
            return unexpected{error{error_code::internal_error,
                "Failed to upload block, status: " + std::to_string(response.value().status_code)}};
        }

        return result<void>{};
#else
        // Fallback: local simulation when network system is not available
        (void)block_id;
        (void)sha256_bytes(data);
        return result<void>{};
#endif
    }

    auto get_blob_endpoint() const -> std::string {
        if (config.endpoint.has_value()) {
            return config.endpoint.value();
        }
        std::string protocol = config.use_ssl ? "https" : "http";
        return protocol + "://" + config.account_name + ".blob.core.windows.net";
    }

    auto create_authorization_header(
        const std::string& method,
        const std::string& resource,
        const std::map<std::string, std::string>& headers,
        const std::string& content_length = "") const -> std::string {
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
        auto creds = credentials->get_credentials();
        if (!creds) {
            return "";
        }

        auto azure_creds = std::dynamic_pointer_cast<const azure_credentials>(creds);
        if (!azure_creds || !azure_creds->account_key.has_value()) {
            return "";
        }

        // Build the string to sign for SharedKey authentication
        std::ostringstream string_to_sign;
        string_to_sign << method << "\n";

        // Content headers
        auto get_header = [&headers](const std::string& name) -> std::string {
            auto it = headers.find(name);
            return (it != headers.end()) ? it->second : "";
        };

        string_to_sign << get_header("Content-Encoding") << "\n";
        string_to_sign << get_header("Content-Language") << "\n";
        string_to_sign << content_length << "\n";
        string_to_sign << get_header("Content-MD5") << "\n";
        string_to_sign << get_header("Content-Type") << "\n";
        string_to_sign << get_header("Date") << "\n";
        string_to_sign << get_header("If-Modified-Since") << "\n";
        string_to_sign << get_header("If-Match") << "\n";
        string_to_sign << get_header("If-None-Match") << "\n";
        string_to_sign << get_header("If-Unmodified-Since") << "\n";
        string_to_sign << get_header("Range") << "\n";

        // Canonicalized headers (x-ms-*)
        std::map<std::string, std::string> ms_headers;
        for (const auto& [key, value] : headers) {
            std::string lower_key = key;
            std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lower_key.starts_with("x-ms-")) {
                ms_headers[lower_key] = value;
            }
        }

        for (const auto& [key, value] : ms_headers) {
            string_to_sign << key << ":" << value << "\n";
        }

        // Canonicalized resource
        string_to_sign << "/" << config.account_name << resource;

        // Sign
        auto key_bytes = base64_decode(azure_creds->account_key.value());
        auto signature = hmac_sha256(key_bytes, string_to_sign.str());
        std::string signature_b64 = base64_encode(signature);

        return "SharedKey " + config.account_name + ":" + signature_b64;
#else
        (void)method;
        (void)resource;
        (void)headers;
        (void)content_length;
        return "";
#endif
    }

    auto commit_block_list() -> result<upload_result> {
        if (!http_client_) {
            return unexpected{error{error_code::not_initialized, "HTTP client not initialized"}};
        }

        // Build PUT Block List URL: /{container}/{blob}?comp=blocklist
        std::string url = get_blob_endpoint() + "/" + config.container + "/" +
                          url_encode(blob_name, false) + "?comp=blocklist";

        // Build XML body with block IDs
        std::ostringstream xml_body;
        xml_body << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        xml_body << "<BlockList>\n";
        for (const auto& block_id : block_ids) {
            xml_body << "  <Latest>" << block_id << "</Latest>\n";
        }
        xml_body << "</BlockList>";
        std::string body = xml_body.str();

        // Build request headers
        std::map<std::string, std::string> headers;
        headers["x-ms-version"] = config.api_version;
        headers["x-ms-date"] = get_rfc1123_time();
        headers["Content-Type"] = "application/xml";
        headers["Content-Length"] = std::to_string(body.size());
        headers["x-ms-blob-content-type"] = detect_content_type(blob_name);

        // Add authorization header
        std::string resource = "/" + config.container + "/" + blob_name;
        std::string auth = create_authorization_header("PUT", resource, headers,
                                                        std::to_string(body.size()));
        if (!auth.empty()) {
            headers["Authorization"] = auth;
        }

        auto response = http_client_->put(url, body, headers);
        if (!response.has_value()) {
            return unexpected{error{error_code::internal_error,
                "Failed to commit block list: " + response.error().message}};
        }

        if (response.value().status_code != 201) {
            return unexpected{error{error_code::internal_error,
                "Failed to commit block list, status: " + std::to_string(response.value().status_code)}};
        }

        upload_result result;
        result.key = blob_name;
        result.bytes_uploaded = bytes_written_;

        // Extract ETag from response
        auto etag = response.value().get_header("ETag");
        if (etag.has_value()) {
            result.etag = etag.value();
        } else {
            // Generate ETag from block IDs as fallback
            std::ostringstream etag_builder;
            for (const auto& block_id : block_ids) {
                etag_builder << block_id;
            }
            auto hash = sha256(etag_builder.str());
            result.etag = "\"" + bytes_to_hex(hash) + "\"";
        }

        return result;
    }
};

azure_blob_upload_stream::azure_blob_upload_stream(
    const std::string& blob_name,
    const azure_blob_config& config,
    std::shared_ptr<credential_provider> credentials,
    const cloud_transfer_options& options,
    std::shared_ptr<azure_http_client_interface> http_client)
    : impl_(std::make_unique<impl>(blob_name, config, std::move(credentials), options, std::move(http_client))) {}

azure_blob_upload_stream::~azure_blob_upload_stream() = default;

azure_blob_upload_stream::azure_blob_upload_stream(azure_blob_upload_stream&&) noexcept = default;
auto azure_blob_upload_stream::operator=(azure_blob_upload_stream&&) noexcept -> azure_blob_upload_stream& = default;

auto azure_blob_upload_stream::write(std::span<const std::byte> data) -> result<std::size_t> {
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
        std::size_t space_in_buffer = impl_->config.multipart.part_size - impl_->block_buffer.size();
        std::size_t to_copy = std::min(remaining, space_in_buffer);

        impl_->block_buffer.insert(impl_->block_buffer.end(), ptr, ptr + to_copy);
        ptr += to_copy;
        remaining -= to_copy;
        bytes_processed += to_copy;

        if (impl_->block_buffer.size() >= impl_->config.multipart.part_size) {
            auto wait_result = impl_->wait_for_slot();
            if (!wait_result.has_value()) {
                return unexpected{wait_result.error()};
            }

            auto collect_result = impl_->collect_completed_uploads();
            if (!collect_result.has_value()) {
                return unexpected{collect_result.error()};
            }

            std::string block_id = generate_block_id(impl_->current_block_number);
            impl_->block_ids.push_back(block_id);

            std::vector<std::byte> block_data(impl_->block_buffer.begin(), impl_->block_buffer.end());
            impl_->upload_block_async(block_id, std::move(block_data));

            impl_->current_block_number++;
            impl_->block_buffer.clear();
        }
    }

    impl_->bytes_written_ += bytes_processed;
    return bytes_processed;
}

auto azure_blob_upload_stream::finalize() -> result<upload_result> {
    if (!impl_) {
        return unexpected{error{error_code::not_initialized, "Stream not initialized"}};
    }

    if (impl_->finalized) {
        return unexpected{error{error_code::internal_error, "Stream already finalized"}};
    }

    if (impl_->aborted) {
        return unexpected{error{error_code::internal_error, "Stream was aborted"}};
    }

    // Upload remaining data as final block
    if (!impl_->block_buffer.empty()) {
        std::string block_id = generate_block_id(impl_->current_block_number);
        impl_->block_ids.push_back(block_id);

        auto result = impl_->upload_block(
            block_id,
            std::span<const std::byte>(impl_->block_buffer));

        if (!result.has_value()) {
            return unexpected{result.error()};
        }
    }

    auto wait_result = impl_->wait_all_uploads();
    if (!wait_result.has_value()) {
        return unexpected{wait_result.error()};
    }

    impl_->finalized = true;
    return impl_->commit_block_list();
}

auto azure_blob_upload_stream::abort() -> result<void> {
    if (!impl_) {
        return unexpected{error{error_code::not_initialized, "Stream not initialized"}};
    }

    impl_->aborted = true;
    return result<void>{};
}

auto azure_blob_upload_stream::bytes_written() const -> uint64_t {
    return impl_ ? impl_->bytes_written_ : 0;
}

auto azure_blob_upload_stream::upload_id() const -> std::optional<std::string> {
    // Azure doesn't have an upload ID concept like S3
    return std::nullopt;
}

// ============================================================================
// Azure Blob Download Stream Implementation
// ============================================================================

struct azure_blob_download_stream::impl {
    std::string blob_name;
    azure_blob_config config;
    std::shared_ptr<credential_provider> credentials;

    cloud_object_metadata metadata_;
    uint64_t bytes_read_ = 0;
    uint64_t total_size_ = 0;
    std::vector<std::byte> buffer;
    std::size_t buffer_pos = 0;
    bool initialized = false;

#ifdef BUILD_WITH_NETWORK_SYSTEM
    std::shared_ptr<kcenon::network::core::http_client> http_client_;
#endif

    impl(const std::string& name,
         const azure_blob_config& cfg,
         std::shared_ptr<credential_provider> creds)
        : blob_name(name), config(cfg), credentials(std::move(creds)) {
#ifdef BUILD_WITH_NETWORK_SYSTEM
        http_client_ = std::make_shared<kcenon::network::core::http_client>(
            std::chrono::milliseconds(30000));  // 30 second timeout
#endif
    }

    auto get_blob_endpoint() const -> std::string {
        if (config.endpoint.has_value()) {
            return config.endpoint.value();
        }
        std::string protocol = config.use_ssl ? "https" : "http";
        return protocol + "://" + config.account_name + ".blob.core.windows.net";
    }

    auto create_authorization_header(
        const std::string& method,
        const std::string& resource,
        const std::map<std::string, std::string>& headers,
        const std::string& content_length = "") const -> std::string {
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
        auto creds = credentials->get_credentials();
        if (!creds) {
            return "";
        }

        auto azure_creds = std::dynamic_pointer_cast<const azure_credentials>(creds);
        if (!azure_creds || !azure_creds->account_key.has_value()) {
            return "";
        }

        std::ostringstream string_to_sign;
        string_to_sign << method << "\n";

        auto get_header = [&headers](const std::string& name) -> std::string {
            auto it = headers.find(name);
            return (it != headers.end()) ? it->second : "";
        };

        string_to_sign << get_header("Content-Encoding") << "\n";
        string_to_sign << get_header("Content-Language") << "\n";
        string_to_sign << content_length << "\n";
        string_to_sign << get_header("Content-MD5") << "\n";
        string_to_sign << get_header("Content-Type") << "\n";
        string_to_sign << get_header("Date") << "\n";
        string_to_sign << get_header("If-Modified-Since") << "\n";
        string_to_sign << get_header("If-Match") << "\n";
        string_to_sign << get_header("If-None-Match") << "\n";
        string_to_sign << get_header("If-Unmodified-Since") << "\n";
        string_to_sign << get_header("Range") << "\n";

        std::map<std::string, std::string> ms_headers;
        for (const auto& [key, value] : headers) {
            std::string lower_key = key;
            std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lower_key.starts_with("x-ms-")) {
                ms_headers[lower_key] = value;
            }
        }

        for (const auto& [key, value] : ms_headers) {
            string_to_sign << key << ":" << value << "\n";
        }

        string_to_sign << "/" << config.account_name << resource;

        auto key_bytes = base64_decode(azure_creds->account_key.value());
        auto signature = hmac_sha256(key_bytes, string_to_sign.str());
        std::string signature_b64 = base64_encode(signature);

        return "SharedKey " + config.account_name + ":" + signature_b64;
#else
        (void)method;
        (void)resource;
        (void)headers;
        (void)content_length;
        return "";
#endif
    }

    auto initialize() -> result<void> {
#ifdef BUILD_WITH_NETWORK_SYSTEM
        if (!http_client_) {
            return unexpected{error{error_code::not_initialized, "HTTP client not initialized"}};
        }

        // Use HEAD request to get blob metadata
        std::string url = get_blob_endpoint() + "/" + config.container + "/" +
                          url_encode(blob_name, false);

        std::map<std::string, std::string> headers;
        headers["x-ms-version"] = config.api_version;
        headers["x-ms-date"] = get_rfc1123_time();

        std::string resource = "/" + config.container + "/" + blob_name;
        std::string auth = create_authorization_header("HEAD", resource, headers, "");
        if (!auth.empty()) {
            headers["Authorization"] = auth;
        }

        auto response = http_client_->head(url, headers);
        if (response.is_err()) {
            return unexpected{error{error_code::internal_error,
                "Failed to get blob metadata: " + response.error().message}};
        }

        if (response.value().status_code != 200) {
            return unexpected{error{error_code::file_not_found,
                "Blob not found, status: " + std::to_string(response.value().status_code)}};
        }

        metadata_.key = blob_name;

        // Extract Content-Length
        auto content_length = response.value().get_header("Content-Length");
        if (content_length.has_value()) {
            total_size_ = std::stoull(content_length.value());
            metadata_.size = total_size_;
        }

        // Extract Content-Type
        auto content_type = response.value().get_header("Content-Type");
        if (content_type.has_value()) {
            metadata_.content_type = content_type.value();
        } else {
            metadata_.content_type = detect_content_type(blob_name);
        }

        // Extract ETag
        auto etag = response.value().get_header("ETag");
        if (etag.has_value()) {
            metadata_.etag = etag.value();
        }

        // Extract Last-Modified
        auto last_modified = response.value().get_header("Last-Modified");
        if (last_modified.has_value()) {
            // Parse RFC 1123 date format to system_clock time_point
            std::tm tm = {};
            std::istringstream ss(last_modified.value());
            ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S");
            if (!ss.fail()) {
                auto time_t = std::mktime(&tm);
                metadata_.last_modified = std::chrono::system_clock::from_time_t(time_t);
            }
        }

        initialized = true;
        return result<void>{};
#else
        metadata_.key = blob_name;
        metadata_.size = 0;
        metadata_.content_type = detect_content_type(blob_name);
        initialized = true;
        return result<void>{};
#endif
    }

    auto fetch_range(uint64_t start, uint64_t end) -> result<std::vector<std::byte>> {
#ifdef BUILD_WITH_NETWORK_SYSTEM
        if (!http_client_) {
            return unexpected{error{error_code::not_initialized, "HTTP client not initialized"}};
        }

        std::string url = get_blob_endpoint() + "/" + config.container + "/" +
                          url_encode(blob_name, false);

        std::map<std::string, std::string> headers;
        headers["x-ms-version"] = config.api_version;
        headers["x-ms-date"] = get_rfc1123_time();
        headers["Range"] = "bytes=" + std::to_string(start) + "-" + std::to_string(end);

        std::string resource = "/" + config.container + "/" + blob_name;
        std::string auth = create_authorization_header("GET", resource, headers, "");
        if (!auth.empty()) {
            headers["Authorization"] = auth;
        }

        auto response = http_client_->get(url, {}, headers);
        if (response.is_err()) {
            return unexpected{error{error_code::internal_error,
                "Failed to download blob range: " + response.error().message}};
        }

        if (response.value().status_code != 200 && response.value().status_code != 206) {
            return unexpected{error{error_code::internal_error,
                "Failed to download blob, status: " + std::to_string(response.value().status_code)}};
        }

        std::vector<std::byte> data(response.value().body.size());
        std::memcpy(data.data(), response.value().body.data(), response.value().body.size());
        return data;
#else
        (void)start;
        (void)end;
        return std::vector<std::byte>{};
#endif
    }
};

azure_blob_download_stream::azure_blob_download_stream(
    const std::string& blob_name,
    const azure_blob_config& config,
    std::shared_ptr<credential_provider> credentials)
    : impl_(std::make_unique<impl>(blob_name, config, std::move(credentials))) {
    impl_->initialize();
}

azure_blob_download_stream::~azure_blob_download_stream() = default;

azure_blob_download_stream::azure_blob_download_stream(azure_blob_download_stream&&) noexcept = default;
auto azure_blob_download_stream::operator=(azure_blob_download_stream&&) noexcept -> azure_blob_download_stream& = default;

auto azure_blob_download_stream::read(std::span<std::byte> buffer) -> result<std::size_t> {
    if (!impl_) {
        return unexpected{error{error_code::not_initialized, "Stream not initialized"}};
    }

    if (!impl_->initialized) {
        return unexpected{error{error_code::not_initialized, "Stream not initialized"}};
    }

    if (impl_->bytes_read_ >= impl_->total_size_) {
        return 0;  // EOF
    }

    // Calculate range to fetch
    uint64_t start = impl_->bytes_read_;
    uint64_t end = std::min(start + buffer.size() - 1, impl_->total_size_ - 1);

    auto data_result = impl_->fetch_range(start, end);
    if (!data_result.has_value()) {
        return unexpected{data_result.error()};
    }

    auto& data = data_result.value();
    std::size_t bytes_to_copy = std::min(data.size(), buffer.size());
    std::memcpy(buffer.data(), data.data(), bytes_to_copy);

    impl_->bytes_read_ += bytes_to_copy;
    return bytes_to_copy;
}

auto azure_blob_download_stream::has_more() const -> bool {
    return impl_ && impl_->bytes_read_ < impl_->total_size_;
}

auto azure_blob_download_stream::bytes_read() const -> uint64_t {
    return impl_ ? impl_->bytes_read_ : 0;
}

auto azure_blob_download_stream::total_size() const -> uint64_t {
    return impl_ ? impl_->total_size_ : 0;
}

auto azure_blob_download_stream::metadata() const -> const cloud_object_metadata& {
    static cloud_object_metadata empty_metadata;
    return impl_ ? impl_->metadata_ : empty_metadata;
}

// ============================================================================
// Azure Blob Storage Implementation
// ============================================================================

struct azure_blob_storage::impl {
    azure_blob_config config_;
    std::shared_ptr<credential_provider> credentials_;
    cloud_storage_state state_ = cloud_storage_state::disconnected;
    cloud_storage_statistics stats_;

    std::shared_ptr<azure_http_client_interface> http_client_;

    std::function<void(const upload_progress&)> upload_progress_callback_;
    std::function<void(const download_progress&)> download_progress_callback_;
    std::function<void(cloud_storage_state)> state_changed_callback_;

    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point connected_at_;

    impl(const azure_blob_config& config,
         std::shared_ptr<credential_provider> credentials,
         std::shared_ptr<azure_http_client_interface> http_client = nullptr)
        : config_(config), credentials_(std::move(credentials)), http_client_(std::move(http_client)) {
#ifdef BUILD_WITH_NETWORK_SYSTEM
        if (!http_client_) {
            http_client_ = std::make_shared<real_azure_http_client>(
                std::chrono::milliseconds(30000));  // 30 second timeout
        }
#endif
    }

    void set_state(cloud_storage_state new_state) {
        state_ = new_state;
        if (state_changed_callback_) {
            state_changed_callback_(new_state);
        }
    }

    auto get_blob_endpoint() const -> std::string {
        if (config_.endpoint.has_value()) {
            return config_.endpoint.value();
        }

        std::string protocol = config_.use_ssl ? "https" : "http";
        return protocol + "://" + config_.account_name + ".blob.core.windows.net";
    }

    auto get_blob_url(const std::string& blob_name) const -> std::string {
        return get_blob_endpoint() + "/" + config_.container + "/" + blob_name;
    }

    auto create_authorization_header(
        const std::string& method,
        const std::string& resource,
        const std::map<std::string, std::string>& headers,
        const std::string& content_length = "") const -> std::string {
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
        auto creds = credentials_->get_credentials();
        if (!creds) {
            return "";
        }

        auto azure_creds = std::dynamic_pointer_cast<const azure_credentials>(creds);
        if (!azure_creds || !azure_creds->account_key.has_value()) {
            return "";
        }

        // Build the string to sign for SharedKey authentication
        std::ostringstream string_to_sign;
        string_to_sign << method << "\n";

        // Content headers
        auto get_header = [&headers](const std::string& name) -> std::string {
            auto it = headers.find(name);
            return (it != headers.end()) ? it->second : "";
        };

        string_to_sign << get_header("Content-Encoding") << "\n";
        string_to_sign << get_header("Content-Language") << "\n";
        string_to_sign << content_length << "\n";
        string_to_sign << get_header("Content-MD5") << "\n";
        string_to_sign << get_header("Content-Type") << "\n";
        string_to_sign << get_header("Date") << "\n";
        string_to_sign << get_header("If-Modified-Since") << "\n";
        string_to_sign << get_header("If-Match") << "\n";
        string_to_sign << get_header("If-None-Match") << "\n";
        string_to_sign << get_header("If-Unmodified-Since") << "\n";
        string_to_sign << get_header("Range") << "\n";

        // Canonicalized headers (x-ms-*)
        std::map<std::string, std::string> ms_headers;
        for (const auto& [key, value] : headers) {
            std::string lower_key = key;
            std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lower_key.starts_with("x-ms-")) {
                ms_headers[lower_key] = value;
            }
        }

        for (const auto& [key, value] : ms_headers) {
            string_to_sign << key << ":" << value << "\n";
        }

        // Canonicalized resource
        string_to_sign << "/" << config_.account_name << resource;

        // Sign
        auto key_bytes = base64_decode(azure_creds->account_key.value());
        auto signature = hmac_sha256(key_bytes, string_to_sign.str());
        std::string signature_b64 = base64_encode(signature);

        return "SharedKey " + config_.account_name + ":" + signature_b64;
#else
        (void)method;
        (void)resource;
        (void)headers;
        (void)content_length;
        return "";
#endif
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

azure_blob_storage::azure_blob_storage(
    const azure_blob_config& config,
    std::shared_ptr<credential_provider> credentials)
    : impl_(std::make_unique<impl>(config, std::move(credentials), nullptr)) {}

azure_blob_storage::~azure_blob_storage() = default;

azure_blob_storage::azure_blob_storage(azure_blob_storage&&) noexcept = default;
auto azure_blob_storage::operator=(azure_blob_storage&&) noexcept -> azure_blob_storage& = default;

auto azure_blob_storage::create(
    const azure_blob_config& config,
    std::shared_ptr<credential_provider> credentials) -> std::unique_ptr<azure_blob_storage> {
    return create(config, std::move(credentials), nullptr);
}

auto azure_blob_storage::create(
    const azure_blob_config& config,
    std::shared_ptr<credential_provider> credentials,
    std::shared_ptr<azure_http_client_interface> http_client) -> std::unique_ptr<azure_blob_storage> {
    if (config.container.empty()) {
        return nullptr;
    }

    if (config.account_name.empty()) {
        return nullptr;
    }

    if (!credentials) {
        return nullptr;
    }

    auto storage = std::unique_ptr<azure_blob_storage>(new azure_blob_storage(config, credentials));
    if (http_client) {
        storage->impl_->http_client_ = std::move(http_client);
    }
    return storage;
}

auto azure_blob_storage::provider() const -> cloud_provider {
    return cloud_provider::azure_blob;
}

auto azure_blob_storage::provider_name() const -> std::string_view {
    return "azure-blob";
}

auto azure_blob_storage::connect() -> result<void> {
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

auto azure_blob_storage::disconnect() -> result<void> {
    if (!impl_) {
        return unexpected{error{error_code::not_initialized, "Storage not initialized"}};
    }

    impl_->set_state(cloud_storage_state::disconnected);
    return result<void>{};
}

auto azure_blob_storage::is_connected() const -> bool {
    return impl_ && impl_->state_ == cloud_storage_state::connected;
}

auto azure_blob_storage::state() const -> cloud_storage_state {
    return impl_ ? impl_->state_ : cloud_storage_state::disconnected;
}

auto azure_blob_storage::upload(
    const std::string& key,
    std::span<const std::byte> data,
    const cloud_transfer_options& options) -> result<upload_result> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

    auto start_time = std::chrono::steady_clock::now();

    bool use_block_blob = impl_->config_.multipart.enabled &&
                          data.size() >= impl_->config_.multipart.threshold;

    if (use_block_blob) {
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

    // Single PUT request for small blobs
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

auto azure_blob_storage::upload_file(
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

auto azure_blob_storage::download(const std::string& key) -> result<std::vector<std::byte>> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

#ifdef BUILD_WITH_NETWORK_SYSTEM
    if (!impl_->http_client_) {
        return unexpected{error{error_code::not_initialized, "HTTP client not initialized"}};
    }

    std::string url = impl_->get_blob_endpoint() + "/" + impl_->config_.container + "/" +
                      url_encode(key, false);

    std::map<std::string, std::string> headers;
    headers["x-ms-version"] = impl_->config_.api_version;
    headers["x-ms-date"] = get_rfc1123_time();

    std::string resource = "/" + impl_->config_.container + "/" + key;
    std::string auth = impl_->create_authorization_header("GET", resource, headers, "");
    if (!auth.empty()) {
        headers["Authorization"] = auth;
    }

    auto response = impl_->http_client_->get(url, {}, headers);
    if (!response.has_value()) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error,
            "Failed to download blob: " + response.error().message}};
    }

    if (response.value().status_code == 404) {
        return unexpected{error{error_code::file_not_found, "Blob not found: " + key}};
    }

    if (response.value().status_code != 200) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error,
            "Failed to download blob, status: " + std::to_string(response.value().status_code)}};
    }

    std::vector<std::byte> data(response.value().body.size());
    std::memcpy(data.data(), response.value().body.data(), response.value().body.size());

    impl_->update_download_stats(data.size());
    return data;
#else
    impl_->update_download_stats(0);
    return std::vector<std::byte>{};
#endif
}

auto azure_blob_storage::download_file(
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

auto azure_blob_storage::delete_object(const std::string& key) -> result<delete_result> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

#ifdef BUILD_WITH_NETWORK_SYSTEM
    if (!impl_->http_client_) {
        return unexpected{error{error_code::not_initialized, "HTTP client not initialized"}};
    }

    std::string url = impl_->get_blob_endpoint() + "/" + impl_->config_.container + "/" +
                      url_encode(key, false);

    std::map<std::string, std::string> headers;
    headers["x-ms-version"] = impl_->config_.api_version;
    headers["x-ms-date"] = get_rfc1123_time();

    std::string resource = "/" + impl_->config_.container + "/" + key;
    std::string auth = impl_->create_authorization_header("DELETE", resource, headers, "");
    if (!auth.empty()) {
        headers["Authorization"] = auth;
    }

    auto response = impl_->http_client_->del(url, headers);
    if (!response.has_value()) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error,
            "Failed to delete blob: " + response.error().message}};
    }

    // 202 Accepted or 404 Not Found are both valid responses for delete
    if (response.value().status_code != 202 && response.value().status_code != 404) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error,
            "Failed to delete blob, status: " + std::to_string(response.value().status_code)}};
    }

    delete_result result;
    result.key = key;
    // delete_marker set to false for non-versioned blobs (Azure returns 202 for successful delete)
    result.delete_marker = false;

    impl_->update_delete_stats();
    return result;
#else
    delete_result result;
    result.key = key;
    result.delete_marker = false;

    impl_->update_delete_stats();
    return result;
#endif
}

auto azure_blob_storage::delete_objects(
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

auto azure_blob_storage::exists(const std::string& key) -> result<bool> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

#ifdef BUILD_WITH_NETWORK_SYSTEM
    if (!impl_->http_client_) {
        return unexpected{error{error_code::not_initialized, "HTTP client not initialized"}};
    }

    std::string url = impl_->get_blob_endpoint() + "/" + impl_->config_.container + "/" +
                      url_encode(key, false);

    std::map<std::string, std::string> headers;
    headers["x-ms-version"] = impl_->config_.api_version;
    headers["x-ms-date"] = get_rfc1123_time();

    std::string resource = "/" + impl_->config_.container + "/" + key;
    std::string auth = impl_->create_authorization_header("HEAD", resource, headers, "");
    if (!auth.empty()) {
        headers["Authorization"] = auth;
    }

    auto response = impl_->http_client_->head(url, headers);
    if (!response.has_value()) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error,
            "Failed to check blob existence: " + response.error().message}};
    }

    return response.value().status_code == 200;
#else
    return false;
#endif
}

auto azure_blob_storage::get_metadata(const std::string& key) -> result<cloud_object_metadata> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

#ifdef BUILD_WITH_NETWORK_SYSTEM
    if (!impl_->http_client_) {
        return unexpected{error{error_code::not_initialized, "HTTP client not initialized"}};
    }

    std::string url = impl_->get_blob_endpoint() + "/" + impl_->config_.container + "/" +
                      url_encode(key, false);

    std::map<std::string, std::string> headers;
    headers["x-ms-version"] = impl_->config_.api_version;
    headers["x-ms-date"] = get_rfc1123_time();

    std::string resource = "/" + impl_->config_.container + "/" + key;
    std::string auth = impl_->create_authorization_header("HEAD", resource, headers, "");
    if (!auth.empty()) {
        headers["Authorization"] = auth;
    }

    auto response = impl_->http_client_->head(url, headers);
    if (!response.has_value()) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error,
            "Failed to get blob metadata: " + response.error().message}};
    }

    if (response.value().status_code == 404) {
        return unexpected{error{error_code::file_not_found, "Blob not found: " + key}};
    }

    if (response.value().status_code != 200) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error,
            "Failed to get blob metadata, status: " + std::to_string(response.value().status_code)}};
    }

    cloud_object_metadata metadata;
    metadata.key = key;

    // Extract Content-Length
    auto content_length = response.value().get_header("Content-Length");
    if (content_length.has_value()) {
        metadata.size = std::stoull(content_length.value());
    }

    // Extract Content-Type
    auto content_type = response.value().get_header("Content-Type");
    if (content_type.has_value()) {
        metadata.content_type = content_type.value();
    } else {
        metadata.content_type = detect_content_type(key);
    }

    // Extract ETag
    auto etag = response.value().get_header("ETag");
    if (etag.has_value()) {
        metadata.etag = etag.value();
    }

    // Extract Last-Modified
    auto last_modified = response.value().get_header("Last-Modified");
    if (last_modified.has_value()) {
        std::tm tm = {};
        std::istringstream ss(last_modified.value());
        ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S");
        if (!ss.fail()) {
            auto time_t = std::mktime(&tm);
            metadata.last_modified = std::chrono::system_clock::from_time_t(time_t);
        }
    }

    return metadata;
#else
    cloud_object_metadata metadata;
    metadata.key = key;
    metadata.content_type = detect_content_type(key);

    return metadata;
#endif
}

auto azure_blob_storage::list_objects(
    const list_objects_options& options) -> result<list_objects_result> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

#ifdef BUILD_WITH_NETWORK_SYSTEM
    if (!impl_->http_client_) {
        return unexpected{error{error_code::not_initialized, "HTTP client not initialized"}};
    }

    // Build List Blobs URL
    std::ostringstream url_stream;
    url_stream << impl_->get_blob_endpoint() << "/" << impl_->config_.container
               << "?restype=container&comp=list";

    if (options.prefix.has_value() && !options.prefix.value().empty()) {
        url_stream << "&prefix=" << url_encode(options.prefix.value());
    }
    if (options.max_keys > 0) {
        url_stream << "&maxresults=" << options.max_keys;
    }
    if (options.continuation_token.has_value() && !options.continuation_token.value().empty()) {
        url_stream << "&marker=" << url_encode(options.continuation_token.value());
    }
    if (options.delimiter.has_value() && !options.delimiter.value().empty()) {
        url_stream << "&delimiter=" << url_encode(options.delimiter.value());
    }

    std::string url = url_stream.str();

    std::map<std::string, std::string> headers;
    headers["x-ms-version"] = impl_->config_.api_version;
    headers["x-ms-date"] = get_rfc1123_time();

    std::string resource = "/" + impl_->config_.container;
    std::string auth = impl_->create_authorization_header("GET", resource, headers, "");
    if (!auth.empty()) {
        headers["Authorization"] = auth;
    }

    auto response = impl_->http_client_->get(url, {}, headers);
    if (!response.has_value()) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error,
            "Failed to list blobs: " + response.error().message}};
    }

    if (response.value().status_code != 200) {
        impl_->update_error_stats();
        return unexpected{error{error_code::internal_error,
            "Failed to list blobs, status: " + std::to_string(response.value().status_code)}};
    }

    // Parse XML response
    std::string xml_response = response.value().get_body_string();
    list_objects_result result;

    // Parse <Blob> elements
    std::string blob_start = "<Blob>";
    std::string blob_end = "</Blob>";
    std::size_t pos = 0;

    while ((pos = xml_response.find(blob_start, pos)) != std::string::npos) {
        auto end_pos = xml_response.find(blob_end, pos);
        if (end_pos == std::string::npos) break;

        std::string blob_xml = xml_response.substr(pos, end_pos - pos + blob_end.length());

        cloud_object_metadata metadata;

        // Extract Name
        auto name = extract_xml_element(blob_xml, "Name");
        if (name.has_value()) {
            metadata.key = name.value();
        }

        // Extract Content-Length from Properties
        auto content_length = extract_xml_element(blob_xml, "Content-Length");
        if (content_length.has_value()) {
            metadata.size = std::stoull(content_length.value());
        }

        // Extract Content-Type
        auto content_type = extract_xml_element(blob_xml, "Content-Type");
        if (content_type.has_value()) {
            metadata.content_type = content_type.value();
        }

        // Extract ETag
        auto etag = extract_xml_element(blob_xml, "Etag");
        if (etag.has_value()) {
            metadata.etag = etag.value();
        }

        // Extract Last-Modified
        auto last_modified = extract_xml_element(blob_xml, "Last-Modified");
        if (last_modified.has_value()) {
            std::tm tm = {};
            std::istringstream ss(last_modified.value());
            ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S");
            if (!ss.fail()) {
                auto time_t = std::mktime(&tm);
                metadata.last_modified = std::chrono::system_clock::from_time_t(time_t);
            }
        }

        result.objects.push_back(std::move(metadata));
        pos = end_pos + blob_end.length();
    }

    // Parse <BlobPrefix> elements (common prefixes for delimiter mode)
    std::string prefix_start = "<BlobPrefix>";
    std::string prefix_end = "</BlobPrefix>";
    pos = 0;

    while ((pos = xml_response.find(prefix_start, pos)) != std::string::npos) {
        auto end_pos = xml_response.find(prefix_end, pos);
        if (end_pos == std::string::npos) break;

        std::string prefix_xml = xml_response.substr(pos, end_pos - pos + prefix_end.length());

        auto name = extract_xml_element(prefix_xml, "Name");
        if (name.has_value()) {
            result.common_prefixes.push_back(name.value());
        }

        pos = end_pos + prefix_end.length();
    }

    // Check for NextMarker (pagination)
    auto next_marker = extract_xml_element(xml_response, "NextMarker");
    if (next_marker.has_value() && !next_marker.value().empty()) {
        result.is_truncated = true;
        result.continuation_token = next_marker.value();
    } else {
        result.is_truncated = false;
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

auto azure_blob_storage::copy_object(
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

auto azure_blob_storage::upload_async(
    const std::string& key,
    std::span<const std::byte> data,
    const cloud_transfer_options& options) -> std::future<result<upload_result>> {
    return std::async(std::launch::async, [this, key, data = std::vector<std::byte>(data.begin(), data.end()), options]() {
        return this->upload(key, data, options);
    });
}

auto azure_blob_storage::upload_file_async(
    const std::filesystem::path& local_path,
    const std::string& key,
    const cloud_transfer_options& options) -> std::future<result<upload_result>> {
    return std::async(std::launch::async, [this, local_path, key, options]() {
        return this->upload_file(local_path, key, options);
    });
}

auto azure_blob_storage::download_async(
    const std::string& key) -> std::future<result<std::vector<std::byte>>> {
    return std::async(std::launch::async, [this, key]() {
        return this->download(key);
    });
}

auto azure_blob_storage::download_file_async(
    const std::string& key,
    const std::filesystem::path& local_path) -> std::future<result<download_result>> {
    return std::async(std::launch::async, [this, key, local_path]() {
        return this->download_file(key, local_path);
    });
}

auto azure_blob_storage::create_upload_stream(
    const std::string& key,
    const cloud_transfer_options& options) -> std::unique_ptr<cloud_upload_stream> {
    if (!is_connected()) {
        return nullptr;
    }

    return std::unique_ptr<cloud_upload_stream>(
        new azure_blob_upload_stream(key, impl_->config_, impl_->credentials_, options, impl_->http_client_));
}

auto azure_blob_storage::create_download_stream(
    const std::string& key) -> std::unique_ptr<cloud_download_stream> {
    if (!is_connected()) {
        return nullptr;
    }

    return std::unique_ptr<cloud_download_stream>(
        new azure_blob_download_stream(key, impl_->config_, impl_->credentials_));
}

auto azure_blob_storage::generate_presigned_url(
    const std::string& key,
    const presigned_url_options& options) -> result<std::string> {
    return generate_blob_sas(key, options);
}

void azure_blob_storage::on_upload_progress(
    std::function<void(const upload_progress&)> callback) {
    if (impl_) {
        impl_->upload_progress_callback_ = std::move(callback);
    }
}

void azure_blob_storage::on_download_progress(
    std::function<void(const download_progress&)> callback) {
    if (impl_) {
        impl_->download_progress_callback_ = std::move(callback);
    }
}

void azure_blob_storage::on_state_changed(
    std::function<void(cloud_storage_state)> callback) {
    if (impl_) {
        impl_->state_changed_callback_ = std::move(callback);
    }
}

auto azure_blob_storage::get_statistics() const -> cloud_storage_statistics {
    if (!impl_) {
        return {};
    }

    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->stats_;
}

void azure_blob_storage::reset_statistics() {
    if (impl_) {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        impl_->stats_ = {};
        impl_->stats_.connected_at = impl_->connected_at_;
    }
}

auto azure_blob_storage::config() const -> const cloud_storage_config& {
    return impl_->config_;
}

auto azure_blob_storage::bucket() const -> std::string_view {
    return impl_ ? impl_->config_.container : std::string_view{};
}

auto azure_blob_storage::region() const -> std::string_view {
    return impl_ ? impl_->config_.region : std::string_view{};
}

auto azure_blob_storage::get_azure_config() const -> const azure_blob_config& {
    return impl_->config_;
}

auto azure_blob_storage::container() const -> std::string_view {
    return impl_ ? impl_->config_.container : std::string_view{};
}

auto azure_blob_storage::account_name() const -> std::string_view {
    return impl_ ? impl_->config_.account_name : std::string_view{};
}

auto azure_blob_storage::endpoint_url() const -> std::string {
    if (!impl_) {
        return "";
    }

    return impl_->get_blob_endpoint();
}

auto azure_blob_storage::set_access_tier(
    const std::string& key,
    const std::string& tier) -> result<void> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

    (void)key;
    (void)tier;

    return result<void>{};
}

auto azure_blob_storage::get_access_tier(
    const std::string& key) -> result<std::string> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized, "Not connected"}};
    }

    (void)key;

    return std::string{"Hot"};
}

auto azure_blob_storage::generate_container_sas(
    const presigned_url_options& options) -> result<std::string> {
    if (!impl_) {
        return unexpected{error{error_code::not_initialized, "Storage not initialized"}};
    }

#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    auto creds = impl_->credentials_->get_credentials();
    if (!creds) {
        return unexpected{error{error_code::internal_error, "Invalid credentials"}};
    }

    auto azure_creds = std::dynamic_pointer_cast<const azure_credentials>(creds);
    if (!azure_creds || !azure_creds->account_key.has_value()) {
        return unexpected{error{error_code::internal_error, "Account key required for SAS generation"}};
    }

    std::string start_time = get_iso8601_time();
    std::string expiry_time = get_future_iso8601_time(options.expiration);

    std::string permissions = (options.method == "PUT") ? "racwdl" : "rl";

    std::ostringstream string_to_sign;
    string_to_sign << permissions << "\n";
    string_to_sign << start_time << "\n";
    string_to_sign << expiry_time << "\n";
    string_to_sign << "/blob/" << impl_->config_.account_name << "/" << impl_->config_.container << "\n";
    string_to_sign << "\n";  // signed identifier
    string_to_sign << "\n";  // IP range
    string_to_sign << "https\n";  // protocol
    string_to_sign << impl_->config_.api_version << "\n";
    string_to_sign << "c\n";  // resource type (container)
    string_to_sign << "\n";  // snapshot time
    string_to_sign << "\n";  // encryption scope
    string_to_sign << "\n";  // rscd
    string_to_sign << "\n";  // rsce
    string_to_sign << "\n";  // rscl
    string_to_sign << "";    // rsct

    auto key_bytes = base64_decode(azure_creds->account_key.value());
    auto signature = hmac_sha256(key_bytes, string_to_sign.str());
    std::string sig = base64_encode(signature);

    std::ostringstream sas;
    sas << "sv=" << impl_->config_.api_version;
    sas << "&ss=b&srt=co";
    sas << "&sp=" << permissions;
    sas << "&st=" << url_encode(start_time);
    sas << "&se=" << url_encode(expiry_time);
    sas << "&spr=https";
    sas << "&sig=" << url_encode(sig);

    return impl_->get_blob_endpoint() + "/" + impl_->config_.container + "?" + sas.str();
#else
    (void)options;
    return unexpected{error{error_code::internal_error, "Encryption support required for SAS generation"}};
#endif
}

auto azure_blob_storage::generate_blob_sas(
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

    auto azure_creds = std::dynamic_pointer_cast<const azure_credentials>(creds);
    if (!azure_creds || !azure_creds->account_key.has_value()) {
        return unexpected{error{error_code::internal_error, "Account key required for SAS generation"}};
    }

    std::string start_time = get_iso8601_time();
    std::string expiry_time = get_future_iso8601_time(options.expiration);

    std::string permissions = (options.method == "PUT") ? "racwd" : "r";

    std::ostringstream string_to_sign;
    string_to_sign << permissions << "\n";
    string_to_sign << start_time << "\n";
    string_to_sign << expiry_time << "\n";
    string_to_sign << "/blob/" << impl_->config_.account_name << "/" << impl_->config_.container << "/" << key << "\n";
    string_to_sign << "\n";  // signed identifier
    string_to_sign << "\n";  // IP range
    string_to_sign << "https\n";  // protocol
    string_to_sign << impl_->config_.api_version << "\n";
    string_to_sign << "b\n";  // resource type (blob)
    string_to_sign << "\n";  // snapshot time
    string_to_sign << "\n";  // encryption scope
    string_to_sign << "\n";  // rscd
    string_to_sign << "\n";  // rsce
    string_to_sign << "\n";  // rscl
    string_to_sign << "";    // rsct

    auto key_bytes = base64_decode(azure_creds->account_key.value());
    auto signature = hmac_sha256(key_bytes, string_to_sign.str());
    std::string sig = base64_encode(signature);

    std::ostringstream sas;
    sas << "sv=" << impl_->config_.api_version;
    sas << "&sr=b";
    sas << "&sp=" << permissions;
    sas << "&st=" << url_encode(start_time);
    sas << "&se=" << url_encode(expiry_time);
    sas << "&spr=https";
    sas << "&sig=" << url_encode(sig);

    return impl_->get_blob_url(key) + "?" + sas.str();
#else
    (void)key;
    (void)options;
    return unexpected{error{error_code::internal_error, "Encryption support required for SAS generation"}};
#endif
}

// ============================================================================
// Azure Blob Credential Provider Implementation
// ============================================================================

struct azure_blob_credential_provider::impl {
    credential_type type_;
    std::shared_ptr<azure_credentials> credentials_;
    credential_state state_ = credential_state::uninitialized;
    std::function<void(credential_state)> state_changed_callback_;
    bool auto_refresh_enabled_ = false;
    std::chrono::seconds auto_refresh_interval_{60};
    std::string auth_type_;

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

azure_blob_credential_provider::azure_blob_credential_provider(const azure_credentials& creds)
    : impl_(std::make_unique<impl>(credential_type::static_credentials, "account-key")) {
    impl_->credentials_ = std::make_shared<azure_credentials>(creds);
    impl_->set_state(credential_state::valid);
}

azure_blob_credential_provider::azure_blob_credential_provider(
    credential_type type, const std::string& account_name)
    : impl_(std::make_unique<impl>(type, "")) {
    impl_->credentials_ = std::make_shared<azure_credentials>();
    impl_->credentials_->account_name = account_name;
}

azure_blob_credential_provider::~azure_blob_credential_provider() = default;

azure_blob_credential_provider::azure_blob_credential_provider(azure_blob_credential_provider&&) noexcept = default;
auto azure_blob_credential_provider::operator=(azure_blob_credential_provider&&) noexcept -> azure_blob_credential_provider& = default;

auto azure_blob_credential_provider::create(
    const azure_credentials& creds) -> std::unique_ptr<azure_blob_credential_provider> {
    if (creds.account_name.empty()) {
        return nullptr;
    }

    if (!creds.account_key.has_value() &&
        !creds.connection_string.has_value() &&
        !creds.sas_token.has_value() &&
        !creds.client_id.has_value()) {
        return nullptr;
    }

    return std::unique_ptr<azure_blob_credential_provider>(new azure_blob_credential_provider(creds));
}

auto azure_blob_credential_provider::create_from_connection_string(
    const std::string& connection_string) -> std::unique_ptr<azure_blob_credential_provider> {
    auto info = parse_connection_string(connection_string);
    if (!info.valid) {
        return nullptr;
    }

    azure_credentials creds;
    creds.account_name = info.account_name;

    if (!info.account_key.empty()) {
        creds.account_key = info.account_key;
    }
    if (!info.sas_token.empty()) {
        creds.sas_token = info.sas_token;
    }

    creds.connection_string = connection_string;

    auto provider = std::unique_ptr<azure_blob_credential_provider>(
        new azure_blob_credential_provider(credential_type::static_credentials, info.account_name));
    provider->impl_->credentials_ = std::make_shared<azure_credentials>(creds);
    provider->impl_->auth_type_ = "connection-string";
    provider->impl_->set_state(credential_state::valid);

    return provider;
}

auto azure_blob_credential_provider::create_from_environment()
    -> std::unique_ptr<azure_blob_credential_provider> {
    // Try connection string first
    const char* conn_str = std::getenv("AZURE_STORAGE_CONNECTION_STRING");
    if (conn_str) {
        return create_from_connection_string(conn_str);
    }

    // Try account name and key
    const char* account_name = std::getenv("AZURE_STORAGE_ACCOUNT");
    const char* account_key = std::getenv("AZURE_STORAGE_KEY");

    if (account_name && account_key) {
        azure_credentials creds;
        creds.account_name = account_name;
        creds.account_key = account_key;

        auto provider = std::unique_ptr<azure_blob_credential_provider>(
            new azure_blob_credential_provider(credential_type::environment, account_name));
        provider->impl_->credentials_ = std::make_shared<azure_credentials>(creds);
        provider->impl_->auth_type_ = "environment";
        provider->impl_->set_state(credential_state::valid);

        return provider;
    }

    // Try SAS token
    const char* sas_token = std::getenv("AZURE_STORAGE_SAS_TOKEN");
    if (account_name && sas_token) {
        return create_from_sas_token(account_name, sas_token);
    }

    return nullptr;
}

auto azure_blob_credential_provider::create_from_sas_token(
    const std::string& account_name,
    const std::string& sas_token) -> std::unique_ptr<azure_blob_credential_provider> {
    azure_credentials creds;
    creds.account_name = account_name;
    creds.sas_token = sas_token;

    auto provider = std::unique_ptr<azure_blob_credential_provider>(
        new azure_blob_credential_provider(credential_type::static_credentials, account_name));
    provider->impl_->credentials_ = std::make_shared<azure_credentials>(creds);
    provider->impl_->auth_type_ = "sas-token";
    provider->impl_->set_state(credential_state::valid);

    return provider;
}

auto azure_blob_credential_provider::create_from_client_credentials(
    const std::string& tenant_id,
    const std::string& client_id,
    const std::string& client_secret,
    const std::string& account_name) -> std::unique_ptr<azure_blob_credential_provider> {
    azure_credentials creds;
    creds.account_name = account_name;
    creds.tenant_id = tenant_id;
    creds.client_id = client_id;
    creds.client_secret = client_secret;

    auto provider = std::unique_ptr<azure_blob_credential_provider>(
        new azure_blob_credential_provider(credential_type::static_credentials, account_name));
    provider->impl_->credentials_ = std::make_shared<azure_credentials>(creds);
    provider->impl_->auth_type_ = "client-credentials";
    provider->impl_->set_state(credential_state::valid);

    return provider;
}

auto azure_blob_credential_provider::create_default(
    const std::string& account_name) -> std::unique_ptr<azure_blob_credential_provider> {
    // Try environment variables first
    auto provider = create_from_environment();
    if (provider) {
        return provider;
    }

    // For now, return nullptr if no credentials found
    // TODO: Implement managed identity support
    (void)account_name;
    return nullptr;
}

auto azure_blob_credential_provider::provider() const -> cloud_provider {
    return cloud_provider::azure_blob;
}

auto azure_blob_credential_provider::get_credentials() const
    -> std::shared_ptr<const cloud_credentials> {
    if (!impl_) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->credentials_;
}

auto azure_blob_credential_provider::refresh() -> bool {
    if (!impl_) {
        return false;
    }

    // For static credentials and SAS tokens, refresh is a no-op
    if (impl_->type_ == credential_type::static_credentials) {
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

auto azure_blob_credential_provider::needs_refresh(std::chrono::seconds buffer) const -> bool {
    if (!impl_ || !impl_->credentials_) {
        return true;
    }

    if (impl_->credentials_->expiration.has_value()) {
        auto time_left = impl_->credentials_->time_until_expiration();
        if (time_left.has_value() && time_left.value() <= buffer) {
            return true;
        }
    }

    return false;
}

auto azure_blob_credential_provider::state() const -> credential_state {
    return impl_ ? impl_->state_ : credential_state::uninitialized;
}

void azure_blob_credential_provider::on_state_changed(
    std::function<void(credential_state)> callback) {
    if (impl_) {
        impl_->state_changed_callback_ = std::move(callback);
    }
}

void azure_blob_credential_provider::set_auto_refresh(
    bool enable,
    std::chrono::seconds check_interval) {
    if (impl_) {
        impl_->auto_refresh_enabled_ = enable;
        impl_->auto_refresh_interval_ = check_interval;
    }
}

auto azure_blob_credential_provider::account_name() const -> std::string {
    if (!impl_ || !impl_->credentials_) {
        return "";
    }
    return impl_->credentials_->account_name;
}

auto azure_blob_credential_provider::auth_type() const -> std::string_view {
    return impl_ ? impl_->auth_type_ : std::string_view{};
}

}  // namespace kcenon::file_transfer
