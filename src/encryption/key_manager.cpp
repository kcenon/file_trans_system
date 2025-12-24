/**
 * @file key_manager.cpp
 * @brief Secure key management system implementation
 */

#ifdef FILE_TRANS_ENABLE_ENCRYPTION

#include "kcenon/file_transfer/encryption/key_manager.h"

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <shared_mutex>

namespace kcenon::file_transfer {

namespace {

/**
 * @brief Securely zero memory
 */
void secure_zero_memory(void* ptr, std::size_t size) {
    if (ptr != nullptr && size > 0) {
        OPENSSL_cleanse(ptr, size);
    }
}

/**
 * @brief Generate a unique key ID
 */
auto generate_key_id() -> std::string {
    std::vector<std::byte> random_bytes(16);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(random_bytes.data()),
                   static_cast<int>(random_bytes.size())) != 1) {
        return "";
    }

    static constexpr char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(random_bytes.size() * 2);
    for (auto byte : random_bytes) {
        result.push_back(hex_chars[static_cast<uint8_t>(byte) >> 4]);
        result.push_back(hex_chars[static_cast<uint8_t>(byte) & 0x0F]);
    }
    return result;
}

}  // namespace

// ============================================================================
// memory_key_storage::impl
// ============================================================================

struct memory_key_storage::impl {
    mutable std::shared_mutex mutex;
    std::map<std::string, std::vector<std::byte>> keys;

    ~impl() {
        std::unique_lock lock(mutex);
        for (auto& [id, key] : keys) {
            secure_zero_memory(key.data(), key.size());
        }
        keys.clear();
    }
};

// ============================================================================
// memory_key_storage
// ============================================================================

memory_key_storage::memory_key_storage()
    : impl_(std::make_unique<impl>()) {}

memory_key_storage::~memory_key_storage() = default;

memory_key_storage::memory_key_storage(memory_key_storage&&) noexcept = default;
auto memory_key_storage::operator=(memory_key_storage&&) noexcept
    -> memory_key_storage& = default;

auto memory_key_storage::create() -> std::unique_ptr<memory_key_storage> {
    return std::unique_ptr<memory_key_storage>(new memory_key_storage());
}

auto memory_key_storage::store(
    const std::string& key_id,
    std::span<const std::byte> key_data) -> result<void> {
    if (key_id.empty()) {
        return unexpected(error(error_code::invalid_configuration, "Empty key ID"));
    }

    std::unique_lock lock(impl_->mutex);
    impl_->keys[key_id] = std::vector<std::byte>(key_data.begin(), key_data.end());
    return {};
}

auto memory_key_storage::retrieve(
    const std::string& key_id) -> result<std::vector<std::byte>> {
    std::shared_lock lock(impl_->mutex);

    auto it = impl_->keys.find(key_id);
    if (it == impl_->keys.end()) {
        return unexpected(error(error_code::file_not_found, "Key not found: " + key_id));
    }

    return it->second;
}

auto memory_key_storage::remove(
    const std::string& key_id) -> result<void> {
    std::unique_lock lock(impl_->mutex);

    auto it = impl_->keys.find(key_id);
    if (it == impl_->keys.end()) {
        return unexpected(error(error_code::file_not_found, "Key not found: " + key_id));
    }

    secure_zero_memory(it->second.data(), it->second.size());
    impl_->keys.erase(it);
    return {};
}

auto memory_key_storage::exists(const std::string& key_id) -> bool {
    std::shared_lock lock(impl_->mutex);
    return impl_->keys.count(key_id) > 0;
}

auto memory_key_storage::list_keys() -> std::vector<std::string> {
    std::shared_lock lock(impl_->mutex);

    std::vector<std::string> result;
    result.reserve(impl_->keys.size());
    for (const auto& [id, _] : impl_->keys) {
        result.push_back(id);
    }
    return result;
}

// ============================================================================
// key_manager::impl
// ============================================================================

struct key_manager::impl {
    std::unique_ptr<key_storage_interface> storage;
    mutable std::shared_mutex mutex;
    std::map<std::string, key_metadata> metadata_cache;
    std::map<std::string, std::vector<managed_key>> key_versions;
    key_rotation_policy rotation_policy;

