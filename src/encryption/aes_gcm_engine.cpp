/**
 * @file aes_gcm_engine.cpp
 * @brief AES-256-GCM encryption engine implementation
 */

#ifdef FILE_TRANS_ENABLE_ENCRYPTION

#include "kcenon/file_transfer/encryption/aes_gcm_engine.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace kcenon::file_transfer {

namespace {

/**
 * @brief Get OpenSSL error message
 */
auto get_openssl_error() -> std::string {
    unsigned long err = ERR_get_error();
    if (err == 0) {
        return "Unknown OpenSSL error";
    }
    std::array<char, 256> buffer{};
    ERR_error_string_n(err, buffer.data(), buffer.size());
    return std::string(buffer.data());
}

/**
 * @brief Securely zero memory
 */
void secure_zero_memory(void* ptr, std::size_t size) {
    if (ptr != nullptr && size > 0) {
        OPENSSL_cleanse(ptr, size);
    }
}

/**
 * @brief RAII wrapper for EVP_CIPHER_CTX
 */
class evp_cipher_ctx_wrapper {
public:
    evp_cipher_ctx_wrapper() : ctx_(EVP_CIPHER_CTX_new()) {}

    ~evp_cipher_ctx_wrapper() {
        if (ctx_) {
            EVP_CIPHER_CTX_free(ctx_);
        }
    }

    evp_cipher_ctx_wrapper(const evp_cipher_ctx_wrapper&) = delete;
    auto operator=(const evp_cipher_ctx_wrapper&) -> evp_cipher_ctx_wrapper& = delete;

    evp_cipher_ctx_wrapper(evp_cipher_ctx_wrapper&& other) noexcept
        : ctx_(other.ctx_) {
        other.ctx_ = nullptr;
    }

    auto operator=(evp_cipher_ctx_wrapper&& other) noexcept -> evp_cipher_ctx_wrapper& {
        if (this != &other) {
            if (ctx_) {
                EVP_CIPHER_CTX_free(ctx_);
            }
            ctx_ = other.ctx_;
            other.ctx_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] auto get() const -> EVP_CIPHER_CTX* { return ctx_; }
    [[nodiscard]] explicit operator bool() const { return ctx_ != nullptr; }

private:
    EVP_CIPHER_CTX* ctx_;
};

}  // namespace

// ============================================================================
// aes_gcm_stream_context::impl
// ============================================================================

struct aes_gcm_stream_context::impl {
    evp_cipher_ctx_wrapper ctx;
    std::vector<std::byte> key;
    encryption_metadata metadata;
    aes_gcm_config config;
    uint64_t bytes_processed = 0;
    uint64_t total_size = 0;
    bool is_encrypting = true;
    bool finalized = false;

    impl(std::span<const std::byte> key_data,
         bool encrypting,
         uint64_t total,
         std::span<const std::byte> aad,
         const aes_gcm_config& cfg)
        : key(key_data.begin(), key_data.end())
        , config(cfg)
        , total_size(total)
        , is_encrypting(encrypting) {
        metadata.algorithm = encryption_algorithm::aes_256_gcm;
        metadata.original_size = total;

        if (!aad.empty()) {
            metadata.aad.assign(aad.begin(), aad.end());
        }
    }

    impl(std::span<const std::byte> key_data,
         const encryption_metadata& meta,
         const aes_gcm_config& cfg)
        : key(key_data.begin(), key_data.end())
        , metadata(meta)
        , config(cfg)
        , total_size(meta.original_size)
        , is_encrypting(false) {}

    ~impl() {
        if (config.secure_memory && !key.empty()) {
            secure_zero_memory(key.data(), key.size());
        }
    }

