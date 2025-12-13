/**
 * @file encryption_interface.h
 * @brief Encryption abstraction layer interface
 * @version 0.1.0
 *
 * This file defines the encryption abstraction interface that supports
 * multiple encryption algorithms with AES-256-GCM as the primary implementation.
 */

#ifndef KCENON_FILE_TRANSFER_ENCRYPTION_ENCRYPTION_INTERFACE_H
#define KCENON_FILE_TRANSFER_ENCRYPTION_ENCRYPTION_INTERFACE_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "encryption_config.h"
#include "key_derivation.h"
#include "kcenon/file_transfer/core/types.h"

namespace kcenon::file_transfer {

/**
 * @brief Encryption result containing encrypted data and metadata
 */
struct encryption_result {
    /// Encrypted data (ciphertext)
    std::vector<std::byte> ciphertext;

    /// Encryption metadata for decryption
    encryption_metadata metadata;
};

/**
 * @brief Decryption result containing original data
 */
struct decryption_result {
    /// Decrypted data (plaintext)
    std::vector<std::byte> plaintext;

    /// Original size before encryption
    uint64_t original_size;
};

/**
 * @brief Encryption statistics
 */
struct encryption_statistics {
    uint64_t bytes_encrypted = 0;       ///< Total bytes encrypted
    uint64_t bytes_decrypted = 0;       ///< Total bytes decrypted
    uint64_t encryption_ops = 0;        ///< Number of encryption operations
    uint64_t decryption_ops = 0;        ///< Number of decryption operations
    uint64_t errors = 0;                ///< Total errors
    std::chrono::microseconds total_encrypt_time{0};  ///< Total encryption time
    std::chrono::microseconds total_decrypt_time{0};  ///< Total decryption time
};

/**
 * @brief Encryption progress callback data
 */
struct encryption_progress {
    uint64_t bytes_processed;   ///< Bytes processed so far
    uint64_t total_bytes;       ///< Total bytes to process
    bool is_encryption;         ///< true for encryption, false for decryption

    [[nodiscard]] auto percentage() const -> double {
        if (total_bytes == 0) return 100.0;
        return static_cast<double>(bytes_processed) / static_cast<double>(total_bytes) * 100.0;
    }
};

/**
 * @brief Stream encryption context for large file processing
 *
 * Allows encrypting/decrypting large files in chunks without loading
 * the entire file into memory.
 */
class encryption_stream_context {
public:
    virtual ~encryption_stream_context() = default;

    /**
     * @brief Process next chunk of data
     * @param input Input data chunk
     * @return Result containing processed output or error
     */
    [[nodiscard]] virtual auto process_chunk(
        std::span<const std::byte> input) -> result<std::vector<std::byte>> = 0;

    /**
     * @brief Finalize the stream and get any remaining data
     * @return Result containing final output (including auth tag) or error
     */
    [[nodiscard]] virtual auto finalize() -> result<std::vector<std::byte>> = 0;

    /**
     * @brief Get the encryption metadata
     * @return Metadata (only fully populated after finalize for encryption)
     */
    [[nodiscard]] virtual auto get_metadata() const -> encryption_metadata = 0;

    /**
     * @brief Get bytes processed so far
     */
    [[nodiscard]] virtual auto bytes_processed() const -> uint64_t = 0;

    /**
     * @brief Check if stream is for encryption (vs decryption)
     */
    [[nodiscard]] virtual auto is_encryption() const -> bool = 0;
};

/**
 * @brief Encryption interface base class
 *
 * Provides an abstraction layer for different encryption algorithms.
 * All implementations must support both single-shot and streaming operations.
 *
 * Security Requirements:
 * - All implementations must use constant-time comparison for authentication
 * - Sensitive data must be zeroed after use when secure_memory is enabled
 * - IVs/nonces must never be reused with the same key
 *
 * @code
 * // Example usage with AES-256-GCM
 * auto encryptor = aes_gcm_encryption::create(aes_gcm_config{});
 * if (encryptor) {
 *     // Set up with password-derived key
 *     auto kdf = argon2_key_derivation::create({});
 *     auto derived = kdf->derive_key("password");
 *     encryptor->set_key(derived.value().key);
 *
 *     // Encrypt data
 *     auto result = encryptor->encrypt(plaintext);
 *     if (result.has_value()) {
 *         auto encrypted = result.value();
 *         // Store encrypted.ciphertext and encrypted.metadata
 *     }
 * }
 * @endcode
 */
class encryption_interface {
public:
    virtual ~encryption_interface() = default;

