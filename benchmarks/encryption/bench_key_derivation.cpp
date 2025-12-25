/**
 * @file bench_key_derivation.cpp
 * @brief Benchmarks for key derivation functions (PBKDF2, Argon2id)
 *
 * Performance Targets (from Issue #90):
 * - Key derivation: >= 100 ops/sec
 */

#include <benchmark/benchmark.h>

#ifdef FILE_TRANS_ENABLE_ENCRYPTION

#include <kcenon/file_transfer/encryption/key_manager.h>

#include "utils/benchmark_helpers.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kcenon::file_transfer::benchmark {

// ============================================================================
// PBKDF2 Benchmarks
// ============================================================================

/**
 * @brief Benchmark PBKDF2 key derivation
 * Target: >= 100 ops/sec
 */
static void BM_PBKDF2_Key_Derivation(::benchmark::State& state) {
    auto kdf = pbkdf2_key_derivation::create();
    if (!kdf) {
        state.SkipWithError("Failed to create PBKDF2 KDF");
        return;
    }

    const std::string password = "secure-benchmark-password-123!@#";

    // Pre-generate salt for deterministic benchmarking
    auto salt_result = kdf->generate_salt();
    if (!salt_result.has_value()) {
        state.SkipWithError("Failed to generate salt");
        return;
    }
    auto salt = std::move(salt_result.value());

    for (auto _ : state) {
        auto result = kdf->derive_key(password, salt);
        if (!result.has_value()) {
            state.SkipWithError("Key derivation failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark PBKDF2 with varying iteration counts
 */
static void BM_PBKDF2_Iterations(::benchmark::State& state) {
    const auto iterations = static_cast<uint32_t>(state.range(0));

    pbkdf2_config config;
    config.iterations = iterations;

    auto kdf = pbkdf2_key_derivation::create(config);
    if (!kdf) {
        state.SkipWithError("Failed to create PBKDF2 KDF");
        return;
    }

    const std::string password = "secure-benchmark-password-123!@#";

    auto salt_result = kdf->generate_salt();
    if (!salt_result.has_value()) {
        state.SkipWithError("Failed to generate salt");
        return;
    }
    auto salt = std::move(salt_result.value());

    for (auto _ : state) {
        auto result = kdf->derive_key(password, salt);
        if (!result.has_value()) {
            state.SkipWithError("Key derivation failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    state.counters["iterations"] = ::benchmark::Counter(
        static_cast<double>(iterations));
}

// ============================================================================
// Argon2id Benchmarks
// ============================================================================

/**
 * @brief Benchmark Argon2id key derivation with default settings
 */
static void BM_Argon2_Key_Derivation(::benchmark::State& state) {
    auto kdf = argon2_key_derivation::create();
    if (!kdf) {
        state.SkipWithError("Failed to create Argon2 KDF");
        return;
    }

    const std::string password = "secure-benchmark-password-123!@#";

    auto salt_result = kdf->generate_salt();
    if (!salt_result.has_value()) {
        state.SkipWithError("Failed to generate salt");
        return;
    }
    auto salt = std::move(salt_result.value());

    for (auto _ : state) {
        auto result = kdf->derive_key(password, salt);
        if (!result.has_value()) {
            state.SkipWithError("Key derivation failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark Argon2id with varying memory costs
 */
static void BM_Argon2_Memory_Cost(::benchmark::State& state) {
    const auto memory_kb = static_cast<uint32_t>(state.range(0));

    argon2_config config;
    config.memory_kb = memory_kb;
    config.time_cost = 3;
    config.parallelism = 4;

    auto kdf = argon2_key_derivation::create(config);
    if (!kdf) {
        state.SkipWithError("Failed to create Argon2 KDF");
        return;
    }

    const std::string password = "secure-benchmark-password-123!@#";

    auto salt_result = kdf->generate_salt();
    if (!salt_result.has_value()) {
        state.SkipWithError("Failed to generate salt");
        return;
    }
    auto salt = std::move(salt_result.value());

    for (auto _ : state) {
        auto result = kdf->derive_key(password, salt);
        if (!result.has_value()) {
            state.SkipWithError("Key derivation failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    state.counters["memory_mb"] = ::benchmark::Counter(
        static_cast<double>(memory_kb) / 1024.0);
}

/**
 * @brief Benchmark Argon2id with varying time costs
 */
static void BM_Argon2_Time_Cost(::benchmark::State& state) {
    const auto time_cost = static_cast<uint32_t>(state.range(0));

    argon2_config config;
    config.memory_kb = 65536;  // 64 MB
    config.time_cost = time_cost;
    config.parallelism = 4;

    auto kdf = argon2_key_derivation::create(config);
    if (!kdf) {
        state.SkipWithError("Failed to create Argon2 KDF");
        return;
    }

    const std::string password = "secure-benchmark-password-123!@#";

    auto salt_result = kdf->generate_salt();
    if (!salt_result.has_value()) {
        state.SkipWithError("Failed to generate salt");
        return;
    }
    auto salt = std::move(salt_result.value());

    for (auto _ : state) {
        auto result = kdf->derive_key(password, salt);
        if (!result.has_value()) {
            state.SkipWithError("Key derivation failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    state.counters["time_cost"] = ::benchmark::Counter(
        static_cast<double>(time_cost));
}

// ============================================================================
// Key Manager Benchmarks
// ============================================================================

/**
 * @brief Benchmark random key generation via KeyManager
 */
static void BM_KeyManager_Generate_Random(::benchmark::State& state) {
    auto manager = key_manager::create();
    if (!manager) {
        state.SkipWithError("Failed to create key manager");
        return;
    }

    uint64_t key_counter = 0;

    for (auto _ : state) {
        std::string key_id = "bench-key-" + std::to_string(key_counter++);
        auto result = manager->generate_key(key_id);
        if (!result.has_value()) {
            state.SkipWithError("Key generation failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());

        // Cleanup
        state.PauseTiming();
        manager->delete_key(key_id);
        state.ResumeTiming();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark key storage and retrieval
 */
static void BM_KeyManager_Store_Retrieve(::benchmark::State& state) {
    auto manager = key_manager::create();
    if (!manager) {
        state.SkipWithError("Failed to create key manager");
        return;
    }

    // Pre-generate keys
    constexpr int num_keys = 100;
    for (int i = 0; i < num_keys; ++i) {
        std::string key_id = "stored-key-" + std::to_string(i);
        manager->generate_key(key_id);
    }

    uint64_t access_counter = 0;

    for (auto _ : state) {
        std::string key_id = "stored-key-" + std::to_string(access_counter % num_keys);
        auto result = manager->get_key(key_id);
        if (!result.has_value()) {
            state.SkipWithError("Key retrieval failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
        ++access_counter;
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark key rotation
 */
static void BM_KeyManager_Rotation(::benchmark::State& state) {
    auto manager = key_manager::create();
    if (!manager) {
        state.SkipWithError("Failed to create key manager");
        return;
    }

    // Create initial key
    auto initial = manager->generate_key("rotation-test-key");
    if (!initial.has_value()) {
        state.SkipWithError("Failed to create initial key");
        return;
    }

    for (auto _ : state) {
        auto result = manager->rotate_key("rotation-test-key");
        if (!result.has_value()) {
            state.SkipWithError("Key rotation failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark salt generation
 */
static void BM_Salt_Generation(::benchmark::State& state) {
    auto kdf = pbkdf2_key_derivation::create();
    if (!kdf) {
        state.SkipWithError("Failed to create KDF");
        return;
    }

    for (auto _ : state) {
        auto result = kdf->generate_salt();
        if (!result.has_value()) {
            state.SkipWithError("Salt generation failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark secure memory zeroing
 */
static void BM_Secure_Zero(::benchmark::State& state) {
    const auto data_size = static_cast<std::size_t>(state.range(0));
    std::vector<std::byte> data(data_size, std::byte{0xFF});

    for (auto _ : state) {
        // Fill with non-zero data
        state.PauseTiming();
        std::fill(data.begin(), data.end(), std::byte{0xFF});
        state.ResumeTiming();

        key_manager::secure_zero(data);
        ::benchmark::DoNotOptimize(data.data());
    }

    state.SetBytesProcessed(static_cast<int64_t>(data_size) *
                           static_cast<int64_t>(state.iterations()));
}

// ============================================================================
// Register Benchmarks
// ============================================================================

// PBKDF2 benchmarks
BENCHMARK(BM_PBKDF2_Key_Derivation)
    ->Unit(::benchmark::kMillisecond);

BENCHMARK(BM_PBKDF2_Iterations)
    ->Arg(100000)   // Low security
    ->Arg(310000)   // OWASP minimum (2023)
    ->Arg(600000)   // Default (high security)
    ->Arg(1000000)  // Very high security
    ->Unit(::benchmark::kMillisecond);

// Argon2 benchmarks
BENCHMARK(BM_Argon2_Key_Derivation)
    ->Unit(::benchmark::kMillisecond);

BENCHMARK(BM_Argon2_Memory_Cost)
    ->Arg(16384)    // 16 MB
    ->Arg(32768)    // 32 MB
    ->Arg(65536)    // 64 MB (default)
    ->Arg(131072)   // 128 MB
    ->Unit(::benchmark::kMillisecond);

BENCHMARK(BM_Argon2_Time_Cost)
    ->Arg(1)
    ->Arg(2)
    ->Arg(3)  // Default
    ->Arg(4)
    ->Arg(5)
    ->Unit(::benchmark::kMillisecond);

// Key manager benchmarks
BENCHMARK(BM_KeyManager_Generate_Random)
    ->Unit(::benchmark::kMicrosecond);

BENCHMARK(BM_KeyManager_Store_Retrieve)
    ->Unit(::benchmark::kNanosecond);

BENCHMARK(BM_KeyManager_Rotation)
    ->Unit(::benchmark::kMicrosecond);

BENCHMARK(BM_Salt_Generation)
    ->Unit(::benchmark::kNanosecond);

// Secure memory operations
BENCHMARK(BM_Secure_Zero)
    ->Arg(32)       // Key size
    ->Arg(256)      // Small buffer
    ->Arg(4096)     // Page size
    ->Arg(65536)    // Large buffer
    ->Unit(::benchmark::kNanosecond);

}  // namespace kcenon::file_transfer::benchmark

#else  // FILE_TRANS_ENABLE_ENCRYPTION

#include <benchmark/benchmark.h>

static void BM_KeyDerivation_Disabled(::benchmark::State& state) {
    for (auto _ : state) {
        state.SkipWithError("Encryption not enabled");
    }
}

BENCHMARK(BM_KeyDerivation_Disabled);

#endif  // FILE_TRANS_ENABLE_ENCRYPTION
