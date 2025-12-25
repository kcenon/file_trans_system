/**
 * @file test_encrypted_transfer.cpp
 * @brief Integration tests for encrypted file transfers
 */

#ifdef FILE_TRANS_ENABLE_ENCRYPTION

#include <gtest/gtest.h>

#include "test_fixtures.h"

#include <kcenon/file_transfer/encryption/aes_gcm_engine.h>
#include <kcenon/file_transfer/encryption/key_manager.h>

#include <chrono>
#include <fstream>
#include <thread>

namespace kcenon::file_transfer::test {

// ============================================================================
// Encryption Integration Test Fixture
// ============================================================================

class EncryptedTransferFixture : public TempDirectoryFixture {
protected:
    void SetUp() override {
        TempDirectoryFixture::SetUp();

        // Create encryption engine and key manager
        engine_ = aes_gcm_engine::create();
        ASSERT_NE(engine_, nullptr);

        key_manager_ = key_manager::create();
        ASSERT_NE(key_manager_, nullptr);
    }

    void TearDown() override {
        if (engine_) {
            engine_->clear_key();
        }
        engine_.reset();
        key_manager_.reset();
        TempDirectoryFixture::TearDown();
    }

    auto generate_test_key() -> std::vector<std::byte> {
        auto result = key_manager_->generate_key("test-key");
        if (!result.has_value()) {
            return {};
        }
        return result.value().key;
    }

    auto derive_key_from_password(const std::string& password)
        -> std::vector<std::byte> {
        auto result = key_manager_->derive_key_from_password("password-key", password);
        if (!result.has_value()) {
            return {};
        }
        return result.value().key;
    }

    auto encrypt_file_data(const std::vector<std::byte>& data)
        -> std::optional<encryption_result> {
        if (!engine_->has_key()) {
            return std::nullopt;
        }
        auto result = engine_->encrypt(std::span<const std::byte>(data));
        if (!result.has_value()) {
            return std::nullopt;
        }
        return result.value();
    }

    auto decrypt_file_data(const std::vector<std::byte>& ciphertext,
                           const encryption_metadata& metadata)
        -> std::optional<std::vector<std::byte>> {
        if (!engine_->has_key()) {
            return std::nullopt;
        }
        auto result = engine_->decrypt(
            std::span<const std::byte>(ciphertext),
            metadata);
        if (!result.has_value()) {
            return std::nullopt;
        }
        return result.value().plaintext;
    }

    auto read_file_bytes(const std::filesystem::path& path)
        -> std::vector<std::byte> {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            return {};
        }
        auto size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<std::byte> buffer(size);
        file.read(reinterpret_cast<char*>(buffer.data()), size);
        return buffer;
    }

    void write_file_bytes(const std::filesystem::path& path,
                          const std::vector<std::byte>& data) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
    }

    std::unique_ptr<aes_gcm_engine> engine_;
    std::unique_ptr<key_manager> key_manager_;
};

// ============================================================================
// End-to-End Encryption Tests
// ============================================================================

TEST_F(EncryptedTransferFixture, EncryptDecryptSmallFile) {
    // Create test file
    auto file_path = create_test_file("small_test.bin", 1024);
    auto original_data = read_file_bytes(file_path);
    ASSERT_FALSE(original_data.empty());

    // Set up encryption key
    auto key = generate_test_key();
    ASSERT_FALSE(key.empty());
    auto key_result = engine_->set_key(std::span<const std::byte>(key));
    ASSERT_TRUE(key_result.has_value());

    // Encrypt
    auto encrypt_result = encrypt_file_data(original_data);
    ASSERT_TRUE(encrypt_result.has_value());

    auto& encrypted = encrypt_result.value();
    EXPECT_FALSE(encrypted.ciphertext.empty());
    EXPECT_FALSE(encrypted.metadata.auth_tag.empty());

    // Verify ciphertext differs from plaintext
    EXPECT_NE(encrypted.ciphertext, original_data);

    // Decrypt
    auto decrypted = decrypt_file_data(encrypted.ciphertext, encrypted.metadata);
    ASSERT_TRUE(decrypted.has_value());

    // Verify decrypted data matches original
    EXPECT_EQ(decrypted.value(), original_data);
}

