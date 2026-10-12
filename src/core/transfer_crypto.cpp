#include <kcenon/file_transfer/encryption/transfer_crypto.h>
#include <atomic>
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
#include <kcenon/file_transfer/encryption/aes_gcm_engine.h>
#include <kcenon/file_transfer/encryption/key_manager.h>
#include <openssl/kdf.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#endif

namespace kcenon::file_transfer {
namespace {
auto unavailable() -> error { return {error_code::invalid_configuration, "Transfer encryption requires crypto support and a configured key"}; }
auto chunk_aad(const chunk& data) -> std::vector<std::byte> {
    auto h = data.header; wire::writer writer;
    writer(h.id.bytes, h.chunk_index, h.chunk_offset, h.original_size, h.compressed_size, h.checksum, h.flags);
    std::vector<std::byte> bytes; for (auto b : writer.data) bytes.push_back(std::byte{b}); return bytes;
}
}
struct transfer_crypto::impl {
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    std::unique_ptr<aes_gcm_engine> engine;
    std::array<uint8_t, 32> mac_key{};
    std::atomic<uint64_t> encryption_count{0};
    ~impl() { OPENSSL_cleanse(mac_key.data(), mac_key.size()); }
#endif
};
transfer_crypto::transfer_crypto() : impl_(std::make_unique<impl>()) {}
transfer_crypto::~transfer_crypto() = default;
auto transfer_crypto::validate_secret(std::span<const std::byte> key, std::string_view password) -> result<void> {
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    if ((key.empty() && password.empty()) || (!key.empty() && !password.empty()) || (!key.empty() && key.size() != 32))
        return unexpected{error{error_code::invalid_configuration, "Configure exactly one 32-byte encryption key or nonempty password"}};
    return {};
#else
    (void)key; (void)password; return unexpected{unavailable()};
#endif
}
auto transfer_crypto::random_salt() -> result<salt> {
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    salt value{};
    if (RAND_bytes(value.data(), value.size()) != 1) return unexpected{error{error_code::internal_error, "Cannot generate transfer salt"}};
    return value;
#else
    return unexpected{unavailable()};
#endif
}
auto transfer_crypto::create(std::span<const std::byte> key, std::string_view password,
    const transfer_id& id, const salt& client_salt, bool upload, const salt* server_salt) -> result<std::shared_ptr<transfer_crypto>> {
    auto valid = validate_secret(key, password); if (!valid) return unexpected{valid.error()};
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    std::vector<std::byte> secret(key.begin(), key.end());
    if (!password.empty()) {
        auto kdf = pbkdf2_key_derivation::create();
        if (!kdf) return unexpected{error{error_code::internal_error, "Cannot initialize password derivation"}};
        auto derived = kdf->derive_key(password, std::as_bytes(std::span{client_salt}));
        if (!derived) return unexpected{derived.error()};
        secret = std::move(derived.value().key);
    }
    std::vector<uint8_t> salts(client_salt.begin(), client_salt.end());
    if (server_salt) salts.insert(salts.end(), server_salt->begin(), server_salt->end());
    std::string domain = std::string("FTS1/AES256GCM/v1/") + (upload ? "upload/" : "download/") + (server_salt ? "accepted/" : "request/");
    std::vector<uint8_t> info(domain.begin(), domain.end()); info.insert(info.end(), id.bytes.begin(), id.bytes.end());
    std::array<uint8_t, 64> material{}; std::size_t length = material.size();
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr), EVP_PKEY_CTX_free);
    bool derived = ctx && EVP_PKEY_derive_init(ctx.get()) > 0 &&
        EVP_PKEY_CTX_set_hkdf_md(ctx.get(), EVP_sha256()) > 0 &&
        EVP_PKEY_CTX_set1_hkdf_salt(ctx.get(), salts.data(), salts.size()) > 0 &&
        EVP_PKEY_CTX_set1_hkdf_key(ctx.get(), reinterpret_cast<const unsigned char*>(secret.data()), secret.size()) > 0 &&
        EVP_PKEY_CTX_add1_hkdf_info(ctx.get(), info.data(), info.size()) > 0 &&
        EVP_PKEY_derive(ctx.get(), material.data(), &length) > 0 && length == material.size();
    OPENSSL_cleanse(secret.data(), secret.size());
    if (!derived) { OPENSSL_cleanse(material.data(), material.size()); return unexpected{error{error_code::internal_error, "Transfer key derivation failed"}}; }
    auto crypto = std::shared_ptr<transfer_crypto>(new transfer_crypto);
    crypto->impl_->engine = aes_gcm_engine::create();
    auto assigned = crypto->impl_->engine ? crypto->impl_->engine->set_key(std::as_bytes(std::span{material}.first<32>())) : result<void>{unexpected{unavailable()}};
    std::copy(material.begin() + 32, material.end(), crypto->impl_->mac_key.begin());
    OPENSSL_cleanse(material.data(), material.size());
    if (!assigned) return unexpected{assigned.error()};
    return crypto;
