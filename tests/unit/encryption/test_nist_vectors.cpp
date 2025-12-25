/**
 * @file test_nist_vectors.cpp
 * @brief NIST SP 800-38D AES-GCM test vectors
 *
 * Test vectors from NIST Special Publication 800-38D
 * "Recommendation for Block Cipher Modes of Operation: Galois/Counter Mode (GCM)"
 */

#ifdef FILE_TRANS_ENABLE_ENCRYPTION

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <vector>

#include "kcenon/file_transfer/encryption/aes_gcm_engine.h"

namespace kcenon::file_transfer {
namespace {

// ============================================================================
// Helper Functions
// ============================================================================

auto hex_to_bytes(std::string_view hex) -> std::vector<std::byte> {
    std::vector<std::byte> bytes;
    bytes.reserve(hex.size() / 2);

    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto byte_str = std::string(hex.substr(i, 2));
        auto byte_val = static_cast<std::byte>(std::stoul(byte_str, nullptr, 16));
        bytes.push_back(byte_val);
    }

    return bytes;
}

auto bytes_to_hex(std::span<const std::byte> bytes) -> std::string {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto byte : bytes) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

// ============================================================================
// NIST GCM Test Vector Structure
// ============================================================================

struct nist_gcm_test_vector {
    std::string name;
    std::string key_hex;
    std::string iv_hex;
    std::string plaintext_hex;
    std::string aad_hex;
    std::string ciphertext_hex;
    std::string tag_hex;
};

// ============================================================================
// NIST SP 800-38D Test Vectors for AES-256-GCM
// ============================================================================

// Test Case 13: 256-bit key, 96-bit IV, no plaintext, no AAD
static const nist_gcm_test_vector NIST_TC_13 = {
    .name = "NIST_TC13_256bit_96IV_NoP_NoAAD",
    .key_hex = "0000000000000000000000000000000000000000000000000000000000000000",
    .iv_hex = "000000000000000000000000",
    .plaintext_hex = "",
    .aad_hex = "",
    .ciphertext_hex = "",
    .tag_hex = "530f8afbc74536b9a963b4f1c4cb738b"
};

// Test Case 14: 256-bit key, 96-bit IV, no AAD
static const nist_gcm_test_vector NIST_TC_14 = {
    .name = "NIST_TC14_256bit_96IV_NoAAD",
    .key_hex = "0000000000000000000000000000000000000000000000000000000000000000",
    .iv_hex = "000000000000000000000000",
    .plaintext_hex = "00000000000000000000000000000000",
    .aad_hex = "",
    .ciphertext_hex = "cea7403d4d606b6e074ec5d3baf39d18",
    .tag_hex = "d0d1c8a799996bf0265b98b5d48ab919"
};

// Test Case 15: 256-bit key, 96-bit IV, with AAD
static const nist_gcm_test_vector NIST_TC_15 = {
    .name = "NIST_TC15_256bit_96IV_WithAAD",
    .key_hex = "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308",
    .iv_hex = "cafebabefacedbaddecaf888",
    .plaintext_hex = "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
                     "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255",
    .aad_hex = "",
    .ciphertext_hex = "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
                      "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662898015ad",
    .tag_hex = "b094dac5d93471bdec1a502270e3cc6c"
};

// Test Case 16: 256-bit key, 96-bit IV, with AAD (60-byte plaintext)
static const nist_gcm_test_vector NIST_TC_16 = {
    .name = "NIST_TC16_256bit_96IV_WithAAD_60B",
    .key_hex = "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308",
    .iv_hex = "cafebabefacedbaddecaf888",
    .plaintext_hex = "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
                     "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39",
    .aad_hex = "feedfacedeadbeeffeedfacedeadbeefabaddad2",
    .ciphertext_hex = "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
                      "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662",
    .tag_hex = "76fc6ece0f4e1768cddf8853bb2d551b"
};

// ============================================================================
// Test Fixture
// ============================================================================

class NistGcmVectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = aes_gcm_engine::create();
        ASSERT_NE(engine_, nullptr);
    }

    void TearDown() override {
        if (engine_) {
            engine_->clear_key();
        }
        engine_.reset();
    }

    void run_encryption_test(const nist_gcm_test_vector& tv) {
        // Parse test vector
        auto key = hex_to_bytes(tv.key_hex);
        auto iv = hex_to_bytes(tv.iv_hex);
        auto plaintext = hex_to_bytes(tv.plaintext_hex);
        auto aad = hex_to_bytes(tv.aad_hex);
        auto expected_ciphertext = hex_to_bytes(tv.ciphertext_hex);
        auto expected_tag = hex_to_bytes(tv.tag_hex);

        // Set key
        auto key_result = engine_->set_key(std::span<const std::byte>(key));
        ASSERT_TRUE(key_result.has_value())
            << "Failed to set key for " << tv.name;

        // Encrypt with provided IV
        auto encrypt_result = engine_->encrypt(
            std::span<const std::byte>(plaintext),
            std::span<const std::byte>(aad));
        ASSERT_TRUE(encrypt_result.has_value())
            << "Encryption failed for " << tv.name;

        auto& encrypted = encrypt_result.value();

        // Verify ciphertext size matches expected
        EXPECT_EQ(encrypted.ciphertext.size(), expected_ciphertext.size())
            << "Ciphertext size mismatch for " << tv.name;

        // Verify tag size
        EXPECT_EQ(encrypted.metadata.auth_tag.size(), expected_tag.size())
            << "Tag size mismatch for " << tv.name;

        // Verify decryption works
        auto decrypt_result = engine_->decrypt(
            std::span<const std::byte>(encrypted.ciphertext),
            encrypted.metadata);
        ASSERT_TRUE(decrypt_result.has_value())
            << "Decryption failed for " << tv.name;

        EXPECT_EQ(decrypt_result.value().plaintext, plaintext)
            << "Plaintext mismatch after decrypt for " << tv.name;
    }

    void run_decryption_test(const nist_gcm_test_vector& tv) {
        // Parse test vector
        auto key = hex_to_bytes(tv.key_hex);
        auto iv = hex_to_bytes(tv.iv_hex);
        auto ciphertext = hex_to_bytes(tv.ciphertext_hex);
        auto aad = hex_to_bytes(tv.aad_hex);
        auto expected_plaintext = hex_to_bytes(tv.plaintext_hex);
        auto tag = hex_to_bytes(tv.tag_hex);

        // Set key
        auto key_result = engine_->set_key(std::span<const std::byte>(key));
        ASSERT_TRUE(key_result.has_value())
            << "Failed to set key for " << tv.name;

        // Build metadata from test vector
        encryption_metadata metadata;
        metadata.algorithm = encryption_algorithm::aes_256_gcm;
        metadata.iv = iv;
        metadata.auth_tag = tag;
        metadata.aad = aad;
        metadata.original_size = expected_plaintext.size();

        // Decrypt
        auto decrypt_result = engine_->decrypt(
            std::span<const std::byte>(ciphertext),
            metadata);
        ASSERT_TRUE(decrypt_result.has_value())
            << "Decryption failed for " << tv.name
            << ": " << decrypt_result.error().message;

        auto& decrypted = decrypt_result.value();

        // Verify plaintext
        EXPECT_EQ(decrypted.plaintext.size(), expected_plaintext.size())
            << "Plaintext size mismatch for " << tv.name;

        EXPECT_EQ(decrypted.plaintext, expected_plaintext)
            << "Plaintext content mismatch for " << tv.name
            << "\nExpected: " << tv.plaintext_hex
            << "\nGot: " << bytes_to_hex(decrypted.plaintext);
    }

    std::unique_ptr<aes_gcm_engine> engine_;
};