    auto initialize() -> result<void> {
        if (!ctx) {
            return unexpected(error(error_code::internal_error, "Failed to create cipher context"));
        }

        const EVP_CIPHER* cipher = EVP_aes_256_gcm();

        if (is_encrypting) {
            // Generate random IV for encryption
            metadata.iv.resize(config.iv_size);
            if (RAND_bytes(reinterpret_cast<unsigned char*>(metadata.iv.data()),
                           static_cast<int>(metadata.iv.size())) != 1) {
                return unexpected(error(error_code::internal_error, "Failed to generate IV"));
            }

            if (EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr) != 1) {
                return unexpected(error(error_code::internal_error, get_openssl_error()));
            }

            if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN,
                                    static_cast<int>(config.iv_size), nullptr) != 1) {
                return unexpected(error(error_code::internal_error, "Failed to set IV length"));
            }

            if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr,
                                   reinterpret_cast<const unsigned char*>(key.data()),
                                   reinterpret_cast<const unsigned char*>(metadata.iv.data())) != 1) {
                return unexpected(error(error_code::internal_error, get_openssl_error()));
            }

            // Process AAD if present
            if (!metadata.aad.empty()) {
                int len = 0;
                if (EVP_EncryptUpdate(ctx.get(), nullptr, &len,
                                      reinterpret_cast<const unsigned char*>(metadata.aad.data()),
                                      static_cast<int>(metadata.aad.size())) != 1) {
                    return unexpected(error(error_code::internal_error, "Failed to process AAD"));
                }
            }
        } else {
            // Decryption
            if (EVP_DecryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr) != 1) {
                return unexpected(error(error_code::internal_error, get_openssl_error()));
            }

            if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN,
                                    static_cast<int>(metadata.iv.size()), nullptr) != 1) {
                return unexpected(error(error_code::internal_error, "Failed to set IV length"));
            }

            if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr,
                                   reinterpret_cast<const unsigned char*>(key.data()),
                                   reinterpret_cast<const unsigned char*>(metadata.iv.data())) != 1) {
                return unexpected(error(error_code::internal_error, get_openssl_error()));
            }

            // Process AAD if present
            if (!metadata.aad.empty()) {
                int len = 0;
                if (EVP_DecryptUpdate(ctx.get(), nullptr, &len,
                                      reinterpret_cast<const unsigned char*>(metadata.aad.data()),
                                      static_cast<int>(metadata.aad.size())) != 1) {
                    return unexpected(error(error_code::internal_error, "Failed to process AAD"));
                }
            }

            // Set expected tag for verification
            if (!metadata.auth_tag.empty()) {
                if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG,
                                        static_cast<int>(metadata.auth_tag.size()),
                                        const_cast<std::byte*>(metadata.auth_tag.data())) != 1) {
                    return unexpected(error(error_code::internal_error, "Failed to set auth tag"));
                }
            }
        }

        return {};
    }
};

// ============================================================================
// aes_gcm_stream_context
// ============================================================================

aes_gcm_stream_context::aes_gcm_stream_context(
    std::span<const std::byte> key,
    bool encrypting,
    uint64_t total_size,
    std::span<const std::byte> aad,
    const aes_gcm_config& config)
    : impl_(std::make_unique<impl>(key, encrypting, total_size, aad, config)) {
    impl_->initialize();
}

aes_gcm_stream_context::aes_gcm_stream_context(
    std::span<const std::byte> key,
    const encryption_metadata& metadata,
    const aes_gcm_config& config)
    : impl_(std::make_unique<impl>(key, metadata, config)) {
    impl_->initialize();
}

aes_gcm_stream_context::~aes_gcm_stream_context() = default;

aes_gcm_stream_context::aes_gcm_stream_context(aes_gcm_stream_context&&) noexcept = default;
auto aes_gcm_stream_context::operator=(aes_gcm_stream_context&&) noexcept
    -> aes_gcm_stream_context& = default;

