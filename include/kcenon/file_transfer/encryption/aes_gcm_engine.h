/**
 * @file aes_gcm_engine.h
 * @brief AES-256-GCM encryption engine implementation
 * @version 0.1.0
 *
 * This file implements the AES-256-GCM encryption algorithm with authenticated
 * encryption for secure file transfers.
 */

#ifndef KCENON_FILE_TRANSFER_ENCRYPTION_AES_GCM_ENGINE_H
#define KCENON_FILE_TRANSFER_ENCRYPTION_AES_GCM_ENGINE_H

#ifdef FILE_TRANS_ENABLE_ENCRYPTION

#include <atomic>
#include <memory>
#include <mutex>

#include "encryption_config.h"
#include "encryption_interface.h"

namespace kcenon::file_transfer {

/**
 * @brief AES-256-GCM encryption stream context implementation
 *
 * Handles streaming encryption/decryption for large files.
 */
class aes_gcm_stream_context : public encryption_stream_context {
public:
    ~aes_gcm_stream_context() override;

    aes_gcm_stream_context(const aes_gcm_stream_context&) = delete;
    auto operator=(const aes_gcm_stream_context&) -> aes_gcm_stream_context& = delete;
    aes_gcm_stream_context(aes_gcm_stream_context&&) noexcept;
    auto operator=(aes_gcm_stream_context&&) noexcept -> aes_gcm_stream_context&;

    [[nodiscard]] auto process_chunk(
        std::span<const std::byte> input) -> result<std::vector<std::byte>> override;

    [[nodiscard]] auto finalize() -> result<std::vector<std::byte>> override;

    [[nodiscard]] auto get_metadata() const -> encryption_metadata override;

    [[nodiscard]] auto bytes_processed() const -> uint64_t override;

    [[nodiscard]] auto is_encryption() const -> bool override;

private:
    friend class aes_gcm_engine;

    aes_gcm_stream_context(
        std::span<const std::byte> key,
        bool encrypting,
        uint64_t total_size,
        std::span<const std::byte> aad,
        const aes_gcm_config& config);

    aes_gcm_stream_context(
        std::span<const std::byte> key,
        const encryption_metadata& metadata,
        const aes_gcm_config& config);

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief AES-256-GCM encryption engine
 *
 * Implements AES-256-GCM authenticated encryption with:
 * - 256-bit key strength
 * - 96-bit IV (recommended by NIST)
 * - 128-bit authentication tag
 * - Counter-based IV generation to prevent reuse
 * - Hardware AES acceleration support (AES-NI)
 *
 * @code
 * // Create AES-GCM engine
 * auto engine = aes_gcm_engine::create(aes_gcm_config{});
 * if (engine) {
 *     // Set up encryption key
 *     engine->set_key(key_bytes);
 *
 *     // Encrypt data
 *     auto result = engine->encrypt(plaintext);
 *     if (result.has_value()) {
 *         auto encrypted = result.value();
 *         // Use encrypted.ciphertext and encrypted.metadata
 *     }
 * }
 * @endcode
 */
class aes_gcm_engine : public encryption_interface {
public:
    /**
     * @brief Create an AES-256-GCM encryption engine
     * @param config Configuration options
     * @return Unique pointer to engine instance, or nullptr on failure
     */
    [[nodiscard]] static auto create(
        const aes_gcm_config& config = {}) -> std::unique_ptr<aes_gcm_engine>;

    ~aes_gcm_engine() override;

    aes_gcm_engine(const aes_gcm_engine&) = delete;
    auto operator=(const aes_gcm_engine&) -> aes_gcm_engine& = delete;
    aes_gcm_engine(aes_gcm_engine&&) noexcept;
    auto operator=(aes_gcm_engine&&) noexcept -> aes_gcm_engine&;

    // ========================================================================
    // Algorithm Info
    // ========================================================================

    [[nodiscard]] auto algorithm() const -> encryption_algorithm override;
    [[nodiscard]] auto algorithm_name() const -> std::string_view override;

    // ========================================================================
    // Key Management
    // ========================================================================

    [[nodiscard]] auto set_key(
        std::span<const std::byte> key) -> result<void> override;

    [[nodiscard]] auto set_key(
        const derived_key& derived) -> result<void> override;

    [[nodiscard]] auto has_key() const -> bool override;

    void clear_key() override;

    [[nodiscard]] auto key_size() const -> std::size_t override;

    // ========================================================================
    // Single-shot Encryption/Decryption
    // ========================================================================

    [[nodiscard]] auto encrypt(
        std::span<const std::byte> plaintext,
        std::span<const std::byte> aad = {}) -> result<encryption_result> override;

    [[nodiscard]] auto decrypt(
        std::span<const std::byte> ciphertext,
        const encryption_metadata& metadata) -> result<decryption_result> override;

    [[nodiscard]] auto encrypt_async(
        std::span<const std::byte> plaintext,
        std::span<const std::byte> aad = {}) -> std::future<result<encryption_result>> override;

    [[nodiscard]] auto decrypt_async(
        std::span<const std::byte> ciphertext,
        const encryption_metadata& metadata) -> std::future<result<decryption_result>> override;

    // ========================================================================
    // Streaming Encryption/Decryption
    // ========================================================================

    [[nodiscard]] auto create_encrypt_stream(
        uint64_t total_size = 0,
        std::span<const std::byte> aad = {}) -> std::unique_ptr<encryption_stream_context> override;

    [[nodiscard]] auto create_decrypt_stream(
        const encryption_metadata& metadata) -> std::unique_ptr<encryption_stream_context> override;

    // ========================================================================
    // Chunk-based Encryption
    // ========================================================================

    [[nodiscard]] auto encrypt_chunk(
        std::span<const std::byte> chunk_data,
        uint64_t chunk_index) -> result<encryption_result> override;

    [[nodiscard]] auto decrypt_chunk(
        std::span<const std::byte> encrypted_chunk,
        const encryption_metadata& metadata,
        uint64_t chunk_index) -> result<decryption_result> override;

    // ========================================================================
    // State and Statistics
    // ========================================================================

    [[nodiscard]] auto state() const -> encryption_state override;

    [[nodiscard]] auto get_statistics() const -> encryption_statistics override;

    void reset_statistics() override;

    [[nodiscard]] auto config() const -> const encryption_config& override;

    // ========================================================================
    // Progress Callback
    // ========================================================================

    void on_progress(
        std::function<void(const encryption_progress&)> callback) override;

    // ========================================================================
    // Utility Functions
    // ========================================================================

    [[nodiscard]] auto generate_iv() -> result<std::vector<std::byte>> override;

    [[nodiscard]] auto verify_tag(
        std::span<const std::byte> ciphertext,
        const encryption_metadata& metadata) -> bool override;

    [[nodiscard]] auto iv_size() const -> std::size_t override;

    [[nodiscard]] auto tag_size() const -> std::size_t override;

    [[nodiscard]] auto calculate_ciphertext_size(
        std::size_t plaintext_size) const -> std::size_t override;

private:
    explicit aes_gcm_engine(const aes_gcm_config& config);

    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace kcenon::file_transfer

#endif  // FILE_TRANS_ENABLE_ENCRYPTION

#endif  // KCENON_FILE_TRANSFER_ENCRYPTION_AES_GCM_ENGINE_H
