/**
 * @file quic_transport.cpp
 * @brief QUIC transport implementation
 */

#include "kcenon/file_transfer/transport/quic_transport.h"
#include "kcenon/file_transfer/transport/connection_migration.h"
#include "kcenon/file_transfer/transport/session_resumption.h"
#include "kcenon/file_transfer/core/logging.h"

#include <condition_variable>
#include <queue>
#include <thread>

#include <network_system/core/messaging_quic_client.h>

namespace kcenon::file_transfer {

// Alias for network_system namespace
namespace ns = kcenon::network::core;

struct quic_transport::impl {
    quic_transport_config config;
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

    // Handshake state
    std::atomic<bool> handshake_complete{false};
    std::optional<std::string> negotiated_alpn;

    // 0-RTT session resumption
    std::shared_ptr<session_resumption_manager> session_manager;
    std::atomic<bool> used_0rtt_{false};
    std::atomic<bool> is_0rtt_accepted_{false};

    // Connection migration
    std::shared_ptr<connection_migration_manager> migration_manager_;
    std::function<void(const migration_event_data&)> migration_callback;
    std::mutex migration_mutex;

    std::shared_ptr<ns::messaging_quic_client> quic_client;

    explicit impl(quic_transport_config cfg) : config(std::move(cfg)) {
        quic_client = std::make_shared<ns::messaging_quic_client>(
            "quic_transport");
    }