    // Non-copyable
    encryption_interface(const encryption_interface&) = delete;
    auto operator=(const encryption_interface&) -> encryption_interface& = delete;

    // Movable
    encryption_interface(encryption_interface&&) noexcept = default;
    auto operator=(encryption_interface&&) noexcept -> encryption_interface& = default;

    /**
     * @brief Get the encryption algorithm type
     * @return Algorithm type
     */
    [[nodiscard]] virtual auto algorithm() const -> encryption_algorithm = 0;

    /**
     * @brief Get the algorithm name as string
     * @return Algorithm name (e.g., "aes-256-gcm")
     */
    [[nodiscard]] virtual auto algorithm_name() const -> std::string_view = 0;

    // ========================================================================
    // Key Management
    // ========================================================================

    /**
     * @brief Set encryption key directly
     * @param key Raw key bytes (must be correct size for algorithm)
     * @return Result indicating success or error
     */
    [[nodiscard]] virtual auto set_key(
        std::span<const std::byte> key) -> result<void> = 0;

    /**
     * @brief Set encryption key from derived key
     * @param derived Derived key from KDF
     * @return Result indicating success or error
     */
    [[nodiscard]] virtual auto set_key(
        const derived_key& derived) -> result<void> = 0;

    /**
     * @brief Check if a key has been set
     * @return true if key is set, false otherwise
     */
    [[nodiscard]] virtual auto has_key() const -> bool = 0;

    /**
     * @brief Clear the current key from memory
     */
    virtual void clear_key() = 0;

    /**
     * @brief Get the required key size for this algorithm
     * @return Key size in bytes
     */
    [[nodiscard]] virtual auto key_size() const -> std::size_t = 0;

    // ========================================================================
    // Single-shot Encryption/Decryption
    // ========================================================================

    /**
     * @brief Encrypt data (synchronous)
     * @param plaintext Data to encrypt
     * @param aad Optional additional authenticated data
     * @return Result containing encrypted data and metadata or error
     */
    [[nodiscard]] virtual auto encrypt(
        std::span<const std::byte> plaintext,
        std::span<const std::byte> aad = {}) -> result<encryption_result> = 0;

    /**
     * @brief Decrypt data (synchronous)
     * @param ciphertext Encrypted data
     * @param metadata Encryption metadata
     * @return Result containing decrypted data or error
     */
    [[nodiscard]] virtual auto decrypt(
        std::span<const std::byte> ciphertext,
        const encryption_metadata& metadata) -> result<decryption_result> = 0;

    /**
     * @brief Encrypt data (asynchronous)
     * @param plaintext Data to encrypt
     * @param aad Optional additional authenticated data
     * @return Future containing encrypted data and metadata or error
     */
    [[nodiscard]] virtual auto encrypt_async(
        std::span<const std::byte> plaintext,
        std::span<const std::byte> aad = {}) -> std::future<result<encryption_result>> = 0;

    /**
     * @brief Decrypt data (asynchronous)
     * @param ciphertext Encrypted data
     * @param metadata Encryption metadata
     * @return Future containing decrypted data or error
     */
    [[nodiscard]] virtual auto decrypt_async(
        std::span<const std::byte> ciphertext,
        const encryption_metadata& metadata) -> std::future<result<decryption_result>> = 0;

    // ========================================================================
    // Streaming Encryption/Decryption
    // ========================================================================

    /**
     * @brief Create a streaming encryption context
     * @param total_size Total size of data to encrypt (for progress reporting)
     * @param aad Optional additional authenticated data
     * @return Stream context or nullptr on failure
     */
    [[nodiscard]] virtual auto create_encrypt_stream(
        uint64_t total_size = 0,
        std::span<const std::byte> aad = {}) -> std::unique_ptr<encryption_stream_context> = 0;

    /**
     * @brief Create a streaming decryption context
     * @param metadata Encryption metadata for decryption
     * @return Stream context or nullptr on failure
     */
    [[nodiscard]] virtual auto create_decrypt_stream(
        const encryption_metadata& metadata) -> std::unique_ptr<encryption_stream_context> = 0;

    // ========================================================================
    // Chunk-based Encryption (for file transfer)
    // ========================================================================

    /**
     * @brief Encrypt a file chunk
     * @param chunk_data Chunk data to encrypt
     * @param chunk_index Chunk index (used for IV derivation if needed)
     * @return Result containing encrypted chunk and per-chunk metadata
     */
    [[nodiscard]] virtual auto encrypt_chunk(
        std::span<const std::byte> chunk_data,
        uint64_t chunk_index) -> result<encryption_result> = 0;