    explicit impl(std::unique_ptr<key_storage_interface> stor)
        : storage(std::move(stor)) {
        if (!storage) {
            storage = memory_key_storage::create();
        }
    }

    auto get_or_create_metadata(const std::string& key_id) -> key_metadata& {
        auto it = metadata_cache.find(key_id);
        if (it == metadata_cache.end()) {
            key_metadata meta;
            meta.key_id = key_id;
            meta.created_at = std::chrono::system_clock::now();
            meta.last_used_at = meta.created_at;
            it = metadata_cache.emplace(key_id, std::move(meta)).first;
        }
        return it->second;
    }
};

// ============================================================================
// key_manager
// ============================================================================

key_manager::key_manager(std::unique_ptr<key_storage_interface> storage)
    : impl_(std::make_unique<impl>(std::move(storage))) {}

key_manager::~key_manager() = default;

key_manager::key_manager(key_manager&&) noexcept = default;
auto key_manager::operator=(key_manager&&) noexcept -> key_manager& = default;

auto key_manager::create(
    std::unique_ptr<key_storage_interface> storage) -> std::unique_ptr<key_manager> {
    return std::unique_ptr<key_manager>(new key_manager(std::move(storage)));
}

auto key_manager::generate_key(
    const std::string& key_id,
    std::size_t key_size,
    encryption_algorithm algorithm) -> result<managed_key> {
    if (key_id.empty()) {
        return unexpected(error(error_code::invalid_configuration, "Empty key ID"));
    }

    auto random_result = generate_random_bytes(key_size);
    if (!random_result.has_value()) {
        return unexpected(random_result.error());
    }

    managed_key key;
    key.key = std::move(random_result.value());
    key.algorithm = algorithm;
    key.metadata.key_id = key_id;
    key.metadata.created_at = std::chrono::system_clock::now();
    key.metadata.last_used_at = key.metadata.created_at;
    key.metadata.is_active = true;

    auto store_result = store_key(key);
    if (!store_result.has_value()) {
        secure_zero(key.key);
        return unexpected(store_result.error());
    }

    return key;
}

auto key_manager::generate_random_bytes(std::size_t size) -> result<std::vector<std::byte>> {
    if (size == 0) {
        return unexpected(error(error_code::invalid_configuration, "Size must be positive"));
    }

    std::vector<std::byte> bytes(size);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()),
                   static_cast<int>(size)) != 1) {
        return unexpected(error(error_code::internal_error, "Failed to generate random bytes"));
    }

    return bytes;
}

auto key_manager::derive_key_from_password(
    const std::string& key_id,
    std::string_view password,
    const argon2_config& config) -> result<managed_key> {
    auto kdf = argon2_key_derivation::create(config);
    if (!kdf) {
        return unexpected(error(error_code::internal_error, "Failed to create Argon2 KDF"));
    }

    auto derived_result = kdf->derive_key(password);
    if (!derived_result.has_value()) {
        return unexpected(derived_result.error());
    }

    auto& derived = derived_result.value();

    managed_key key;
    key.key = std::move(derived.key);
    key.algorithm = encryption_algorithm::aes_256_gcm;
    key.metadata.key_id = key_id;
    key.metadata.derivation_params = derived.params;
    key.metadata.created_at = std::chrono::system_clock::now();
    key.metadata.last_used_at = key.metadata.created_at;
    key.metadata.is_active = true;

    auto store_result = store_key(key);
    if (!store_result.has_value()) {
        secure_zero(key.key);
        return unexpected(store_result.error());
    }

    return key;
}