    void set_state(transport_state new_state) {
        auto old_state = current_state.exchange(new_state);
        if (old_state != new_state) {
            FT_LOG_DEBUG(log_category::transfer,
                "QUIC transport state changed: " +
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

    auto build_quic_config(const std::string& host, uint16_t port) const
        -> ns::quic_client_config {
        ns::quic_client_config quic_cfg;

        if (config.cert_path.has_value()) {
            quic_cfg.client_cert_file = config.cert_path.value();
        }
        if (config.key_path.has_value()) {
            quic_cfg.client_key_file = config.key_path.value();
        }
        if (config.ca_path.has_value()) {
            quic_cfg.ca_cert_file = config.ca_path.value();
        }

        quic_cfg.verify_server = !config.skip_cert_verify;
        quic_cfg.max_idle_timeout_ms =
            static_cast<uint64_t>(config.max_idle_timeout.count() * 1000);
        quic_cfg.initial_max_data = config.initial_max_data;
        quic_cfg.initial_max_stream_data = config.initial_max_stream_data;
        quic_cfg.initial_max_streams_bidi = config.max_bidi_streams;
        quic_cfg.initial_max_streams_uni = config.max_uni_streams;
        quic_cfg.enable_early_data = config.enable_0rtt;

        if (!config.alpn.empty()) {
            quic_cfg.alpn_protocols = {config.alpn};
        }

        // Load session ticket for 0-RTT if available
        if (config.enable_0rtt && session_manager) {
            auto ticket = session_manager->get_ticket_for_server(host, port);
            if (ticket.has_value()) {
                quic_cfg.session_ticket = std::move(ticket.value());
                FT_LOG_DEBUG(log_category::transfer,
                    "Using session ticket for 0-RTT connection to " +
                    host + ":" + std::to_string(port));
            }
        }

        return quic_cfg;
    }
};

quic_transport::quic_transport(quic_transport_config config)
    : impl_(std::make_unique<impl>(std::move(config))) {
    get_logger().initialize();
    FT_LOG_DEBUG(log_category::transfer, "QUIC transport created");
}

quic_transport::~quic_transport() {
    if (impl_ && is_connected()) {
        (void)disconnect();
    }
}

quic_transport::quic_transport(quic_transport&&) noexcept = default;
auto quic_transport::operator=(quic_transport&&) noexcept -> quic_transport& = default;

auto quic_transport::create(const quic_transport_config& config)
    -> std::unique_ptr<quic_transport> {
    return std::unique_ptr<quic_transport>(new quic_transport(config));
}

auto quic_transport::type() const -> std::string_view {
    return "quic";
}

auto quic_transport::connect(const endpoint& remote) -> result<connection_result> {
    return connect(remote, impl_->config.connect_timeout);
}

auto quic_transport::connect(
    const endpoint& remote,
    std::chrono::milliseconds timeout) -> result<connection_result> {

    if (impl_->current_state == transport_state::connected ||
        impl_->current_state == transport_state::connecting) {
        return unexpected{error{error_code::already_initialized,
            "Transport is already connected or connecting"}};
    }

    FT_LOG_INFO(log_category::transfer,
        "QUIC transport connecting to " + remote.host + ":" + std::to_string(remote.port));

    impl_->set_state(transport_state::connecting);

    connection_result conn_result;
    conn_result.remote_address = remote.host;
    conn_result.remote_port = remote.port;

    // Reset 0-RTT state for new connection
    impl_->used_0rtt_ = false;
    impl_->is_0rtt_accepted_ = false;

    // Build QUIC configuration (includes session ticket if available)
    auto quic_cfg = impl_->build_quic_config(remote.host, remote.port);

    // Track if we're attempting 0-RTT
    bool attempting_0rtt = quic_cfg.session_ticket.has_value();
    if (attempting_0rtt) {
        impl_->used_0rtt_ = true;
    }

    // Setup callbacks before connecting
    impl_->quic_client->set_receive_callback(
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

    impl_->quic_client->set_connected_callback([this]() {
        impl_->handshake_complete = true;
        FT_LOG_DEBUG(log_category::transfer, "QUIC handshake complete");
    });

    impl_->quic_client->set_disconnected_callback([this]() {
        impl_->set_state(transport_state::disconnected);
    });

    impl_->quic_client->set_error_callback([this](std::error_code ec) {
        FT_LOG_ERROR(log_category::transfer,
            "QUIC transport error: " + ec.message());
        impl_->increment_errors();

        if (impl_->event_callback) {
            transport_event_data event_data;
            event_data.event = transport_event::error;
            event_data.error_message = ec.message();
            impl_->event_callback(event_data);
        }
    });

    // Connect with QUIC configuration
    auto result = impl_->quic_client->start_client(
        remote.host, remote.port, quic_cfg);

    if (result.is_err()) {
        impl_->set_state(transport_state::error);
        impl_->increment_errors();
        conn_result.success = false;
        conn_result.error_message = result.error().message;
        FT_LOG_ERROR(log_category::transfer,
            "QUIC transport connection failed: " + result.error().message);
        return unexpected{error{error_code::connection_failed,
            "Connection failed: " + result.error().message}};
    }

    // Wait for connection with timeout
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!impl_->quic_client->is_connected()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            impl_->set_state(transport_state::error);
            impl_->increment_errors();
            (void)impl_->quic_client->stop_client();
            conn_result.success = false;
            conn_result.error_message = "Connection timeout";
            FT_LOG_ERROR(log_category::transfer, "QUIC transport connection timeout");
            return unexpected{error{error_code::connection_failed,
                "Connection timeout"}};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    impl_->remote_ep = remote;
    impl_->set_state(transport_state::connected);

    // Get negotiated ALPN
    impl_->negotiated_alpn = impl_->quic_client->alpn_protocol();

    {
        std::lock_guard lock(impl_->stats_mutex);
        impl_->stats.connected_at = std::chrono::steady_clock::now();
    }

    // Handle 0-RTT acceptance/rejection and session ticket management
    if (attempting_0rtt) {
        // Check if 0-RTT was accepted (based on handshake completion timing)
        // In QUIC, if 0-RTT is accepted, early data is processed immediately
        impl_->is_0rtt_accepted_ = impl_->quic_client->is_handshake_complete();

        if (impl_->is_0rtt_accepted_) {
            FT_LOG_INFO(log_category::transfer,
                "0-RTT accepted for " + remote.host + ":" + std::to_string(remote.port));
            if (impl_->session_manager) {
                impl_->session_manager->on_0rtt_accepted(remote.host, remote.port);
            }
        } else {
            FT_LOG_INFO(log_category::transfer,
                "0-RTT rejected for " + remote.host + ":" + std::to_string(remote.port));
            if (impl_->session_manager) {
                impl_->session_manager->on_0rtt_rejected(remote.host, remote.port);
            }
        }
    }

    conn_result.success = true;
    FT_LOG_INFO(log_category::transfer,
        "QUIC transport connected to " + remote.host + ":" + std::to_string(remote.port));

    return conn_result;
}

auto quic_transport::connect_async(const endpoint& remote)
    -> std::future<result<connection_result>> {
    return std::async(std::launch::async, [this, remote]() {
        return connect(remote);
    });
}

auto quic_transport::disconnect() -> result<void> {
    if (impl_->current_state == transport_state::disconnected) {
        return {};  // Already disconnected
    }

    FT_LOG_INFO(log_category::transfer, "QUIC transport disconnecting");
    impl_->set_state(transport_state::disconnecting);

    auto result = impl_->quic_client->stop_client();
    if (result.is_err()) {
        impl_->increment_errors();
        FT_LOG_ERROR(log_category::transfer,
            "QUIC transport disconnect failed: " + result.error().message);
        impl_->set_state(transport_state::error);
        return unexpected{error{error_code::internal_error,
            "Disconnect failed: " + result.error().message}};
    }

    impl_->quic_client->wait_for_stop();

    impl_->local_ep = std::nullopt;
    impl_->remote_ep = std::nullopt;
    impl_->handshake_complete = false;
    impl_->negotiated_alpn = std::nullopt;
    impl_->used_0rtt_ = false;
    impl_->is_0rtt_accepted_ = false;
    impl_->set_state(transport_state::disconnected);

    FT_LOG_INFO(log_category::transfer, "QUIC transport disconnected");
    return {};
}

auto quic_transport::is_connected() const -> bool {
    return impl_->current_state == transport_state::connected &&
           impl_->quic_client->is_connected();
}

auto quic_transport::state() const -> transport_state {
    return impl_->current_state;
}

auto quic_transport::send(
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

    auto result = impl_->quic_client->send_packet(std::move(send_data));
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

auto quic_transport::receive(const receive_options& options)
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

auto quic_transport::receive_into(
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

auto quic_transport::send_async(
    std::span<const std::byte> data,
    const send_options& options) -> std::future<result<std::size_t>> {

    // Copy data for async operation
    std::vector<std::byte> data_copy(data.begin(), data.end());

    return std::async(std::launch::async, [this, data_copy = std::move(data_copy), options]() {
        return send(std::span<const std::byte>(data_copy), options);
    });
}

auto quic_transport::receive_async(const receive_options& options)
    -> std::future<result<std::vector<std::byte>>> {
    return std::async(std::launch::async, [this, options]() {
        return receive(options);
    });
}

void quic_transport::on_event(std::function<void(const transport_event_data&)> callback) {
    impl_->event_callback = std::move(callback);
}

void quic_transport::on_state_changed(std::function<void(transport_state)> callback) {
    impl_->state_callback = std::move(callback);
}

auto quic_transport::get_statistics() const -> transport_statistics {
    std::lock_guard lock(impl_->stats_mutex);

    // Update RTT from QUIC connection stats if available
    if (impl_->quic_client->is_connected()) {
        auto quic_stats = impl_->quic_client->stats();
        impl_->stats.rtt = std::chrono::duration_cast<std::chrono::milliseconds>(
            quic_stats.smoothed_rtt);
        impl_->stats.bytes_sent = quic_stats.bytes_sent;
        impl_->stats.bytes_received = quic_stats.bytes_received;
        impl_->stats.packets_sent = quic_stats.packets_sent;
        impl_->stats.packets_received = quic_stats.packets_received;
    }

    return impl_->stats;
}

auto quic_transport::local_endpoint() const -> std::optional<endpoint> {
    return impl_->local_ep;
}

auto quic_transport::remote_endpoint() const -> std::optional<endpoint> {
    return impl_->remote_ep;
}

auto quic_transport::config() const -> const transport_config& {
    return impl_->config;
}

// ============================================================================
// QUIC-specific features
// ============================================================================

auto quic_transport::create_stream() -> result<uint64_t> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized,
            "Transport is not connected"}};
    }

    auto stream_result = impl_->quic_client->create_stream();
    if (stream_result.is_err()) {
        return unexpected{error{error_code::internal_error,
            "Failed to create stream: " + stream_result.error().message}};
    }

    return stream_result.value();
}

auto quic_transport::create_unidirectional_stream() -> result<uint64_t> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized,
            "Transport is not connected"}};
    }

    auto stream_result = impl_->quic_client->create_unidirectional_stream();
    if (stream_result.is_err()) {
        return unexpected{error{error_code::internal_error,
            "Failed to create unidirectional stream: " + stream_result.error().message}};
    }

    return stream_result.value();
}

