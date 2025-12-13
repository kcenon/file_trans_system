/**
 * @file encryption_config.h
 * @brief Encryption configuration types
 * @version 0.1.0
 *
 * This file defines configuration structures for encryption implementations.
 */

#ifndef KCENON_FILE_TRANSFER_ENCRYPTION_ENCRYPTION_CONFIG_H
#define KCENON_FILE_TRANSFER_ENCRYPTION_ENCRYPTION_CONFIG_H

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kcenon::file_transfer {

/**
 * @brief Encryption algorithm enumeration
 */
enum class encryption_algorithm {
    none,           ///< No encryption
    aes_256_gcm,    ///< AES-256-GCM (recommended)
    aes_256_cbc,    ///< AES-256-CBC (legacy)
    chacha20_poly1305  ///< ChaCha20-Poly1305 (alternative)
};

/**
 * @brief Convert encryption_algorithm to string
 */
[[nodiscard]] constexpr auto to_string(encryption_algorithm algo) -> const char* {
    switch (algo) {
        case encryption_algorithm::none: return "none";
        case encryption_algorithm::aes_256_gcm: return "aes-256-gcm";
        case encryption_algorithm::aes_256_cbc: return "aes-256-cbc";
        case encryption_algorithm::chacha20_poly1305: return "chacha20-poly1305";
        default: return "unknown";
    }
}

/**
 * @brief Key derivation function enumeration
 */
enum class key_derivation_function {
    none,       ///< No key derivation (raw key)
    pbkdf2,     ///< PBKDF2-HMAC-SHA256
    argon2id,   ///< Argon2id (recommended)
    scrypt      ///< scrypt
};

/**
 * @brief Convert key_derivation_function to string
 */
[[nodiscard]] constexpr auto to_string(key_derivation_function kdf) -> const char* {
    switch (kdf) {
        case key_derivation_function::none: return "none";
        case key_derivation_function::pbkdf2: return "pbkdf2";
        case key_derivation_function::argon2id: return "argon2id";
        case key_derivation_function::scrypt: return "scrypt";
        default: return "unknown";
    }
}

/**
 * @brief Encryption state enumeration
 */
enum class encryption_state {
    uninitialized,  ///< Not initialized
    ready,          ///< Ready for encryption/decryption
    processing,     ///< Currently processing
    error           ///< Error state
};

/**
 * @brief Convert encryption_state to string
 */
[[nodiscard]] constexpr auto to_string(encryption_state state) -> const char* {
    switch (state) {
        case encryption_state::uninitialized: return "uninitialized";
        case encryption_state::ready: return "ready";
        case encryption_state::processing: return "processing";
        case encryption_state::error: return "error";
        default: return "unknown";
    }
}

// Standard sizes for cryptographic parameters
inline constexpr std::size_t AES_256_KEY_SIZE = 32;      ///< 256 bits
inline constexpr std::size_t AES_GCM_IV_SIZE = 12;       ///< 96 bits (recommended)
inline constexpr std::size_t AES_GCM_TAG_SIZE = 16;      ///< 128 bits
inline constexpr std::size_t AES_BLOCK_SIZE = 16;        ///< 128 bits
inline constexpr std::size_t SALT_SIZE = 32;             ///< 256 bits
inline constexpr std::size_t CHACHA20_KEY_SIZE = 32;     ///< 256 bits
inline constexpr std::size_t CHACHA20_NONCE_SIZE = 12;   ///< 96 bits
inline constexpr std::size_t CHACHA20_TAG_SIZE = 16;     ///< 128 bits

/**
 * @brief Encryption metadata stored with encrypted data
 *
 * This structure contains all information needed to decrypt the data,
 * except for the key itself.
 */
struct encryption_metadata {
    encryption_algorithm algorithm = encryption_algorithm::aes_256_gcm;
    key_derivation_function kdf = key_derivation_function::none;

    /// Initialization vector / nonce
    std::vector<std::byte> iv;

    /// Salt for key derivation (if KDF is used)
    std::vector<std::byte> salt;

    /// Authentication tag (for AEAD algorithms)
    std::vector<std::byte> auth_tag;

    /// Additional authenticated data (AAD) used during encryption
    std::vector<std::byte> aad;

    /// KDF iterations (for PBKDF2/scrypt)
    uint32_t kdf_iterations = 0;

    /// Argon2 memory cost in KB
    uint32_t argon2_memory_kb = 0;

    /// Argon2 parallelism
    uint32_t argon2_parallelism = 0;

    /// Original data size before encryption
    uint64_t original_size = 0;

    /// Version for format compatibility
    uint8_t version = 1;
};

/**
 * @brief Base encryption configuration
 */
struct encryption_config {
    encryption_algorithm algorithm = encryption_algorithm::aes_256_gcm;

    /// Enable authenticated encryption (AEAD)
    bool use_aead = true;

    /// Chunk size for streaming encryption (0 = process all at once)
    std::size_t stream_chunk_size = 64 * 1024;  // 64KB

    /// Additional authenticated data to include
    std::optional<std::vector<std::byte>> aad;

    /// Zero memory after use for security
    bool secure_memory = true;

    virtual ~encryption_config() = default;
};

/**
 * @brief AES-256-GCM specific configuration
 */
struct aes_gcm_config : encryption_config {
    aes_gcm_config() {
        algorithm = encryption_algorithm::aes_256_gcm;
        use_aead = true;
    }

    /// IV size (default: 12 bytes / 96 bits, recommended by NIST)
    std::size_t iv_size = AES_GCM_IV_SIZE;

    /// Tag size (default: 16 bytes / 128 bits)
    std::size_t tag_size = AES_GCM_TAG_SIZE;

    /// Generate random IV for each encryption
    bool random_iv = true;
};

/**
 * @brief AES-256-CBC specific configuration (legacy support)
 */
struct aes_cbc_config : encryption_config {
    aes_cbc_config() {
        algorithm = encryption_algorithm::aes_256_cbc;
        use_aead = false;  // CBC doesn't provide authentication
    }

    /// IV size (must be AES block size)
    std::size_t iv_size = AES_BLOCK_SIZE;

    /// Enable PKCS7 padding
    bool pkcs7_padding = true;

    /// Use HMAC for authentication (recommended for CBC)
    bool use_hmac = true;
};

/**
 * @brief ChaCha20-Poly1305 specific configuration
 */
struct chacha20_config : encryption_config {
    chacha20_config() {
        algorithm = encryption_algorithm::chacha20_poly1305;
        use_aead = true;
    }

    /// Nonce size (default: 12 bytes / 96 bits)
    std::size_t nonce_size = CHACHA20_NONCE_SIZE;

    /// Generate random nonce for each encryption
    bool random_nonce = true;
};

/**
 * @brief Encryption configuration builder
 */
class encryption_config_builder {
public:
    /**
     * @brief Start building AES-256-GCM configuration (recommended)
     */
    static auto aes_gcm() -> encryption_config_builder {
        encryption_config_builder builder;
        builder.aes_gcm_config_ = aes_gcm_config{};
        return builder;
    }

    /**
     * @brief Start building AES-256-CBC configuration (legacy)
     */
    static auto aes_cbc() -> encryption_config_builder {
        encryption_config_builder builder;
        builder.aes_cbc_config_ = aes_cbc_config{};
        return builder;
    }

    /**
     * @brief Start building ChaCha20-Poly1305 configuration
     */
    static auto chacha20() -> encryption_config_builder {
        encryption_config_builder builder;
        builder.chacha20_config_ = chacha20_config{};
        return builder;
    }

    // Common options
    auto with_stream_chunk_size(std::size_t size) -> encryption_config_builder& {
        if (aes_gcm_config_.has_value()) {
            aes_gcm_config_->stream_chunk_size = size;
        } else if (aes_cbc_config_.has_value()) {
            aes_cbc_config_->stream_chunk_size = size;
        } else if (chacha20_config_.has_value()) {
            chacha20_config_->stream_chunk_size = size;
        }
        return *this;
    }

    auto with_aad(std::vector<std::byte> aad) -> encryption_config_builder& {
        if (aes_gcm_config_.has_value()) {
            aes_gcm_config_->aad = std::move(aad);
        } else if (aes_cbc_config_.has_value()) {
            aes_cbc_config_->aad = std::move(aad);
        } else if (chacha20_config_.has_value()) {
            chacha20_config_->aad = std::move(aad);
        }
        return *this;
    }

    auto with_secure_memory(bool enable) -> encryption_config_builder& {
        if (aes_gcm_config_.has_value()) {
            aes_gcm_config_->secure_memory = enable;
        } else if (aes_cbc_config_.has_value()) {
            aes_cbc_config_->secure_memory = enable;
        } else if (chacha20_config_.has_value()) {
            chacha20_config_->secure_memory = enable;
        }
        return *this;
    }

    // AES-GCM specific options
    auto with_iv_size(std::size_t size) -> encryption_config_builder& {
        if (aes_gcm_config_.has_value()) {
            aes_gcm_config_->iv_size = size;
        }
        return *this;
    }

    auto with_tag_size(std::size_t size) -> encryption_config_builder& {
        if (aes_gcm_config_.has_value()) {
            aes_gcm_config_->tag_size = size;
        }
        return *this;
    }

    auto with_random_iv(bool enable) -> encryption_config_builder& {
        if (aes_gcm_config_.has_value()) {
            aes_gcm_config_->random_iv = enable;
        }
        return *this;
    }

    // AES-CBC specific options
    auto with_hmac(bool enable) -> encryption_config_builder& {
        if (aes_cbc_config_.has_value()) {
            aes_cbc_config_->use_hmac = enable;
        }
        return *this;
    }

    // ChaCha20 specific options
    auto with_random_nonce(bool enable) -> encryption_config_builder& {
        if (chacha20_config_.has_value()) {
            chacha20_config_->random_nonce = enable;
        }
        return *this;
    }

    /**
     * @brief Build AES-GCM configuration
     * @return AES-GCM configuration
     */
    [[nodiscard]] auto build_aes_gcm() const -> aes_gcm_config {
        if (aes_gcm_config_.has_value()) {
            return aes_gcm_config_.value();
        }
        return aes_gcm_config{};
    }

    /**
     * @brief Build AES-CBC configuration
     * @return AES-CBC configuration
     */
    [[nodiscard]] auto build_aes_cbc() const -> aes_cbc_config {
        if (aes_cbc_config_.has_value()) {
            return aes_cbc_config_.value();
        }
        return aes_cbc_config{};
    }

    /**
     * @brief Build ChaCha20 configuration
     * @return ChaCha20 configuration
     */
    [[nodiscard]] auto build_chacha20() const -> chacha20_config {
        if (chacha20_config_.has_value()) {
            return chacha20_config_.value();
        }
        return chacha20_config{};
    }

private:
    std::optional<aes_gcm_config> aes_gcm_config_;
    std::optional<aes_cbc_config> aes_cbc_config_;
    std::optional<chacha20_config> chacha20_config_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_ENCRYPTION_ENCRYPTION_CONFIG_H
