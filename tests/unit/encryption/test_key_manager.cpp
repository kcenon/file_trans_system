/**
 * @file test_key_manager.cpp
 * @brief Unit tests for key management system
 */

#ifdef FILE_TRANS_ENABLE_ENCRYPTION

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <thread>
#include <vector>

#include "kcenon/file_transfer/encryption/key_manager.h"

namespace kcenon::file_transfer {
namespace {

// ============================================================================
// Test Fixtures
// ============================================================================

class KeyManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        storage_ = memory_key_storage::create();
        ASSERT_NE(storage_, nullptr);

        manager_ = key_manager::create(memory_key_storage::create());
        ASSERT_NE(manager_, nullptr);
    }

    void TearDown() override {
        manager_.reset();
        storage_.reset();
    }

    std::unique_ptr<memory_key_storage> storage_;
    std::unique_ptr<key_manager> manager_;
};

class PBKDF2Test : public ::testing::Test {
protected:
    void SetUp() override {
        kdf_ = pbkdf2_key_derivation::create();
        ASSERT_NE(kdf_, nullptr);
    }

    std::unique_ptr<pbkdf2_key_derivation> kdf_;
};

class Argon2Test : public ::testing::Test {
protected:
    void SetUp() override {
        kdf_ = argon2_key_derivation::create();
        ASSERT_NE(kdf_, nullptr);
    }

    std::unique_ptr<argon2_key_derivation> kdf_;
};

// ============================================================================
// Memory Key Storage Tests
// ============================================================================

TEST(MemoryKeyStorageTest, CreateStorage) {
    auto storage = memory_key_storage::create();
    ASSERT_NE(storage, nullptr);
}

TEST(MemoryKeyStorageTest, StoreAndRetrieve) {
    auto storage = memory_key_storage::create();
    ASSERT_NE(storage, nullptr);

    std::vector<std::byte> key_data = {
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}
    };

    auto store_result = storage->store("test-key", key_data);
    EXPECT_TRUE(store_result.has_value());
    EXPECT_TRUE(storage->exists("test-key"));

    auto retrieve_result = storage->retrieve("test-key");
    ASSERT_TRUE(retrieve_result.has_value());
    EXPECT_EQ(retrieve_result.value(), key_data);
}

TEST(MemoryKeyStorageTest, RetrieveNonExistent) {
    auto storage = memory_key_storage::create();
    ASSERT_NE(storage, nullptr);

    auto result = storage->retrieve("nonexistent");
    EXPECT_FALSE(result.has_value());
}

TEST(MemoryKeyStorageTest, RemoveKey) {
    auto storage = memory_key_storage::create();
    ASSERT_NE(storage, nullptr);

    std::vector<std::byte> key_data = {std::byte{0x01}};
    storage->store("test-key", key_data);

    EXPECT_TRUE(storage->exists("test-key"));

    auto remove_result = storage->remove("test-key");
    EXPECT_TRUE(remove_result.has_value());
    EXPECT_FALSE(storage->exists("test-key"));
}

TEST(MemoryKeyStorageTest, ListKeys) {
    auto storage = memory_key_storage::create();
    ASSERT_NE(storage, nullptr);

    std::vector<std::byte> key_data = {std::byte{0x01}};
    storage->store("key-1", key_data);
    storage->store("key-2", key_data);
    storage->store("key-3", key_data);

    auto keys = storage->list_keys();
    EXPECT_EQ(keys.size(), 3);
}

// ============================================================================
// Key Manager Creation Tests
// ============================================================================

TEST_F(KeyManagerTest, CreateWithDefaultStorage) {
    auto manager = key_manager::create();
    ASSERT_NE(manager, nullptr);
}

TEST_F(KeyManagerTest, CreateWithCustomStorage) {
    auto storage = memory_key_storage::create();
    auto manager = key_manager::create(std::move(storage));
    ASSERT_NE(manager, nullptr);
}

