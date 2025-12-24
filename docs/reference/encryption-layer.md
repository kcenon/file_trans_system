# Encryption Abstraction Layer

## Overview

The encryption abstraction layer provides a unified interface for different encryption algorithms with AES-256-GCM as the primary implementation. This layer enables end-to-end encryption for file transfers, supporting both single-shot and streaming operations.

## Architecture

```
┌─────────────────────────────────────────┐
│         Application Layer               │
│    (file_transfer_client/server)        │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│       encryption_interface              │
│  (Abstract base class)                  │
├─────────────────────────────────────────┤
│  - set_key() / clear_key()              │
│  - encrypt() / decrypt()                │
│  - encrypt_async() / decrypt_async()    │
│  - encrypt_chunk() / decrypt_chunk()    │
│  - create_encrypt_stream()              │
│  - create_decrypt_stream()              │
└─────────────────┬───────────────────────┘
                  │
       ┌──────────┼──────────┐
       │          │          │
┌──────▼──────┐ ┌─▼────────┐ ┌▼─────────────┐
│aes_gcm_enc  │ │aes_cbc_  │ │chacha20_     │
│             │ │encryption│ │encryption    │
└─────────────┘ └──────────┘ └──────────────┘

┌─────────────────────────────────────────┐
│     key_derivation_interface            │
├─────────────────────────────────────────┤
│  - derive_key() from password           │
│  - generate_salt()                      │
│  - validate_password()                  │
└─────────────────┬───────────────────────┘
                  │
       ┌──────────┼──────────┐
       │          │          │
┌──────▼──────┐ ┌─▼────────┐ ┌▼─────────────┐
│pbkdf2_kdf   │ │argon2_kdf│ │scrypt_kdf    │
└─────────────┘ └──────────┘ └──────────────┘
```

## Components

### encryption_interface

The base class that defines the encryption contract:

```cpp
#include "kcenon/file_transfer/encryption/encryption_interface.h"

// Abstract interface methods
class encryption_interface {
public:
    // Algorithm identification
    virtual auto algorithm() const -> encryption_algorithm = 0;
    virtual auto algorithm_name() const -> std::string_view = 0;

    // Key management
    virtual auto set_key(std::span<const std::byte> key) -> result<void> = 0;
    virtual auto set_key(const derived_key& derived) -> result<void> = 0;
    virtual auto has_key() const -> bool = 0;
    virtual void clear_key() = 0;
    virtual auto key_size() const -> std::size_t = 0;

    // Single-shot encryption/decryption
    virtual auto encrypt(std::span<const std::byte> plaintext,
                        std::span<const std::byte> aad = {})
        -> result<encryption_result> = 0;
    virtual auto decrypt(std::span<const std::byte> ciphertext,
                        const encryption_metadata& metadata)
        -> result<decryption_result> = 0;

    // Asynchronous operations
    virtual auto encrypt_async(std::span<const std::byte> plaintext,
                              std::span<const std::byte> aad = {})
        -> std::future<result<encryption_result>> = 0;
    virtual auto decrypt_async(std::span<const std::byte> ciphertext,
                              const encryption_metadata& metadata)
        -> std::future<result<decryption_result>> = 0;

    // Streaming operations
    virtual auto create_encrypt_stream(uint64_t total_size = 0,
                                       std::span<const std::byte> aad = {})
        -> std::unique_ptr<encryption_stream_context> = 0;
    virtual auto create_decrypt_stream(const encryption_metadata& metadata)
        -> std::unique_ptr<encryption_stream_context> = 0;

    // Chunk-based operations (for file transfer)
    virtual auto encrypt_chunk(std::span<const std::byte> chunk_data,
                              uint64_t chunk_index)
        -> result<encryption_result> = 0;
    virtual auto decrypt_chunk(std::span<const std::byte> encrypted_chunk,
                              const encryption_metadata& metadata,
                              uint64_t chunk_index)
        -> result<decryption_result> = 0;
};
```

### key_derivation_interface

Interface for deriving encryption keys from passwords:

```cpp
#include "kcenon/file_transfer/encryption/key_derivation.h"

class key_derivation_interface {
public:
    // Key derivation type
    virtual auto type() const -> key_derivation_function = 0;

    // Derive key from password
    virtual auto derive_key(std::string_view password,
                           std::span<const std::byte> salt)
        -> result<derived_key> = 0;
    virtual auto derive_key(std::string_view password)
        -> result<derived_key> = 0;  // Generates random salt

    // Re-derive with stored parameters
    virtual auto derive_key_with_params(std::string_view password,
                                        const key_derivation_params& params)
        -> result<derived_key> = 0;

    // Utility functions
    virtual auto generate_salt(std::size_t length = SALT_SIZE)
        -> result<std::vector<std::byte>> = 0;
    virtual auto validate_password(std::string_view password)
        -> result<void> = 0;
    virtual void secure_zero(std::span<std::byte> data) = 0;
};
```