auto key_manager::derive_key_pbkdf2(
    const std::string& key_id,
    std::string_view password,
    const pbkdf2_config& config) -> result<managed_key> {
    auto kdf = pbkdf2_key_derivation::create(config);
    if (!kdf) {
        return unexpected(error(error_code::internal_error, "Failed to create PBKDF2 KDF"));
    }

    auto derived_result = kdf->derive_key(password);
    if (!derived_result.has_value()) {
        return unexpected(derived_result.error());
    }

    auto& derived = derived_result.value();

    managed_key key;
    key.key = std::move(derived.key);
    key.algorithm = encryption_algorithm::aes_256_gcm;
    key.metadata.key_id = key_id;
    key.metadata.derivation_params = derived.params;
    key.metadata.created_at = std::chrono::system_clock::now();
    key.metadata.last_used_at = key.metadata.created_at;
    key.metadata.is_active = true;

    auto store_result = store_key(key);
    if (!store_result.has_value()) {
        secure_zero(key.key);
        return unexpected(store_result.error());
    }

    return key;
}

auto key_manager::rederive_key(
    const std::string& key_id,
    std::string_view password) -> result<managed_key> {
    std::shared_lock lock(impl_->mutex);

    auto it = impl_->metadata_cache.find(key_id);
    if (it == impl_->metadata_cache.end() || !it->second.derivation_params.has_value()) {
        return unexpected(error(error_code::file_not_found,
                               "Key metadata or derivation params not found"));
    }

    const auto& params = it->second.derivation_params.value();
    lock.unlock();

    std::unique_ptr<key_derivation_interface> kdf;
    if (params.kdf == key_derivation_function::argon2id) {
        argon2_config config;
        config.memory_kb = params.memory_kb;
        config.time_cost = params.iterations;
        config.parallelism = params.parallelism;
        config.key_length = params.key_length;
        kdf = argon2_key_derivation::create(config);
    } else if (params.kdf == key_derivation_function::pbkdf2) {
        pbkdf2_config config;
        config.iterations = params.iterations;
        config.key_length = params.key_length;
        kdf = pbkdf2_key_derivation::create(config);
    } else {
        return unexpected(error(error_code::invalid_configuration, "Unsupported KDF type"));
    }

    if (!kdf) {
        return unexpected(error(error_code::internal_error, "Failed to create KDF"));
    }

    auto derived_result = kdf->derive_key_with_params(password, params);
    if (!derived_result.has_value()) {
        return unexpected(derived_result.error());
    }

    auto& derived = derived_result.value();

    managed_key key;
    key.key = std::move(derived.key);
    key.algorithm = encryption_algorithm::aes_256_gcm;
    key.metadata = it->second;
    key.metadata.last_used_at = std::chrono::system_clock::now();

    return key;
}

auto key_manager::store_key(const managed_key& key) -> result<void> {
    auto store_result = impl_->storage->store(key.metadata.key_id, key.key);
    if (!store_result.has_value()) {
        return unexpected(store_result.error());
    }

    std::unique_lock lock(impl_->mutex);
    impl_->metadata_cache[key.metadata.key_id] = key.metadata;
    return {};
}

auto key_manager::get_key(const std::string& key_id) -> result<managed_key> {
    auto key_data = impl_->storage->retrieve(key_id);
    if (!key_data.has_value()) {
        return unexpected(key_data.error());
    }

    std::shared_lock lock(impl_->mutex);
    auto it = impl_->metadata_cache.find(key_id);
    if (it == impl_->metadata_cache.end()) {
        return unexpected(error(error_code::file_not_found, "Key metadata not found"));
    }

    managed_key key;
    key.key = std::move(key_data.value());
    key.metadata = it->second;
    key.metadata.last_used_at = std::chrono::system_clock::now();
    key.metadata.usage_count++;

    return key;
}

auto key_manager::delete_key(const std::string& key_id) -> result<void> {
    auto remove_result = impl_->storage->remove(key_id);
    if (!remove_result.has_value()) {
        return unexpected(remove_result.error());
    }

    std::unique_lock lock(impl_->mutex);
    impl_->metadata_cache.erase(key_id);
    impl_->key_versions.erase(key_id);
    return {};
}

auto key_manager::key_exists(const std::string& key_id) -> bool {
    return impl_->storage->exists(key_id);
}

auto key_manager::list_keys() -> std::vector<key_metadata> {
    std::shared_lock lock(impl_->mutex);

    std::vector<key_metadata> result;
    result.reserve(impl_->metadata_cache.size());
    for (const auto& [_, meta] : impl_->metadata_cache) {
        result.push_back(meta);
    }
    return result;
}

