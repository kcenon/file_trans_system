/**
 * @file test_aes_gcm_engine.cpp
 * @brief Unit tests for AES-256-GCM encryption engine
 */

#ifdef FILE_TRANS_ENABLE_ENCRYPTION

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <thread>
#include <vector>

#include "kcenon/file_transfer/encryption/aes_gcm_engine.h"

namespace kcenon::file_transfer {
namespace {

// ============================================================================
// Test Fixtures
// ============================================================================

class AesGcmEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = aes_gcm_engine::create();
        ASSERT_NE(engine_, nullptr);

        // Generate a random 256-bit key
        key_.resize(AES_256_KEY_SIZE);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        for (auto& byte : key_) {
            byte = static_cast<std::byte>(dis(gen));
        }

        auto result = engine_->set_key(std::span<const std::byte>(key_));
        ASSERT_TRUE(result.has_value());
    }

    void TearDown() override {
        engine_->clear_key();
        engine_.reset();
    }

    auto generate_random_data(std::size_t size) -> std::vector<std::byte> {
        std::vector<std::byte> data(size);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        for (auto& byte : data) {
            byte = static_cast<std::byte>(dis(gen));
        }
        return data;
    }

    std::unique_ptr<aes_gcm_engine> engine_;
    std::vector<std::byte> key_;
};

// ============================================================================
// Creation Tests
// ============================================================================

TEST_F(AesGcmEngineTest, CreateWithDefaultConfig) {
    auto engine = aes_gcm_engine::create();
    ASSERT_NE(engine, nullptr);
    EXPECT_EQ(engine->algorithm(), encryption_algorithm::aes_256_gcm);
    EXPECT_EQ(engine->algorithm_name(), "aes-256-gcm");
}

TEST_F(AesGcmEngineTest, CreateWithCustomConfig) {
    aes_gcm_config config;
    config.iv_size = 16;
    config.tag_size = 12;
    config.secure_memory = false;

    auto engine = aes_gcm_engine::create(config);
    ASSERT_NE(engine, nullptr);
    EXPECT_EQ(engine->iv_size(), 16);
    EXPECT_EQ(engine->tag_size(), 12);
}

// ============================================================================
// Key Management Tests
// ============================================================================

TEST_F(AesGcmEngineTest, SetValidKey) {
    auto engine = aes_gcm_engine::create();
    ASSERT_NE(engine, nullptr);

    EXPECT_FALSE(engine->has_key());
    EXPECT_EQ(engine->state(), encryption_state::uninitialized);

    auto result = engine->set_key(std::span<const std::byte>(key_));
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(engine->has_key());
    EXPECT_EQ(engine->state(), encryption_state::ready);
}

TEST_F(AesGcmEngineTest, SetInvalidKeySize) {
    auto engine = aes_gcm_engine::create();
    ASSERT_NE(engine, nullptr);

    std::vector<std::byte> short_key(16);  // 128-bit key (invalid)
    auto result = engine->set_key(std::span<const std::byte>(short_key));
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(engine->has_key());
}

TEST_F(AesGcmEngineTest, ClearKey) {
    EXPECT_TRUE(engine_->has_key());
    engine_->clear_key();
    EXPECT_FALSE(engine_->has_key());
    EXPECT_EQ(engine_->state(), encryption_state::uninitialized);
}

TEST_F(AesGcmEngineTest, KeySize) {
    EXPECT_EQ(engine_->key_size(), AES_256_KEY_SIZE);
    EXPECT_EQ(engine_->key_size(), 32);
}

TEST_F(AesGcmEngineTest, SetKeyFromDerivedKey) {
    auto engine = aes_gcm_engine::create();
    ASSERT_NE(engine, nullptr);

    derived_key dk;
    dk.key = key_;
    dk.params.kdf = key_derivation_function::argon2id;

    auto result = engine->set_key(dk);
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(engine->has_key());
}

// ============================================================================
// Single-shot Encryption/Decryption Tests
// ============================================================================

