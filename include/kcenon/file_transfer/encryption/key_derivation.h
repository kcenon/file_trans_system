/**
 * @file key_derivation.h
 * @brief Key derivation function interfaces and configurations
 * @version 0.1.0
 *
 * This file defines the key derivation interface for deriving encryption keys
 * from passwords or other key material.
 */

#ifndef KCENON_FILE_TRANSFER_ENCRYPTION_KEY_DERIVATION_H
#define KCENON_FILE_TRANSFER_ENCRYPTION_KEY_DERIVATION_H

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "encryption_config.h"
#include "kcenon/file_transfer/core/types.h"

namespace kcenon::file_transfer {

// Default parameters for key derivation functions
// These are security-sensitive and should be chosen carefully

/// PBKDF2 recommended minimum iterations (OWASP 2023)
inline constexpr uint32_t PBKDF2_DEFAULT_ITERATIONS = 600000;

/// Argon2id recommended memory cost (64 MB)
inline constexpr uint32_t ARGON2_DEFAULT_MEMORY_KB = 65536;

/// Argon2id recommended time cost (iterations)
inline constexpr uint32_t ARGON2_DEFAULT_TIME_COST = 3;

/// Argon2id recommended parallelism
inline constexpr uint32_t ARGON2_DEFAULT_PARALLELISM = 4;

/// scrypt recommended N parameter (2^17)
inline constexpr uint32_t SCRYPT_DEFAULT_N = 131072;

/// scrypt recommended r parameter
inline constexpr uint32_t SCRYPT_DEFAULT_R = 8;

/// scrypt recommended p parameter
inline constexpr uint32_t SCRYPT_DEFAULT_P = 1;

/**
 * @brief PBKDF2 configuration
 */
struct pbkdf2_config {
    /// Number of iterations (higher = slower but more secure)
    uint32_t iterations = PBKDF2_DEFAULT_ITERATIONS;

    /// Hash algorithm (currently only SHA-256 supported)
    std::string hash_algorithm = "SHA-256";

    /// Output key length in bytes
    std::size_t key_length = AES_256_KEY_SIZE;

    /// Salt length in bytes
    std::size_t salt_length = SALT_SIZE;
};

/**
 * @brief Argon2id configuration
 */
struct argon2_config {
    /// Memory cost in KB
    uint32_t memory_kb = ARGON2_DEFAULT_MEMORY_KB;

    /// Time cost (number of iterations)
    uint32_t time_cost = ARGON2_DEFAULT_TIME_COST;

    /// Parallelism (number of threads)
    uint32_t parallelism = ARGON2_DEFAULT_PARALLELISM;

    /// Output key length in bytes
    std::size_t key_length = AES_256_KEY_SIZE;

    /// Salt length in bytes
    std::size_t salt_length = SALT_SIZE;
};

/**
 * @brief scrypt configuration
 */
struct scrypt_config {
    /// CPU/memory cost parameter (must be power of 2)
    uint32_t n = SCRYPT_DEFAULT_N;

    /// Block size parameter
    uint32_t r = SCRYPT_DEFAULT_R;

    /// Parallelization parameter
    uint32_t p = SCRYPT_DEFAULT_P;

    /// Output key length in bytes
    std::size_t key_length = AES_256_KEY_SIZE;

    /// Salt length in bytes
    std::size_t salt_length = SALT_SIZE;
};

/**
 * @brief Key derivation parameters for storing with encrypted data
 */
struct key_derivation_params {
    key_derivation_function kdf = key_derivation_function::argon2id;

    /// Salt used for key derivation
    std::vector<std::byte> salt;

    /// PBKDF2/Argon2 iterations or scrypt N
    uint32_t iterations = 0;

    /// Argon2 memory cost in KB
    uint32_t memory_kb = 0;

    /// Argon2 parallelism or scrypt p
    uint32_t parallelism = 0;

    /// scrypt r parameter
    uint32_t block_size = 0;

    /// Output key length
    std::size_t key_length = AES_256_KEY_SIZE;
};

/**
 * @brief Derived key result
 */
struct derived_key {
    /// The derived key bytes
    std::vector<std::byte> key;

    /// Parameters used for derivation (for storage)
    key_derivation_params params;
};

/**
 * @brief Key derivation interface
 *
 * Provides an abstraction layer for different key derivation functions.
 * All implementations must securely handle password material and
 * zero memory after use.
 *
 * @code
 * // Example usage with Argon2id
 * auto kdf = argon2_key_derivation::create(argon2_config{});
 * if (kdf) {
 *     auto result = kdf->derive_key("password", salt);
 *     if (result.has_value()) {
 *         auto key = result.value();
 *         // Use key for encryption
 *     }
 * }
 * @endcode
 */
class key_derivation_interface {
public:
    virtual ~key_derivation_interface() = default;

