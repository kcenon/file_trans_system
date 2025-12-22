/**
 * @file tcp_transport.cpp
 * @brief TCP transport implementation
 */

#include "kcenon/file_transfer/transport/tcp_transport.h"
#include "kcenon/file_transfer/core/logging.h"

#include <condition_variable>
#include <queue>
#include <thread>

#include <kcenon/network/core/messaging_client.h>

namespace kcenon::file_transfer {

struct tcp_transport::impl {
    tcp_transport_config config;
    std::atomic<transport_state> current_state{transport_state::disconnected};

    // Connection info
    std::optional<endpoint> local_ep;
    std::optional<endpoint> remote_ep;

    // Callbacks
    std::function<void(const transport_event_data&)> event_callback;
    std::function<void(transport_state)> state_callback;

    // Statistics
    mutable std::mutex stats_mutex;
    transport_statistics stats;

    // Receive buffer
    mutable std::mutex receive_mutex;
    std::condition_variable receive_cv;
    std::queue<std::vector<std::byte>> receive_queue;

    std::shared_ptr<network_system::core::messaging_client> network_client;

    explicit impl(tcp_transport_config cfg) : config(std::move(cfg)) {
        network_client = std::make_shared<network_system::core::messaging_client>(
            "tcp_transport");
    }

    void set_state(transport_state new_state) {
        auto old_state = current_state.exchange(new_state);
        if (old_state != new_state) {
            FT_LOG_DEBUG(log_category::transfer,
                "TCP transport state changed: " +
                std::string(to_string(old_state)) + " -> " +
                std::string(to_string(new_state)));

            if (state_callback) {
                state_callback(new_state);
            }

            // Emit event for connected/disconnected
            if (event_callback) {
                transport_event_data event_data;
                if (new_state == transport_state::connected) {
                    event_data.event = transport_event::connected;
                } else if (new_state == transport_state::disconnected) {
                    event_data.event = transport_event::disconnected;
                } else if (new_state == transport_state::error) {
                    event_data.event = transport_event::error;
                }
                event_callback(event_data);
            }
        }
    }

    void update_send_stats(std::size_t bytes) {
        std::lock_guard lock(stats_mutex);
        stats.bytes_sent += bytes;
        stats.packets_sent++;
    }

    void update_receive_stats(std::size_t bytes) {
        std::lock_guard lock(stats_mutex);
        stats.bytes_received += bytes;
        stats.packets_received++;
    }