// ============================================================================
// Random Key Generation Tests
// ============================================================================

TEST_F(KeyManagerTest, GenerateRandomKey) {
    auto result = manager_->generate_key("test-key");
    ASSERT_TRUE(result.has_value());

    const auto& key = result.value();
    EXPECT_EQ(key.key.size(), AES_256_KEY_SIZE);
    EXPECT_EQ(key.metadata.key_id, "test-key");
    EXPECT_TRUE(key.metadata.is_active);
    EXPECT_EQ(key.algorithm, encryption_algorithm::aes_256_gcm);
}

TEST_F(KeyManagerTest, GenerateRandomKeyCustomSize) {
    auto result = manager_->generate_key("test-key-16", 16);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().key.size(), 16);
}

TEST_F(KeyManagerTest, GenerateRandomBytes) {
    auto result = manager_->generate_random_bytes(64);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 64);
}

TEST_F(KeyManagerTest, GenerateRandomBytesZeroSize) {
    auto result = manager_->generate_random_bytes(0);
    EXPECT_FALSE(result.has_value());
}

TEST_F(KeyManagerTest, GeneratedKeysAreUnique) {
    auto result1 = manager_->generate_key("key-1");
    auto result2 = manager_->generate_key("key-2");

    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());

    EXPECT_NE(result1.value().key, result2.value().key);
}

// ============================================================================
// Key Storage Tests
// ============================================================================

TEST_F(KeyManagerTest, StoreAndRetrieveKey) {
    auto gen_result = manager_->generate_key("test-key");
    ASSERT_TRUE(gen_result.has_value());

    auto get_result = manager_->get_key("test-key");
    ASSERT_TRUE(get_result.has_value());

    EXPECT_EQ(gen_result.value().key, get_result.value().key);
}

TEST_F(KeyManagerTest, KeyExists) {
    manager_->generate_key("existing-key");

    EXPECT_TRUE(manager_->key_exists("existing-key"));
    EXPECT_FALSE(manager_->key_exists("nonexistent-key"));
}

TEST_F(KeyManagerTest, DeleteKey) {
    manager_->generate_key("to-delete");
    EXPECT_TRUE(manager_->key_exists("to-delete"));

    auto delete_result = manager_->delete_key("to-delete");
    EXPECT_TRUE(delete_result.has_value());
    EXPECT_FALSE(manager_->key_exists("to-delete"));
}

TEST_F(KeyManagerTest, ListKeys) {
    manager_->generate_key("key-a");
    manager_->generate_key("key-b");
    manager_->generate_key("key-c");

    auto keys = manager_->list_keys();
    EXPECT_EQ(keys.size(), 3);
}

// ============================================================================
// Key Rotation Tests
// ============================================================================

TEST_F(KeyManagerTest, RotateKey) {
    auto original = manager_->generate_key("rotate-me");
    ASSERT_TRUE(original.has_value());

    auto rotated = manager_->rotate_key("rotate-me");
    ASSERT_TRUE(rotated.has_value());

    EXPECT_NE(original.value().key, rotated.value().key);
    EXPECT_EQ(rotated.value().metadata.version, original.value().metadata.version + 1);
}

TEST_F(KeyManagerTest, GetKeyVersions) {
    manager_->generate_key("versioned-key");
    manager_->rotate_key("versioned-key");
    manager_->rotate_key("versioned-key");

    auto versions = manager_->get_key_versions("versioned-key");
    EXPECT_EQ(versions.size(), 2);  // Original + first rotation
}

TEST_F(KeyManagerTest, RotationPolicy) {
    key_rotation_policy policy;
    policy.auto_rotate = true;
    policy.max_uses = 100;
    policy.max_age = std::chrono::hours{24};
    policy.keep_versions = 5;

    manager_->set_rotation_policy(policy);

    auto retrieved = manager_->get_rotation_policy();
    EXPECT_TRUE(retrieved.auto_rotate);
    EXPECT_EQ(retrieved.max_uses, 100);
    EXPECT_EQ(retrieved.keep_versions, 5);
}