auto quic_transport::send_on_stream(
    uint64_t stream_id,
    std::span<const std::byte> data,
    bool fin) -> result<std::size_t> {

    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized,
            "Transport is not connected"}};
    }

    if (data.empty() && !fin) {
        return 0;
    }

    // Convert to uint8_t vector
    std::vector<std::uint8_t> send_data(data.size());
    std::transform(data.begin(), data.end(), send_data.begin(),
        [](std::byte b) { return static_cast<std::uint8_t>(b); });

    auto result = impl_->quic_client->send_on_stream(
        stream_id, std::move(send_data), fin);

    if (result.is_err()) {
        impl_->increment_errors();
        return unexpected{error{error_code::internal_error,
            "Send on stream failed: " + result.error().message}};
    }

    impl_->update_send_stats(data.size());
    return data.size();
}

auto quic_transport::close_stream(uint64_t stream_id) -> result<void> {
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized,
            "Transport is not connected"}};
    }

    auto result = impl_->quic_client->close_stream(stream_id);
    if (result.is_err()) {
        return unexpected{error{error_code::internal_error,
            "Failed to close stream: " + result.error().message}};
    }

    return {};
}

auto quic_transport::is_handshake_complete() const -> bool {
    return impl_->handshake_complete &&
           impl_->quic_client->is_handshake_complete();
}