void key_manager::set_rotation_policy(const key_rotation_policy& policy) {
    std::unique_lock lock(impl_->mutex);
    impl_->rotation_policy = policy;
}

auto key_manager::get_rotation_policy() const -> key_rotation_policy {
    std::shared_lock lock(impl_->mutex);
    return impl_->rotation_policy;
}

auto key_manager::rotate_key(const std::string& key_id) -> result<managed_key> {
    auto old_key_result = get_key(key_id);
    if (!old_key_result.has_value()) {
        return unexpected(old_key_result.error());
    }

    auto& old_key = old_key_result.value();

    // Store old key version
    {
        std::unique_lock lock(impl_->mutex);
        impl_->key_versions[key_id].push_back(old_key);

        // Limit stored versions
        auto& versions = impl_->key_versions[key_id];
        while (versions.size() > impl_->rotation_policy.keep_versions) {
            secure_zero(versions.front().key);
            versions.erase(versions.begin());
        }
    }

    // Generate new key
    auto new_key_result = generate_key(
        key_id,
        old_key.key.size(),
        old_key.algorithm);

    if (!new_key_result.has_value()) {
        return unexpected(new_key_result.error());
    }

    auto& new_key = new_key_result.value();
    new_key.metadata.version = old_key.metadata.version + 1;

    // Update metadata
    {
        std::unique_lock lock(impl_->mutex);
        impl_->metadata_cache[key_id] = new_key.metadata;
    }

    return new_key;
}

auto key_manager::needs_rotation(const std::string& key_id) -> bool {
    std::shared_lock lock(impl_->mutex);

    auto it = impl_->metadata_cache.find(key_id);
    if (it == impl_->metadata_cache.end()) {
        return false;
    }

    const auto& meta = it->second;
    const auto& policy = impl_->rotation_policy;

    if (!policy.auto_rotate) {
        return false;
    }

    // Check usage count
    if (meta.usage_count >= policy.max_uses) {
        return true;
    }

    // Check age
    auto age = std::chrono::system_clock::now() - meta.created_at;
    if (age >= policy.max_age) {
        return true;
    }

    // Check expiration
    if (meta.expires_at.has_value() &&
        std::chrono::system_clock::now() >= meta.expires_at.value()) {
        return true;
    }

    return false;
}

auto key_manager::get_key_versions(
    const std::string& key_id) -> std::vector<managed_key> {
    std::shared_lock lock(impl_->mutex);

    auto it = impl_->key_versions.find(key_id);
    if (it == impl_->key_versions.end()) {
        return {};
    }

    return it->second;
}

auto key_manager::export_key_metadata(
    const std::string& key_id) -> result<std::vector<std::byte>> {
    std::shared_lock lock(impl_->mutex);

    auto it = impl_->metadata_cache.find(key_id);
    if (it == impl_->metadata_cache.end()) {
        return unexpected(error(error_code::file_not_found, "Key metadata not found"));
    }

    const auto& meta = it->second;

    // Simple serialization: key_id + version + algorithm
    std::string serialized = meta.key_id + "|" +
                             std::to_string(meta.version) + "|" +
                             std::to_string(meta.usage_count);

    std::vector<std::byte> result(serialized.size());
    std::memcpy(result.data(), serialized.data(), serialized.size());

    return result;
}

auto key_manager::import_key_metadata(
    std::span<const std::byte> data) -> result<key_metadata> {
    std::string serialized(reinterpret_cast<const char*>(data.data()), data.size());

    // Parse: key_id|version|usage_count
    auto pos1 = serialized.find('|');
    auto pos2 = serialized.rfind('|');

    if (pos1 == std::string::npos || pos1 == pos2) {
        return unexpected(error(error_code::invalid_configuration, "Invalid metadata format"));
    }

    key_metadata meta;
    meta.key_id = serialized.substr(0, pos1);
    meta.version = static_cast<uint32_t>(std::stoul(serialized.substr(pos1 + 1, pos2 - pos1 - 1)));
    meta.usage_count = std::stoull(serialized.substr(pos2 + 1));
    meta.created_at = std::chrono::system_clock::now();
    meta.last_used_at = meta.created_at;

    return meta;
}

