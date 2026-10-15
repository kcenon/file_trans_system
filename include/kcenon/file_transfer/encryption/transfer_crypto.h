#pragma once
#include <kcenon/file_transfer/core/protocol_codec.h>
#include <memory>

namespace kcenon::file_transfer {
// Per-transfer authenticated encryption. Shared secrets never enter the wire.
// The request salt establishes client key confirmation; a fresh server salt
// then creates independent data/MAC keys for the accepted transfer.
class transfer_crypto {
public:
    using salt = std::array<uint8_t, 32>;
    static auto validate_secret(std::span<const std::byte> key, std::string_view password) -> result<void>;
    static auto random_salt() -> result<salt>;
    static auto create(std::span<const std::byte> key, std::string_view password,
        const transfer_id& id, const salt& client_salt, bool upload,
        const salt* server_salt = nullptr) -> result<std::shared_ptr<transfer_crypto>>;
    ~transfer_crypto();
    auto seal(chunk& data) -> result<void>;
    auto open(chunk& data) -> result<void>;
    template<class Message> auto sign(message_type type, Message& message) const -> result<void> {
        message.auth_tag.fill(0);
        auto bytes = wire::encode(type, message);
        if (!bytes) return unexpected{bytes.error()};
        auto tag = authenticate(bytes.value());
        if (!tag) return unexpected{tag.error()};
        message.auth_tag = tag.value(); return {};
    }
    template<class Message> auto verify(message_type type, const Message& message) const -> result<void> {
        auto copy = message; auto signed_copy = sign(type, copy);
        if (!signed_copy) return signed_copy;
        if (!equal_tags(copy.auth_tag, message.auth_tag))
            return unexpected{error{error_code::file_hash_mismatch, "Transfer message authentication failed"}};
        return {};
    }
private:
    struct impl;
    std::unique_ptr<impl> impl_;
    transfer_crypto();
    auto authenticate(std::span<const uint8_t> bytes) const -> result<salt>;
    static auto equal_tags(const salt& a, const salt& b) -> bool;
};
} // namespace kcenon::file_transfer
