/**
 * @file key_manager.h
 * @brief Secure key management system for encryption
 * @version 0.1.0
 *
 * This file implements a secure key management system for encryption key
 * generation, derivation, storage, and exchange.
 */

#ifndef KCENON_FILE_TRANSFER_ENCRYPTION_KEY_MANAGER_H
#define KCENON_FILE_TRANSFER_ENCRYPTION_KEY_MANAGER_H

#ifdef FILE_TRANS_ENABLE_ENCRYPTION

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "encryption_config.h"
#include "key_derivation.h"
#include "kcenon/file_transfer/core/types.h"

namespace kcenon::file_transfer {

/**
 * @brief Key metadata for tracking and management
 */
struct key_metadata {
    /// Unique identifier for the key
    std::string key_id;

    /// Human-readable description
    std::string description;

    /// Key derivation parameters (if derived from password)
    std::optional<key_derivation_params> derivation_params;

    /// Creation timestamp
    std::chrono::system_clock::time_point created_at;

    /// Last used timestamp
    std::chrono::system_clock::time_point last_used_at;

    /// Expiration timestamp (optional)
    std::optional<std::chrono::system_clock::time_point> expires_at;

    /// Number of times the key has been used
    uint64_t usage_count = 0;

    /// Whether the key is currently active
    bool is_active = true;

    /// Key version for rotation tracking
    uint32_t version = 1;
};

/**
 * @brief Managed key with secure storage
 */
struct managed_key {
    /// Raw key bytes
    std::vector<std::byte> key;

    /// Key metadata
    key_metadata metadata;

    /// Algorithm this key is intended for
    encryption_algorithm algorithm = encryption_algorithm::aes_256_gcm;
};

/**
 * @brief Key rotation policy
 */
struct key_rotation_policy {
    /// Enable automatic rotation
    bool auto_rotate = false;

    /// Rotate after this many uses
    uint64_t max_uses = 1000000;

    /// Rotate after this duration
    std::chrono::hours max_age{24 * 30};  // 30 days default

    /// Keep this many old key versions
    uint32_t keep_versions = 3;
};

/**
 * @brief Key storage backend interface
 *
 * Abstract interface for different key storage implementations.
 * Implementations may use secure enclaves, HSMs, or file-based storage.
 */
class key_storage_interface {
public:
    virtual ~key_storage_interface() = default;

    key_storage_interface(const key_storage_interface&) = delete;
    auto operator=(const key_storage_interface&) -> key_storage_interface& = delete;
    key_storage_interface(key_storage_interface&&) noexcept = default;
    auto operator=(key_storage_interface&&) noexcept -> key_storage_interface& = default;

    /**
     * @brief Store a key securely
     * @param key_id Unique identifier for the key
     * @param key_data Raw key bytes
     * @return Result indicating success or error
     */
    [[nodiscard]] virtual auto store(
        const std::string& key_id,
        std::span<const std::byte> key_data) -> result<void> = 0;

    /**
     * @brief Retrieve a key by ID
     * @param key_id Key identifier
     * @return Result containing key bytes or error
     */
    [[nodiscard]] virtual auto retrieve(
        const std::string& key_id) -> result<std::vector<std::byte>> = 0;

    /**
     * @brief Delete a key by ID
     * @param key_id Key identifier
     * @return Result indicating success or error
     */
    [[nodiscard]] virtual auto remove(
        const std::string& key_id) -> result<void> = 0;

    /**
     * @brief Check if a key exists
     * @param key_id Key identifier
     * @return true if key exists
     */
    [[nodiscard]] virtual auto exists(
        const std::string& key_id) -> bool = 0;

    /**
     * @brief List all stored key IDs
     * @return Vector of key identifiers
     */
    [[nodiscard]] virtual auto list_keys() -> std::vector<std::string> = 0;

protected:
    key_storage_interface() = default;
};

/**
 * @brief In-memory key storage (non-persistent)
 *
 * Stores keys in memory with secure zeroing on destruction.
 * Suitable for temporary keys or testing.
 */
class memory_key_storage : public key_storage_interface {
public:
    [[nodiscard]] static auto create() -> std::unique_ptr<memory_key_storage>;