TEST_F(AesGcmEngineTest, EncryptDecryptSmallData) {
    std::vector<std::byte> plaintext = {
        std::byte{0x48}, std::byte{0x65}, std::byte{0x6c}, std::byte{0x6c},
        std::byte{0x6f}, std::byte{0x21}  // "Hello!"
    };

    auto encrypt_result = engine_->encrypt(std::span<const std::byte>(plaintext));
    ASSERT_TRUE(encrypt_result.has_value());

    auto& encrypted = encrypt_result.value();
    EXPECT_FALSE(encrypted.ciphertext.empty());
    EXPECT_EQ(encrypted.metadata.algorithm, encryption_algorithm::aes_256_gcm);
    EXPECT_EQ(encrypted.metadata.iv.size(), AES_GCM_IV_SIZE);
    EXPECT_EQ(encrypted.metadata.auth_tag.size(), AES_GCM_TAG_SIZE);
    EXPECT_EQ(encrypted.metadata.original_size, plaintext.size());

    // Ciphertext should be different from plaintext
    EXPECT_NE(encrypted.ciphertext, plaintext);

    auto decrypt_result = engine_->decrypt(
        std::span<const std::byte>(encrypted.ciphertext),
        encrypted.metadata);
    ASSERT_TRUE(decrypt_result.has_value());

    auto& decrypted = decrypt_result.value();
    EXPECT_EQ(decrypted.plaintext, plaintext);
    EXPECT_EQ(decrypted.original_size, plaintext.size());
}

TEST_F(AesGcmEngineTest, EncryptDecryptLargeData) {
    auto plaintext = generate_random_data(1024 * 1024);  // 1 MB

    auto encrypt_result = engine_->encrypt(std::span<const std::byte>(plaintext));
    ASSERT_TRUE(encrypt_result.has_value());

    auto& encrypted = encrypt_result.value();
    EXPECT_EQ(encrypted.ciphertext.size(), plaintext.size());

    auto decrypt_result = engine_->decrypt(
        std::span<const std::byte>(encrypted.ciphertext),
        encrypted.metadata);
    ASSERT_TRUE(decrypt_result.has_value());

    EXPECT_EQ(decrypt_result.value().plaintext, plaintext);
}

TEST_F(AesGcmEngineTest, EncryptDecryptWithAad) {
    std::vector<std::byte> plaintext = generate_random_data(256);
    std::vector<std::byte> aad = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}
    };

    auto encrypt_result = engine_->encrypt(
        std::span<const std::byte>(plaintext),
        std::span<const std::byte>(aad));
    ASSERT_TRUE(encrypt_result.has_value());

    auto& encrypted = encrypt_result.value();
    EXPECT_EQ(encrypted.metadata.aad, aad);

    auto decrypt_result = engine_->decrypt(
        std::span<const std::byte>(encrypted.ciphertext),
        encrypted.metadata);
    ASSERT_TRUE(decrypt_result.has_value());

    EXPECT_EQ(decrypt_result.value().plaintext, plaintext);
}