auto quic_transport::alpn_protocol() const -> std::optional<std::string> {
    return impl_->negotiated_alpn;
}

// ============================================================================
// 0-RTT Session Resumption Implementation
// ============================================================================

void quic_transport::set_session_manager(
    std::shared_ptr<session_resumption_manager> manager) {
    impl_->session_manager = std::move(manager);
}

auto quic_transport::session_manager() const
    -> std::shared_ptr<session_resumption_manager> {
    return impl_->session_manager;
}

auto quic_transport::is_0rtt_available() const -> bool {
    if (!impl_->config.enable_0rtt || !impl_->session_manager) {
        return false;
    }

    auto remote = impl_->remote_ep;
    if (!remote.has_value()) {
        return false;
    }

    return impl_->session_manager->can_use_0rtt(remote->host, remote->port);
}

auto quic_transport::used_0rtt() const -> bool {
    return impl_->used_0rtt_;
}

auto quic_transport::is_0rtt_accepted() const -> bool {
    return impl_->is_0rtt_accepted_;
}

auto quic_transport::connect_with_0rtt(
    const endpoint& remote,
    std::span<const std::byte> early_data) -> result<connection_result> {

    if (impl_->current_state == transport_state::connected ||
        impl_->current_state == transport_state::connecting) {
        return unexpected{error{error_code::already_initialized,
            "Transport is already connected or connecting"}};
    }

    // Check if 0-RTT is possible
    bool can_0rtt = impl_->config.enable_0rtt && impl_->session_manager &&
                    impl_->session_manager->can_use_0rtt(remote.host, remote.port);

    if (!can_0rtt) {
        FT_LOG_DEBUG(log_category::transfer,
            "0-RTT not available for " + remote.host + ":" + std::to_string(remote.port) +
            ", falling back to regular connection");

        // Fall back to regular connection, send early_data after handshake
        auto conn_result = connect(remote);
        if (conn_result.has_value() && !early_data.empty()) {
            // Send the data after connection is established
            auto send_result = send(early_data);
            if (!send_result.has_value()) {
                FT_LOG_WARN(log_category::transfer,
                    "Failed to send data after fallback connection: " +
                    send_result.error().message);
            }
        }
        return conn_result;
    }

    FT_LOG_INFO(log_category::transfer,
        "Attempting 0-RTT connection to " + remote.host + ":" + std::to_string(remote.port));

    impl_->set_state(transport_state::connecting);
    impl_->used_0rtt_ = true;
    impl_->is_0rtt_accepted_ = false;

    connection_result conn_result;
    conn_result.remote_address = remote.host;
    conn_result.remote_port = remote.port;

    // Build QUIC configuration with session ticket
    auto quic_cfg = impl_->build_quic_config(remote.host, remote.port);

    // Setup callbacks
    impl_->quic_client->set_receive_callback(
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

    impl_->quic_client->set_connected_callback([this]() {
        impl_->handshake_complete = true;
        FT_LOG_DEBUG(log_category::transfer, "QUIC handshake complete (0-RTT)");
    });

    impl_->quic_client->set_disconnected_callback([this]() {
        impl_->set_state(transport_state::disconnected);
    });

    impl_->quic_client->set_error_callback([this](std::error_code ec) {
        FT_LOG_ERROR(log_category::transfer,
            "QUIC transport error (0-RTT): " + ec.message());
        impl_->increment_errors();

        if (impl_->event_callback) {
            transport_event_data event_data;
            event_data.event = transport_event::error;
            event_data.error_message = ec.message();
            impl_->event_callback(event_data);
        }
    });

    // Start connection with QUIC configuration
    auto result = impl_->quic_client->start_client(
        remote.host, remote.port, quic_cfg);

    if (result.is_err()) {
        impl_->set_state(transport_state::error);
        impl_->increment_errors();
        conn_result.success = false;
        conn_result.error_message = result.error().message;
        FT_LOG_ERROR(log_category::transfer,
            "QUIC 0-RTT connection failed: " + result.error().message);
        return unexpected{error{error_code::connection_failed,
            "Connection failed: " + result.error().message}};
    }

    // Send early data immediately (0-RTT)
    if (!early_data.empty()) {
        std::vector<std::uint8_t> early_bytes(early_data.size());
        std::transform(early_data.begin(), early_data.end(), early_bytes.begin(),
            [](std::byte b) { return static_cast<std::uint8_t>(b); });

        auto send_result = impl_->quic_client->send_packet(std::move(early_bytes));
        if (send_result.is_err()) {
            FT_LOG_WARN(log_category::transfer,
                "Failed to send 0-RTT early data: " + send_result.error().message);
            // Don't fail the connection, early data may be retried
        } else {
            FT_LOG_DEBUG(log_category::transfer,
                "Sent " + std::to_string(early_data.size()) + " bytes as 0-RTT early data");
            impl_->update_send_stats(early_data.size());
        }
    }

    // Wait for connection with timeout
    auto timeout = impl_->config.connect_timeout;
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!impl_->quic_client->is_connected()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            impl_->set_state(transport_state::error);
            impl_->increment_errors();
            (void)impl_->quic_client->stop_client();
            conn_result.success = false;
            conn_result.error_message = "Connection timeout";
            FT_LOG_ERROR(log_category::transfer, "QUIC 0-RTT connection timeout");
            return unexpected{error{error_code::connection_failed,
                "Connection timeout"}};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    impl_->remote_ep = remote;
    impl_->set_state(transport_state::connected);
    impl_->negotiated_alpn = impl_->quic_client->alpn_protocol();

    {
        std::lock_guard lock(impl_->stats_mutex);
        impl_->stats.connected_at = std::chrono::steady_clock::now();
    }

    // Check 0-RTT acceptance
    impl_->is_0rtt_accepted_ = impl_->quic_client->is_handshake_complete();
    if (impl_->is_0rtt_accepted_) {
        FT_LOG_INFO(log_category::transfer,
            "0-RTT accepted for " + remote.host + ":" + std::to_string(remote.port));
        impl_->session_manager->on_0rtt_accepted(remote.host, remote.port);
    } else {
        FT_LOG_INFO(log_category::transfer,
            "0-RTT rejected for " + remote.host + ":" + std::to_string(remote.port) +
            ", data will be resent");
        impl_->session_manager->on_0rtt_rejected(remote.host, remote.port);

        // Resend early data if 0-RTT was rejected
        if (!early_data.empty()) {
            auto resend_result = send(early_data);
            if (!resend_result.has_value()) {
                FT_LOG_WARN(log_category::transfer,
                    "Failed to resend data after 0-RTT rejection: " +
                    resend_result.error().message);
            }
        }
    }

    conn_result.success = true;
    FT_LOG_INFO(log_category::transfer,
        "QUIC 0-RTT connected to " + remote.host + ":" + std::to_string(remote.port));

    return conn_result;
}

// ============================================================================
// QUIC transport factory implementation
// ============================================================================

auto quic_transport_factory::create(const transport_config& config)
    -> std::unique_ptr<transport_interface> {
    if (config.type != transport_type::quic) {
        return nullptr;
    }

    const auto* quic_config = dynamic_cast<const quic_transport_config*>(&config);
    if (quic_config) {
        return quic_transport::create(*quic_config);
    }

    // Use base config with defaults
    quic_transport_config default_config;
    default_config.connect_timeout = config.connect_timeout;
    default_config.read_timeout = config.read_timeout;
    default_config.write_timeout = config.write_timeout;
    default_config.send_buffer_size = config.send_buffer_size;
    default_config.receive_buffer_size = config.receive_buffer_size;
    default_config.keep_alive = config.keep_alive;
    default_config.keep_alive_interval = config.keep_alive_interval;
    default_config.max_retry_attempts = config.max_retry_attempts;
    default_config.retry_delay = config.retry_delay;

    return quic_transport::create(default_config);
}

auto quic_transport_factory::supported_types() const -> std::vector<std::string> {
    return {"quic"};
}

// ============================================================================
// Connection Migration Implementation
// ============================================================================

void quic_transport::set_migration_manager(
    std::shared_ptr<connection_migration_manager> manager) {
    std::lock_guard lock(impl_->migration_mutex);
    impl_->migration_manager_ = std::move(manager);

    // Set up internal migration event forwarding
    if (impl_->migration_manager_) {
        impl_->migration_manager_->on_migration_event(
            [this](const migration_event_data& event) {
                std::lock_guard lock(impl_->migration_mutex);
                if (impl_->migration_callback) {
                    impl_->migration_callback(event);
                }
            });
    }
}

auto quic_transport::migration_manager() const
    -> std::shared_ptr<connection_migration_manager> {
    std::lock_guard lock(impl_->migration_mutex);
    return impl_->migration_manager_;
}

auto quic_transport::is_migration_available() const -> bool {
    if (!is_connected()) {
        return false;
    }

    std::lock_guard lock(impl_->migration_mutex);
    if (!impl_->migration_manager_) {
        return false;
    }

    return impl_->migration_manager_->is_migration_available();
}

auto quic_transport::current_network_path() const -> std::optional<network_path> {
    std::lock_guard lock(impl_->migration_mutex);
    if (!impl_->migration_manager_) {
        // Build path from current connection info
        if (impl_->remote_ep.has_value()) {
            network_path path;
            path.remote_address = impl_->remote_ep->host;
            path.remote_port = impl_->remote_ep->port;
            if (impl_->local_ep.has_value()) {
                path.local_address = impl_->local_ep->host;
                path.local_port = impl_->local_ep->port;
            }
            path.validated = true;
            path.created_at = std::chrono::steady_clock::now();
            return path;
        }
        return std::nullopt;
    }

    return impl_->migration_manager_->current_path();
}

auto quic_transport::migrate_to(const network_path& new_path)
    -> result<migration_result> {

    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized,
            "Transport is not connected"}};
    }

    std::lock_guard lock(impl_->migration_mutex);
    if (!impl_->migration_manager_) {
        return unexpected{error{error_code::not_initialized,
            "Migration manager not configured"}};
    }

    FT_LOG_INFO(log_category::transfer,
        "Initiating migration to: " + new_path.to_string());

    auto result = impl_->migration_manager_->migrate_to_path(new_path);
    if (!result.has_value()) {
        FT_LOG_ERROR(log_category::transfer,
            "Migration failed: " + result.error().message);
        return result;
    }

    if (result.value().success) {
        // Update local endpoint if migration succeeded
        if (!new_path.local_address.empty()) {
            endpoint new_local;
            new_local.host = new_path.local_address;
            new_local.port = new_path.local_port;
            impl_->local_ep = new_local;
        }

        FT_LOG_INFO(log_category::transfer,
            "Migration completed successfully in " +
            std::to_string(result.value().duration.count()) + "ms");
    }

    return result;
}