    // Non-copyable
    key_derivation_interface(const key_derivation_interface&) = delete;
    auto operator=(const key_derivation_interface&) -> key_derivation_interface& = delete;

    // Movable
    key_derivation_interface(key_derivation_interface&&) noexcept = default;
    auto operator=(key_derivation_interface&&) noexcept -> key_derivation_interface& = default;

    /**
     * @brief Get the KDF type identifier
     * @return KDF type
     */
    [[nodiscard]] virtual auto type() const -> key_derivation_function = 0;

    /**
     * @brief Derive a key from password and salt
     * @param password Password string
     * @param salt Salt bytes
     * @return Result containing derived key or error
     */
    [[nodiscard]] virtual auto derive_key(
        std::string_view password,
        std::span<const std::byte> salt) -> result<derived_key> = 0;

    /**
     * @brief Derive a key from password, generating random salt
     * @param password Password string
     * @return Result containing derived key (with salt in params) or error
     */
    [[nodiscard]] virtual auto derive_key(
        std::string_view password) -> result<derived_key> = 0;

    /**
     * @brief Derive a key from raw key material and salt
     * @param key_material Input key material
     * @param salt Salt bytes
     * @return Result containing derived key or error
     */
    [[nodiscard]] virtual auto derive_key(
        std::span<const std::byte> key_material,
        std::span<const std::byte> salt) -> result<derived_key> = 0;

    /**
     * @brief Re-derive a key using stored parameters
     * @param password Password string
     * @param params Previously stored parameters
     * @return Result containing derived key or error
     */
    [[nodiscard]] virtual auto derive_key_with_params(
        std::string_view password,
        const key_derivation_params& params) -> result<derived_key> = 0;

    /**
     * @brief Generate a cryptographically secure random salt
     * @param length Salt length in bytes
     * @return Result containing random salt or error
     */
    [[nodiscard]] virtual auto generate_salt(
        std::size_t length = SALT_SIZE) -> result<std::vector<std::byte>> = 0;

    /**
     * @brief Get the configured output key length
     * @return Key length in bytes
     */
    [[nodiscard]] virtual auto key_length() const -> std::size_t = 0;

    /**
     * @brief Get the configured salt length
     * @return Salt length in bytes
     */
    [[nodiscard]] virtual auto salt_length() const -> std::size_t = 0;

    /**
     * @brief Validate password strength (optional)
     * @param password Password to validate
     * @return Result with void on success, error describing weakness on failure
     */
    [[nodiscard]] virtual auto validate_password(
        std::string_view password) -> result<void> = 0;

    /**
     * @brief Securely zero a memory region
     * @param data Data to zero
     *
     * This function zeros memory in a way that won't be optimized away
     * by the compiler, ensuring sensitive data is properly cleared.
     */
    virtual void secure_zero(std::span<std::byte> data) = 0;

protected:
    key_derivation_interface() = default;
};

/**
 * @brief Key derivation factory interface
 *
 * Creates key derivation instances based on configuration.
 */
class key_derivation_factory {
public:
    virtual ~key_derivation_factory() = default;

    /**
     * @brief Create a PBKDF2 key derivation instance
     * @param config PBKDF2 configuration
     * @return Key derivation instance or nullptr on failure
     */
    [[nodiscard]] virtual auto create_pbkdf2(
        const pbkdf2_config& config = {}) -> std::unique_ptr<key_derivation_interface> = 0;

    /**
     * @brief Create an Argon2id key derivation instance
     * @param config Argon2 configuration
     * @return Key derivation instance or nullptr on failure
     */
    [[nodiscard]] virtual auto create_argon2(
        const argon2_config& config = {}) -> std::unique_ptr<key_derivation_interface> = 0;

    /**
     * @brief Create a scrypt key derivation instance
     * @param config scrypt configuration
     * @return Key derivation instance or nullptr on failure
     */
    [[nodiscard]] virtual auto create_scrypt(
        const scrypt_config& config = {}) -> std::unique_ptr<key_derivation_interface> = 0;

