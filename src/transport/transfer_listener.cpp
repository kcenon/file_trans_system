#define NETWORK_USE_EXPERIMENTAL
#include <kcenon/file_transfer/transport/transfer_listener.h>
#include <kcenon/network/core/messaging_server.h>
#include <kcenon/network/session/messaging_session.h>
#include <kcenon/network/experimental/quic_server.h>
#include <kcenon/network/session/quic_session.h>
#include <map>
#include <mutex>
namespace kcenon::file_transfer {
namespace {
template<class T> auto converted(T r) -> result<void> {
    if (r.is_err()) return unexpected{error{error_code::connection_failed, r.error().message}};
    return {};
}
class tcp_connection final : public accepted_transport {
    std::shared_ptr<network::session::messaging_session> session_;
    std::function<void()> cleanup_;
public:
    tcp_connection(std::shared_ptr<network::session::messaging_session> session, std::function<void()> cleanup)
        : session_(std::move(session)), cleanup_(std::move(cleanup)) {}
    auto send(std::vector<uint8_t> bytes) -> result<void> override { return converted(session_->send(std::move(bytes))); }
    void close() override { session_->close(); }
    void on_receive(std::function<void(const std::vector<uint8_t>&)> cb) override { session_->set_receive_callback(std::move(cb)); }
    void on_disconnect(std::function<void()> cb) override { session_->set_disconnection_callback([cb=std::move(cb), cleanup=cleanup_](const auto&) { cb(); cleanup(); }); }
};
class tcp_listener final : public transfer_listener {
    std::shared_ptr<network::core::messaging_server> server_ = std::make_shared<network::core::messaging_server>("fts-tcp");
    std::function<void(std::shared_ptr<accepted_transport>)> callback_;
    std::mutex mutex_;
    std::map<void*, std::shared_ptr<accepted_transport>> connections_;
public:
    ~tcp_listener() override { (void)stop(); }
    void on_accept(std::function<void(std::shared_ptr<accepted_transport>)> cb) override { callback_=std::move(cb); }
    auto type() const -> std::string override { return "tcp"; }
    auto start(const endpoint& endpoint) -> result<void> override {
        server_->set_connection_callback([this](auto session) {
            auto connection=std::make_shared<tcp_connection>(session, [this, id=session.get()] {
                std::shared_ptr<accepted_transport> removed;
                { std::lock_guard lock(mutex_); auto it=connections_.find(id); if(it!=connections_.end()) { removed=std::move(it->second); connections_.erase(it); } }
            });
            { std::lock_guard lock(mutex_); connections_[session.get()]=connection; }
            callback_(std::move(connection));
        });
        return converted(server_->start_server(endpoint.port));
    }
    auto stop() -> result<void> override {
        decltype(connections_) connections;
        { std::lock_guard lock(mutex_); connections.swap(connections_); }
        for(auto& [_, connection] : connections) connection->close();
        connections.clear();
        return converted(server_->stop_server());
    }
};
class quic_connection final : public accepted_transport {
    std::shared_ptr<network::session::quic_session> session_;
    std::function<void()> disconnected_;
public:
    explicit quic_connection(std::shared_ptr<network::session::quic_session> session) : session_(std::move(session)) {}
    auto send(std::vector<uint8_t> bytes) -> result<void> override { return converted(session_->send(std::move(bytes))); }
    void close() override { session_->close(); }
    void on_receive(std::function<void(const std::vector<uint8_t>&)> cb) override { session_->set_receive_callback(std::move(cb)); }
    void on_disconnect(std::function<void()> cb) override { disconnected_=std::move(cb); }
    void disconnected() { if (disconnected_) disconnected_(); }
};
class quic_listener final : public transfer_listener {
    quic_transport_config config_;
    std::shared_ptr<network::core::messaging_quic_server> server_ = std::make_shared<network::core::messaging_quic_server>("fts-quic");
    std::function<void(std::shared_ptr<accepted_transport>)> callback_;
    std::mutex mutex_;
    std::map<void*, std::shared_ptr<quic_connection>> connections_;
public:
    explicit quic_listener(quic_transport_config config) : config_(std::move(config)) {}
    ~quic_listener() override { (void)stop(); }
    void on_accept(std::function<void(std::shared_ptr<accepted_transport>)> cb) override { callback_=std::move(cb); }
    auto type() const -> std::string override { return "quic"; }
    auto start(const endpoint& endpoint) -> result<void> override {
#ifndef FILE_TRANS_ENABLE_QUIC
        (void)endpoint; return unexpected{error{error_code::invalid_configuration, "QUIC transport is disabled"}};
#else
        network::core::quic_server_config config;
        config.cert_file=config_.cert_path.value_or(""); config.key_file=config_.key_path.value_or(""); config.ca_cert_file=config_.ca_path;
        config.alpn_protocols={config_.alpn}; config.max_idle_timeout_ms=config_.max_idle_timeout.count()*1000;
        config.initial_max_data=config_.initial_max_data; config.initial_max_stream_data=config_.initial_max_stream_data;
        config.initial_max_streams_bidi=config_.max_bidi_streams; config.initial_max_streams_uni=config_.max_uni_streams;
        server_->set_connection_callback(network::core::messaging_quic_server::connection_callback_t{[this](auto session) {
            auto connection=std::make_shared<quic_connection>(session);
            { std::lock_guard lock(mutex_); connections_[session.get()]=connection; }
            callback_(std::move(connection));
        }});
        server_->set_disconnection_callback(network::core::messaging_quic_server::disconnection_callback_t{[this](auto session) {
            std::shared_ptr<quic_connection> connection;
            { std::lock_guard lock(mutex_); auto it=connections_.find(session.get()); if(it!=connections_.end()) { connection=it->second; connections_.erase(it); } }
            if(connection) connection->disconnected();
        }});
        return converted(server_->start_server(endpoint.port, config));
#endif
    }
    auto stop() -> result<void> override {
        decltype(connections_) connections;
        { std::lock_guard lock(mutex_); connections.swap(connections_); }
        for(auto& [_, connection] : connections) connection->close();
        connections.clear();
        return converted(server_->stop_server());
    }
};
} // namespace
auto transfer_listener::create(const transfer_transport_config& config) -> std::unique_ptr<transfer_listener> {
    if(auto quic=std::get_if<quic_transport_config>(&config)) return std::make_unique<quic_listener>(*quic);
    return std::make_unique<tcp_listener>();
}
} // namespace kcenon::file_transfer