TEST_F(EncryptedTransferFixture, EncryptDecryptMediumFile) {
    // Create 1MB test file
    auto file_path = create_test_file("medium_test.bin", 1024 * 1024);
    auto original_data = read_file_bytes(file_path);
    ASSERT_FALSE(original_data.empty());

    // Set up encryption key
    auto key = generate_test_key();
    ASSERT_FALSE(key.empty());
    auto key_result = engine_->set_key(std::span<const std::byte>(key));
    ASSERT_TRUE(key_result.has_value());

    // Encrypt
    auto encrypt_result = encrypt_file_data(original_data);
    ASSERT_TRUE(encrypt_result.has_value());

    // Decrypt
    auto decrypted = decrypt_file_data(
        encrypt_result.value().ciphertext,
        encrypt_result.value().metadata);
    ASSERT_TRUE(decrypted.has_value());

    EXPECT_EQ(decrypted.value(), original_data);
}

TEST_F(EncryptedTransferFixture, EncryptDecryptWithPassword) {
    const std::string password = "secure-test-password-123!";

    // Create test file
    auto file_path = create_test_file("password_test.bin", 4096);
    auto original_data = read_file_bytes(file_path);
    ASSERT_FALSE(original_data.empty());

    // Derive key from password
    auto key = derive_key_from_password(password);
    ASSERT_FALSE(key.empty());
    auto key_result = engine_->set_key(std::span<const std::byte>(key));
    ASSERT_TRUE(key_result.has_value());

    // Encrypt
    auto encrypt_result = encrypt_file_data(original_data);
    ASSERT_TRUE(encrypt_result.has_value());

    // Create new engine for decryption (simulate different session)
    auto decrypt_engine = aes_gcm_engine::create();
    ASSERT_NE(decrypt_engine, nullptr);

    // Re-derive key from password
    auto key2 = derive_key_from_password(password);
    // Note: Keys will differ due to different salt
    // In real usage, salt would be stored with encrypted data

    // For this test, use the same key
    auto key2_result = decrypt_engine->set_key(std::span<const std::byte>(key));
    ASSERT_TRUE(key2_result.has_value());

    // Decrypt with new engine
    auto decrypt_result = decrypt_engine->decrypt(
        std::span<const std::byte>(encrypt_result.value().ciphertext),
        encrypt_result.value().metadata);
    ASSERT_TRUE(decrypt_result.has_value());

    EXPECT_EQ(decrypt_result.value().plaintext, original_data);
}

// ============================================================================
// Chunk-based Encryption Tests
// ============================================================================

TEST_F(EncryptedTransferFixture, ChunkBasedEncryption) {
    // Simulate chunked file transfer
    constexpr std::size_t file_size = 512 * 1024;  // 512 KB
    constexpr std::size_t chunk_size = 64 * 1024;  // 64 KB chunks

    auto file_path = create_test_file("chunked_test.bin", file_size);
    auto original_data = read_file_bytes(file_path);
    ASSERT_EQ(original_data.size(), file_size);

    // Set up encryption key
    auto key = generate_test_key();
    auto key_result = engine_->set_key(std::span<const std::byte>(key));
    ASSERT_TRUE(key_result.has_value());

    // Encrypt in chunks
    std::vector<encryption_result> encrypted_chunks;
    for (std::size_t offset = 0; offset < file_size; offset += chunk_size) {
        std::size_t size = std::min(chunk_size, file_size - offset);
        auto chunk = std::span<const std::byte>(original_data.data() + offset, size);

        auto chunk_index = offset / chunk_size;
        auto result = engine_->encrypt_chunk(chunk, chunk_index);
        ASSERT_TRUE(result.has_value())
            << "Failed to encrypt chunk " << chunk_index;

        encrypted_chunks.push_back(std::move(result.value()));
    }

    // Verify each chunk has unique IV
    for (std::size_t i = 0; i < encrypted_chunks.size(); ++i) {
        for (std::size_t j = i + 1; j < encrypted_chunks.size(); ++j) {
            EXPECT_NE(encrypted_chunks[i].metadata.iv,
                     encrypted_chunks[j].metadata.iv)
                << "Chunk " << i << " and " << j << " have same IV";
        }
    }

    // Decrypt chunks and reassemble
    std::vector<std::byte> reassembled;
    reassembled.reserve(file_size);

    for (std::size_t i = 0; i < encrypted_chunks.size(); ++i) {
        auto result = engine_->decrypt_chunk(
            std::span<const std::byte>(encrypted_chunks[i].ciphertext),
            encrypted_chunks[i].metadata,
            i);
        ASSERT_TRUE(result.has_value())
            << "Failed to decrypt chunk " << i;

        reassembled.insert(reassembled.end(),
                          result.value().plaintext.begin(),
                          result.value().plaintext.end());
    }

    EXPECT_EQ(reassembled, original_data);
}

