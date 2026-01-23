/**
 * @file cloud_http_client.cpp
 * @brief Unified HTTP client adapter implementation for cloud storage
 * @version 0.1.0
 */

#include "kcenon/file_transfer/cloud/cloud_http_client.h"
#include "kcenon/file_transfer/cloud/cloud_utils.h"

#include <random>
#include <thread>

#include "kcenon/file_transfer/config/feature_flags.h"

#if KCENON_WITH_NETWORK_SYSTEM
#include <kcenon/network/core/http_client.h>
#endif

namespace kcenon::file_transfer {

// ============================================================================
// Implementation
// ============================================================================

struct cloud_http_client::impl {
#if KCENON_WITH_NETWORK_SYSTEM
    std::shared_ptr<kcenon::network::core::http_client> client;
#endif
    bool available = false;

    explicit impl(std::chrono::milliseconds timeout) {
#if KCENON_WITH_NETWORK_SYSTEM
        client = std::make_shared<kcenon::network::core::http_client>(timeout);
        available = true;
#else
        (void)timeout;
        available = false;
#endif
    }

#if KCENON_WITH_NETWORK_SYSTEM
    static auto convert_response(
        const kcenon::network::internal::http_response& resp) -> http_response_base {
        http_response_base result;
        result.status_code = resp.status_code;
        result.headers = resp.headers;
        result.body = std::vector<uint8_t>(resp.body.begin(), resp.body.end());
        return result;
    }
#endif
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

cloud_http_client::cloud_http_client(std::chrono::milliseconds timeout)
    : impl_(std::make_unique<impl>(timeout)) {}

cloud_http_client::~cloud_http_client() = default;

cloud_http_client::cloud_http_client(cloud_http_client&&) noexcept = default;
auto cloud_http_client::operator=(cloud_http_client&&) noexcept
    -> cloud_http_client& = default;

// ============================================================================
// HTTP Operations
// ============================================================================

auto cloud_http_client::get(
    const std::string& url,
    const std::map<std::string, std::string>& query,
    const std::map<std::string, std::string>& headers)
    -> result<http_response_base> {
#if KCENON_WITH_NETWORK_SYSTEM
    if (!impl_->client) {
        return unexpected{error{error_code::internal_error,
            "HTTP client not initialized"}};
    }

    auto response = impl_->client->get(url, query, headers);
    if (response.is_err()) {
        return unexpected{error{error_code::internal_error,
            "HTTP GET request failed"}};
    }
    return impl_->convert_response(response.value());
#else
    (void)url;
    (void)query;
    (void)headers;
    return unexpected{error{error_code::internal_error,
        "HTTP client not available (KCENON_WITH_NETWORK_SYSTEM not defined)"}};
#endif
}

auto cloud_http_client::post(
    const std::string& url,
    const std::string& body,
    const std::map<std::string, std::string>& headers)
    -> result<http_response_base> {
#if KCENON_WITH_NETWORK_SYSTEM
    if (!impl_->client) {
        return unexpected{error{error_code::internal_error,
            "HTTP client not initialized"}};
    }

    auto response = impl_->client->post(url, body, headers);
    if (response.is_err()) {
        return unexpected{error{error_code::internal_error,
            "HTTP POST request failed"}};
    }
    return impl_->convert_response(response.value());
#else
    (void)url;
    (void)body;
    (void)headers;
    return unexpected{error{error_code::internal_error,
        "HTTP client not available (KCENON_WITH_NETWORK_SYSTEM not defined)"}};
#endif
}

auto cloud_http_client::post(
    const std::string& url,
    const std::vector<uint8_t>& body,
    const std::map<std::string, std::string>& headers)
    -> result<http_response_base> {
#if KCENON_WITH_NETWORK_SYSTEM
    if (!impl_->client) {
        return unexpected{error{error_code::internal_error,
            "HTTP client not initialized"}};
    }

    auto response = impl_->client->post(url, body, headers);
    if (response.is_err()) {
        return unexpected{error{error_code::internal_error,
            "HTTP POST request failed"}};
    }
    return impl_->convert_response(response.value());
#else
    (void)url;
    (void)body;
    (void)headers;
    return unexpected{error{error_code::internal_error,
        "HTTP client not available (KCENON_WITH_NETWORK_SYSTEM not defined)"}};
#endif
}

auto cloud_http_client::put(
    const std::string& url,
    const std::string& body,
    const std::map<std::string, std::string>& headers)
    -> result<http_response_base> {
#if KCENON_WITH_NETWORK_SYSTEM
    if (!impl_->client) {
        return unexpected{error{error_code::internal_error,
            "HTTP client not initialized"}};
    }

    auto response = impl_->client->put(url, body, headers);
    if (response.is_err()) {
        return unexpected{error{error_code::internal_error,
            "HTTP PUT request failed"}};
    }
    return impl_->convert_response(response.value());
#else
    (void)url;
    (void)body;
    (void)headers;
    return unexpected{error{error_code::internal_error,
        "HTTP client not available (KCENON_WITH_NETWORK_SYSTEM not defined)"}};
#endif
}

auto cloud_http_client::put(
    const std::string& url,
    const std::vector<uint8_t>& body,
    const std::map<std::string, std::string>& headers)
    -> result<http_response_base> {
    std::string body_str(body.begin(), body.end());
    return put(url, body_str, headers);
}

auto cloud_http_client::del(
    const std::string& url,
    const std::map<std::string, std::string>& headers)
    -> result<http_response_base> {
#if KCENON_WITH_NETWORK_SYSTEM
    if (!impl_->client) {
        return unexpected{error{error_code::internal_error,
            "HTTP client not initialized"}};
    }

    auto response = impl_->client->del(url, headers);
    if (response.is_err()) {
        return unexpected{error{error_code::internal_error,
            "HTTP DELETE request failed"}};
    }
    return impl_->convert_response(response.value());
#else
    (void)url;
    (void)headers;
    return unexpected{error{error_code::internal_error,
        "HTTP client not available (KCENON_WITH_NETWORK_SYSTEM not defined)"}};
#endif
}

auto cloud_http_client::head(
    const std::string& url,
    const std::map<std::string, std::string>& headers)
    -> result<http_response_base> {
#if KCENON_WITH_NETWORK_SYSTEM
    if (!impl_->client) {
        return unexpected{error{error_code::internal_error,
            "HTTP client not initialized"}};
    }

    auto response = impl_->client->head(url, headers);
    if (response.is_err()) {
        return unexpected{error{error_code::internal_error,
            "HTTP HEAD request failed"}};
    }
    return impl_->convert_response(response.value());
#else
    (void)url;
    (void)headers;
    return unexpected{error{error_code::internal_error,
        "HTTP client not available (KCENON_WITH_NETWORK_SYSTEM not defined)"}};
#endif
}

// ============================================================================
// Utilities
// ============================================================================

auto cloud_http_client::is_available() const noexcept -> bool {
    return impl_->available;
}

auto cloud_http_client::calculate_retry_delay(
    const cloud_retry_policy& policy,
    std::size_t attempt) -> std::chrono::milliseconds {
    return cloud_utils::calculate_retry_delay(policy, attempt);
}

// ============================================================================
// Factory Function
// ============================================================================

auto make_cloud_http_client(std::chrono::milliseconds timeout)
    -> std::shared_ptr<cloud_http_client> {
    return std::make_shared<cloud_http_client>(timeout);
}

}  // namespace kcenon::file_transfer