auto aes_gcm_stream_context::process_chunk(
    std::span<const std::byte> input) -> result<std::vector<std::byte>> {
    if (!impl_ || impl_->finalized) {
        return unexpected(error(error_code::internal_error, "Stream context invalid or finalized"));
    }

    std::vector<std::byte> output(input.size() + AES_BLOCK_SIZE);
    int out_len = 0;

    if (impl_->is_encrypting) {
        if (EVP_EncryptUpdate(impl_->ctx.get(),
                              reinterpret_cast<unsigned char*>(output.data()),
                              &out_len,
                              reinterpret_cast<const unsigned char*>(input.data()),
                              static_cast<int>(input.size())) != 1) {
            return unexpected(error(error_code::internal_error, get_openssl_error()));
        }
    } else {
        if (EVP_DecryptUpdate(impl_->ctx.get(),
                              reinterpret_cast<unsigned char*>(output.data()),
                              &out_len,
                              reinterpret_cast<const unsigned char*>(input.data()),
                              static_cast<int>(input.size())) != 1) {
            return unexpected(error(error_code::internal_error, get_openssl_error()));
        }
    }

    output.resize(static_cast<std::size_t>(out_len));
    impl_->bytes_processed += input.size();

    return output;
}

auto aes_gcm_stream_context::finalize() -> result<std::vector<std::byte>> {
    if (!impl_ || impl_->finalized) {
        return unexpected(error(error_code::internal_error, "Stream context invalid or already finalized"));
    }

    std::vector<std::byte> output(AES_BLOCK_SIZE + AES_GCM_TAG_SIZE);
    int out_len = 0;

    if (impl_->is_encrypting) {
        if (EVP_EncryptFinal_ex(impl_->ctx.get(),
                                reinterpret_cast<unsigned char*>(output.data()),
                                &out_len) != 1) {
            return unexpected(error(error_code::internal_error, get_openssl_error()));
        }

        output.resize(static_cast<std::size_t>(out_len));

        // Get authentication tag
        impl_->metadata.auth_tag.resize(impl_->config.tag_size);
        if (EVP_CIPHER_CTX_ctrl(impl_->ctx.get(), EVP_CTRL_GCM_GET_TAG,
                                static_cast<int>(impl_->config.tag_size),
                                impl_->metadata.auth_tag.data()) != 1) {
            return unexpected(error(error_code::internal_error, "Failed to get auth tag"));
        }
    } else {
        if (EVP_DecryptFinal_ex(impl_->ctx.get(),
                                reinterpret_cast<unsigned char*>(output.data()),
                                &out_len) != 1) {
            return unexpected(error(error_code::chunk_checksum_error,
                                   "Authentication failed - data may have been tampered"));
        }
        output.resize(static_cast<std::size_t>(out_len));
    }

    impl_->finalized = true;
    return output;
}

auto aes_gcm_stream_context::get_metadata() const -> encryption_metadata {
    return impl_ ? impl_->metadata : encryption_metadata{};
}

auto aes_gcm_stream_context::bytes_processed() const -> uint64_t {
    return impl_ ? impl_->bytes_processed : 0;
}

auto aes_gcm_stream_context::is_encryption() const -> bool {
    return impl_ ? impl_->is_encrypting : true;
}

// ============================================================================
// aes_gcm_engine::impl
// ============================================================================

struct aes_gcm_engine::impl {
    aes_gcm_config config;
    std::vector<std::byte> key;
    std::atomic<encryption_state> current_state{encryption_state::uninitialized};
    mutable std::mutex stats_mutex;
    encryption_statistics stats;
    std::function<void(const encryption_progress&)> progress_callback;
    std::atomic<uint64_t> iv_counter{0};

    explicit impl(const aes_gcm_config& cfg) : config(cfg) {}

    ~impl() {
        if (config.secure_memory && !key.empty()) {
            secure_zero_memory(key.data(), key.size());
        }
    }

    auto generate_counter_iv() -> std::vector<std::byte> {
        std::vector<std::byte> iv(config.iv_size);

        // First 8 bytes: random
        if (RAND_bytes(reinterpret_cast<unsigned char*>(iv.data()), 8) != 1) {
            return {};
        }

        // Last 4 bytes: counter (for uniqueness)
        uint64_t counter = iv_counter.fetch_add(1);
        if (iv.size() >= 12) {
            std::memcpy(iv.data() + 8, &counter, 4);
        }

        return iv;
    }