    /**
     * @brief Decrypt a file chunk
     * @param encrypted_chunk Encrypted chunk data
     * @param metadata Per-chunk encryption metadata
     * @param chunk_index Chunk index
     * @return Result containing decrypted chunk data
     */
    [[nodiscard]] virtual auto decrypt_chunk(
        std::span<const std::byte> encrypted_chunk,
        const encryption_metadata& metadata,
        uint64_t chunk_index) -> result<decryption_result> = 0;

    // ========================================================================
    // State and Statistics
    // ========================================================================

    /**
     * @brief Get current encryption state
     * @return Current state
     */
    [[nodiscard]] virtual auto state() const -> encryption_state = 0;

    /**
     * @brief Get encryption statistics
     * @return Current statistics
     */
    [[nodiscard]] virtual auto get_statistics() const -> encryption_statistics = 0;

    /**
     * @brief Reset statistics counters
     */
    virtual void reset_statistics() = 0;

    /**
     * @brief Get encryption configuration
     * @return Configuration reference
     */
    [[nodiscard]] virtual auto config() const -> const encryption_config& = 0;

    // ========================================================================
    // Progress Callback
    // ========================================================================

    /**
     * @brief Set progress callback for long operations
     * @param callback Function called periodically during encryption/decryption
     */
    virtual void on_progress(
        std::function<void(const encryption_progress&)> callback) = 0;

    // ========================================================================
    // Utility Functions
    // ========================================================================

    /**
     * @brief Generate a random IV/nonce appropriate for this algorithm
     * @return Result containing random IV or error
     */
    [[nodiscard]] virtual auto generate_iv() -> result<std::vector<std::byte>> = 0;

    /**
     * @brief Verify authentication tag (for AEAD algorithms)
     * @param ciphertext Encrypted data
     * @param metadata Encryption metadata with tag
     * @return true if tag is valid, false otherwise
     */
    [[nodiscard]] virtual auto verify_tag(
        std::span<const std::byte> ciphertext,
        const encryption_metadata& metadata) -> bool = 0;

    /**
     * @brief Get the IV/nonce size for this algorithm
     * @return IV size in bytes
     */
    [[nodiscard]] virtual auto iv_size() const -> std::size_t = 0;

    /**
     * @brief Get the authentication tag size for this algorithm
     * @return Tag size in bytes (0 if not AEAD)
     */
    [[nodiscard]] virtual auto tag_size() const -> std::size_t = 0;

    /**
     * @brief Calculate the ciphertext size for a given plaintext size
     * @param plaintext_size Size of plaintext in bytes
     * @return Expected ciphertext size in bytes
     */
    [[nodiscard]] virtual auto calculate_ciphertext_size(
        std::size_t plaintext_size) const -> std::size_t = 0;

protected:
    encryption_interface() = default;
};

/**
 * @brief Encryption factory interface
 *
 * Creates encryption instances based on configuration.
 */
class encryption_factory {
public:
    virtual ~encryption_factory() = default;

    /**
     * @brief Create an AES-256-GCM encryption instance
     * @param config AES-GCM configuration
     * @return Encryption instance or nullptr on failure
     */
    [[nodiscard]] virtual auto create_aes_gcm(
        const aes_gcm_config& config = {}) -> std::unique_ptr<encryption_interface> = 0;

    /**
     * @brief Create an AES-256-CBC encryption instance
     * @param config AES-CBC configuration
     * @return Encryption instance or nullptr on failure
     */
    [[nodiscard]] virtual auto create_aes_cbc(
        const aes_cbc_config& config = {}) -> std::unique_ptr<encryption_interface> = 0;

    /**
     * @brief Create a ChaCha20-Poly1305 encryption instance
     * @param config ChaCha20 configuration
     * @return Encryption instance or nullptr on failure
     */
    [[nodiscard]] virtual auto create_chacha20(
        const chacha20_config& config = {}) -> std::unique_ptr<encryption_interface> = 0;

    /**
     * @brief Create an encryption instance from metadata
     * @param metadata Previously stored metadata
     * @return Encryption instance configured for decryption
     */
    [[nodiscard]] virtual auto create_from_metadata(
        const encryption_metadata& metadata) -> std::unique_ptr<encryption_interface> = 0;

    /**
     * @brief Get supported encryption algorithms
     * @return Vector of supported algorithms
     */
    [[nodiscard]] virtual auto supported_algorithms() const
        -> std::vector<encryption_algorithm> = 0;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_ENCRYPTION_ENCRYPTION_INTERFACE_H