void key_manager::secure_zero(std::span<std::byte> data) {
    secure_zero_memory(data.data(), data.size());
}

auto key_manager::constant_time_compare(
    std::span<const std::byte> a,
    std::span<const std::byte> b) -> bool {
    if (a.size() != b.size()) {
        return false;
    }

    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

void key_manager::record_usage(const std::string& key_id) {
    std::unique_lock lock(impl_->mutex);

    auto it = impl_->metadata_cache.find(key_id);
    if (it != impl_->metadata_cache.end()) {
        it->second.usage_count++;
        it->second.last_used_at = std::chrono::system_clock::now();
    }
}

auto key_manager::get_usage_stats(
    const std::string& key_id) -> result<key_metadata> {
    std::shared_lock lock(impl_->mutex);

    auto it = impl_->metadata_cache.find(key_id);
    if (it == impl_->metadata_cache.end()) {
        return unexpected(error(error_code::file_not_found, "Key metadata not found"));
    }

    return it->second;
}

// ============================================================================
// pbkdf2_key_derivation::impl
// ============================================================================

struct pbkdf2_key_derivation::impl {
    pbkdf2_config config;

    explicit impl(const pbkdf2_config& cfg) : config(cfg) {}
};

// ============================================================================
// pbkdf2_key_derivation
// ============================================================================

pbkdf2_key_derivation::pbkdf2_key_derivation(const pbkdf2_config& config)
    : impl_(std::make_unique<impl>(config)) {}

pbkdf2_key_derivation::~pbkdf2_key_derivation() = default;

pbkdf2_key_derivation::pbkdf2_key_derivation(pbkdf2_key_derivation&&) noexcept = default;
auto pbkdf2_key_derivation::operator=(pbkdf2_key_derivation&&) noexcept
    -> pbkdf2_key_derivation& = default;

auto pbkdf2_key_derivation::create(
    const pbkdf2_config& config) -> std::unique_ptr<pbkdf2_key_derivation> {
    return std::unique_ptr<pbkdf2_key_derivation>(new pbkdf2_key_derivation(config));
}

auto pbkdf2_key_derivation::type() const -> key_derivation_function {
    return key_derivation_function::pbkdf2;
}

auto pbkdf2_key_derivation::derive_key(
    std::string_view password,
    std::span<const std::byte> salt) -> result<derived_key> {
    if (password.empty()) {
        return unexpected(error(error_code::invalid_configuration, "Empty password"));
    }

    if (salt.size() < 8) {
        return unexpected(error(error_code::invalid_configuration, "Salt too short"));
    }

    derived_key result;
    result.key.resize(impl_->config.key_length);

    if (PKCS5_PBKDF2_HMAC(
            password.data(),
            static_cast<int>(password.size()),
            reinterpret_cast<const unsigned char*>(salt.data()),
            static_cast<int>(salt.size()),
            static_cast<int>(impl_->config.iterations),
            EVP_sha256(),
            static_cast<int>(impl_->config.key_length),
            reinterpret_cast<unsigned char*>(result.key.data())) != 1) {
        return unexpected(error(error_code::internal_error, "PBKDF2 derivation failed"));
    }

    result.params.kdf = key_derivation_function::pbkdf2;
    result.params.salt.assign(salt.begin(), salt.end());
    result.params.iterations = impl_->config.iterations;
    result.params.key_length = impl_->config.key_length;

    return result;
}

auto pbkdf2_key_derivation::derive_key(
    std::string_view password) -> result<derived_key> {
    auto salt_result = generate_salt();
    if (!salt_result.has_value()) {
        return unexpected(salt_result.error());
    }

    return derive_key(password, salt_result.value());
}

auto pbkdf2_key_derivation::derive_key(
    std::span<const std::byte> key_material,
    std::span<const std::byte> salt) -> result<derived_key> {
    std::string_view password(
        reinterpret_cast<const char*>(key_material.data()),
        key_material.size());
    return derive_key(password, salt);
}

auto pbkdf2_key_derivation::derive_key_with_params(
    std::string_view password,
    const key_derivation_params& params) -> result<derived_key> {
    if (params.kdf != key_derivation_function::pbkdf2) {
        return unexpected(error(error_code::invalid_configuration, "Invalid KDF type"));
    }

    pbkdf2_config config;
    config.iterations = params.iterations;
    config.key_length = params.key_length;

    auto kdf = create(config);
    return kdf->derive_key(password, params.salt);
}

auto pbkdf2_key_derivation::generate_salt(std::size_t length) -> result<std::vector<std::byte>> {
    std::vector<std::byte> salt(length);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(salt.data()),
                   static_cast<int>(length)) != 1) {
        return unexpected(error(error_code::internal_error, "Failed to generate salt"));
    }
    return salt;
}