### encryption_config

Configuration types for different algorithms:

```cpp
#include "kcenon/file_transfer/encryption/encryption_config.h"

// AES-256-GCM configuration (recommended)
aes_gcm_config gcm_config;
gcm_config.iv_size = 12;        // 96 bits (NIST recommended)
gcm_config.tag_size = 16;       // 128 bits
gcm_config.random_iv = true;
gcm_config.secure_memory = true;

// AES-256-CBC configuration (legacy)
aes_cbc_config cbc_config;
cbc_config.use_hmac = true;     // For authentication
cbc_config.pkcs7_padding = true;

// ChaCha20-Poly1305 configuration (alternative)
chacha20_config chacha_config;
chacha_config.nonce_size = 12;
chacha_config.random_nonce = true;
```

## Usage Examples

### Basic Encryption with Password

```cpp
// Create key derivation function
auto kdf = argon2_key_derivation::create(argon2_config{});

// Derive key from password
auto derived = kdf->derive_key("user-password");
if (!derived.has_value()) {
    std::cerr << "Key derivation failed: " << derived.error().message << "\n";
    return;
}

// Create encryptor (requires FILE_TRANS_ENABLE_ENCRYPTION)
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
#include "kcenon/file_transfer/encryption/aes_gcm_engine.h"

auto encryptor = aes_gcm_engine::create(aes_gcm_config{});
encryptor->set_key(derived.value());
#endif

// Encrypt data
std::vector<std::byte> plaintext = /* ... */;
auto encrypted = encryptor->encrypt(plaintext);
if (encrypted.has_value()) {
    // Store encrypted.value().ciphertext and encrypted.value().metadata
}
```

### Decryption with Stored Parameters

```cpp
// Load stored metadata (includes KDF parameters)
encryption_metadata metadata = /* load from storage */;
key_derivation_params kdf_params = /* extract from metadata */;

// Re-derive key using same parameters
auto kdf = create_kdf_from_params(kdf_params);
auto derived = kdf->derive_key_with_params("user-password", kdf_params);

// Create decryptor
auto decryptor = create_encryption_from_metadata(metadata);
decryptor->set_key(derived.value());

// Decrypt
auto decrypted = decryptor->decrypt(ciphertext, metadata);
if (decrypted.has_value()) {
    auto& plaintext = decrypted.value().plaintext;
}
```

### Streaming Encryption for Large Files

```cpp
auto encryptor = aes_gcm_engine::create(aes_gcm_config{});
encryptor->set_key(key);

// Create streaming context
auto stream = encryptor->create_encrypt_stream(file_size);

// Process file in chunks
std::vector<std::byte> output;
while (has_more_data()) {
    auto chunk = read_chunk(64 * 1024);  // 64KB chunks
    auto result = stream->process_chunk(chunk);
    if (result.has_value()) {
        output.insert(output.end(),
                     result.value().begin(), result.value().end());
    }
}

// Finalize (get auth tag)
auto final = stream->finalize();
output.insert(output.end(), final.value().begin(), final.value().end());

// Get metadata for storage
auto metadata = stream->get_metadata();
```

### Chunk-based Encryption for Transfer

```cpp
auto encryptor = aes_gcm_engine::create(aes_gcm_config{});
encryptor->set_key(key);

// Encrypt each file chunk
for (uint64_t i = 0; i < total_chunks; ++i) {
    auto chunk_data = get_chunk(i);
    auto encrypted = encryptor->encrypt_chunk(chunk_data, i);

    if (encrypted.has_value()) {
        // Send encrypted.value().ciphertext with per-chunk metadata
        send_chunk(encrypted.value());
    }
}
```

### Using Configuration Builders

```cpp
// AES-GCM with custom settings
auto gcm_config = encryption_config_builder::aes_gcm()
    .with_stream_chunk_size(128 * 1024)  // 128KB
    .with_iv_size(12)
    .with_secure_memory(true)
    .build_aes_gcm();

// Argon2id with custom settings
auto kdf_config = key_derivation_config_builder::argon2()
    .with_memory(131072)      // 128 MB
    .with_time_cost(4)
    .with_parallelism(8)
    .with_key_length(32)
    .build_argon2();
```

## Supported Algorithms

### Encryption Algorithms

| Algorithm | Type | Key Size | IV/Nonce | Tag Size | Notes |
|-----------|------|----------|----------|----------|-------|
| AES-256-GCM | AEAD | 256 bits | 96 bits | 128 bits | Recommended |
| AES-256-CBC | Block | 256 bits | 128 bits | N/A | Legacy, use with HMAC |
| ChaCha20-Poly1305 | AEAD | 256 bits | 96 bits | 128 bits | Alternative |