    void increment_errors() {
        std::lock_guard lock(stats_mutex);
        stats.errors++;
    }
};

tcp_transport::tcp_transport(tcp_transport_config config)
    : impl_(std::make_unique<impl>(std::move(config))) {
    get_logger().initialize();
    FT_LOG_DEBUG(log_category::transfer, "TCP transport created");
}

tcp_transport::~tcp_transport() {
    if (impl_ && is_connected()) {
        (void)disconnect();
    }
}

tcp_transport::tcp_transport(tcp_transport&&) noexcept = default;
auto tcp_transport::operator=(tcp_transport&&) noexcept -> tcp_transport& = default;

auto tcp_transport::create(const tcp_transport_config& config)
    -> std::unique_ptr<tcp_transport> {
    return std::unique_ptr<tcp_transport>(new tcp_transport(config));
}

auto tcp_transport::type() const -> std::string_view {
    return "tcp";
}

auto tcp_transport::connect(const endpoint& remote) -> result<connection_result> {
    return connect(remote, impl_->config.connect_timeout);
}

auto tcp_transport::connect(
    const endpoint& remote,
    std::chrono::milliseconds timeout) -> result<connection_result> {

    if (impl_->current_state == transport_state::connected ||
        impl_->current_state == transport_state::connecting) {
        return unexpected{error{error_code::already_initialized,
            "Transport is already connected or connecting"}};
    }

    FT_LOG_INFO(log_category::transfer,
        "TCP transport connecting to " + remote.host + ":" + std::to_string(remote.port));

    impl_->set_state(transport_state::connecting);

    connection_result conn_result;
    conn_result.remote_address = remote.host;
    conn_result.remote_port = remote.port;

    // Apply configuration
    // Note: Additional socket options would be configured here
    (void)timeout;  // TODO: Use timeout in network_client configuration

    auto result = impl_->network_client->start_client(remote.host, remote.port);
    if (result.is_err()) {
        impl_->set_state(transport_state::error);
        impl_->increment_errors();
        conn_result.success = false;
        conn_result.error_message = result.error().message;
        FT_LOG_ERROR(log_category::transfer,
            "TCP transport connection failed: " + result.error().message);
        return unexpected{error{error_code::connection_failed,
            "Connection failed: " + result.error().message}};
    }

    // Setup receive callback for received data
    impl_->network_client->set_receive_callback(
        [this](const std::vector<std::uint8_t>& data) {
            std::vector<std::byte> byte_data(data.size());
            std::transform(data.begin(), data.end(), byte_data.begin(),
                [](std::uint8_t b) { return std::byte{b}; });

            {
                std::lock_guard lock(impl_->receive_mutex);
                impl_->receive_queue.push(byte_data);
            }
            impl_->receive_cv.notify_one();
            impl_->update_receive_stats(data.size());

            if (impl_->event_callback) {
                transport_event_data event_data;
                event_data.event = transport_event::data_received;
                event_data.data = byte_data;
                impl_->event_callback(event_data);
            }
        });

    impl_->remote_ep = remote;
    impl_->set_state(transport_state::connected);

    {
        std::lock_guard lock(impl_->stats_mutex);
        impl_->stats.connected_at = std::chrono::steady_clock::now();
    }

    conn_result.success = true;
    FT_LOG_INFO(log_category::transfer,
        "TCP transport connected to " + remote.host + ":" + std::to_string(remote.port));

    return conn_result;
}

auto tcp_transport::connect_async(const endpoint& remote)
    -> std::future<result<connection_result>> {
    return std::async(std::launch::async, [this, remote]() {
        return connect(remote);
    });
}

auto tcp_transport::disconnect() -> result<void> {
    if (impl_->current_state == transport_state::disconnected) {
        return {};  // Already disconnected
    }

    FT_LOG_INFO(log_category::transfer, "TCP transport disconnecting");
    impl_->set_state(transport_state::disconnecting);

    auto result = impl_->network_client->stop_client();
    if (result.is_err()) {
        impl_->increment_errors();
        FT_LOG_ERROR(log_category::transfer,
            "TCP transport disconnect failed: " + result.error().message);
        impl_->set_state(transport_state::error);
        return unexpected{error{error_code::internal_error,
            "Disconnect failed: " + result.error().message}};
    }

    impl_->local_ep = std::nullopt;
    impl_->remote_ep = std::nullopt;
    impl_->set_state(transport_state::disconnected);

    FT_LOG_INFO(log_category::transfer, "TCP transport disconnected");
    return {};
}

auto tcp_transport::is_connected() const -> bool {
    return impl_->current_state == transport_state::connected;
}

auto tcp_transport::state() const -> transport_state {
    return impl_->current_state;
}

auto tcp_transport::send(
    std::span<const std::byte> data,
    const send_options& options) -> result<std::size_t> {

    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized,
            "Transport is not connected"}};
    }

    if (data.empty()) {
        return 0;
    }

    // Convert to uint8_t vector
    std::vector<std::uint8_t> send_data(data.size());
    std::transform(data.begin(), data.end(), send_data.begin(),
        [](std::byte b) { return static_cast<std::uint8_t>(b); });

    auto result = impl_->network_client->send_packet(std::move(send_data));
    if (result.is_err()) {
        impl_->increment_errors();
        return unexpected{error{error_code::internal_error,
            "Send failed: " + result.error().message}};
    }

    impl_->update_send_stats(data.size());

    if (options.on_progress) {
        options.on_progress(data.size());
    }

    return data.size();
}

