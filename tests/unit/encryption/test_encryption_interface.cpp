/**
 * @file test_encryption_interface.cpp
 * @brief Unit tests for encryption abstraction layer
 */

#include <gtest/gtest.h>

#include "kcenon/file_transfer/encryption/encryption_interface.h"
#include "kcenon/file_transfer/encryption/encryption_config.h"
#include "kcenon/file_transfer/encryption/key_derivation.h"

namespace kcenon::file_transfer {
namespace {

// ============================================================================
// Encryption Config Tests
// ============================================================================

class EncryptionConfigTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(EncryptionConfigTest, DefaultAesGcmConfig) {
    aes_gcm_config config;

    EXPECT_EQ(config.algorithm, encryption_algorithm::aes_256_gcm);
    EXPECT_TRUE(config.use_aead);
    EXPECT_EQ(config.iv_size, AES_GCM_IV_SIZE);
    EXPECT_EQ(config.tag_size, AES_GCM_TAG_SIZE);
    EXPECT_TRUE(config.random_iv);
    EXPECT_TRUE(config.secure_memory);
    EXPECT_EQ(config.stream_chunk_size, 64 * 1024);
}

TEST_F(EncryptionConfigTest, DefaultAesCbcConfig) {
    aes_cbc_config config;

    EXPECT_EQ(config.algorithm, encryption_algorithm::aes_256_cbc);
    EXPECT_FALSE(config.use_aead);
    EXPECT_EQ(config.iv_size, AES_BLOCK_SIZE);
    EXPECT_TRUE(config.pkcs7_padding);
    EXPECT_TRUE(config.use_hmac);
}

TEST_F(EncryptionConfigTest, DefaultChaCha20Config) {
    chacha20_config config;

    EXPECT_EQ(config.algorithm, encryption_algorithm::chacha20_poly1305);
    EXPECT_TRUE(config.use_aead);
    EXPECT_EQ(config.nonce_size, CHACHA20_NONCE_SIZE);
    EXPECT_TRUE(config.random_nonce);
}

TEST_F(EncryptionConfigTest, AesGcmConfigBuilder) {
    auto config = encryption_config_builder::aes_gcm()
        .with_stream_chunk_size(128 * 1024)
        .with_iv_size(16)
        .with_tag_size(12)
        .with_random_iv(false)
        .with_secure_memory(false)
        .build_aes_gcm();

    EXPECT_EQ(config.algorithm, encryption_algorithm::aes_256_gcm);
    EXPECT_EQ(config.stream_chunk_size, 128 * 1024);
    EXPECT_EQ(config.iv_size, 16);
    EXPECT_EQ(config.tag_size, 12);
    EXPECT_FALSE(config.random_iv);
    EXPECT_FALSE(config.secure_memory);
}

TEST_F(EncryptionConfigTest, AesCbcConfigBuilder) {
    auto config = encryption_config_builder::aes_cbc()
        .with_hmac(false)
        .with_secure_memory(true)
        .build_aes_cbc();

    EXPECT_EQ(config.algorithm, encryption_algorithm::aes_256_cbc);
    EXPECT_FALSE(config.use_hmac);
    EXPECT_TRUE(config.secure_memory);
}

TEST_F(EncryptionConfigTest, ChaCha20ConfigBuilder) {
    auto config = encryption_config_builder::chacha20()
        .with_random_nonce(false)
        .with_stream_chunk_size(32 * 1024)
        .build_chacha20();

    EXPECT_EQ(config.algorithm, encryption_algorithm::chacha20_poly1305);
    EXPECT_FALSE(config.random_nonce);
    EXPECT_EQ(config.stream_chunk_size, 32 * 1024);
}

TEST_F(EncryptionConfigTest, ConfigWithAad) {
    std::vector<std::byte> aad = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    auto config = encryption_config_builder::aes_gcm()
        .with_aad(aad)
        .build_aes_gcm();

    ASSERT_TRUE(config.aad.has_value());
    EXPECT_EQ(config.aad->size(), 3);
}

// ============================================================================
// Encryption Algorithm Tests
// ============================================================================

class EncryptionAlgorithmTest : public ::testing::Test {};

TEST_F(EncryptionAlgorithmTest, AlgorithmToString) {
    EXPECT_STREQ(to_string(encryption_algorithm::none), "none");
    EXPECT_STREQ(to_string(encryption_algorithm::aes_256_gcm), "aes-256-gcm");
    EXPECT_STREQ(to_string(encryption_algorithm::aes_256_cbc), "aes-256-cbc");
    EXPECT_STREQ(to_string(encryption_algorithm::chacha20_poly1305), "chacha20-poly1305");
}

TEST_F(EncryptionAlgorithmTest, StateToString) {
    EXPECT_STREQ(to_string(encryption_state::uninitialized), "uninitialized");
    EXPECT_STREQ(to_string(encryption_state::ready), "ready");
    EXPECT_STREQ(to_string(encryption_state::processing), "processing");
    EXPECT_STREQ(to_string(encryption_state::error), "error");
}

// ============================================================================
// Encryption Metadata Tests
// ============================================================================

class EncryptionMetadataTest : public ::testing::Test {};

TEST_F(EncryptionMetadataTest, DefaultValues) {
    encryption_metadata metadata;

    EXPECT_EQ(metadata.algorithm, encryption_algorithm::aes_256_gcm);
    EXPECT_EQ(metadata.kdf, key_derivation_function::none);
    EXPECT_TRUE(metadata.iv.empty());
    EXPECT_TRUE(metadata.salt.empty());
    EXPECT_TRUE(metadata.auth_tag.empty());
    EXPECT_TRUE(metadata.aad.empty());
    EXPECT_EQ(metadata.kdf_iterations, 0);
    EXPECT_EQ(metadata.original_size, 0);
    EXPECT_EQ(metadata.version, 1);
}

TEST_F(EncryptionMetadataTest, WithValues) {
    encryption_metadata metadata;
    metadata.algorithm = encryption_algorithm::chacha20_poly1305;
    metadata.kdf = key_derivation_function::argon2id;
    metadata.iv = {std::byte{0x01}, std::byte{0x02}};
    metadata.salt = {std::byte{0x03}, std::byte{0x04}};
    metadata.auth_tag = {std::byte{0x05}};
    metadata.kdf_iterations = 3;
    metadata.argon2_memory_kb = 65536;
    metadata.argon2_parallelism = 4;
    metadata.original_size = 1024;

    EXPECT_EQ(metadata.algorithm, encryption_algorithm::chacha20_poly1305);
    EXPECT_EQ(metadata.kdf, key_derivation_function::argon2id);
    EXPECT_EQ(metadata.iv.size(), 2);
    EXPECT_EQ(metadata.salt.size(), 2);
    EXPECT_EQ(metadata.auth_tag.size(), 1);
    EXPECT_EQ(metadata.kdf_iterations, 3);
    EXPECT_EQ(metadata.argon2_memory_kb, 65536);
    EXPECT_EQ(metadata.argon2_parallelism, 4);
    EXPECT_EQ(metadata.original_size, 1024);
}

// ============================================================================
// Encryption Constants Tests
// ============================================================================

class EncryptionConstantsTest : public ::testing::Test {};

TEST_F(EncryptionConstantsTest, KeySizes) {
    EXPECT_EQ(AES_256_KEY_SIZE, 32);
    EXPECT_EQ(CHACHA20_KEY_SIZE, 32);
}

TEST_F(EncryptionConstantsTest, IvNonceSizes) {
    EXPECT_EQ(AES_GCM_IV_SIZE, 12);
    EXPECT_EQ(AES_BLOCK_SIZE, 16);
    EXPECT_EQ(CHACHA20_NONCE_SIZE, 12);
}

TEST_F(EncryptionConstantsTest, TagSizes) {
    EXPECT_EQ(AES_GCM_TAG_SIZE, 16);
    EXPECT_EQ(CHACHA20_TAG_SIZE, 16);
}

TEST_F(EncryptionConstantsTest, SaltSize) {
    EXPECT_EQ(SALT_SIZE, 32);
}

// ============================================================================
// Key Derivation Config Tests
// ============================================================================

class KeyDerivationConfigTest : public ::testing::Test {};

TEST_F(KeyDerivationConfigTest, KdfFunctionToString) {
    EXPECT_STREQ(to_string(key_derivation_function::none), "none");
    EXPECT_STREQ(to_string(key_derivation_function::pbkdf2), "pbkdf2");
    EXPECT_STREQ(to_string(key_derivation_function::argon2id), "argon2id");
    EXPECT_STREQ(to_string(key_derivation_function::scrypt), "scrypt");
}

TEST_F(KeyDerivationConfigTest, DefaultPbkdf2Config) {
    pbkdf2_config config;

    EXPECT_EQ(config.iterations, PBKDF2_DEFAULT_ITERATIONS);
    EXPECT_EQ(config.hash_algorithm, "SHA-256");
    EXPECT_EQ(config.key_length, AES_256_KEY_SIZE);
    EXPECT_EQ(config.salt_length, SALT_SIZE);
}

TEST_F(KeyDerivationConfigTest, DefaultArgon2Config) {
    argon2_config config;

    EXPECT_EQ(config.memory_kb, ARGON2_DEFAULT_MEMORY_KB);
    EXPECT_EQ(config.time_cost, ARGON2_DEFAULT_TIME_COST);
    EXPECT_EQ(config.parallelism, ARGON2_DEFAULT_PARALLELISM);
    EXPECT_EQ(config.key_length, AES_256_KEY_SIZE);
    EXPECT_EQ(config.salt_length, SALT_SIZE);
}

TEST_F(KeyDerivationConfigTest, DefaultScryptConfig) {
    scrypt_config config;

    EXPECT_EQ(config.n, SCRYPT_DEFAULT_N);
    EXPECT_EQ(config.r, SCRYPT_DEFAULT_R);
    EXPECT_EQ(config.p, SCRYPT_DEFAULT_P);
    EXPECT_EQ(config.key_length, AES_256_KEY_SIZE);
    EXPECT_EQ(config.salt_length, SALT_SIZE);
}

TEST_F(KeyDerivationConfigTest, Pbkdf2ConfigBuilder) {
    auto config = key_derivation_config_builder::pbkdf2()
        .with_iterations(100000)
        .with_key_length(64)
        .with_salt_length(16)
        .build_pbkdf2();

    EXPECT_EQ(config.iterations, 100000);
    EXPECT_EQ(config.key_length, 64);
    EXPECT_EQ(config.salt_length, 16);
}

TEST_F(KeyDerivationConfigTest, Argon2ConfigBuilder) {
    auto config = key_derivation_config_builder::argon2()
        .with_memory(131072)
        .with_time_cost(4)
        .with_parallelism(8)
        .with_key_length(48)
        .build_argon2();

    EXPECT_EQ(config.memory_kb, 131072);
    EXPECT_EQ(config.time_cost, 4);
    EXPECT_EQ(config.parallelism, 8);
    EXPECT_EQ(config.key_length, 48);
}

TEST_F(KeyDerivationConfigTest, ScryptConfigBuilder) {
    auto config = key_derivation_config_builder::scrypt()
        .with_scrypt_n(262144)
        .with_scrypt_r(16)
        .with_scrypt_p(2)
        .with_key_length(32)
        .build_scrypt();

    EXPECT_EQ(config.n, 262144);
    EXPECT_EQ(config.r, 16);
    EXPECT_EQ(config.p, 2);
    EXPECT_EQ(config.key_length, 32);
}

// ============================================================================
// Key Derivation Params Tests
// ============================================================================

class KeyDerivationParamsTest : public ::testing::Test {};

TEST_F(KeyDerivationParamsTest, DefaultValues) {
    key_derivation_params params;

    EXPECT_EQ(params.kdf, key_derivation_function::argon2id);
    EXPECT_TRUE(params.salt.empty());
    EXPECT_EQ(params.iterations, 0);
    EXPECT_EQ(params.memory_kb, 0);
    EXPECT_EQ(params.parallelism, 0);
    EXPECT_EQ(params.block_size, 0);
    EXPECT_EQ(params.key_length, AES_256_KEY_SIZE);
}

TEST_F(KeyDerivationParamsTest, WithValues) {
    key_derivation_params params;
    params.kdf = key_derivation_function::scrypt;
    params.salt = {std::byte{0x01}, std::byte{0x02}};
    params.iterations = 131072;
    params.block_size = 8;
    params.parallelism = 1;
    params.key_length = 64;

    EXPECT_EQ(params.kdf, key_derivation_function::scrypt);
    EXPECT_EQ(params.salt.size(), 2);
    EXPECT_EQ(params.iterations, 131072);
    EXPECT_EQ(params.block_size, 8);
    EXPECT_EQ(params.parallelism, 1);
    EXPECT_EQ(params.key_length, 64);
}

// ============================================================================
// Derived Key Tests
// ============================================================================

class DerivedKeyTest : public ::testing::Test {};

TEST_F(DerivedKeyTest, DefaultValues) {
    derived_key key;

    EXPECT_TRUE(key.key.empty());
    EXPECT_EQ(key.params.kdf, key_derivation_function::argon2id);
}

TEST_F(DerivedKeyTest, WithValues) {
    derived_key key;
    key.key = {std::byte{0x00}, std::byte{0x01}, std::byte{0x02}};
    key.params.kdf = key_derivation_function::pbkdf2;
    key.params.iterations = 600000;

    EXPECT_EQ(key.key.size(), 3);
    EXPECT_EQ(key.params.kdf, key_derivation_function::pbkdf2);
    EXPECT_EQ(key.params.iterations, 600000);
}

// ============================================================================
// Encryption Statistics Tests
// ============================================================================

class EncryptionStatisticsTest : public ::testing::Test {};

TEST_F(EncryptionStatisticsTest, DefaultValues) {
    encryption_statistics stats;

    EXPECT_EQ(stats.bytes_encrypted, 0);
    EXPECT_EQ(stats.bytes_decrypted, 0);
    EXPECT_EQ(stats.encryption_ops, 0);
    EXPECT_EQ(stats.decryption_ops, 0);
    EXPECT_EQ(stats.errors, 0);
    EXPECT_EQ(stats.total_encrypt_time, std::chrono::microseconds{0});
    EXPECT_EQ(stats.total_decrypt_time, std::chrono::microseconds{0});
}

// ============================================================================
// Encryption Progress Tests
// ============================================================================

class EncryptionProgressTest : public ::testing::Test {};

TEST_F(EncryptionProgressTest, PercentageCalculation) {
    encryption_progress progress;
    progress.bytes_processed = 50;
    progress.total_bytes = 100;
    progress.is_encryption = true;

    EXPECT_DOUBLE_EQ(progress.percentage(), 50.0);
}

TEST_F(EncryptionProgressTest, ZeroTotalBytes) {
    encryption_progress progress;
    progress.bytes_processed = 0;
    progress.total_bytes = 0;
    progress.is_encryption = false;

    EXPECT_DOUBLE_EQ(progress.percentage(), 100.0);
}

TEST_F(EncryptionProgressTest, CompleteProgress) {
    encryption_progress progress;
    progress.bytes_processed = 1024;
    progress.total_bytes = 1024;
    progress.is_encryption = true;

    EXPECT_DOUBLE_EQ(progress.percentage(), 100.0);
}

TEST_F(EncryptionProgressTest, PartialProgress) {
    encryption_progress progress;
    progress.bytes_processed = 333;
    progress.total_bytes = 1000;
    progress.is_encryption = true;

    EXPECT_NEAR(progress.percentage(), 33.3, 0.1);
}

// ============================================================================
// KDF Constants Tests
// ============================================================================

class KdfConstantsTest : public ::testing::Test {};

TEST_F(KdfConstantsTest, Pbkdf2Defaults) {
    EXPECT_EQ(PBKDF2_DEFAULT_ITERATIONS, 600000);
}

TEST_F(KdfConstantsTest, Argon2Defaults) {
    EXPECT_EQ(ARGON2_DEFAULT_MEMORY_KB, 65536);  // 64 MB
    EXPECT_EQ(ARGON2_DEFAULT_TIME_COST, 3);
    EXPECT_EQ(ARGON2_DEFAULT_PARALLELISM, 4);
}

TEST_F(KdfConstantsTest, ScryptDefaults) {
    EXPECT_EQ(SCRYPT_DEFAULT_N, 131072);  // 2^17
    EXPECT_EQ(SCRYPT_DEFAULT_R, 8);
    EXPECT_EQ(SCRYPT_DEFAULT_P, 1);
}

}  // namespace
}  // namespace kcenon::file_transfer
