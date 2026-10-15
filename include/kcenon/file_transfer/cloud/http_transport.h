#pragma once
#include <kcenon/file_transfer/core/types.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <map>
#include <span>

namespace kcenon::file_transfer {
struct cloud_http_body_source {
    uint64_t size{};
    std::string sha256;
    // Offset-based reads make request retries replayable without buffering.
    std::function<result<std::size_t>(uint64_t, std::span<std::byte>)> read;
};
struct cloud_http_request {
    std::string method, url;
    std::map<std::string, std::string> headers;
    std::span<const std::byte> body;
    std::optional<cloud_http_body_source> source;
    std::chrono::milliseconds connect_timeout{30000}, timeout{30000};
    bool verify_ssl{true};
    std::optional<std::string> ca_cert_path;
    std::size_t max_response_bytes{0}; // Zero permits the buffered convenience API.
};
struct cloud_http_response {
    int status_code{};
    std::map<std::string, std::string> headers;
    std::vector<uint8_t> body;
    auto get_body_string() const -> std::string { return {body.begin(), body.end()}; }
    auto get_header(std::string name) const -> std::optional<std::string> {
        auto lower = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); }); return text;
        };
        name = lower(std::move(name));
        for (const auto& [key, value] : headers) if (lower(key) == name) return value;
        return std::nullopt;
    }
};
// Synchronous callback; body is valid only for the duration of the call.
// Injected implementations must support concurrent multipart requests.
using cloud_http_transport = std::function<result<cloud_http_response>(const cloud_http_request&)>;
auto make_default_cloud_http_transport() -> cloud_http_transport;
} // namespace kcenon::file_transfer