TEST_F(KeyManagerTest, NeedsRotation) {
    key_rotation_policy policy;
    policy.auto_rotate = true;
    policy.max_uses = 5;

    manager_->set_rotation_policy(policy);
    manager_->generate_key("usage-key");

    EXPECT_FALSE(manager_->needs_rotation("usage-key"));

    for (int i = 0; i < 5; ++i) {
        manager_->record_usage("usage-key");
    }

    EXPECT_TRUE(manager_->needs_rotation("usage-key"));
}

// ============================================================================
// Usage Tracking Tests
// ============================================================================

TEST_F(KeyManagerTest, RecordUsage) {
    manager_->generate_key("tracked-key");

    for (int i = 0; i < 10; ++i) {
        manager_->record_usage("tracked-key");
    }

    auto stats = manager_->get_usage_stats("tracked-key");
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats.value().usage_count, 10);
}

// ============================================================================
// Secure Memory Tests
// ============================================================================

TEST_F(KeyManagerTest, SecureZero) {
    std::vector<std::byte> data = {
        std::byte{0xFF}, std::byte{0xAB}, std::byte{0xCD}, std::byte{0xEF}
    };

    key_manager::secure_zero(data);

    for (auto byte : data) {
        EXPECT_EQ(byte, std::byte{0});
    }
}

TEST_F(KeyManagerTest, ConstantTimeCompare) {
    std::vector<std::byte> a = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    std::vector<std::byte> b = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    std::vector<std::byte> c = {std::byte{0x01}, std::byte{0x02}, std::byte{0x04}};

    EXPECT_TRUE(key_manager::constant_time_compare(a, b));
    EXPECT_FALSE(key_manager::constant_time_compare(a, c));
}

TEST_F(KeyManagerTest, ConstantTimeCompareDifferentSize) {
    std::vector<std::byte> a = {std::byte{0x01}, std::byte{0x02}};
    std::vector<std::byte> b = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    EXPECT_FALSE(key_manager::constant_time_compare(a, b));
}

// ============================================================================
// PBKDF2 Key Derivation Tests
// ============================================================================

TEST_F(PBKDF2Test, CreateWithDefaultConfig) {
    EXPECT_EQ(kdf_->type(), key_derivation_function::pbkdf2);
    EXPECT_EQ(kdf_->key_length(), AES_256_KEY_SIZE);
    EXPECT_EQ(kdf_->salt_length(), SALT_SIZE);
}

TEST_F(PBKDF2Test, DeriveKeyWithAutoSalt) {
    auto result = kdf_->derive_key("test-password");
    ASSERT_TRUE(result.has_value());

    const auto& derived = result.value();
    EXPECT_EQ(derived.key.size(), AES_256_KEY_SIZE);
    EXPECT_FALSE(derived.params.salt.empty());
    EXPECT_EQ(derived.params.kdf, key_derivation_function::pbkdf2);
}

TEST_F(PBKDF2Test, DeriveKeyWithExplicitSalt) {
    std::vector<std::byte> salt(SALT_SIZE);
    for (size_t i = 0; i < salt.size(); ++i) {
        salt[i] = static_cast<std::byte>(i);
    }

    auto result = kdf_->derive_key("test-password", salt);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().params.salt, salt);
}

TEST_F(PBKDF2Test, DeriveKeyDeterministic) {
    std::vector<std::byte> salt(SALT_SIZE);
    for (size_t i = 0; i < salt.size(); ++i) {
        salt[i] = static_cast<std::byte>(i);
    }

    auto result1 = kdf_->derive_key("same-password", salt);
    auto result2 = kdf_->derive_key("same-password", salt);

    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result1.value().key, result2.value().key);
}