    auto derive_chunk_iv(uint64_t chunk_index) -> std::vector<std::byte> {
        std::vector<std::byte> iv(config.iv_size);

        // Generate base IV from key hash + chunk index
        // This ensures unique IV per chunk while being deterministic
        if (RAND_bytes(reinterpret_cast<unsigned char*>(iv.data()),
                       static_cast<int>(config.iv_size - 4)) != 1) {
            return {};
        }

        // Embed chunk index in last 4 bytes
        if (iv.size() >= 12) {
            uint32_t idx = static_cast<uint32_t>(chunk_index);
            std::memcpy(iv.data() + iv.size() - 4, &idx, 4);
        }

        return iv;
    }

    void update_stats_encrypt(uint64_t bytes, std::chrono::microseconds duration) {
        std::lock_guard<std::mutex> lock(stats_mutex);
        stats.bytes_encrypted += bytes;
        stats.encryption_ops++;
        stats.total_encrypt_time += duration;
    }

    void update_stats_decrypt(uint64_t bytes, std::chrono::microseconds duration) {
        std::lock_guard<std::mutex> lock(stats_mutex);
        stats.bytes_decrypted += bytes;
        stats.decryption_ops++;
        stats.total_decrypt_time += duration;
    }

    void increment_errors() {
        std::lock_guard<std::mutex> lock(stats_mutex);
        stats.errors++;
    }
};

// ============================================================================
// aes_gcm_engine
// ============================================================================

aes_gcm_engine::aes_gcm_engine(const aes_gcm_config& config)
    : impl_(std::make_unique<impl>(config)) {}

aes_gcm_engine::~aes_gcm_engine() = default;

aes_gcm_engine::aes_gcm_engine(aes_gcm_engine&&) noexcept = default;
auto aes_gcm_engine::operator=(aes_gcm_engine&&) noexcept -> aes_gcm_engine& = default;

auto aes_gcm_engine::create(const aes_gcm_config& config) -> std::unique_ptr<aes_gcm_engine> {
    return std::unique_ptr<aes_gcm_engine>(new aes_gcm_engine(config));
}

auto aes_gcm_engine::algorithm() const -> encryption_algorithm {
    return encryption_algorithm::aes_256_gcm;
}

auto aes_gcm_engine::algorithm_name() const -> std::string_view {
    return "aes-256-gcm";
}

auto aes_gcm_engine::set_key(std::span<const std::byte> key) -> result<void> {
    if (key.size() != AES_256_KEY_SIZE) {
        return unexpected(error(error_code::invalid_configuration,
                               "Invalid key size: expected 32 bytes"));
    }

    impl_->key.assign(key.begin(), key.end());
    impl_->current_state = encryption_state::ready;
    return {};
}

auto aes_gcm_engine::set_key(const derived_key& derived) -> result<void> {
    return set_key(std::span<const std::byte>(derived.key));
}

auto aes_gcm_engine::has_key() const -> bool {
    return !impl_->key.empty();
}

void aes_gcm_engine::clear_key() {
    if (impl_->config.secure_memory && !impl_->key.empty()) {
        secure_zero_memory(impl_->key.data(), impl_->key.size());
    }
    impl_->key.clear();
    impl_->current_state = encryption_state::uninitialized;
}

auto aes_gcm_engine::key_size() const -> std::size_t {
    return AES_256_KEY_SIZE;
}