auto tcp_transport::receive(const receive_options& options)
    -> result<std::vector<std::byte>> {

    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized,
            "Transport is not connected"}};
    }

    std::unique_lock lock(impl_->receive_mutex);

    if (impl_->receive_queue.empty()) {
        // Wait for data
        auto wait_result = impl_->receive_cv.wait_for(lock, options.timeout,
            [this] { return !impl_->receive_queue.empty() ||
                            impl_->current_state != transport_state::connected; });

        if (!wait_result) {
            return unexpected{error{error_code::transfer_timeout,
                "Receive timeout"}};
        }

        if (impl_->current_state != transport_state::connected) {
            return unexpected{error{error_code::connection_lost,
                "Connection lost"}};
        }
    }

    if (impl_->receive_queue.empty()) {
        return unexpected{error{error_code::internal_error,
            "No data available"}};
    }

    auto data = std::move(impl_->receive_queue.front());
    impl_->receive_queue.pop();

    // Truncate if exceeds max_size
    if (data.size() > options.max_size) {
        data.resize(options.max_size);
    }

    return data;
}

auto tcp_transport::receive_into(
    std::span<std::byte> buffer,
    const receive_options& options) -> result<std::size_t> {

    auto data_result = receive(options);
    if (!data_result.has_value()) {
        return unexpected{data_result.error()};
    }

    auto& data = data_result.value();
    auto copy_size = std::min(buffer.size(), data.size());
    std::copy_n(data.begin(), copy_size, buffer.begin());

    return copy_size;
}

auto tcp_transport::send_async(
    std::span<const std::byte> data,
    const send_options& options) -> std::future<result<std::size_t>> {

    // Copy data for async operation
    std::vector<std::byte> data_copy(data.begin(), data.end());

    return std::async(std::launch::async, [this, data_copy = std::move(data_copy), options]() {
        return send(std::span<const std::byte>(data_copy), options);
    });
}

auto tcp_transport::receive_async(const receive_options& options)
    -> std::future<result<std::vector<std::byte>>> {
    return std::async(std::launch::async, [this, options]() {
        return receive(options);
    });
}

void tcp_transport::on_event(std::function<void(const transport_event_data&)> callback) {
    impl_->event_callback = std::move(callback);
}

void tcp_transport::on_state_changed(std::function<void(transport_state)> callback) {
    impl_->state_callback = std::move(callback);
}

auto tcp_transport::get_statistics() const -> transport_statistics {
    std::lock_guard lock(impl_->stats_mutex);
    return impl_->stats;
}

auto tcp_transport::local_endpoint() const -> std::optional<endpoint> {
    return impl_->local_ep;
}

auto tcp_transport::remote_endpoint() const -> std::optional<endpoint> {
    return impl_->remote_ep;
}

auto tcp_transport::config() const -> const transport_config& {
    return impl_->config;
}

// ============================================================================
// TCP transport factory implementation
// ============================================================================

auto tcp_transport_factory::create(const transport_config& config)
    -> std::unique_ptr<transport_interface> {
    if (config.type != transport_type::tcp) {
        return nullptr;
    }

    const auto* tcp_config = dynamic_cast<const tcp_transport_config*>(&config);
    if (tcp_config) {
        return tcp_transport::create(*tcp_config);
    }

    // Use base config with defaults
    tcp_transport_config default_config;
    default_config.connect_timeout = config.connect_timeout;
    default_config.read_timeout = config.read_timeout;
    default_config.write_timeout = config.write_timeout;
    default_config.send_buffer_size = config.send_buffer_size;
    default_config.receive_buffer_size = config.receive_buffer_size;
    default_config.keep_alive = config.keep_alive;
    default_config.keep_alive_interval = config.keep_alive_interval;
    default_config.max_retry_attempts = config.max_retry_attempts;
    default_config.retry_delay = config.retry_delay;

    return tcp_transport::create(default_config);
}

auto tcp_transport_factory::supported_types() const -> std::vector<std::string> {
    return {"tcp"};
}

}  // namespace kcenon::file_transfer
