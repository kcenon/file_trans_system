/**
 * @file file_transfer_server.cpp
 * @brief File transfer server implementation
 */

#include "kcenon/file_transfer/server/file_transfer_server.h"
#include "kcenon/file_transfer/core/logging.h"

#include <chrono>
#include <filesystem>
#include <thread>

#ifdef BUILD_WITH_NETWORK_SYSTEM
#include <kcenon/network/core/messaging_server.h>
#include <kcenon/network/session/messaging_session.h>
#endif

namespace kcenon::file_transfer {

struct file_transfer_server::impl {
    server_config config;
    std::atomic<server_state> current_state{server_state::stopped};
    uint16_t listen_port{0};

#ifdef BUILD_WITH_NETWORK_SYSTEM
    std::shared_ptr<network_system::core::messaging_server> network_server;
#endif

    // Callbacks
    std::function<bool(const upload_request&)> upload_callback;
    std::function<bool(const download_request&)> download_callback;
    std::function<void(const client_info&)> connect_callback;
    std::function<void(const client_info&)> disconnect_callback;
    std::function<void(const transfer_result&)> complete_callback;
    std::function<void(const transfer_progress&)> progress_callback;

    // Statistics
    mutable std::mutex stats_mutex;
    server_statistics statistics;
    storage_stats storage_statistics;

    // Quota management
    std::unique_ptr<quota_manager> quota_mgr;

    // Client tracking
    std::shared_mutex clients_mutex;
    std::unordered_map<uint64_t, client_info> clients;
    std::atomic<uint64_t> next_client_id{1};

    // Helper to log client connection
    void log_client_connected(const client_info& info) {
        transfer_log_context ctx;
        ctx.client_id = std::to_string(info.id.value);
        FT_LOG_INFO_CTX(log_category::server, "Client connected", ctx);
    }

    // Helper to log client disconnection
    void log_client_disconnected(const client_info& info) {
        transfer_log_context ctx;
        ctx.client_id = std::to_string(info.id.value);
        FT_LOG_INFO_CTX(log_category::server, "Client disconnected", ctx);
    }