    ~memory_key_storage() override;

    memory_key_storage(const memory_key_storage&) = delete;
    auto operator=(const memory_key_storage&) -> memory_key_storage& = delete;
    memory_key_storage(memory_key_storage&&) noexcept;
    auto operator=(memory_key_storage&&) noexcept -> memory_key_storage&;

    [[nodiscard]] auto store(
        const std::string& key_id,
        std::span<const std::byte> key_data) -> result<void> override;

    [[nodiscard]] auto retrieve(
        const std::string& key_id) -> result<std::vector<std::byte>> override;

    [[nodiscard]] auto remove(
        const std::string& key_id) -> result<void> override;

    [[nodiscard]] auto exists(
        const std::string& key_id) -> bool override;

    [[nodiscard]] auto list_keys() -> std::vector<std::string> override;

private:
    memory_key_storage();

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief Key manager for secure key lifecycle management
 *
 * Provides comprehensive key management including:
 * - Random key generation using CSPRNG
 * - Password-based key derivation (Argon2id/PBKDF2)
 * - Secure key storage
 * - Key rotation support
 * - Key exchange protocol helpers
 *
 * @code
 * // Create key manager with in-memory storage
 * auto storage = memory_key_storage::create();
 * auto manager = key_manager::create(std::move(storage));
 *
 * // Generate a random key
 * auto key_result = manager->generate_key("my-key");
 * if (key_result.has_value()) {
 *     auto& key = key_result.value();
 *     // Use key for encryption
 * }
 *
 * // Derive key from password
 * auto derived = manager->derive_key_from_password("my-password-key", "password");
 * if (derived.has_value()) {
 *     // Use derived key
 * }
 * @endcode
 */
class key_manager {
public:
    /**
     * @brief Create a key manager with the specified storage backend
     * @param storage Storage backend for keys
     * @return Key manager instance or nullptr on failure
     */
    [[nodiscard]] static auto create(
        std::unique_ptr<key_storage_interface> storage = nullptr) -> std::unique_ptr<key_manager>;

    ~key_manager();

    key_manager(const key_manager&) = delete;
    auto operator=(const key_manager&) -> key_manager& = delete;
    key_manager(key_manager&&) noexcept;
    auto operator=(key_manager&&) noexcept -> key_manager&;

    // ========================================================================
    // Key Generation
    // ========================================================================

    /**
     * @brief Generate a cryptographically secure random key
     * @param key_id Unique identifier for the key
     * @param key_size Key size in bytes (default: 32 for AES-256)
     * @param algorithm Target encryption algorithm
     * @return Result containing the managed key or error
     */
    [[nodiscard]] auto generate_key(
        const std::string& key_id,
        std::size_t key_size = AES_256_KEY_SIZE,
        encryption_algorithm algorithm = encryption_algorithm::aes_256_gcm)
        -> result<managed_key>;

    /**
     * @brief Generate random bytes using CSPRNG
     * @param size Number of bytes to generate
     * @return Result containing random bytes or error
     */
    [[nodiscard]] auto generate_random_bytes(
        std::size_t size) -> result<std::vector<std::byte>>;

    // ========================================================================
    // Password-based Key Derivation
    // ========================================================================

    /**
     * @brief Derive a key from password using Argon2id
     * @param key_id Unique identifier for the key
     * @param password Password to derive from
     * @param config Argon2 configuration (optional)
     * @return Result containing the managed key or error
     */
    [[nodiscard]] auto derive_key_from_password(
        const std::string& key_id,
        std::string_view password,
        const argon2_config& config = {}) -> result<managed_key>;

    /**
     * @brief Derive a key from password using PBKDF2
     * @param key_id Unique identifier for the key
     * @param password Password to derive from
     * @param config PBKDF2 configuration
     * @return Result containing the managed key or error
     */
    [[nodiscard]] auto derive_key_pbkdf2(
        const std::string& key_id,
        std::string_view password,
        const pbkdf2_config& config = {}) -> result<managed_key>;