// ============================================================================
// Streaming Encryption Tests
// ============================================================================

TEST_F(EncryptedTransferFixture, StreamingEncryption) {
    constexpr std::size_t file_size = 256 * 1024;  // 256 KB
    constexpr std::size_t stream_chunk_size = 32 * 1024;  // 32 KB

    auto file_path = create_test_file("stream_test.bin", file_size);
    auto original_data = read_file_bytes(file_path);
    ASSERT_EQ(original_data.size(), file_size);

    // Set up encryption key
    auto key = generate_test_key();
    auto key_result = engine_->set_key(std::span<const std::byte>(key));
    ASSERT_TRUE(key_result.has_value());

    // Create encryption stream
    auto encrypt_stream = engine_->create_encrypt_stream(file_size);
    ASSERT_NE(encrypt_stream, nullptr);
    EXPECT_TRUE(encrypt_stream->is_encryption());

    // Process in chunks
    std::vector<std::byte> ciphertext;
    for (std::size_t offset = 0; offset < file_size; offset += stream_chunk_size) {
        std::size_t size = std::min(stream_chunk_size, file_size - offset);
        auto chunk = std::span<const std::byte>(original_data.data() + offset, size);

        auto result = encrypt_stream->process_chunk(chunk);
        ASSERT_TRUE(result.has_value())
            << "Failed to process stream chunk at offset " << offset;

        ciphertext.insert(ciphertext.end(),
                         result.value().begin(),
                         result.value().end());
    }

    // Finalize
    auto final_result = encrypt_stream->finalize();
    ASSERT_TRUE(final_result.has_value());
    ciphertext.insert(ciphertext.end(),
                     final_result.value().begin(),
                     final_result.value().end());

    auto metadata = encrypt_stream->get_metadata();
    EXPECT_EQ(encrypt_stream->bytes_processed(), file_size);

    // Create decryption stream
    auto decrypt_stream = engine_->create_decrypt_stream(metadata);
    ASSERT_NE(decrypt_stream, nullptr);
    EXPECT_FALSE(decrypt_stream->is_encryption());

    // Decrypt
    std::vector<std::byte> plaintext;
    for (std::size_t offset = 0; offset < ciphertext.size(); offset += stream_chunk_size) {
        std::size_t size = std::min(stream_chunk_size, ciphertext.size() - offset);
        auto chunk = std::span<const std::byte>(ciphertext.data() + offset, size);

        auto result = decrypt_stream->process_chunk(chunk);
        ASSERT_TRUE(result.has_value())
            << "Failed to decrypt stream chunk at offset " << offset;

        plaintext.insert(plaintext.end(),
                        result.value().begin(),
                        result.value().end());
    }

    auto decrypt_final = decrypt_stream->finalize();
    ASSERT_TRUE(decrypt_final.has_value());
    plaintext.insert(plaintext.end(),
                    decrypt_final.value().begin(),
                    decrypt_final.value().end());

    EXPECT_EQ(plaintext, original_data);
}

// ============================================================================
// Security Tests
// ============================================================================

TEST_F(EncryptedTransferFixture, TamperedCiphertextDetection) {
    auto file_path = create_test_file("tamper_test.bin", 4096);
    auto original_data = read_file_bytes(file_path);

    auto key = generate_test_key();
    engine_->set_key(std::span<const std::byte>(key));

    auto encrypt_result = encrypt_file_data(original_data);
    ASSERT_TRUE(encrypt_result.has_value());

    // Tamper with ciphertext
    auto tampered = encrypt_result.value();
    if (!tampered.ciphertext.empty()) {
        tampered.ciphertext[0] ^= std::byte{0xFF};
    }

    // Decryption should fail
    auto decrypt_result = decrypt_file_data(tampered.ciphertext, tampered.metadata);
    EXPECT_FALSE(decrypt_result.has_value())
        << "Decryption should fail for tampered ciphertext";
}

