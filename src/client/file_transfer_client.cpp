/**
 * @file file_transfer_client.cpp
 * @brief File transfer client implementation
 */

#include "kcenon/file_transfer/client/file_transfer_client.h"

#include <chrono>
#include <filesystem>
#include <thread>

#ifdef BUILD_WITH_NETWORK_SYSTEM
#include <kcenon/network/core/messaging_client.h>
#endif

namespace kcenon::file_transfer {

struct file_transfer_client::impl {
    client_config config;
    std::atomic<connection_state> current_state{connection_state::disconnected};
    endpoint server_endpoint;

#ifdef BUILD_WITH_NETWORK_SYSTEM
    std::shared_ptr<network_system::core::messaging_client> network_client;
#endif

    // Callbacks
    std::function<void(const transfer_progress&)> progress_callback;
    std::function<void(const transfer_result&)> complete_callback;
    std::function<void(connection_state)> state_callback;

    // Statistics
    mutable std::mutex stats_mutex;
    client_statistics statistics;
    compression_statistics compression_stats;

    // Active transfers
    std::mutex transfers_mutex;
    std::unordered_map<uint64_t, transfer_handle> active_transfers;
    std::atomic<uint64_t> next_transfer_id{1};

    explicit impl(client_config cfg) : config(std::move(cfg)) {}

    void set_state(connection_state new_state) {
        auto old_state = current_state.exchange(new_state);
        if (old_state != new_state && state_callback) {
            state_callback(new_state);
        }
    }
};

// Builder implementation
file_transfer_client::builder::builder() = default;

auto file_transfer_client::builder::with_compression(compression_mode mode) -> builder& {
    config_.compression = mode;
    return *this;
}

auto file_transfer_client::builder::with_compression_level(compression_level level) -> builder& {
    config_.comp_level = level;
    return *this;
}

auto file_transfer_client::builder::with_chunk_size(std::size_t size) -> builder& {
    config_.chunk_size = size;
    return *this;
}

auto file_transfer_client::builder::with_auto_reconnect(
    bool enable, reconnect_policy policy) -> builder& {
    config_.auto_reconnect = enable;
    config_.reconnect = policy;
    return *this;
}

auto file_transfer_client::builder::with_upload_bandwidth_limit(
    std::size_t bytes_per_second) -> builder& {
    config_.upload_bandwidth_limit = bytes_per_second;
    return *this;
}

auto file_transfer_client::builder::with_download_bandwidth_limit(
    std::size_t bytes_per_second) -> builder& {
    config_.download_bandwidth_limit = bytes_per_second;
    return *this;
}

auto file_transfer_client::builder::with_connect_timeout(
    std::chrono::milliseconds timeout) -> builder& {
    config_.connect_timeout = timeout;
    return *this;
}

auto file_transfer_client::builder::build() -> result<file_transfer_client> {
    if (config_.chunk_size < 64 * 1024 || config_.chunk_size > 1024 * 1024) {
        return unexpected{error{error_code::invalid_chunk_size,
                               "Chunk size must be between 64KB and 1MB"}};
    }

    return file_transfer_client{std::move(config_)};
}

// file_transfer_client implementation
file_transfer_client::file_transfer_client(client_config config)
    : impl_(std::make_unique<impl>(std::move(config))) {
#ifdef BUILD_WITH_NETWORK_SYSTEM
    impl_->network_client = std::make_shared<network_system::core::messaging_client>(
        "file_transfer_client");
#endif
}

file_transfer_client::file_transfer_client(file_transfer_client&&) noexcept = default;
auto file_transfer_client::operator=(file_transfer_client&&) noexcept
    -> file_transfer_client& = default;
file_transfer_client::~file_transfer_client() {
    if (impl_ && is_connected()) {
        (void)disconnect();
    }
}

auto file_transfer_client::connect(const endpoint& server_addr) -> result<void> {
    if (impl_->current_state == connection_state::connected ||
        impl_->current_state == connection_state::connecting) {
        return unexpected{error{error_code::already_initialized,
                               "Client is already connected or connecting"}};
    }

    impl_->set_state(connection_state::connecting);
    impl_->server_endpoint = server_addr;

#ifdef BUILD_WITH_NETWORK_SYSTEM
    auto result = impl_->network_client->start_client(server_addr.host, server_addr.port);
    if (!result) {
        impl_->set_state(connection_state::disconnected);
        return unexpected{error{error_code::internal_error,
                               "Failed to connect: " + result.error().message}};
    }
#endif

    impl_->set_state(connection_state::connected);
    return {};
}

auto file_transfer_client::disconnect() -> result<void> {
    if (impl_->current_state != connection_state::connected) {
        return unexpected{error{error_code::not_initialized,
                               "Client is not connected"}};
    }

#ifdef BUILD_WITH_NETWORK_SYSTEM
    auto result = impl_->network_client->stop_client();
    if (!result) {
        return unexpected{error{error_code::internal_error,
                               "Failed to disconnect: " + result.error().message}};
    }
#endif

    impl_->set_state(connection_state::disconnected);
    return {};
}

auto file_transfer_client::is_connected() const -> bool {
    return impl_->current_state == connection_state::connected;
}

auto file_transfer_client::state() const -> connection_state {
    return impl_->current_state;
}

auto file_transfer_client::upload_file(
    const std::filesystem::path& local_path,
    [[maybe_unused]] const std::string& remote_name,
    [[maybe_unused]] const upload_options& options) -> result<transfer_handle> {

    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized,
                               "Client is not connected"}};
    }

    if (!std::filesystem::exists(local_path)) {
        return unexpected{error{error_code::file_not_found,
                               "Local file not found: " + local_path.string()}};
    }

    auto transfer_id = impl_->next_transfer_id++;
    transfer_handle handle{transfer_id};

    {
        std::lock_guard lock(impl_->transfers_mutex);
        impl_->active_transfers[transfer_id] = handle;
    }

    // TODO: Implement actual file upload logic
    // For now, just return the handle

    return handle;
}