### Key Derivation Functions

| KDF | Memory | Time | Notes |
|-----|--------|------|-------|
| Argon2id | Configurable | Configurable | Recommended |
| PBKDF2-SHA256 | Low | High iterations | Compatible |
| scrypt | Configurable | Configurable | Memory-hard |

## Security Constants

```cpp
// Key sizes
inline constexpr std::size_t AES_256_KEY_SIZE = 32;      // 256 bits
inline constexpr std::size_t CHACHA20_KEY_SIZE = 32;     // 256 bits

// IV/Nonce sizes
inline constexpr std::size_t AES_GCM_IV_SIZE = 12;       // 96 bits
inline constexpr std::size_t AES_BLOCK_SIZE = 16;        // 128 bits
inline constexpr std::size_t CHACHA20_NONCE_SIZE = 12;   // 96 bits

// Tag sizes
inline constexpr std::size_t AES_GCM_TAG_SIZE = 16;      // 128 bits
inline constexpr std::size_t CHACHA20_TAG_SIZE = 16;     // 128 bits

// Salt size
inline constexpr std::size_t SALT_SIZE = 32;             // 256 bits

// KDF defaults (OWASP 2023)
inline constexpr uint32_t PBKDF2_DEFAULT_ITERATIONS = 600000;
inline constexpr uint32_t ARGON2_DEFAULT_MEMORY_KB = 65536;   // 64 MB
inline constexpr uint32_t ARGON2_DEFAULT_TIME_COST = 3;
inline constexpr uint32_t ARGON2_DEFAULT_PARALLELISM = 4;
```

## Encryption Metadata

Stored with encrypted data for decryption:

```cpp
struct encryption_metadata {
    encryption_algorithm algorithm;      // Algorithm used
    key_derivation_function kdf;         // KDF used (if password-based)
    std::vector<std::byte> iv;           // Initialization vector
    std::vector<std::byte> salt;         // KDF salt
    std::vector<std::byte> auth_tag;     // Authentication tag
    std::vector<std::byte> aad;          // Additional authenticated data
    uint32_t kdf_iterations;             // KDF iterations
    uint32_t argon2_memory_kb;           // Argon2 memory cost
    uint32_t argon2_parallelism;         // Argon2 parallelism
    uint64_t original_size;              // Original data size
    uint8_t version;                     // Format version
};
```

## Encryption States

| State | Description |
|-------|-------------|
| `uninitialized` | No key set, not ready for operations |
| `ready` | Key set, ready for encryption/decryption |
| `processing` | Currently processing data |
| `error` | Error state, key cleared |

## Statistics

```cpp
auto stats = encryptor->get_statistics();

std::cout << "Bytes encrypted: " << stats.bytes_encrypted << "\n";
std::cout << "Bytes decrypted: " << stats.bytes_decrypted << "\n";
std::cout << "Encryption ops: " << stats.encryption_ops << "\n";
std::cout << "Decryption ops: " << stats.decryption_ops << "\n";
std::cout << "Errors: " << stats.errors << "\n";
std::cout << "Total encrypt time: " << stats.total_encrypt_time.count() << " us\n";
```

## Security Considerations

### Key Management
- Keys are cleared from memory when `clear_key()` is called
- Enable `secure_memory` to zero memory after use
- Never log or expose keys or passwords

