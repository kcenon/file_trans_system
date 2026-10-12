#include <kcenon/file_transfer/cloud/http_transport.h>
#include <curl/curl.h>
#include <memory>

namespace kcenon::file_transfer {
auto make_default_cloud_http_transport() -> cloud_http_transport {
    return [](const cloud_http_request& request) -> result<cloud_http_response> {
        static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (initialized != CURLE_OK) return unexpected{error{error_code::not_initialized, "Unable to initialize libcurl"}};
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> client(curl_easy_init(), curl_easy_cleanup);
        if (!client) return unexpected{error{error_code::not_initialized, "Unable to create HTTP transport"}};
        struct response_state { cloud_http_response value; std::size_t limit; bool failed{}; } state{{}, request.max_response_bytes};
        auto receive = +[](char* bytes, std::size_t size, std::size_t count, void* context) -> std::size_t {
            auto& out = *static_cast<response_state*>(context); auto length = size * count;
            if (out.limit && length > out.limit - out.value.body.size()) { out.failed = true; return 0; }
            try { out.value.body.insert(out.value.body.end(), bytes, bytes + length); }
            catch (...) { out.failed = true; return 0; }
            return length;
        };
        auto header = +[](char* bytes, std::size_t size, std::size_t count, void* context) -> std::size_t {
            auto length = size * count;
            try {
                auto& out = *static_cast<response_state*>(context);
                std::string line(bytes, length); auto colon = line.find(':');
                if (line.starts_with("HTTP/")) out.value.headers.clear();
                else if (colon != std::string::npos) {
                    auto key = line.substr(0, colon); auto value = line.substr(colon + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    auto end = value.find_last_not_of(" \t\r\n"); value.resize(end == std::string::npos ? 0 : end + 1);
                    out.value.headers[key] = std::move(value);
                }
            } catch (...) { return 0; }
            return length;
        };
        curl_slist* raw_headers = nullptr;
        for (const auto& [key, value] : request.headers) {
            auto* next = curl_slist_append(raw_headers, (key + ": " + value).c_str());
            if (!next) { curl_slist_free_all(raw_headers); return unexpected{error{error_code::internal_error, "HTTP header allocation failed"}}; }
            raw_headers = next;
        }
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(raw_headers, curl_slist_free_all);
        curl_easy_setopt(client.get(), CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(client.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(client.get(), CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(request.connect_timeout.count()));
        curl_easy_setopt(client.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout.count()));
        curl_easy_setopt(client.get(), CURLOPT_SSL_VERIFYPEER, request.verify_ssl ? 1L : 0L);
        curl_easy_setopt(client.get(), CURLOPT_SSL_VERIFYHOST, request.verify_ssl ? 2L : 0L);
        if (request.ca_cert_path) curl_easy_setopt(client.get(), CURLOPT_CAINFO, request.ca_cert_path->c_str());
        curl_easy_setopt(client.get(), CURLOPT_HTTPHEADER, headers.get());
        curl_easy_setopt(client.get(), CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(client.get(), CURLOPT_WRITEDATA, &state);
        curl_easy_setopt(client.get(), CURLOPT_HEADERFUNCTION, header);
        curl_easy_setopt(client.get(), CURLOPT_HEADERDATA, &state);
        struct source_state { const cloud_http_body_source* source; uint64_t offset{}; std::optional<error> failure; } input{request.source ? &*request.source : nullptr};
        auto read = +[](char* bytes, std::size_t size, std::size_t count, void* context) -> std::size_t {
            auto& input = *static_cast<source_state*>(context);
            try {
                auto capacity = static_cast<std::size_t>(std::min<uint64_t>(size * count, input.source->size - input.offset));
                if (!capacity) return 0;
                auto result = input.source->read(input.offset, {reinterpret_cast<std::byte*>(bytes), capacity});
                if (!result) input.failure = result.error();
                else if (result.value() == 0 || result.value() > capacity) input.failure = error{error_code::file_read_error, "HTTP upload source ended early"};
                else { input.offset += result.value(); return result.value(); }
            } catch (const std::exception& e) { input.failure = error{error_code::file_read_error, e.what()}; }
            return CURL_READFUNC_ABORT;
        };
        if (request.method == "HEAD") curl_easy_setopt(client.get(), CURLOPT_NOBODY, 1L);
        else if (request.source) {
            curl_easy_setopt(client.get(), CURLOPT_UPLOAD, 1L);
            curl_easy_setopt(client.get(), CURLOPT_UPLOAD_BUFFERSIZE, 65536L);
            curl_easy_setopt(client.get(), CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(request.source->size));
            curl_easy_setopt(client.get(), CURLOPT_READFUNCTION, read);
            curl_easy_setopt(client.get(), CURLOPT_READDATA, &input);
        } else if (request.method == "PUT" || request.method == "POST") {
            curl_easy_setopt(client.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
            curl_easy_setopt(client.get(), CURLOPT_POSTFIELDS, request.body.empty() ? "" : reinterpret_cast<const char*>(request.body.data()));
        }
        curl_easy_setopt(client.get(), CURLOPT_CUSTOMREQUEST, request.method.c_str());
        const auto performed = curl_easy_perform(client.get());
        if (input.failure) return unexpected{*input.failure};
        if (performed != CURLE_OK) return unexpected{error{performed == CURLE_OPERATION_TIMEDOUT ? error_code::connection_timeout : error_code::connection_failed,
            state.failed ? "HTTP response exceeded its memory budget" : std::string("HTTP transport failed: ") + curl_easy_strerror(performed)}};
        long status = 0; curl_easy_getinfo(client.get(), CURLINFO_RESPONSE_CODE, &status);
        state.value.status_code = static_cast<int>(status);
        return std::move(state.value);
    };
}
} // namespace kcenon::file_transfer
