#pragma once
#include "transport_interface.h"
#include <functional>
#include <memory>
namespace kcenon::file_transfer {
// Server half of the byte transport contract. A connection exposes one ordered
// byte stream; the shared FTS1 stream decoder supplies protocol message framing.
class accepted_transport {
public:
    virtual ~accepted_transport() = default;
    virtual auto send(std::vector<uint8_t> bytes) -> result<void> = 0;
    virtual void close() = 0;
    virtual void on_receive(std::function<void(const std::vector<uint8_t>&)> callback) = 0;
    virtual void on_disconnect(std::function<void()> callback) = 0;
};
class transfer_listener {
public:
    virtual ~transfer_listener() = default;
    virtual auto start(const endpoint&) -> result<void> = 0;
    virtual auto stop() -> result<void> = 0;
    virtual auto type() const -> std::string = 0;
    virtual void on_accept(std::function<void(std::shared_ptr<accepted_transport>)>) = 0;
    static auto create(const transfer_transport_config&) -> std::unique_ptr<transfer_listener>;
};
} // namespace kcenon::file_transfer
