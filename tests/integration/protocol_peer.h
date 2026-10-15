#pragma once
#include <gtest/gtest.h>
#include <asio.hpp>
#include <kcenon/network/core/messaging_client.h>
#include <kcenon/file_transfer/core/protocol_codec.h>
#include <kcenon/file_transfer/core/checksum.h>
#include <condition_variable>
#include <deque>

namespace kcenon::file_transfer::test {
using namespace std::chrono_literals;

inline auto available_port() -> uint16_t {
    asio::io_context context;
    asio::ip::tcp::acceptor acceptor(context, {asio::ip::tcp::v4(), 0});
    return acceptor.local_endpoint().port();
}

class protocol_peer {
    std::shared_ptr<network::core::messaging_client> client_ =
        std::make_shared<network::core::messaging_client>("fts_protocol_test");
    std::mutex mutex_;
    std::condition_variable cv_;
    bool connected_ = false;
    std::deque<wire::frame> frames_;
    wire::stream_decoder decoder_;
public:
    explicit protocol_peer(uint16_t port) {
        client_->set_connected_callback([this] {
            std::lock_guard lock(mutex_); connected_ = true; cv_.notify_all();
        });
        client_->set_receive_callback([this](const auto& bytes) {
            std::lock_guard lock(mutex_);
            auto frames = decoder_.feed(bytes);
            if (!frames) return;
            for (const auto& frame : frames.value()) {
                auto decoded = wire::decode_frame(frame);
                if (decoded) frames_.push_back(std::move(decoded.value()));
            }
            cv_.notify_all();
        });
        auto started = client_->start_client("127.0.0.1", port);
        if (started.is_err()) throw std::runtime_error(started.error().message);
        std::unique_lock lock(mutex_);
        if (!cv_.wait_for(lock, 5s, [&] { return connected_; })) throw std::runtime_error("Peer connect timeout");
    }
    ~protocol_peer() { disconnect(); }
    void disconnect() { (void)client_->stop_client(); }
    void raw(std::vector<uint8_t> bytes) {
        auto sent = client_->send_packet(std::move(bytes));
        if (sent.is_err()) throw std::runtime_error(sent.error().message);
    }
    template<class T> void send(message_type type, const T& message) {
        auto bytes = wire::encode(type, message);
        if (!bytes) throw std::runtime_error(bytes.error().message);
        raw(std::move(bytes.value()));
    }
    auto receive() -> wire::frame {
        std::unique_lock lock(mutex_);
        if (!cv_.wait_for(lock, 5s, [&] { return !frames_.empty(); })) throw std::runtime_error("Peer response timeout");
        auto frame = std::move(frames_.front()); frames_.pop_front(); return frame;
    }
    template<class T> auto receive(message_type type) -> T {
        auto frame = receive();
        if (frame.type != type) {
            auto err = wire::decode<msg_error>(frame.payload);
            throw std::runtime_error("Expected " + std::string(to_string(type)) + ", got " +
                std::string(to_string(frame.type)) + (err ? ": " + err.value().message : ""));
        }
        auto decoded = wire::decode<T>(frame.payload);
        if (!decoded) throw std::runtime_error(decoded.error().message);
        return std::move(decoded.value());
    }
};

inline auto sha_bytes(std::span<const std::byte> bytes) -> std::array<uint8_t, 32> {
    auto hash = checksum::sha256(bytes);
    std::array<uint8_t, 32> out{};
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<uint8_t>(std::stoul(hash.substr(i * 2, 2), nullptr, 16));
    return out;
}
inline auto upload_request_for(std::string name, const std::vector<std::byte>& data) -> msg_upload_request {
    msg_upload_request request{};
    request.transfer_id = transfer_id::generate().bytes;
    request.filename = std::move(name); request.file_size = data.size(); request.sha256_hash = sha_bytes(data);
    return request;
}
} // namespace kcenon::file_transfer::test