    /**
     * @brief Re-derive a key using stored parameters
     * @param key_id Key identifier
     * @param password Password
     * @return Result containing the derived key or error
     */
    [[nodiscard]] auto rederive_key(
        const std::string& key_id,
        std::string_view password) -> result<managed_key>;

    // ========================================================================
    // Key Storage and Retrieval
    // ========================================================================

    /**
     * @brief Store a key in the storage backend
     * @param key Key to store
     * @return Result indicating success or error
     */
    [[nodiscard]] auto store_key(const managed_key& key) -> result<void>;

    /**
     * @brief Retrieve a key by ID
     * @param key_id Key identifier
     * @return Result containing the key or error
     */
    [[nodiscard]] auto get_key(const std::string& key_id) -> result<managed_key>;

    /**
     * @brief Delete a key from storage
     * @param key_id Key identifier
     * @return Result indicating success or error
     */
    [[nodiscard]] auto delete_key(const std::string& key_id) -> result<void>;

    /**
     * @brief Check if a key exists
     * @param key_id Key identifier
     * @return true if key exists
     */
    [[nodiscard]] auto key_exists(const std::string& key_id) -> bool;

    /**
     * @brief List all managed keys
     * @return Vector of key metadata
     */
    [[nodiscard]] auto list_keys() -> std::vector<key_metadata>;

    // ========================================================================
    // Key Rotation
    // ========================================================================

    /**
     * @brief Set key rotation policy
     * @param policy Rotation policy configuration
     */
    void set_rotation_policy(const key_rotation_policy& policy);

    /**
     * @brief Get current rotation policy
     * @return Current rotation policy
     */
    [[nodiscard]] auto get_rotation_policy() const -> key_rotation_policy;

    /**
     * @brief Rotate a key
     * @param key_id Key to rotate
     * @return Result containing the new key or error
     */
    [[nodiscard]] auto rotate_key(const std::string& key_id) -> result<managed_key>;

    /**
     * @brief Check if a key needs rotation
     * @param key_id Key identifier
     * @return true if key should be rotated
     */
    [[nodiscard]] auto needs_rotation(const std::string& key_id) -> bool;

    /**
     * @brief Get previous versions of a rotated key
     * @param key_id Key identifier
     * @return Vector of previous key versions
     */
    [[nodiscard]] auto get_key_versions(
        const std::string& key_id) -> std::vector<managed_key>;

    // ========================================================================
    // Key Exchange Helpers
    // ========================================================================

    /**
     * @brief Export key metadata for sharing (excludes actual key)
     * @param key_id Key identifier
     * @return Result containing serialized metadata or error
     */
    [[nodiscard]] auto export_key_metadata(
        const std::string& key_id) -> result<std::vector<std::byte>>;

    /**
     * @brief Import key metadata from serialized form
     * @param data Serialized metadata
     * @return Result containing metadata or error
     */
    [[nodiscard]] auto import_key_metadata(
        std::span<const std::byte> data) -> result<key_metadata>;

    // ========================================================================
    // Secure Memory Operations
    // ========================================================================

    /**
     * @brief Securely zero a memory region
     * @param data Data to zero
     *
     * Uses platform-specific secure zeroing to prevent compiler optimization.
     */
    static void secure_zero(std::span<std::byte> data);

    /**
     * @brief Compare two byte sequences in constant time
     * @param a First sequence
     * @param b Second sequence
     * @return true if sequences are equal
     */
    [[nodiscard]] static auto constant_time_compare(
        std::span<const std::byte> a,
        std::span<const std::byte> b) -> bool;

    // ========================================================================
    // Usage Tracking
    // ========================================================================

    /**
     * @brief Record key usage
     * @param key_id Key identifier
     */
    void record_usage(const std::string& key_id);

    /**
     * @brief Get usage statistics for a key
     * @param key_id Key identifier
     * @return Key metadata with usage info
     */
    [[nodiscard]] auto get_usage_stats(
        const std::string& key_id) -> result<key_metadata>;

private:
    explicit key_manager(std::unique_ptr<key_storage_interface> storage);

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief PBKDF2 key derivation implementation
 */
class pbkdf2_key_derivation : public key_derivation_interface {
public:
    /**
     * @brief Create a PBKDF2 key derivation instance
     * @param config PBKDF2 configuration
     * @return Instance or nullptr on failure
     */
    [[nodiscard]] static auto create(
        const pbkdf2_config& config = {}) -> std::unique_ptr<pbkdf2_key_derivation>;

