/**
 * @file file_transfer_server.cpp
 * @brief File transfer server implementation
 */

#include "kcenon/file_transfer/server/file_transfer_server.h"
#include "kcenon/file_transfer/core/logging.h"
#include "server_runtime.h"
#include <kcenon/file_transfer/encryption/transfer_crypto.h>

#include <chrono>
#include <filesystem>
#include <thread>

#include <kcenon/network/core/messaging_server.h>
#include <kcenon/network/session/messaging_session.h>

namespace kcenon::file_transfer {

struct file_transfer_server::impl {
    server_config config;
    std::atomic<server_state> current_state{server_state::stopped};
    uint16_t listen_port{0};

    std::unique_ptr<transfer_listener> listener;

    // Callbacks
    std::function<bool(const upload_request&)> upload_callback;
    std::function<bool(const download_request&)> download_callback;
    std::function<void(const client_info&)> connect_callback;
    std::function<void(const client_info&)> disconnect_callback;
    std::function<void(const transfer_result&)> complete_callback;
    std::function<void(const transfer_progress&)> progress_callback;
    mutable std::mutex callbacks_mutex;

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
    std::shared_ptr<detail::server_runtime> runtime;

    auto callbacks() const -> detail::server_callbacks {
        std::lock_guard lock(callbacks_mutex);
        return {upload_callback, download_callback, connect_callback,
                disconnect_callback, complete_callback, progress_callback};
    }

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

auto file_transfer_server::builder::with_encryption_key(std::span<const std::byte> key) -> builder& {
    config_.encryption_key.assign(key.begin(), key.end()); config_.encryption_password.clear(); config_.enable_encryption = true; return *this;
}
auto file_transfer_server::builder::with_encryption_password(const std::string& password) -> builder& {
    config_.encryption_password = password; config_.encryption_key.clear(); config_.enable_encryption = true; return *this;
}
auto file_transfer_server::builder::with_required_encryption(bool required) -> builder& {
    config_.require_encryption = required; if (required) config_.enable_encryption = true; return *this;
}

auto file_transfer_server::builder::with_storage_mode(storage_mode mode) -> builder& {
    config_.storage = mode;
    return *this;
}

auto file_transfer_server::builder::with_cloud_storage(
    std::shared_ptr<cloud_storage_interface> cloud_storage) -> builder& {
    if (!config_.cloud_config) {
        config_.cloud_config = cloud_storage_server_config{};
    }
    config_.cloud_config->cloud_storage = std::move(cloud_storage);
    return *this;
}

auto file_transfer_server::builder::with_cloud_credentials(
    std::shared_ptr<credential_provider> credentials) -> builder& {
    if (!config_.cloud_config) {
        config_.cloud_config = cloud_storage_server_config{};
    }
    config_.cloud_config->credentials = std::move(credentials);
    return *this;
}

auto file_transfer_server::builder::with_cloud_key_prefix(std::string prefix) -> builder& {
    if (!config_.cloud_config) {
        config_.cloud_config = cloud_storage_server_config{};
    }
    config_.cloud_config->key_prefix = std::move(prefix);
    return *this;
}

auto file_transfer_server::builder::with_cloud_replication(bool enable) -> builder& {
    if (!config_.cloud_config) {
        config_.cloud_config = cloud_storage_server_config{};
    }
    config_.cloud_config->replicate_writes = enable;
    return *this;
}

auto file_transfer_server::builder::with_cloud_fallback(bool enable) -> builder& {
    if (!config_.cloud_config) {
        config_.cloud_config = cloud_storage_server_config{};
    }
    config_.cloud_config->fallback_reads = enable;
    return *this;
}

auto file_transfer_server::builder::with_cloud_cache(bool enable, uint64_t max_size) -> builder& {
    if (!config_.cloud_config) {
        config_.cloud_config = cloud_storage_server_config{};
    }
    config_.cloud_config->enable_cache = enable;
    config_.cloud_config->max_cache_size = max_size;
    return *this;
}

auto file_transfer_server::builder::with_cloud_cache_directory(
    const std::filesystem::path& dir) -> builder& {
    if (!config_.cloud_config) {
        config_.cloud_config = cloud_storage_server_config{};
    }
    config_.cloud_config->cache_directory = dir;
    return *this;
}

auto file_transfer_server::builder::with_checkpoint_directory(std::filesystem::path directory) -> builder& {
    config_.checkpoint_directory=std::move(directory); return *this;
}

auto file_transfer_server::builder::with_transport(transfer_transport_config config) -> builder& {
    config_.transport = std::move(config); return *this;
}

auto file_transfer_server::builder::build() -> result<file_transfer_server> {
    if (auto quic = std::get_if<quic_transport_config>(&config_.transport)) {
#ifndef FILE_TRANS_ENABLE_QUIC
        return unexpected{error{error_code::invalid_configuration, "QUIC transport is disabled in this build"}};
#else
        if (!quic->cert_path || !quic->key_path || quic->cert_path->empty() || quic->key_path->empty())
            return unexpected{error{error_code::invalid_configuration, "QUIC server requires certificate and key files"}};
#endif
    }

    if (config_.enable_encryption || config_.require_encryption) {
        auto valid = transfer_crypto::validate_secret(config_.encryption_key, config_.encryption_password);
        if (!valid) return unexpected{valid.error()};
    }
    auto chunk_validation = chunk_config{config_.chunk_size}.validate();
    if (!chunk_validation) return unexpected{chunk_validation.error()};
    if (config_.cloud_config && config_.cloud_config->credentials) {
        return unexpected{error{error_code::invalid_configuration,
            "Configure credentials on the cloud storage instance before with_cloud_storage(); "
            "the storage interface cannot replace an existing backend's credentials"}};
    }
    // Validate configuration based on storage mode
    if (!config_.is_valid()) {
        switch (config_.storage) {
            case storage_mode::local_only:
                return unexpected{error{error_code::invalid_configuration,
                                       "storage_directory is required for local storage"}};
            case storage_mode::cloud_only:
                return unexpected{error{error_code::invalid_configuration,
                                       "cloud_storage is required for cloud-only mode"}};
            case storage_mode::hybrid:
                return unexpected{error{error_code::invalid_configuration,
                                       "both storage_directory and cloud_storage are required for hybrid mode"}};
        }
    }

    // Create storage directory if needed (for local or hybrid modes)
    if (config_.storage != storage_mode::cloud_only &&
        !config_.storage_directory.empty()) {
        std::error_code ec;
        if (!std::filesystem::exists(config_.storage_directory)) {
            std::filesystem::create_directories(config_.storage_directory, ec);
            if (ec) {
                return unexpected{error{error_code::file_access_denied,
                                       "Failed to create storage directory: " + ec.message()}};
            }
        }
    }

    // Create cache directory for cloud storage if needed
    if ((config_.storage == storage_mode::cloud_only ||
         config_.storage == storage_mode::hybrid) &&
        config_.cloud_config && config_.cloud_config->enable_cache) {

        auto cache_dir = config_.cloud_config->cache_directory;
        if (cache_dir.empty()) {
            cache_dir = std::filesystem::temp_directory_path() / "file_trans_cache";
            config_.cloud_config->cache_directory = cache_dir;
        }

        std::error_code ec;
        if (!std::filesystem::exists(cache_dir)) {
            std::filesystem::create_directories(cache_dir, ec);
            if (ec) {
                return unexpected{error{error_code::file_access_denied,
                                       "Failed to create cache directory: " + ec.message()}};
            }
        }
    }

    return file_transfer_server{std::move(config_)};
}

// file_transfer_server implementation
file_transfer_server::file_transfer_server(server_config config)
    : impl_(std::make_unique<impl>(std::move(config))) {
    // Initialize logger (safe to call multiple times)
    get_logger().initialize();

    impl_->listener = transfer_listener::create(impl_->config.transport);
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

    auto* owner = impl_.get();
    impl_->runtime = std::make_shared<detail::server_runtime>(impl_->config,
        [owner] { return owner->callbacks(); });
    auto initialized = impl_->runtime->start();
    if (!initialized) {
        impl_->runtime->stop();
        impl_->current_state = server_state::stopped;
        impl_->listen_port = 0;
        return initialized;
    }
    auto runtime = std::weak_ptr<detail::server_runtime>{impl_->runtime};
    impl_->listener->on_accept([runtime](auto session) {
        if (auto active = runtime.lock()) active->connect(session);
        else session->close();
    });

    auto result = impl_->listener->start(listen_addr);
    if (!result) {
        impl_->runtime->stop();
        impl_->current_state = server_state::stopped;
        FT_LOG_ERROR(log_category::server,
            "Failed to start network server: " + result.error().message);
        return unexpected{error{error_code::internal_error,
                               "Failed to start network server: " + result.error().message}};
    }

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

    if (impl_->runtime) impl_->runtime->stop();
    auto result = impl_->listener->stop();
    if (!result) {
        impl_->current_state = server_state::running;
        FT_LOG_ERROR(log_category::server,
            "Failed to stop network server: " + result.error().message);
        return unexpected{error{error_code::internal_error,
                               "Failed to stop network server: " + result.error().message}};
    }

    impl_->current_state = server_state::stopped;
    impl_->listen_port = 0;
    FT_LOG_INFO(log_category::server, "Server stopped");
    return {};
}

auto file_transfer_server::transport_name() const -> std::string { return impl_->listener->type(); }

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
    std::lock_guard lock(impl_->callbacks_mutex);
    impl_->upload_callback = std::move(callback);
}

void file_transfer_server::on_download_request(
    std::function<bool(const download_request&)> callback) {
    FT_LOG_DEBUG(log_category::server, "Download request callback registered");
    std::lock_guard lock(impl_->callbacks_mutex);
    impl_->download_callback = std::move(callback);
}

void file_transfer_server::on_client_connected(
    std::function<void(const client_info&)> callback) {
    FT_LOG_DEBUG(log_category::server, "Client connected callback registered");
    std::lock_guard lock(impl_->callbacks_mutex);
    impl_->connect_callback = std::move(callback);
}

void file_transfer_server::on_client_disconnected(
    std::function<void(const client_info&)> callback) {
    FT_LOG_DEBUG(log_category::server, "Client disconnected callback registered");
    std::lock_guard lock(impl_->callbacks_mutex);
    impl_->disconnect_callback = std::move(callback);
}

void file_transfer_server::on_transfer_complete(
    std::function<void(const transfer_result&)> callback) {
    FT_LOG_DEBUG(log_category::server, "Transfer complete callback registered");
    std::lock_guard lock(impl_->callbacks_mutex);
    impl_->complete_callback = std::move(callback);
}

void file_transfer_server::on_progress(
    std::function<void(const transfer_progress&)> callback) {
    FT_LOG_DEBUG(log_category::server, "Progress callback registered");
    std::lock_guard lock(impl_->callbacks_mutex);
    impl_->progress_callback = std::move(callback);
}

auto file_transfer_server::get_statistics() const -> server_statistics {
    if (impl_->runtime) return impl_->runtime->statistics();
    std::lock_guard lock(impl_->stats_mutex);
    auto stats = impl_->statistics;
    std::shared_lock clients_lock(impl_->clients_mutex);
    stats.active_connections = impl_->clients.size();
    return stats;
}

auto file_transfer_server::get_storage_stats() const -> storage_stats {
    if (impl_->runtime) return impl_->runtime->storage_statistics();
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
    // Public transfers commit through server_runtime. The standalone quota
    // component's construction-time scan is not the live service usage.
    if (!impl_->runtime && impl_->quota_mgr) return impl_->quota_mgr->get_usage();
    auto storage = get_storage_stats();
    quota_usage usage;
    usage.total_quota = storage.total_capacity;
    usage.used_bytes = storage.used_size;
    usage.available_bytes = storage.available_size;
    usage.file_count = storage.file_count;
    if (usage.total_quota > 0)
        usage.usage_percent = static_cast<double>(usage.used_bytes) / usage.total_quota * 100.0;
    return usage;
}

auto file_transfer_server::check_upload_allowed(uint64_t file_size) -> result<void> {
    if (impl_->config.max_file_size && file_size > impl_->config.max_file_size)
        return unexpected{error{error_code::file_too_large, "File size exceeds maximum allowed"}};
    auto usage = get_quota_usage();
    if (usage.total_quota && file_size > usage.available_bytes)
        return unexpected{error{error_code::quota_exceeded, "Storage quota would be exceeded"}};
    // Advisory snapshot; the runtime reserves concurrent uploads atomically.
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