void quic_transport::on_migration_event(
    std::function<void(const migration_event_data&)> callback) {
    std::lock_guard lock(impl_->migration_mutex);
    impl_->migration_callback = std::move(callback);
}

auto quic_transport::migration_state() const -> enum migration_state {
    std::lock_guard lock(impl_->migration_mutex);
    if (!impl_->migration_manager_) {
        return migration_state::idle;
    }
    return impl_->migration_manager_->state();
}

auto quic_transport::get_migration_statistics() const -> migration_statistics {
    std::lock_guard lock(impl_->migration_mutex);
    if (!impl_->migration_manager_) {
        return migration_statistics{};
    }
    return impl_->migration_manager_->get_statistics();
}

auto quic_transport::start_network_monitoring() -> result<void> {
    std::lock_guard lock(impl_->migration_mutex);
    if (!impl_->migration_manager_) {
        // Create a default migration manager if not set
        impl_->migration_manager_ = connection_migration_manager::create();

        // Set up event forwarding
        impl_->migration_manager_->on_migration_event(
            [this](const migration_event_data& event) {
                std::lock_guard lock(impl_->migration_mutex);
                if (impl_->migration_callback) {
                    impl_->migration_callback(event);
                }
            });
    }

    // Set current path based on connection info
    if (is_connected() && impl_->remote_ep.has_value()) {
        network_path current_path;
        current_path.remote_address = impl_->remote_ep->host;
        current_path.remote_port = impl_->remote_ep->port;
        if (impl_->local_ep.has_value()) {
            current_path.local_address = impl_->local_ep->host;
            current_path.local_port = impl_->local_ep->port;
        }
        current_path.validated = true;
        current_path.created_at = std::chrono::steady_clock::now();
        impl_->migration_manager_->set_current_path(current_path);
    }

    return impl_->migration_manager_->start_monitoring();
}

void quic_transport::stop_network_monitoring() {
    std::lock_guard lock(impl_->migration_mutex);
    if (impl_->migration_manager_) {
        impl_->migration_manager_->stop_monitoring();
    }
}

}  // namespace kcenon::file_transfer