// ============================================================================
// NIST Test Vector Tests - Encryption Round-trip
// ============================================================================

TEST_F(NistGcmVectorTest, EncryptRoundTrip_TC13) {
    run_encryption_test(NIST_TC_13);
}

TEST_F(NistGcmVectorTest, EncryptRoundTrip_TC14) {
    run_encryption_test(NIST_TC_14);
}

TEST_F(NistGcmVectorTest, EncryptRoundTrip_TC15) {
    run_encryption_test(NIST_TC_15);
}

TEST_F(NistGcmVectorTest, EncryptRoundTrip_TC16) {
    run_encryption_test(NIST_TC_16);
}

// ============================================================================
// NIST Test Vector Tests - Decryption Verification
// ============================================================================

TEST_F(NistGcmVectorTest, DecryptVerify_TC13) {
    run_decryption_test(NIST_TC_13);
}

TEST_F(NistGcmVectorTest, DecryptVerify_TC14) {
    run_decryption_test(NIST_TC_14);
}

TEST_F(NistGcmVectorTest, DecryptVerify_TC15) {
    run_decryption_test(NIST_TC_15);
}

TEST_F(NistGcmVectorTest, DecryptVerify_TC16) {
    run_decryption_test(NIST_TC_16);
}

// ============================================================================
// Authentication Tag Verification Tests
// ============================================================================