TEST_F(AesGcmEngineTest, EncryptWithoutKey) {
    auto engine = aes_gcm_engine::create();
    std::vector<std::byte> plaintext = generate_random_data(64);

    auto result = engine->encrypt(std::span<const std::byte>(plaintext));
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(AesGcmEngineTest, DecryptTamperedData) {
    std::vector<std::byte> plaintext = generate_random_data(128);

    auto encrypt_result = engine_->encrypt(std::span<const std::byte>(plaintext));
    ASSERT_TRUE(encrypt_result.has_value());

    auto encrypted = encrypt_result.value();

    // Tamper with ciphertext
    if (!encrypted.ciphertext.empty()) {
        encrypted.ciphertext[0] ^= std::byte{0xFF};
    }

    auto decrypt_result = engine_->decrypt(
        std::span<const std::byte>(encrypted.ciphertext),
        encrypted.metadata);
    EXPECT_FALSE(decrypt_result.has_value());
    EXPECT_EQ(decrypt_result.error().code, error_code::chunk_checksum_error);
}

TEST_F(AesGcmEngineTest, DecryptTamperedTag) {
    std::vector<std::byte> plaintext = generate_random_data(128);

    auto encrypt_result = engine_->encrypt(std::span<const std::byte>(plaintext));
    ASSERT_TRUE(encrypt_result.has_value());

    auto encrypted = encrypt_result.value();

    // Tamper with auth tag
    if (!encrypted.metadata.auth_tag.empty()) {
        encrypted.metadata.auth_tag[0] ^= std::byte{0xFF};
    }

    auto decrypt_result = engine_->decrypt(
        std::span<const std::byte>(encrypted.ciphertext),
        encrypted.metadata);
    EXPECT_FALSE(decrypt_result.has_value());
}

TEST_F(AesGcmEngineTest, DecryptWrongAad) {
    std::vector<std::byte> plaintext = generate_random_data(128);
    std::vector<std::byte> aad = {std::byte{0x01}, std::byte{0x02}};

    auto encrypt_result = engine_->encrypt(
        std::span<const std::byte>(plaintext),
        std::span<const std::byte>(aad));
    ASSERT_TRUE(encrypt_result.has_value());

    auto encrypted = encrypt_result.value();

    // Modify AAD
    encrypted.metadata.aad[0] ^= std::byte{0xFF};

    auto decrypt_result = engine_->decrypt(
        std::span<const std::byte>(encrypted.ciphertext),
        encrypted.metadata);
    EXPECT_FALSE(decrypt_result.has_value());
}

// ============================================================================
// Chunk-based Encryption Tests
// ============================================================================

TEST_F(AesGcmEngineTest, EncryptDecryptChunk) {
    auto chunk_data = generate_random_data(65536);  // 64 KB chunk
    uint64_t chunk_index = 0;

    auto encrypt_result = engine_->encrypt_chunk(
        std::span<const std::byte>(chunk_data), chunk_index);
    ASSERT_TRUE(encrypt_result.has_value());

    auto& encrypted = encrypt_result.value();
    EXPECT_FALSE(encrypted.ciphertext.empty());
    EXPECT_EQ(encrypted.metadata.original_size, chunk_data.size());

    auto decrypt_result = engine_->decrypt_chunk(
        std::span<const std::byte>(encrypted.ciphertext),
        encrypted.metadata,
        chunk_index);
    ASSERT_TRUE(decrypt_result.has_value());

    EXPECT_EQ(decrypt_result.value().plaintext, chunk_data);
}

TEST_F(AesGcmEngineTest, EncryptMultipleChunksUniqueIVs) {
    auto chunk1 = generate_random_data(1024);
    auto chunk2 = generate_random_data(1024);
    auto chunk3 = generate_random_data(1024);

    auto result1 = engine_->encrypt_chunk(std::span<const std::byte>(chunk1), 0);
    auto result2 = engine_->encrypt_chunk(std::span<const std::byte>(chunk2), 1);
    auto result3 = engine_->encrypt_chunk(std::span<const std::byte>(chunk3), 2);

    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());
    ASSERT_TRUE(result3.has_value());

    // Each chunk should have a unique IV
    EXPECT_NE(result1.value().metadata.iv, result2.value().metadata.iv);
    EXPECT_NE(result2.value().metadata.iv, result3.value().metadata.iv);
    EXPECT_NE(result1.value().metadata.iv, result3.value().metadata.iv);
}

// ============================================================================
// Streaming Encryption Tests
// ============================================================================

TEST_F(AesGcmEngineTest, StreamingEncryptDecrypt) {
    auto total_data = generate_random_data(256 * 1024);  // 256 KB
    std::size_t chunk_size = 64 * 1024;  // 64 KB chunks

    // Create encryption stream
    auto encrypt_stream = engine_->create_encrypt_stream(total_data.size());
    ASSERT_NE(encrypt_stream, nullptr);
    EXPECT_TRUE(encrypt_stream->is_encryption());

    // Encrypt in chunks
    std::vector<std::byte> ciphertext;
    for (std::size_t offset = 0; offset < total_data.size(); offset += chunk_size) {
        std::size_t size = std::min(chunk_size, total_data.size() - offset);
        auto chunk = std::span<const std::byte>(total_data.data() + offset, size);

        auto result = encrypt_stream->process_chunk(chunk);
        ASSERT_TRUE(result.has_value());
        ciphertext.insert(ciphertext.end(),
                          result.value().begin(),
                          result.value().end());
    }

    // Finalize encryption
    auto final_result = encrypt_stream->finalize();
    ASSERT_TRUE(final_result.has_value());
    ciphertext.insert(ciphertext.end(),
                      final_result.value().begin(),
                      final_result.value().end());

    auto metadata = encrypt_stream->get_metadata();
    EXPECT_EQ(encrypt_stream->bytes_processed(), total_data.size());

    // Create decryption stream
    auto decrypt_stream = engine_->create_decrypt_stream(metadata);
    ASSERT_NE(decrypt_stream, nullptr);
    EXPECT_FALSE(decrypt_stream->is_encryption());

    // Decrypt in chunks
    std::vector<std::byte> plaintext;
    for (std::size_t offset = 0; offset < ciphertext.size(); offset += chunk_size) {
        std::size_t size = std::min(chunk_size, ciphertext.size() - offset);
        auto chunk = std::span<const std::byte>(ciphertext.data() + offset, size);

        auto result = decrypt_stream->process_chunk(chunk);
        ASSERT_TRUE(result.has_value());
        plaintext.insert(plaintext.end(),
                         result.value().begin(),
                         result.value().end());
    }

    // Finalize decryption
    auto decrypt_final = decrypt_stream->finalize();
    ASSERT_TRUE(decrypt_final.has_value());
    plaintext.insert(plaintext.end(),
                     decrypt_final.value().begin(),
                     decrypt_final.value().end());

    EXPECT_EQ(plaintext, total_data);
}