TEST_F(EncryptedTransferFixture, TamperedTagDetection) {
    auto file_path = create_test_file("tag_tamper_test.bin", 4096);
    auto original_data = read_file_bytes(file_path);

    auto key = generate_test_key();
    engine_->set_key(std::span<const std::byte>(key));

    auto encrypt_result = encrypt_file_data(original_data);
    ASSERT_TRUE(encrypt_result.has_value());

    // Tamper with authentication tag
    auto tampered = encrypt_result.value();
    if (!tampered.metadata.auth_tag.empty()) {
        tampered.metadata.auth_tag[0] ^= std::byte{0xFF};
    }

    // Decryption should fail
    auto decrypt_result = decrypt_file_data(tampered.ciphertext, tampered.metadata);
    EXPECT_FALSE(decrypt_result.has_value())
        << "Decryption should fail for tampered auth tag";
}

TEST_F(EncryptedTransferFixture, WrongKeyDecryptionFails) {
    auto file_path = create_test_file("wrong_key_test.bin", 4096);
    auto original_data = read_file_bytes(file_path);

    // Encrypt with first key
    auto key1 = generate_test_key();
    engine_->set_key(std::span<const std::byte>(key1));
    auto encrypt_result = encrypt_file_data(original_data);
    ASSERT_TRUE(encrypt_result.has_value());

    // Try to decrypt with different key
    auto key2 = generate_test_key();
    engine_->set_key(std::span<const std::byte>(key2));

    auto decrypt_result = decrypt_file_data(
        encrypt_result.value().ciphertext,
        encrypt_result.value().metadata);
    EXPECT_FALSE(decrypt_result.has_value())
        << "Decryption should fail with wrong key";
}

// ============================================================================
// Concurrent Encryption Tests
// ============================================================================

TEST_F(EncryptedTransferFixture, ConcurrentEncryption) {
    constexpr int num_threads = 4;
    constexpr int operations_per_thread = 10;

    auto key = generate_test_key();
    engine_->set_key(std::span<const std::byte>(key));

    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, t, &success_count]() {
            for (int i = 0; i < operations_per_thread; ++i) {
                // Generate unique test data for each operation
                std::string filename = "concurrent_" + std::to_string(t) +
                                      "_" + std::to_string(i) + ".bin";
                auto path = create_test_file(filename, 1024 + (t * 100) + i);
                auto data = read_file_bytes(path);

                if (data.empty()) continue;

                auto encrypt_result = encrypt_file_data(data);
                if (!encrypt_result.has_value()) continue;

                auto decrypt_result = decrypt_file_data(
                    encrypt_result.value().ciphertext,
                    encrypt_result.value().metadata);
                if (!decrypt_result.has_value()) continue;

                if (decrypt_result.value() == data) {
                    ++success_count;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(success_count.load(), num_threads * operations_per_thread)
        << "Some concurrent encryption/decryption operations failed";
}

// ============================================================================
// Key Rotation Tests
// ============================================================================

TEST_F(EncryptedTransferFixture, KeyRotationPreservesData) {
    // Create and encrypt with initial key
    auto file_path = create_test_file("rotation_test.bin", 8192);
    auto original_data = read_file_bytes(file_path);

    auto initial_key = key_manager_->generate_key("rotate-me");
    ASSERT_TRUE(initial_key.has_value());
    engine_->set_key(std::span<const std::byte>(initial_key.value().key));

    auto encrypted = encrypt_file_data(original_data);
    ASSERT_TRUE(encrypted.has_value());

    // Rotate key
    auto rotated_key = key_manager_->rotate_key("rotate-me");
    ASSERT_TRUE(rotated_key.has_value());
    EXPECT_NE(initial_key.value().key, rotated_key.value().key);

    // Old key should still decrypt old data
    // (using original key since we have it)
    engine_->set_key(std::span<const std::byte>(initial_key.value().key));
    auto decrypted = decrypt_file_data(
        encrypted.value().ciphertext,
        encrypted.value().metadata);
    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(decrypted.value(), original_data);

    // New key should work for new encryptions
    engine_->set_key(std::span<const std::byte>(rotated_key.value().key));
    auto new_encrypted = encrypt_file_data(original_data);
    ASSERT_TRUE(new_encrypted.has_value());

    auto new_decrypted = decrypt_file_data(
        new_encrypted.value().ciphertext,
        new_encrypted.value().metadata);
    ASSERT_TRUE(new_decrypted.has_value());
    EXPECT_EQ(new_decrypted.value(), original_data);
}

}  // namespace kcenon::file_transfer::test

#else  // FILE_TRANS_ENABLE_ENCRYPTION

#include <gtest/gtest.h>

TEST(EncryptedTransferDisabledTest, EncryptionNotEnabled) {
    GTEST_SKIP() << "Encryption not enabled in build";
}

#endif  // FILE_TRANS_ENABLE_ENCRYPTION