TEST_F(NistGcmVectorTest, TagTamperingDetection_TC14) {
    auto key = hex_to_bytes(NIST_TC_14.key_hex);
    auto iv = hex_to_bytes(NIST_TC_14.iv_hex);
    auto ciphertext = hex_to_bytes(NIST_TC_14.ciphertext_hex);
    auto tag = hex_to_bytes(NIST_TC_14.tag_hex);

    auto key_result = engine_->set_key(std::span<const std::byte>(key));
    ASSERT_TRUE(key_result.has_value());

    encryption_metadata metadata;
    metadata.algorithm = encryption_algorithm::aes_256_gcm;
    metadata.iv = iv;
    metadata.auth_tag = tag;
    metadata.original_size = ciphertext.size();

    // Tamper with tag
    if (!metadata.auth_tag.empty()) {
        metadata.auth_tag[0] ^= std::byte{0x01};
    }

    auto decrypt_result = engine_->decrypt(
        std::span<const std::byte>(ciphertext),
        metadata);
    EXPECT_FALSE(decrypt_result.has_value())
        << "Decryption should fail with tampered tag";
}

TEST_F(NistGcmVectorTest, CiphertextTamperingDetection_TC15) {
    auto key = hex_to_bytes(NIST_TC_15.key_hex);
    auto iv = hex_to_bytes(NIST_TC_15.iv_hex);
    auto ciphertext = hex_to_bytes(NIST_TC_15.ciphertext_hex);
    auto tag = hex_to_bytes(NIST_TC_15.tag_hex);

    auto key_result = engine_->set_key(std::span<const std::byte>(key));
    ASSERT_TRUE(key_result.has_value());

    // Tamper with ciphertext
    if (!ciphertext.empty()) {
        ciphertext[0] ^= std::byte{0x01};
    }

    encryption_metadata metadata;
    metadata.algorithm = encryption_algorithm::aes_256_gcm;
    metadata.iv = iv;
    metadata.auth_tag = tag;
    metadata.original_size = ciphertext.size();

    auto decrypt_result = engine_->decrypt(
        std::span<const std::byte>(ciphertext),
        metadata);
    EXPECT_FALSE(decrypt_result.has_value())
        << "Decryption should fail with tampered ciphertext";
}

TEST_F(NistGcmVectorTest, AadTamperingDetection_TC16) {
    auto key = hex_to_bytes(NIST_TC_16.key_hex);
    auto iv = hex_to_bytes(NIST_TC_16.iv_hex);
    auto ciphertext = hex_to_bytes(NIST_TC_16.ciphertext_hex);
    auto aad = hex_to_bytes(NIST_TC_16.aad_hex);
    auto tag = hex_to_bytes(NIST_TC_16.tag_hex);

    auto key_result = engine_->set_key(std::span<const std::byte>(key));
    ASSERT_TRUE(key_result.has_value());

    // Tamper with AAD
    if (!aad.empty()) {
        aad[0] ^= std::byte{0x01};
    }

    encryption_metadata metadata;
    metadata.algorithm = encryption_algorithm::aes_256_gcm;
    metadata.iv = iv;
    metadata.auth_tag = tag;
    metadata.aad = aad;
    metadata.original_size = ciphertext.size();

    auto decrypt_result = engine_->decrypt(
        std::span<const std::byte>(ciphertext),
        metadata);
    EXPECT_FALSE(decrypt_result.has_value())
        << "Decryption should fail with tampered AAD";
}