    explicit impl(server_config cfg) : config(std::move(cfg)) {
        // Initialize quota manager
        auto qm_result = quota_manager::create(
            config.storage_directory, config.storage_quota);
        if (qm_result.has_value()) {
            quota_mgr = std::make_unique<quota_manager>(std::move(qm_result.value()));
            quota_mgr->set_max_file_size(config.max_file_size);

            // Update storage statistics from quota manager
            auto usage = quota_mgr->get_usage();
            storage_statistics.total_capacity = usage.total_quota;
            storage_statistics.used_size = usage.used_bytes;
            storage_statistics.available_size = usage.available_bytes;
            storage_statistics.file_count = usage.file_count;
        } else {
            // Fallback: Calculate storage stats manually
            if (std::filesystem::exists(config.storage_directory)) {
                storage_statistics.total_capacity = config.storage_quota;
                storage_statistics.used_size = 0;
                storage_statistics.file_count = 0;

                std::error_code ec;
                for (const auto& entry :
                     std::filesystem::directory_iterator(config.storage_directory, ec)) {
                    if (entry.is_regular_file()) {
                        storage_statistics.used_size += entry.file_size();
                        storage_statistics.file_count++;
                    }
                }
                storage_statistics.available_size =
                    storage_statistics.total_capacity - storage_statistics.used_size;
            }
        }
    }
};

// Builder implementation
file_transfer_server::builder::builder() = default;

auto file_transfer_server::builder::with_storage_directory(
    const std::filesystem::path& dir) -> builder& {
    config_.storage_directory = dir;
    return *this;
}

auto file_transfer_server::builder::with_max_connections(std::size_t max_count) -> builder& {
    config_.max_connections = max_count;
    return *this;
}

auto file_transfer_server::builder::with_max_file_size(uint64_t max_bytes) -> builder& {
    config_.max_file_size = max_bytes;
    return *this;
}

auto file_transfer_server::builder::with_storage_quota(uint64_t max_bytes) -> builder& {
    config_.storage_quota = max_bytes;
    return *this;
}

auto file_transfer_server::builder::with_chunk_size(std::size_t size) -> builder& {
    config_.chunk_size = size;
    return *this;
}

auto file_transfer_server::builder::build() -> result<file_transfer_server> {
    if (config_.storage_directory.empty()) {
        return unexpected{error{error_code::invalid_configuration,
                               "storage_directory is required"}};
    }

    // Create storage directory if it doesn't exist
    std::error_code ec;
    if (!std::filesystem::exists(config_.storage_directory)) {
        std::filesystem::create_directories(config_.storage_directory, ec);
        if (ec) {
            return unexpected{error{error_code::file_access_denied,
                                   "Failed to create storage directory: " + ec.message()}};
        }
    }

    return file_transfer_server{std::move(config_)};
}

// file_transfer_server implementation
file_transfer_server::file_transfer_server(server_config config)
    : impl_(std::make_unique<impl>(std::move(config))) {
    // Initialize logger (safe to call multiple times)
    get_logger().initialize();

#ifdef BUILD_WITH_NETWORK_SYSTEM
    impl_->network_server = std::make_shared<network_system::core::messaging_server>(
        "file_transfer_server");
#endif
}

file_transfer_server::file_transfer_server(file_transfer_server&&) noexcept = default;
auto file_transfer_server::operator=(file_transfer_server&&) noexcept
    -> file_transfer_server& = default;
file_transfer_server::~file_transfer_server() {
    if (impl_ && is_running()) {
        (void)stop();
    }
}

auto file_transfer_server::start(const endpoint& listen_addr) -> result<void> {
    if (impl_->current_state != server_state::stopped) {
        FT_LOG_WARN(log_category::server, "Server start failed: already running");
        return unexpected{error{error_code::already_initialized,
                               "Server is already running"}};
    }

    FT_LOG_INFO(log_category::server,
        "Starting server on " + listen_addr.host + ":" + std::to_string(listen_addr.port));

    impl_->current_state = server_state::starting;
    impl_->listen_port = listen_addr.port;

#ifdef BUILD_WITH_NETWORK_SYSTEM
    auto result = impl_->network_server->start_server(listen_addr.port);
    if (result.is_err()) {
        impl_->current_state = server_state::stopped;
        FT_LOG_ERROR(log_category::server,
            "Failed to start network server: " + result.error().message);
        return unexpected{error{error_code::internal_error,
                               "Failed to start network server: " + result.error().message}};
    }
#endif

    impl_->current_state = server_state::running;
    FT_LOG_INFO(log_category::server,
        "Server started successfully on port " + std::to_string(listen_addr.port));
    return {};
}

auto file_transfer_server::stop() -> result<void> {
    if (impl_->current_state != server_state::running) {
        FT_LOG_WARN(log_category::server, "Server stop called but server is not running");
        return unexpected{error{error_code::not_initialized,
                               "Server is not running"}};
    }

    FT_LOG_INFO(log_category::server, "Stopping server");
    impl_->current_state = server_state::stopping;

#ifdef BUILD_WITH_NETWORK_SYSTEM
    auto result = impl_->network_server->stop_server();
    if (result.is_err()) {
        impl_->current_state = server_state::running;
        FT_LOG_ERROR(log_category::server,
            "Failed to stop network server: " + result.error().message);
        return unexpected{error{error_code::internal_error,
                               "Failed to stop network server: " + result.error().message}};
    }
#endif

    impl_->current_state = server_state::stopped;
    impl_->listen_port = 0;
    FT_LOG_INFO(log_category::server, "Server stopped");
    return {};
}

auto file_transfer_server::is_running() const -> bool {
    return impl_->current_state == server_state::running;
}

auto file_transfer_server::state() const -> server_state {
    return impl_->current_state;
}

auto file_transfer_server::port() const -> uint16_t {
    return impl_->listen_port;
}

void file_transfer_server::on_upload_request(
    std::function<bool(const upload_request&)> callback) {
    FT_LOG_DEBUG(log_category::server, "Upload request callback registered");
    impl_->upload_callback = std::move(callback);
}

void file_transfer_server::on_download_request(
    std::function<bool(const download_request&)> callback) {
    FT_LOG_DEBUG(log_category::server, "Download request callback registered");
    impl_->download_callback = std::move(callback);
}

void file_transfer_server::on_client_connected(
    std::function<void(const client_info&)> callback) {
    FT_LOG_DEBUG(log_category::server, "Client connected callback registered");
    impl_->connect_callback = std::move(callback);
}

void file_transfer_server::on_client_disconnected(
    std::function<void(const client_info&)> callback) {
    FT_LOG_DEBUG(log_category::server, "Client disconnected callback registered");
    impl_->disconnect_callback = std::move(callback);
}

void file_transfer_server::on_transfer_complete(
    std::function<void(const transfer_result&)> callback) {
    FT_LOG_DEBUG(log_category::server, "Transfer complete callback registered");
    impl_->complete_callback = std::move(callback);
}

void file_transfer_server::on_progress(
    std::function<void(const transfer_progress&)> callback) {
    FT_LOG_DEBUG(log_category::server, "Progress callback registered");
    impl_->progress_callback = std::move(callback);
}

auto file_transfer_server::get_statistics() const -> server_statistics {
    std::lock_guard lock(impl_->stats_mutex);
    auto stats = impl_->statistics;
    std::shared_lock clients_lock(impl_->clients_mutex);
    stats.active_connections = impl_->clients.size();
    return stats;
}

auto file_transfer_server::get_storage_stats() const -> storage_stats {
    std::lock_guard lock(impl_->stats_mutex);
    return impl_->storage_statistics;
}

auto file_transfer_server::config() const -> const server_config& {
    return impl_->config;
}

auto file_transfer_server::get_quota_manager() -> quota_manager& {
    if (!impl_->quota_mgr) {
        // Create quota manager if not exists (shouldn't happen in normal usage)
        auto qm_result = quota_manager::create(
            impl_->config.storage_directory, impl_->config.storage_quota);
        if (qm_result.has_value()) {
            impl_->quota_mgr = std::make_unique<quota_manager>(std::move(qm_result.value()));
            impl_->quota_mgr->set_max_file_size(impl_->config.max_file_size);
        }
    }
    return *impl_->quota_mgr;
}

auto file_transfer_server::get_quota_manager() const -> const quota_manager& {
    return *impl_->quota_mgr;
}

auto file_transfer_server::get_quota_usage() const -> quota_usage {
    if (impl_->quota_mgr) {
        return impl_->quota_mgr->get_usage();
    }
    // Fallback if quota manager not available
    quota_usage usage;
    std::lock_guard lock(impl_->stats_mutex);
    usage.total_quota = impl_->storage_statistics.total_capacity;
    usage.used_bytes = impl_->storage_statistics.used_size;
    usage.available_bytes = impl_->storage_statistics.available_size;
    usage.file_count = impl_->storage_statistics.file_count;
    if (usage.total_quota > 0) {
        usage.usage_percent = static_cast<double>(usage.used_bytes) /
                              static_cast<double>(usage.total_quota) * 100.0;
    }
    return usage;
}

auto file_transfer_server::check_upload_allowed(uint64_t file_size) -> result<void> {
    if (impl_->quota_mgr) {
        // Check file size limit
        auto file_result = impl_->quota_mgr->check_file_size(file_size);
        if (!file_result.has_value()) {
            FT_LOG_WARN(log_category::server,
                "Upload rejected: file too large (" + std::to_string(file_size) + " bytes)");
            return file_result;
        }

        // Check quota
        auto quota_result = impl_->quota_mgr->check_quota(file_size);
        if (!quota_result.has_value()) {
            FT_LOG_WARN(log_category::server,
                "Upload rejected: quota exceeded");
            return quota_result;
        }
    } else {
        // Fallback check
        if (impl_->config.max_file_size > 0 && file_size > impl_->config.max_file_size) {
            return unexpected{error{error_code::file_too_large,
                                   "File size exceeds maximum allowed"}};
        }
        std::lock_guard lock(impl_->stats_mutex);
        if (impl_->storage_statistics.used_size + file_size >
            impl_->storage_statistics.total_capacity) {
            return unexpected{error{error_code::quota_exceeded,
                                   "Storage quota would be exceeded"}};
        }
    }
    return {};
}

void file_transfer_server::on_quota_warning(
    std::function<void(const quota_usage&)> callback) {
    if (impl_->quota_mgr) {
        impl_->quota_mgr->on_quota_warning(std::move(callback));
    }
}

void file_transfer_server::on_quota_exceeded(
    std::function<void(const quota_usage&)> callback) {
    if (impl_->quota_mgr) {
        impl_->quota_mgr->on_quota_exceeded(std::move(callback));
    }
}

}  // namespace kcenon::file_transfer