auto pbkdf2_key_derivation::key_length() const -> std::size_t {
    return impl_->config.key_length;
}

auto pbkdf2_key_derivation::salt_length() const -> std::size_t {
    return impl_->config.salt_length;
}

auto pbkdf2_key_derivation::validate_password(std::string_view password) -> result<void> {
    if (password.empty()) {
        return unexpected(error(error_code::invalid_configuration, "Password cannot be empty"));
    }
    if (password.size() < 8) {
        return unexpected(error(error_code::invalid_configuration,
                               "Password too short (minimum 8 characters)"));
    }
    return {};
}

void pbkdf2_key_derivation::secure_zero(std::span<std::byte> data) {
    secure_zero_memory(data.data(), data.size());
}

// ============================================================================
// argon2_key_derivation::impl
// ============================================================================

struct argon2_key_derivation::impl {
    argon2_config config;
    bool argon2_available;

    explicit impl(const argon2_config& cfg)
        : config(cfg)
        , argon2_available(argon2_key_derivation::is_available()) {}
};

// ============================================================================
// argon2_key_derivation
// ============================================================================

argon2_key_derivation::argon2_key_derivation(const argon2_config& config)
    : impl_(std::make_unique<impl>(config)) {}

argon2_key_derivation::~argon2_key_derivation() = default;

argon2_key_derivation::argon2_key_derivation(argon2_key_derivation&&) noexcept = default;
auto argon2_key_derivation::operator=(argon2_key_derivation&&) noexcept
    -> argon2_key_derivation& = default;

auto argon2_key_derivation::create(
    const argon2_config& config) -> std::unique_ptr<argon2_key_derivation> {
    return std::unique_ptr<argon2_key_derivation>(new argon2_key_derivation(config));
}

auto argon2_key_derivation::is_available() -> bool {
#ifdef HAVE_ARGON2
    return true;
#else
    // Use OpenSSL's EVP_KDF for Argon2 if available (OpenSSL 3.2+)
#if OPENSSL_VERSION_NUMBER >= 0x30200000L
    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr);
    if (kdf) {
        EVP_KDF_free(kdf);
        return true;
    }
#endif
    return false;
#endif
}

auto argon2_key_derivation::type() const -> key_derivation_function {
    return key_derivation_function::argon2id;
}

auto argon2_key_derivation::derive_key(
    std::string_view password,
    std::span<const std::byte> salt) -> result<derived_key> {
    if (password.empty()) {
        return unexpected(error(error_code::invalid_configuration, "Empty password"));
    }

    if (salt.size() < 8) {
        return unexpected(error(error_code::invalid_configuration, "Salt too short"));
    }

    derived_key result;
    result.key.resize(impl_->config.key_length);

#if OPENSSL_VERSION_NUMBER >= 0x30200000L
    // Try OpenSSL Argon2 (OpenSSL 3.2+)
    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr);
    if (kdf) {
        EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
        EVP_KDF_free(kdf);

        if (ctx) {
            uint32_t threads = impl_->config.parallelism;
            uint32_t lanes = impl_->config.parallelism;
            uint32_t memcost = impl_->config.memory_kb;
            uint32_t iterations = impl_->config.time_cost;

            OSSL_PARAM params[] = {
                OSSL_PARAM_construct_octet_string(
                    "pass",
                    const_cast<char*>(password.data()),
                    password.size()),
                OSSL_PARAM_construct_octet_string(
                    "salt",
                    const_cast<std::byte*>(salt.data()),
                    salt.size()),
                OSSL_PARAM_construct_uint32("threads", &threads),
                OSSL_PARAM_construct_uint32("lanes", &lanes),
                OSSL_PARAM_construct_uint32("memcost", &memcost),
                OSSL_PARAM_construct_uint32("iter", &iterations),
                OSSL_PARAM_construct_end()
            };

            int rc = EVP_KDF_derive(
                ctx,
                reinterpret_cast<unsigned char*>(result.key.data()),
                result.key.size(),
                params);

            EVP_KDF_CTX_free(ctx);

            if (rc == 1) {
                result.params.kdf = key_derivation_function::argon2id;
                result.params.salt.assign(salt.begin(), salt.end());
                result.params.iterations = impl_->config.time_cost;
                result.params.memory_kb = impl_->config.memory_kb;
                result.params.parallelism = impl_->config.parallelism;
                result.params.key_length = impl_->config.key_length;
                return result;
            }
        }
    }