    ~pbkdf2_key_derivation() override;

    pbkdf2_key_derivation(const pbkdf2_key_derivation&) = delete;
    auto operator=(const pbkdf2_key_derivation&) -> pbkdf2_key_derivation& = delete;
    pbkdf2_key_derivation(pbkdf2_key_derivation&&) noexcept;
    auto operator=(pbkdf2_key_derivation&&) noexcept -> pbkdf2_key_derivation&;

    [[nodiscard]] auto type() const -> key_derivation_function override;

    [[nodiscard]] auto derive_key(
        std::string_view password,
        std::span<const std::byte> salt) -> result<derived_key> override;

    [[nodiscard]] auto derive_key(
        std::string_view password) -> result<derived_key> override;

    [[nodiscard]] auto derive_key(
        std::span<const std::byte> key_material,
        std::span<const std::byte> salt) -> result<derived_key> override;

    [[nodiscard]] auto derive_key_with_params(
        std::string_view password,
        const key_derivation_params& params) -> result<derived_key> override;

    [[nodiscard]] auto generate_salt(
        std::size_t length = SALT_SIZE) -> result<std::vector<std::byte>> override;

    [[nodiscard]] auto key_length() const -> std::size_t override;

    [[nodiscard]] auto salt_length() const -> std::size_t override;

    [[nodiscard]] auto validate_password(
        std::string_view password) -> result<void> override;

    void secure_zero(std::span<std::byte> data) override;

private:
    explicit pbkdf2_key_derivation(const pbkdf2_config& config);

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief Argon2id key derivation implementation
 *
 * Note: Argon2id requires libargon2. If not available, this will use
 * PBKDF2 as a fallback with a warning.
 */
class argon2_key_derivation : public key_derivation_interface {
public:
    /**
     * @brief Create an Argon2id key derivation instance
     * @param config Argon2 configuration
     * @return Instance or nullptr on failure
     */
    [[nodiscard]] static auto create(
        const argon2_config& config = {}) -> std::unique_ptr<argon2_key_derivation>;

    /**
     * @brief Check if Argon2id is available
     * @return true if Argon2id library is linked
     */
    [[nodiscard]] static auto is_available() -> bool;

    ~argon2_key_derivation() override;

    argon2_key_derivation(const argon2_key_derivation&) = delete;
    auto operator=(const argon2_key_derivation&) -> argon2_key_derivation& = delete;
    argon2_key_derivation(argon2_key_derivation&&) noexcept;
    auto operator=(argon2_key_derivation&&) noexcept -> argon2_key_derivation&;

    [[nodiscard]] auto type() const -> key_derivation_function override;

    [[nodiscard]] auto derive_key(
        std::string_view password,
        std::span<const std::byte> salt) -> result<derived_key> override;

    [[nodiscard]] auto derive_key(
        std::string_view password) -> result<derived_key> override;

    [[nodiscard]] auto derive_key(
        std::span<const std::byte> key_material,
        std::span<const std::byte> salt) -> result<derived_key> override;

    [[nodiscard]] auto derive_key_with_params(
        std::string_view password,
        const key_derivation_params& params) -> result<derived_key> override;

    [[nodiscard]] auto generate_salt(
        std::size_t length = SALT_SIZE) -> result<std::vector<std::byte>> override;

    [[nodiscard]] auto key_length() const -> std::size_t override;

    [[nodiscard]] auto salt_length() const -> std::size_t override;

    [[nodiscard]] auto validate_password(
        std::string_view password) -> result<void> override;

    void secure_zero(std::span<std::byte> data) override;

private:
    explicit argon2_key_derivation(const argon2_config& config);

    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace kcenon::file_transfer

#endif  // FILE_TRANS_ENABLE_ENCRYPTION

#endif  // KCENON_FILE_TRANSFER_ENCRYPTION_KEY_MANAGER_H