TEST_F(PBKDF2Test, DifferentPasswordsDifferentKeys) {
    auto salt_result = kdf_->generate_salt();
    ASSERT_TRUE(salt_result.has_value());

    auto result1 = kdf_->derive_key("password1", salt_result.value());
    auto result2 = kdf_->derive_key("password2", salt_result.value());

    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());
    EXPECT_NE(result1.value().key, result2.value().key);
}

TEST_F(PBKDF2Test, DeriveKeyWithParams) {
    auto initial = kdf_->derive_key("test-password");
    ASSERT_TRUE(initial.has_value());

    auto rederived = kdf_->derive_key_with_params("test-password", initial.value().params);
    ASSERT_TRUE(rederived.has_value());

    EXPECT_EQ(initial.value().key, rederived.value().key);
}

TEST_F(PBKDF2Test, GenerateSalt) {
    auto salt1 = kdf_->generate_salt();
    auto salt2 = kdf_->generate_salt();

    ASSERT_TRUE(salt1.has_value());
    ASSERT_TRUE(salt2.has_value());

    EXPECT_EQ(salt1.value().size(), SALT_SIZE);
    EXPECT_NE(salt1.value(), salt2.value());  // Random salts should differ
}

TEST_F(PBKDF2Test, ValidatePassword) {
    EXPECT_TRUE(kdf_->validate_password("valid-password").has_value());
    EXPECT_FALSE(kdf_->validate_password("").has_value());
    EXPECT_FALSE(kdf_->validate_password("short").has_value());
}

TEST_F(PBKDF2Test, SecureZero) {
    std::vector<std::byte> data = {std::byte{0xFF}, std::byte{0xAB}};
    kdf_->secure_zero(data);
    for (auto byte : data) {
        EXPECT_EQ(byte, std::byte{0});
    }
}

TEST_F(PBKDF2Test, EmptyPasswordFails) {
    auto result = kdf_->derive_key("");
    EXPECT_FALSE(result.has_value());
}

TEST_F(PBKDF2Test, ShortSaltFails) {
    std::vector<std::byte> short_salt = {std::byte{0x01}};
    auto result = kdf_->derive_key("password", short_salt);
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// Argon2 Key Derivation Tests
// ============================================================================

TEST_F(Argon2Test, CreateWithDefaultConfig) {
    EXPECT_EQ(kdf_->type(), key_derivation_function::argon2id);
    EXPECT_EQ(kdf_->key_length(), AES_256_KEY_SIZE);
}

TEST_F(Argon2Test, CheckAvailability) {
    // Should not crash regardless of availability
    [[maybe_unused]] bool available = argon2_key_derivation::is_available();
}

TEST_F(Argon2Test, DeriveKeyWithAutoSalt) {
    auto result = kdf_->derive_key("test-password");
    ASSERT_TRUE(result.has_value());

    const auto& derived = result.value();
    EXPECT_EQ(derived.key.size(), AES_256_KEY_SIZE);
    EXPECT_FALSE(derived.params.salt.empty());
}

TEST_F(Argon2Test, DeriveKeyDeterministic) {
    std::vector<std::byte> salt(SALT_SIZE);
    for (size_t i = 0; i < salt.size(); ++i) {
        salt[i] = static_cast<std::byte>(i);
    }

    auto result1 = kdf_->derive_key("same-password", salt);
    auto result2 = kdf_->derive_key("same-password", salt);

    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result1.value().key, result2.value().key);
}

TEST_F(Argon2Test, DeriveKeyWithParams) {
    auto initial = kdf_->derive_key("test-password");
    ASSERT_TRUE(initial.has_value());

    auto rederived = kdf_->derive_key_with_params("test-password", initial.value().params);
    ASSERT_TRUE(rederived.has_value());

    EXPECT_EQ(initial.value().key, rederived.value().key);
}

TEST_F(Argon2Test, ValidatePassword) {
    EXPECT_TRUE(kdf_->validate_password("valid-password").has_value());
    EXPECT_FALSE(kdf_->validate_password("").has_value());
    EXPECT_FALSE(kdf_->validate_password("short").has_value());
}