#endif

    // Fallback to PBKDF2 with high iteration count
    pbkdf2_config fallback_config;
    fallback_config.iterations = std::max(
        PBKDF2_DEFAULT_ITERATIONS,
        impl_->config.time_cost * 100000u);
    fallback_config.key_length = impl_->config.key_length;

    auto pbkdf2 = pbkdf2_key_derivation::create(fallback_config);
    auto pbkdf2_result = pbkdf2->derive_key(password, salt);

    if (!pbkdf2_result.has_value()) {
        return unexpected(pbkdf2_result.error());
    }

    result.key = std::move(pbkdf2_result.value().key);
    result.params.kdf = key_derivation_function::argon2id;  // Mark as intended Argon2
    result.params.salt.assign(salt.begin(), salt.end());
    result.params.iterations = impl_->config.time_cost;
    result.params.memory_kb = impl_->config.memory_kb;
    result.params.parallelism = impl_->config.parallelism;
    result.params.key_length = impl_->config.key_length;

    return result;
}

auto argon2_key_derivation::derive_key(
    std::string_view password) -> result<derived_key> {
    auto salt_result = generate_salt();
    if (!salt_result.has_value()) {
        return unexpected(salt_result.error());
    }

    return derive_key(password, salt_result.value());
}

auto argon2_key_derivation::derive_key(
    std::span<const std::byte> key_material,
    std::span<const std::byte> salt) -> result<derived_key> {
    std::string_view password(
        reinterpret_cast<const char*>(key_material.data()),
        key_material.size());
    return derive_key(password, salt);
}

auto argon2_key_derivation::derive_key_with_params(
    std::string_view password,
    const key_derivation_params& params) -> result<derived_key> {
    argon2_config config;
    config.time_cost = params.iterations;
    config.memory_kb = params.memory_kb;
    config.parallelism = params.parallelism;
    config.key_length = params.key_length;

    auto kdf = create(config);
    return kdf->derive_key(password, params.salt);
}

auto argon2_key_derivation::generate_salt(std::size_t length) -> result<std::vector<std::byte>> {
    std::vector<std::byte> salt(length);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(salt.data()),
                   static_cast<int>(length)) != 1) {
        return unexpected(error(error_code::internal_error, "Failed to generate salt"));
    }
    return salt;
}

auto argon2_key_derivation::key_length() const -> std::size_t {
    return impl_->config.key_length;
}

auto argon2_key_derivation::salt_length() const -> std::size_t {
    return impl_->config.salt_length;
}

auto argon2_key_derivation::validate_password(std::string_view password) -> result<void> {
    if (password.empty()) {
        return unexpected(error(error_code::invalid_configuration, "Password cannot be empty"));
    }
    if (password.size() < 8) {
        return unexpected(error(error_code::invalid_configuration,
                               "Password too short (minimum 8 characters)"));
    }
    return {};
}

void argon2_key_derivation::secure_zero(std::span<std::byte> data) {
    secure_zero_memory(data.data(), data.size());
}

}  // namespace kcenon::file_transfer

#endif  // FILE_TRANS_ENABLE_ENCRYPTION