### IV/Nonce Handling
- Never reuse IV/nonce with the same key
- Always use `random_iv = true` for GCM
- Store IV with ciphertext (it's not secret)

### Authentication
- Always use AEAD modes (GCM, ChaCha20-Poly1305)
- Verify auth tag before processing decrypted data
- Use AAD for context binding

### Password Storage
- Never store plaintext passwords
- Store KDF parameters with encrypted data
- Use appropriate iteration counts

## Thread Safety

- All encryption methods are thread-safe
- Each operation uses independent IV/nonce
- Statistics access is protected by mutex
- Key operations require exclusive access

## Error Handling

```cpp
auto result = encryptor->encrypt(plaintext);

if (!result.has_value()) {
    switch (result.error().code) {
        case error_code::not_initialized:
            // No key set
            break;
        case error_code::invalid_configuration:
            // Invalid parameters
            break;
        case error_code::internal_error:
            // Encryption failed
            break;
        default:
            break;
    }
}
```

## Key Management System

The key management system provides secure key lifecycle management including generation, derivation, storage, and rotation.

### Architecture

```
┌─────────────────────────────────────────┐
│            key_manager                  │
├─────────────────────────────────────────┤
│  - generate_key()                       │
│  - derive_key_from_password()           │
│  - store_key() / get_key()              │
│  - rotate_key()                         │
│  - record_usage()                       │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│       key_storage_interface             │
├─────────────────────────────────────────┤
│  - store() / retrieve() / remove()      │
│  - exists() / list_keys()               │
└─────────────────┬───────────────────────┘
                  │
       ┌──────────┼──────────┐
       │          │          │
┌──────▼──────┐ ┌─▼────────┐ ┌▼─────────────┐
│memory_key_  │ │file_key_ │ │hsm_key_      │
│storage      │ │storage   │ │storage       │
│(in-memory)  │ │(future)  │ │(future)      │
└─────────────┘ └──────────┘ └──────────────┘
```

### Key Generation

```cpp
#include "kcenon/file_transfer/encryption/key_manager.h"

// Create key manager with in-memory storage
auto manager = key_manager::create();

// Generate random 256-bit key
auto result = manager->generate_key("my-encryption-key");
if (result.has_value()) {
    auto& key = result.value();
    // key.key contains the raw key bytes
    // key.metadata contains tracking information
}

// Generate random bytes (CSPRNG)
auto random = manager->generate_random_bytes(32);
```

### Password-based Key Derivation

```cpp
// Derive key using Argon2id (recommended)
auto key = manager->derive_key_from_password(
    "password-key",
    "user-secure-password",
    argon2_config{
        .memory_kb = 65536,    // 64 MB
        .time_cost = 3,
        .parallelism = 4
    }
);

// Derive key using PBKDF2 (compatible)
auto key = manager->derive_key_pbkdf2(
    "pbkdf2-key",
    "user-password",
    pbkdf2_config{
        .iterations = 600000  // OWASP 2023 recommendation
    }
);

// Re-derive key with stored parameters
auto rederived = manager->rederive_key("password-key", "user-secure-password");
```

### Key Rotation

```cpp
// Set rotation policy
key_rotation_policy policy;
policy.auto_rotate = true;
policy.max_uses = 1000000;        // Rotate after 1M uses
policy.max_age = std::chrono::hours{24 * 30};  // 30 days
policy.keep_versions = 3;          // Keep last 3 versions

manager->set_rotation_policy(policy);

// Manual rotation
auto new_key = manager->rotate_key("my-key");

// Check if rotation needed
if (manager->needs_rotation("my-key")) {
    manager->rotate_key("my-key");
}

// Access old versions for decryption
auto old_versions = manager->get_key_versions("my-key");
```

### Key Metadata

```cpp
struct key_metadata {
    std::string key_id;
    std::string description;
    std::optional<key_derivation_params> derivation_params;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_used_at;
    std::optional<std::chrono::system_clock::time_point> expires_at;
    uint64_t usage_count;
    bool is_active;
    uint32_t version;
};
```

### Usage Tracking

```cpp
// Record key usage
manager->record_usage("my-key");

// Get usage statistics
auto stats = manager->get_usage_stats("my-key");
if (stats.has_value()) {
    std::cout << "Key used " << stats.value().usage_count << " times\n";
    std::cout << "Last used: " << /* format time */ << "\n";
}
```

### Secure Memory Operations

```cpp
// Securely zero sensitive data
std::vector<std::byte> sensitive_data = /* ... */;
key_manager::secure_zero(sensitive_data);

// Constant-time comparison (prevent timing attacks)
bool equal = key_manager::constant_time_compare(key1, key2);
```

### Key Derivation Classes

```cpp
// PBKDF2 key derivation
auto pbkdf2 = pbkdf2_key_derivation::create(pbkdf2_config{
    .iterations = 600000,
    .key_length = 32
});

auto derived = pbkdf2->derive_key("password");
// derived.value().key contains the 32-byte key
// derived.value().params contains salt and iteration count

// Argon2id key derivation (recommended)
auto argon2 = argon2_key_derivation::create(argon2_config{
    .memory_kb = 65536,
    .time_cost = 3,
    .parallelism = 4,
    .key_length = 32
});

// Check if native Argon2 is available
if (argon2_key_derivation::is_available()) {
    // Using native Argon2id
} else {
    // Falls back to PBKDF2
}
```

### Integration with Encryption

```cpp
// Complete encryption workflow
auto manager = key_manager::create();

// Generate or derive key
auto key_result = manager->derive_key_from_password("file-key", "password");
auto& managed_key = key_result.value();

// Create encryptor
auto encryptor = aes_gcm_engine::create();
encryptor->set_key(managed_key.key);

// Encrypt data
auto encrypted = encryptor->encrypt(plaintext);

// Track key usage
manager->record_usage("file-key");

// Check for rotation
if (manager->needs_rotation("file-key")) {
    auto new_key = manager->rotate_key("file-key");
    // Update encryption key
}
```

## Related Documents

- [Transport Layer](transport-layer.md)
- [API Reference](api-reference.md)
- [Security Guidelines](security.md)