#else
    (void)id; (void)client_salt; (void)upload; (void)server_salt; return unexpected{unavailable()};
#endif
}
auto transfer_crypto::authenticate(std::span<const uint8_t> bytes) const -> result<salt> {
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    salt tag{}; unsigned length = tag.size();
    if (!HMAC(EVP_sha256(), impl_->mac_key.data(), impl_->mac_key.size(), bytes.data(), bytes.size(), tag.data(), &length) || length != tag.size())
        return unexpected{error{error_code::internal_error, "Transfer authentication failed"}};
    return tag;
#else
    (void)bytes; return unexpected{unavailable()};
#endif
}
auto transfer_crypto::equal_tags(const salt& a, const salt& b) -> bool {
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
#else
    (void)a; (void)b; return false;
#endif
}
auto transfer_crypto::seal(chunk& data) -> result<void> {
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    if (impl_->encryption_count++ >= UINT32_MAX || data.data.size() > UINT32_MAX - 28)
        return unexpected{error{error_code::file_too_large, "Encrypted transfer exceeds nonce/size limits"}};
    data.header.flags |= chunk_flags::encrypted;
    data.header.compressed_size = static_cast<uint32_t>(data.data.size() + 28);
    auto encrypted = impl_->engine->encrypt(data.data, chunk_aad(data));
    if (!encrypted) return unexpected{encrypted.error()};
    auto& value = encrypted.value();
    data.data = std::move(value.metadata.iv);
    data.data.insert(data.data.end(), value.metadata.auth_tag.begin(), value.metadata.auth_tag.end());
    data.data.insert(data.data.end(), value.ciphertext.begin(), value.ciphertext.end()); return {};
#else
    (void)data; return unexpected{unavailable()};
#endif
}
auto transfer_crypto::open(chunk& data) -> result<void> {
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    if (!is_encrypted(data.header.flags) || data.data.size() < 28 || data.header.compressed_size != data.data.size())
        return unexpected{error{error_code::invalid_configuration, "Missing or malformed encrypted chunk"}};
    encryption_metadata metadata; metadata.algorithm = encryption_algorithm::aes_256_gcm;
    metadata.iv.assign(data.data.begin(), data.data.begin() + 12);
    metadata.auth_tag.assign(data.data.begin() + 12, data.data.begin() + 28);
    metadata.aad = chunk_aad(data); metadata.original_size = data.data.size() - 28;
    auto decrypted = impl_->engine->decrypt(std::span{data.data}.subspan(28), metadata);
    if (!decrypted) return unexpected{decrypted.error()};
    data.data = std::move(decrypted.value().plaintext); data.header.flags &= ~chunk_flags::encrypted;
    data.header.compressed_size = static_cast<uint32_t>(data.data.size()); return {};
#else
    (void)data; return unexpected{unavailable()};
#endif
}
} // namespace kcenon::file_transfer