auto aes_gcm_engine::encrypt(
    std::span<const std::byte> plaintext,
    std::span<const std::byte> aad) -> result<encryption_result> {
    if (!has_key()) {
        return unexpected(error(error_code::not_initialized, "Key not set"));
    }

    auto start_time = std::chrono::steady_clock::now();
    impl_->current_state = encryption_state::processing;

    evp_cipher_ctx_wrapper ctx;
    if (!ctx) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, "Failed to create cipher context"));
    }

    encryption_result result;
    result.metadata.algorithm = encryption_algorithm::aes_256_gcm;
    result.metadata.original_size = plaintext.size();

    // Generate IV
    result.metadata.iv = impl_->generate_counter_iv();
    if (result.metadata.iv.empty()) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, "Failed to generate IV"));
    }

    // Store AAD
    if (!aad.empty()) {
        result.metadata.aad.assign(aad.begin(), aad.end());
    }

    const EVP_CIPHER* cipher = EVP_aes_256_gcm();

    // Initialize encryption
    if (EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr) != 1) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, get_openssl_error()));
    }

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN,
                            static_cast<int>(impl_->config.iv_size), nullptr) != 1) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, "Failed to set IV length"));
    }

    if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr,
                           reinterpret_cast<const unsigned char*>(impl_->key.data()),
                           reinterpret_cast<const unsigned char*>(result.metadata.iv.data())) != 1) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, get_openssl_error()));
    }

    // Process AAD
    if (!aad.empty()) {
        int len = 0;
        if (EVP_EncryptUpdate(ctx.get(), nullptr, &len,
                              reinterpret_cast<const unsigned char*>(aad.data()),
                              static_cast<int>(aad.size())) != 1) {
            impl_->increment_errors();
            impl_->current_state = encryption_state::error;
            return unexpected(error(error_code::internal_error, "Failed to process AAD"));
        }
    }

    // Encrypt plaintext
    result.ciphertext.resize(plaintext.size() + AES_BLOCK_SIZE);
    int out_len = 0;

    if (EVP_EncryptUpdate(ctx.get(),
                          reinterpret_cast<unsigned char*>(result.ciphertext.data()),
                          &out_len,
                          reinterpret_cast<const unsigned char*>(plaintext.data()),
                          static_cast<int>(plaintext.size())) != 1) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, get_openssl_error()));
    }

    int total_len = out_len;

    // Finalize encryption
    if (EVP_EncryptFinal_ex(ctx.get(),
                            reinterpret_cast<unsigned char*>(result.ciphertext.data()) + out_len,
                            &out_len) != 1) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, get_openssl_error()));
    }

    total_len += out_len;
    result.ciphertext.resize(static_cast<std::size_t>(total_len));

    // Get authentication tag
    result.metadata.auth_tag.resize(impl_->config.tag_size);
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG,
                            static_cast<int>(impl_->config.tag_size),
                            result.metadata.auth_tag.data()) != 1) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, "Failed to get auth tag"));
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    impl_->update_stats_encrypt(plaintext.size(), duration);
    impl_->current_state = encryption_state::ready;

    return result;
}

auto aes_gcm_engine::decrypt(
    std::span<const std::byte> ciphertext,
    const encryption_metadata& metadata) -> result<decryption_result> {
    if (!has_key()) {
        return unexpected(error(error_code::not_initialized, "Key not set"));
    }

    auto start_time = std::chrono::steady_clock::now();
    impl_->current_state = encryption_state::processing;

    evp_cipher_ctx_wrapper ctx;
    if (!ctx) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, "Failed to create cipher context"));
    }

    const EVP_CIPHER* cipher = EVP_aes_256_gcm();

    // Initialize decryption
    if (EVP_DecryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr) != 1) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, get_openssl_error()));
    }

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN,
                            static_cast<int>(metadata.iv.size()), nullptr) != 1) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, "Failed to set IV length"));
    }

    if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr,
                           reinterpret_cast<const unsigned char*>(impl_->key.data()),
                           reinterpret_cast<const unsigned char*>(metadata.iv.data())) != 1) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, get_openssl_error()));
    }

    // Process AAD
    if (!metadata.aad.empty()) {
        int len = 0;
        if (EVP_DecryptUpdate(ctx.get(), nullptr, &len,
                              reinterpret_cast<const unsigned char*>(metadata.aad.data()),
                              static_cast<int>(metadata.aad.size())) != 1) {
            impl_->increment_errors();
            impl_->current_state = encryption_state::error;
            return unexpected(error(error_code::internal_error, "Failed to process AAD"));
        }
    }

    // Decrypt ciphertext
    decryption_result result;
    result.plaintext.resize(ciphertext.size() + AES_BLOCK_SIZE);
    int out_len = 0;

    if (EVP_DecryptUpdate(ctx.get(),
                          reinterpret_cast<unsigned char*>(result.plaintext.data()),
                          &out_len,
                          reinterpret_cast<const unsigned char*>(ciphertext.data()),
                          static_cast<int>(ciphertext.size())) != 1) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, get_openssl_error()));
    }

    int total_len = out_len;

    // Set expected tag
    if (!metadata.auth_tag.empty()) {
        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG,
                                static_cast<int>(metadata.auth_tag.size()),
                                const_cast<std::byte*>(metadata.auth_tag.data())) != 1) {
            impl_->increment_errors();
            impl_->current_state = encryption_state::error;
            return unexpected(error(error_code::internal_error, "Failed to set auth tag"));
        }
    }

    // Finalize and verify tag
    if (EVP_DecryptFinal_ex(ctx.get(),
                            reinterpret_cast<unsigned char*>(result.plaintext.data()) + out_len,
                            &out_len) != 1) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::chunk_checksum_error,
                               "Authentication failed - data may have been tampered"));
    }

    total_len += out_len;
    result.plaintext.resize(static_cast<std::size_t>(total_len));
    result.original_size = metadata.original_size;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    impl_->update_stats_decrypt(ciphertext.size(), duration);
    impl_->current_state = encryption_state::ready;

    return result;
}