    /**
     * @brief Create a key derivation instance from stored parameters
     * @param params Previously stored parameters
     * @return Key derivation instance configured with the same parameters
     */
    [[nodiscard]] virtual auto create_from_params(
        const key_derivation_params& params) -> std::unique_ptr<key_derivation_interface> = 0;

    /**
     * @brief Get supported KDF types
     * @return Vector of supported KDF types
     */
    [[nodiscard]] virtual auto supported_types() const
        -> std::vector<key_derivation_function> = 0;
};

/**
 * @brief Key derivation configuration builder
 */
class key_derivation_config_builder {
public:
    /**
     * @brief Start building PBKDF2 configuration
     */
    static auto pbkdf2() -> key_derivation_config_builder {
        key_derivation_config_builder builder;
        builder.pbkdf2_config_ = pbkdf2_config{};
        return builder;
    }

    /**
     * @brief Start building Argon2id configuration (recommended)
     */
    static auto argon2() -> key_derivation_config_builder {
        key_derivation_config_builder builder;
        builder.argon2_config_ = argon2_config{};
        return builder;
    }

    /**
     * @brief Start building scrypt configuration
     */
    static auto scrypt() -> key_derivation_config_builder {
        key_derivation_config_builder builder;
        builder.scrypt_config_ = scrypt_config{};
        return builder;
    }

    // PBKDF2 options
    auto with_iterations(uint32_t iterations) -> key_derivation_config_builder& {
        if (pbkdf2_config_.has_value()) {
            pbkdf2_config_->iterations = iterations;
        }
        return *this;
    }

    // Argon2 options
    auto with_memory(uint32_t memory_kb) -> key_derivation_config_builder& {
        if (argon2_config_.has_value()) {
            argon2_config_->memory_kb = memory_kb;
        }
        return *this;
    }

    auto with_time_cost(uint32_t time_cost) -> key_derivation_config_builder& {
        if (argon2_config_.has_value()) {
            argon2_config_->time_cost = time_cost;
        }
        return *this;
    }

    auto with_parallelism(uint32_t parallelism) -> key_derivation_config_builder& {
        if (argon2_config_.has_value()) {
            argon2_config_->parallelism = parallelism;
        }
        return *this;
    }

    // scrypt options
    auto with_scrypt_n(uint32_t n) -> key_derivation_config_builder& {
        if (scrypt_config_.has_value()) {
            scrypt_config_->n = n;
        }
        return *this;
    }

    auto with_scrypt_r(uint32_t r) -> key_derivation_config_builder& {
        if (scrypt_config_.has_value()) {
            scrypt_config_->r = r;
        }
        return *this;
    }

    auto with_scrypt_p(uint32_t p) -> key_derivation_config_builder& {
        if (scrypt_config_.has_value()) {
            scrypt_config_->p = p;
        }
        return *this;
    }

    // Common options
    auto with_key_length(std::size_t length) -> key_derivation_config_builder& {
        if (pbkdf2_config_.has_value()) {
            pbkdf2_config_->key_length = length;
        } else if (argon2_config_.has_value()) {
            argon2_config_->key_length = length;
        } else if (scrypt_config_.has_value()) {
            scrypt_config_->key_length = length;
        }
        return *this;
    }

    auto with_salt_length(std::size_t length) -> key_derivation_config_builder& {
        if (pbkdf2_config_.has_value()) {
            pbkdf2_config_->salt_length = length;
        } else if (argon2_config_.has_value()) {
            argon2_config_->salt_length = length;
        } else if (scrypt_config_.has_value()) {
            scrypt_config_->salt_length = length;
        }
        return *this;
    }

    /**
     * @brief Build PBKDF2 configuration
     */
    [[nodiscard]] auto build_pbkdf2() const -> pbkdf2_config {
        if (pbkdf2_config_.has_value()) {
            return pbkdf2_config_.value();
        }
        return pbkdf2_config{};
    }

    /**
     * @brief Build Argon2 configuration
     */
    [[nodiscard]] auto build_argon2() const -> argon2_config {
        if (argon2_config_.has_value()) {
            return argon2_config_.value();
        }
        return argon2_config{};
    }

    /**
     * @brief Build scrypt configuration
     */
    [[nodiscard]] auto build_scrypt() const -> scrypt_config {
        if (scrypt_config_.has_value()) {
            return scrypt_config_.value();
        }
        return scrypt_config{};
    }

private:
    std::optional<pbkdf2_config> pbkdf2_config_;
    std::optional<argon2_config> argon2_config_;
    std::optional<scrypt_config> scrypt_config_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_ENCRYPTION_KEY_DERIVATION_H