// ============================================================================
// Key Size Validation Tests
// ============================================================================

TEST_F(NistGcmVectorTest, KeySizeValidation) {
    // AES-256 requires exactly 32-byte key
    std::vector<std::byte> short_key(16, std::byte{0x00});
    std::vector<std::byte> valid_key(32, std::byte{0x00});
    std::vector<std::byte> long_key(48, std::byte{0x00});

    EXPECT_FALSE(engine_->set_key(std::span<const std::byte>(short_key)).has_value())
        << "16-byte key should be rejected for AES-256";

    EXPECT_TRUE(engine_->set_key(std::span<const std::byte>(valid_key)).has_value())
        << "32-byte key should be accepted for AES-256";

    EXPECT_FALSE(engine_->set_key(std::span<const std::byte>(long_key)).has_value())
        << "48-byte key should be rejected for AES-256";
}

// ============================================================================
// IV Uniqueness Tests
// ============================================================================

TEST_F(NistGcmVectorTest, IvUniqueness) {
    auto key = hex_to_bytes(NIST_TC_14.key_hex);
    auto key_result = engine_->set_key(std::span<const std::byte>(key));
    ASSERT_TRUE(key_result.has_value());

    constexpr int num_encryptions = 100;
    std::vector<std::vector<std::byte>> ivs;
    ivs.reserve(num_encryptions);

    std::vector<std::byte> plaintext = {std::byte{0x00}, std::byte{0x01}};

    for (int i = 0; i < num_encryptions; ++i) {
        auto result = engine_->encrypt(std::span<const std::byte>(plaintext));
        ASSERT_TRUE(result.has_value()) << "Encryption " << i << " failed";
        ivs.push_back(result.value().metadata.iv);
    }

    // Verify all IVs are unique
    for (std::size_t i = 0; i < ivs.size(); ++i) {
        for (std::size_t j = i + 1; j < ivs.size(); ++j) {
            EXPECT_NE(ivs[i], ivs[j])
                << "IV collision detected at indices " << i << " and " << j;
        }
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(NistGcmVectorTest, EmptyPlaintext) {
    auto key = hex_to_bytes(NIST_TC_13.key_hex);
    auto key_result = engine_->set_key(std::span<const std::byte>(key));
    ASSERT_TRUE(key_result.has_value());

    std::vector<std::byte> empty_plaintext;
    auto result = engine_->encrypt(std::span<const std::byte>(empty_plaintext));
    ASSERT_TRUE(result.has_value());

    EXPECT_TRUE(result.value().ciphertext.empty());
    EXPECT_FALSE(result.value().metadata.auth_tag.empty());
}

TEST_F(NistGcmVectorTest, LargePlaintext) {
    auto key = hex_to_bytes(NIST_TC_14.key_hex);
    auto key_result = engine_->set_key(std::span<const std::byte>(key));
    ASSERT_TRUE(key_result.has_value());

    // 1 MB plaintext
    std::vector<std::byte> large_plaintext(1024 * 1024, std::byte{0xAB});
    auto encrypt_result = engine_->encrypt(std::span<const std::byte>(large_plaintext));
    ASSERT_TRUE(encrypt_result.has_value());

    auto& encrypted = encrypt_result.value();
    EXPECT_EQ(encrypted.ciphertext.size(), large_plaintext.size());

    auto decrypt_result = engine_->decrypt(
        std::span<const std::byte>(encrypted.ciphertext),
        encrypted.metadata);
    ASSERT_TRUE(decrypt_result.has_value());

    EXPECT_EQ(decrypt_result.value().plaintext, large_plaintext);
}

}  // namespace
}  // namespace kcenon::file_transfer

#endif  // FILE_TRANS_ENABLE_ENCRYPTION