auto aes_gcm_engine::encrypt_async(
    std::span<const std::byte> plaintext,
    std::span<const std::byte> aad) -> std::future<result<encryption_result>> {
    // Copy data for async operation
    std::vector<std::byte> plaintext_copy(plaintext.begin(), plaintext.end());
    std::vector<std::byte> aad_copy(aad.begin(), aad.end());

    return std::async(std::launch::async, [this, pt = std::move(plaintext_copy),
                                           ad = std::move(aad_copy)]() {
        return this->encrypt(std::span<const std::byte>(pt), std::span<const std::byte>(ad));
    });
}

auto aes_gcm_engine::decrypt_async(
    std::span<const std::byte> ciphertext,
    const encryption_metadata& metadata) -> std::future<result<decryption_result>> {
    // Copy data for async operation
    std::vector<std::byte> ciphertext_copy(ciphertext.begin(), ciphertext.end());
    encryption_metadata metadata_copy = metadata;

    return std::async(std::launch::async, [this, ct = std::move(ciphertext_copy),
                                           meta = std::move(metadata_copy)]() {
        return this->decrypt(std::span<const std::byte>(ct), meta);
    });
}

auto aes_gcm_engine::create_encrypt_stream(
    uint64_t total_size,
    std::span<const std::byte> aad) -> std::unique_ptr<encryption_stream_context> {
    if (!has_key()) {
        return nullptr;
    }

    return std::unique_ptr<encryption_stream_context>(
        new aes_gcm_stream_context(
            std::span<const std::byte>(impl_->key),
            true,
            total_size,
            aad,
            impl_->config));
}

auto aes_gcm_engine::create_decrypt_stream(
    const encryption_metadata& metadata) -> std::unique_ptr<encryption_stream_context> {
    if (!has_key()) {
        return nullptr;
    }

    return std::unique_ptr<encryption_stream_context>(
        new aes_gcm_stream_context(
            std::span<const std::byte>(impl_->key),
            metadata,
            impl_->config));
}