// ============================================================================
// Password-based Key Derivation via Key Manager
// ============================================================================

TEST_F(KeyManagerTest, DeriveKeyFromPasswordArgon2) {
    auto result = manager_->derive_key_from_password("password-key", "secure-password-123");
    ASSERT_TRUE(result.has_value());

    const auto& key = result.value();
    EXPECT_EQ(key.key.size(), AES_256_KEY_SIZE);
    EXPECT_EQ(key.metadata.key_id, "password-key");
    EXPECT_TRUE(key.metadata.derivation_params.has_value());
}

TEST_F(KeyManagerTest, DeriveKeyPBKDF2) {
    auto result = manager_->derive_key_pbkdf2("pbkdf2-key", "secure-password-123");
    ASSERT_TRUE(result.has_value());

    const auto& key = result.value();
    EXPECT_EQ(key.key.size(), AES_256_KEY_SIZE);
    EXPECT_TRUE(key.metadata.derivation_params.has_value());
    EXPECT_EQ(key.metadata.derivation_params->kdf, key_derivation_function::pbkdf2);
}

TEST_F(KeyManagerTest, RederiveKey) {
    auto initial = manager_->derive_key_pbkdf2("rederive-key", "my-password");
    ASSERT_TRUE(initial.has_value());

    auto rederived = manager_->rederive_key("rederive-key", "my-password");
    ASSERT_TRUE(rederived.has_value());

    EXPECT_EQ(initial.value().key, rederived.value().key);
}

TEST_F(KeyManagerTest, RederiveKeyWrongPassword) {
    auto initial = manager_->derive_key_pbkdf2("wrong-pass-key", "correct-password");
    ASSERT_TRUE(initial.has_value());

    auto rederived = manager_->rederive_key("wrong-pass-key", "wrong-password");
    ASSERT_TRUE(rederived.has_value());

    // Keys should be different with wrong password
    EXPECT_NE(initial.value().key, rederived.value().key);
}

// ============================================================================
// Metadata Export/Import Tests
// ============================================================================

TEST_F(KeyManagerTest, ExportImportMetadata) {
    manager_->generate_key("export-key");

    auto export_result = manager_->export_key_metadata("export-key");
    ASSERT_TRUE(export_result.has_value());
    EXPECT_FALSE(export_result.value().empty());

    auto import_result = manager_->import_key_metadata(export_result.value());
    ASSERT_TRUE(import_result.has_value());
    EXPECT_EQ(import_result.value().key_id, "export-key");
}

// ============================================================================
// Concurrency Tests
// ============================================================================

TEST_F(KeyManagerTest, ConcurrentKeyGeneration) {
    constexpr int num_threads = 4;
    constexpr int keys_per_thread = 10;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, t, &success_count]() {
            for (int i = 0; i < keys_per_thread; ++i) {
                std::string key_id = "thread-" + std::to_string(t) + "-key-" + std::to_string(i);
                auto result = manager_->generate_key(key_id);
                if (result.has_value()) {
                    ++success_count;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(success_count.load(), num_threads * keys_per_thread);
}

TEST_F(KeyManagerTest, ConcurrentKeyAccess) {
    manager_->generate_key("shared-key");

    constexpr int num_threads = 4;
    constexpr int reads_per_thread = 20;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, &success_count]() {
            for (int i = 0; i < reads_per_thread; ++i) {
                auto result = manager_->get_key("shared-key");
                if (result.has_value()) {
                    ++success_count;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(success_count.load(), num_threads * reads_per_thread);
}

}  // namespace
}  // namespace kcenon::file_transfer

#else  // FILE_TRANS_ENABLE_ENCRYPTION

#include <gtest/gtest.h>

TEST(KeyManagerDisabledTest, EncryptionNotEnabled) {
    // Placeholder test when encryption is disabled
    EXPECT_TRUE(true);
}

#endif  // FILE_TRANS_ENABLE_ENCRYPTION