// ============================================================================
// Async Encryption Tests
// ============================================================================

TEST_F(AesGcmEngineTest, AsyncEncryptDecrypt) {
    auto plaintext = generate_random_data(1024);

    auto encrypt_future = engine_->encrypt_async(std::span<const std::byte>(plaintext));
    auto encrypt_result = encrypt_future.get();
    ASSERT_TRUE(encrypt_result.has_value());

    auto& encrypted = encrypt_result.value();

    auto decrypt_future = engine_->decrypt_async(
        std::span<const std::byte>(encrypted.ciphertext),
        encrypted.metadata);
    auto decrypt_result = decrypt_future.get();
    ASSERT_TRUE(decrypt_result.has_value());

    EXPECT_EQ(decrypt_result.value().plaintext, plaintext);
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(AesGcmEngineTest, StatisticsTracking) {
    engine_->reset_statistics();

    auto plaintext = generate_random_data(1024);

    // Encrypt
    auto encrypt_result = engine_->encrypt(std::span<const std::byte>(plaintext));
    ASSERT_TRUE(encrypt_result.has_value());

    auto stats = engine_->get_statistics();
    EXPECT_EQ(stats.bytes_encrypted, 1024);
    EXPECT_EQ(stats.encryption_ops, 1);
    EXPECT_GT(stats.total_encrypt_time.count(), 0);

    // Decrypt
    auto decrypt_result = engine_->decrypt(
        std::span<const std::byte>(encrypt_result.value().ciphertext),
        encrypt_result.value().metadata);
    ASSERT_TRUE(decrypt_result.has_value());

    stats = engine_->get_statistics();
    EXPECT_GT(stats.bytes_decrypted, 0);
    EXPECT_EQ(stats.decryption_ops, 1);
    EXPECT_GT(stats.total_decrypt_time.count(), 0);

    // Reset
    engine_->reset_statistics();
    stats = engine_->get_statistics();
    EXPECT_EQ(stats.bytes_encrypted, 0);
    EXPECT_EQ(stats.encryption_ops, 0);
}

// ============================================================================
// IV Generation Tests
// ============================================================================

TEST_F(AesGcmEngineTest, GenerateIV) {
    auto iv1 = engine_->generate_iv();
    auto iv2 = engine_->generate_iv();

    ASSERT_TRUE(iv1.has_value());
    ASSERT_TRUE(iv2.has_value());

    EXPECT_EQ(iv1.value().size(), AES_GCM_IV_SIZE);
    EXPECT_EQ(iv2.value().size(), AES_GCM_IV_SIZE);

    // IVs should be unique
    EXPECT_NE(iv1.value(), iv2.value());
}

// ============================================================================
// Tag Verification Tests
// ============================================================================

TEST_F(AesGcmEngineTest, VerifyTag) {
    auto plaintext = generate_random_data(128);

    auto encrypt_result = engine_->encrypt(std::span<const std::byte>(plaintext));
    ASSERT_TRUE(encrypt_result.has_value());

    auto& encrypted = encrypt_result.value();

    // Valid tag should verify
    EXPECT_TRUE(engine_->verify_tag(
        std::span<const std::byte>(encrypted.ciphertext),
        encrypted.metadata));

    // Tampered data should fail verification
    auto tampered = encrypted;
    if (!tampered.ciphertext.empty()) {
        tampered.ciphertext[0] ^= std::byte{0xFF};
    }
    EXPECT_FALSE(engine_->verify_tag(
        std::span<const std::byte>(tampered.ciphertext),
        tampered.metadata));
}

// ============================================================================
// Ciphertext Size Calculation Tests
// ============================================================================

TEST_F(AesGcmEngineTest, CalculateCiphertextSize) {
    EXPECT_EQ(engine_->calculate_ciphertext_size(0), 0);
    EXPECT_EQ(engine_->calculate_ciphertext_size(16), 16);
    EXPECT_EQ(engine_->calculate_ciphertext_size(1024), 1024);
    EXPECT_EQ(engine_->calculate_ciphertext_size(1000000), 1000000);
}

// ============================================================================
// IV and Tag Size Tests
// ============================================================================

TEST_F(AesGcmEngineTest, IvSize) {
    EXPECT_EQ(engine_->iv_size(), AES_GCM_IV_SIZE);
    EXPECT_EQ(engine_->iv_size(), 12);
}

TEST_F(AesGcmEngineTest, TagSize) {
    EXPECT_EQ(engine_->tag_size(), AES_GCM_TAG_SIZE);
    EXPECT_EQ(engine_->tag_size(), 16);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(AesGcmEngineTest, EncryptEmptyData) {
    std::vector<std::byte> empty_data;

    auto encrypt_result = engine_->encrypt(std::span<const std::byte>(empty_data));
    ASSERT_TRUE(encrypt_result.has_value());

    auto& encrypted = encrypt_result.value();
    EXPECT_TRUE(encrypted.ciphertext.empty());
    EXPECT_EQ(encrypted.metadata.original_size, 0);

    auto decrypt_result = engine_->decrypt(
        std::span<const std::byte>(encrypted.ciphertext),
        encrypted.metadata);
    ASSERT_TRUE(decrypt_result.has_value());
    EXPECT_TRUE(decrypt_result.value().plaintext.empty());
}

TEST_F(AesGcmEngineTest, EncryptSingleByte) {
    std::vector<std::byte> single_byte = {std::byte{0x42}};

    auto encrypt_result = engine_->encrypt(std::span<const std::byte>(single_byte));
    ASSERT_TRUE(encrypt_result.has_value());

    auto decrypt_result = engine_->decrypt(
        std::span<const std::byte>(encrypt_result.value().ciphertext),
        encrypt_result.value().metadata);
    ASSERT_TRUE(decrypt_result.has_value());

    EXPECT_EQ(decrypt_result.value().plaintext, single_byte);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(AesGcmEngineTest, ConcurrentEncryption) {
    constexpr int num_threads = 4;
    constexpr int operations_per_thread = 10;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, &success_count]() {
            for (int j = 0; j < operations_per_thread; ++j) {
                auto plaintext = generate_random_data(256);

                auto encrypt_result = engine_->encrypt(
                    std::span<const std::byte>(plaintext));
                if (!encrypt_result.has_value()) continue;

                auto decrypt_result = engine_->decrypt(
                    std::span<const std::byte>(encrypt_result.value().ciphertext),
                    encrypt_result.value().metadata);
                if (!decrypt_result.has_value()) continue;

                if (decrypt_result.value().plaintext == plaintext) {
                    ++success_count;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(success_count.load(), num_threads * operations_per_thread);
}

// ============================================================================
// Config Tests
// ============================================================================

TEST_F(AesGcmEngineTest, GetConfig) {
    auto& config = engine_->config();
    EXPECT_EQ(config.algorithm, encryption_algorithm::aes_256_gcm);
    EXPECT_TRUE(config.use_aead);
}

// ============================================================================
// Move Semantics Tests
// ============================================================================

TEST_F(AesGcmEngineTest, MoveConstruction) {
    auto engine1 = aes_gcm_engine::create();
    engine1->set_key(std::span<const std::byte>(key_));

    auto plaintext = generate_random_data(64);
    auto encrypt_result = engine1->encrypt(std::span<const std::byte>(plaintext));
    ASSERT_TRUE(encrypt_result.has_value());

    auto engine2 = std::move(engine1);
    ASSERT_NE(engine2, nullptr);

    auto decrypt_result = engine2->decrypt(
        std::span<const std::byte>(encrypt_result.value().ciphertext),
        encrypt_result.value().metadata);
    ASSERT_TRUE(decrypt_result.has_value());
    EXPECT_EQ(decrypt_result.value().plaintext, plaintext);
}

}  // namespace
}  // namespace kcenon::file_transfer

#endif  // FILE_TRANS_ENABLE_ENCRYPTION