auto aes_gcm_engine::encrypt_chunk(
    std::span<const std::byte> chunk_data,
    uint64_t chunk_index) -> result<encryption_result> {
    if (!has_key()) {
        return unexpected(error(error_code::not_initialized, "Key not set"));
    }

    auto start_time = std::chrono::steady_clock::now();
    impl_->current_state = encryption_state::processing;

    evp_cipher_ctx_wrapper ctx;
    if (!ctx) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, "Failed to create cipher context"));
    }

    encryption_result result;
    result.metadata.algorithm = encryption_algorithm::aes_256_gcm;
    result.metadata.original_size = chunk_data.size();

    // Generate chunk-specific IV with embedded chunk index
    result.metadata.iv = impl_->derive_chunk_iv(chunk_index);
    if (result.metadata.iv.empty()) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, "Failed to generate chunk IV"));
    }

    const EVP_CIPHER* cipher = EVP_aes_256_gcm();

    if (EVP_EncryptInit_ex(ctx.get(), cipher, nullptr,
                           reinterpret_cast<const unsigned char*>(impl_->key.data()),
                           reinterpret_cast<const unsigned char*>(result.metadata.iv.data())) != 1) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, get_openssl_error()));
    }

    result.ciphertext.resize(chunk_data.size() + AES_BLOCK_SIZE);
    int out_len = 0;

    if (EVP_EncryptUpdate(ctx.get(),
                          reinterpret_cast<unsigned char*>(result.ciphertext.data()),
                          &out_len,
                          reinterpret_cast<const unsigned char*>(chunk_data.data()),
                          static_cast<int>(chunk_data.size())) != 1) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, get_openssl_error()));
    }

    int total_len = out_len;

    if (EVP_EncryptFinal_ex(ctx.get(),
                            reinterpret_cast<unsigned char*>(result.ciphertext.data()) + out_len,
                            &out_len) != 1) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, get_openssl_error()));
    }

    total_len += out_len;
    result.ciphertext.resize(static_cast<std::size_t>(total_len));

    result.metadata.auth_tag.resize(impl_->config.tag_size);
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG,
                            static_cast<int>(impl_->config.tag_size),
                            result.metadata.auth_tag.data()) != 1) {
        impl_->increment_errors();
        impl_->current_state = encryption_state::error;
        return unexpected(error(error_code::internal_error, "Failed to get auth tag"));
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    impl_->update_stats_encrypt(chunk_data.size(), duration);
    impl_->current_state = encryption_state::ready;

    return result;
}

auto aes_gcm_engine::decrypt_chunk(
    std::span<const std::byte> encrypted_chunk,
    const encryption_metadata& metadata,
    [[maybe_unused]] uint64_t chunk_index) -> result<decryption_result> {
    // chunk_index is unused here as IV is stored in metadata
    return decrypt(encrypted_chunk, metadata);
}

auto aes_gcm_engine::state() const -> encryption_state {
    return impl_->current_state.load();
}

auto aes_gcm_engine::get_statistics() const -> encryption_statistics {
    std::lock_guard<std::mutex> lock(impl_->stats_mutex);
    return impl_->stats;
}

void aes_gcm_engine::reset_statistics() {
    std::lock_guard<std::mutex> lock(impl_->stats_mutex);
    impl_->stats = encryption_statistics{};
}

auto aes_gcm_engine::config() const -> const encryption_config& {
    return impl_->config;
}

void aes_gcm_engine::on_progress(
    std::function<void(const encryption_progress&)> callback) {
    impl_->progress_callback = std::move(callback);
}

auto aes_gcm_engine::generate_iv() -> result<std::vector<std::byte>> {
    auto iv = impl_->generate_counter_iv();
    if (iv.empty()) {
        return unexpected(error(error_code::internal_error, "Failed to generate IV"));
    }
    return iv;
}

auto aes_gcm_engine::verify_tag(
    std::span<const std::byte> ciphertext,
    const encryption_metadata& metadata) -> bool {
    auto result = decrypt(ciphertext, metadata);
    return result.has_value();
}

auto aes_gcm_engine::iv_size() const -> std::size_t {
    return impl_->config.iv_size;
}

auto aes_gcm_engine::tag_size() const -> std::size_t {
    return impl_->config.tag_size;
}

auto aes_gcm_engine::calculate_ciphertext_size(std::size_t plaintext_size) const -> std::size_t {
    // AES-GCM ciphertext is same size as plaintext (no padding)
    return plaintext_size;
}

}  // namespace kcenon::file_transfer

#endif  // FILE_TRANS_ENABLE_ENCRYPTION