auto file_transfer_client::download_file(
    const std::string& remote_name,
    [[maybe_unused]] const std::filesystem::path& local_path,
    [[maybe_unused]] const download_options& options) -> result<transfer_handle> {

    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized,
                               "Client is not connected"}};
    }

    if (remote_name.empty()) {
        return unexpected{error{error_code::invalid_file_path,
                               "Remote filename is empty"}};
    }

    auto transfer_id = impl_->next_transfer_id++;
    transfer_handle handle{transfer_id};

    {
        std::lock_guard lock(impl_->transfers_mutex);
        impl_->active_transfers[transfer_id] = handle;
    }

    // TODO: Implement actual file download logic
    // For now, just return the handle

    return handle;
}

auto file_transfer_client::list_files(
    [[maybe_unused]] const list_options& options) -> result<std::vector<file_info>> {

    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized,
                               "Client is not connected"}};
    }

    // TODO: Implement actual file listing logic
    // For now, return empty list
    return std::vector<file_info>{};
}

void file_transfer_client::on_progress(
    std::function<void(const transfer_progress&)> callback) {
    impl_->progress_callback = std::move(callback);
}

void file_transfer_client::on_complete(
    std::function<void(const transfer_result&)> callback) {
    impl_->complete_callback = std::move(callback);
}

void file_transfer_client::on_connection_state_changed(
    std::function<void(connection_state)> callback) {
    impl_->state_callback = std::move(callback);
}

auto file_transfer_client::get_statistics() const -> client_statistics {
    std::lock_guard lock(impl_->stats_mutex);
    auto stats = impl_->statistics;
    std::lock_guard transfers_lock(impl_->transfers_mutex);
    stats.active_transfers = impl_->active_transfers.size();
    return stats;
}

auto file_transfer_client::get_compression_stats() const -> compression_statistics {
    std::lock_guard lock(impl_->stats_mutex);
    return impl_->compression_stats;
}

auto file_transfer_client::config() const -> const client_config& {
    return impl_->config;
}

}  // namespace kcenon::file_transfer
