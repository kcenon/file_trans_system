/**
 * @file bench_encryption_throughput.cpp
 * @brief Benchmarks for AES-256-GCM encryption/decryption throughput
 *
 * Performance Targets (from Issue #90):
 * - Encryption throughput: >= 1 GB/s
 * - Decryption throughput: >= 1.5 GB/s
 * - Transfer overhead: <= 10%
 */

#include <benchmark/benchmark.h>

#ifdef FILE_TRANS_ENABLE_ENCRYPTION

#include <kcenon/file_transfer/encryption/aes_gcm_engine.h>
#include <kcenon/file_transfer/encryption/key_manager.h>

#include "utils/benchmark_helpers.h"

#include <cstddef>
#include <vector>

namespace kcenon::file_transfer::benchmark {

// ============================================================================
// Global Test Setup
// ============================================================================

namespace {

class encryption_benchmark_fixture {
public:
    encryption_benchmark_fixture() {
        engine_ = aes_gcm_engine::create();
        if (engine_) {
            // Generate a deterministic key for benchmarking
            key_.resize(AES_256_KEY_SIZE);
            for (std::size_t i = 0; i < key_.size(); ++i) {
                key_[i] = static_cast<std::byte>(i & 0xFF);
            }
            engine_->set_key(std::span<const std::byte>(key_));
        }
    }

    auto get_engine() -> aes_gcm_engine* { return engine_.get(); }
    auto get_key() const -> const std::vector<std::byte>& { return key_; }

private:
    std::unique_ptr<aes_gcm_engine> engine_;
    std::vector<std::byte> key_;
};

auto get_fixture() -> encryption_benchmark_fixture& {
    static encryption_benchmark_fixture fixture;
    return fixture;
}

}  // namespace

// ============================================================================
// Single-shot Encryption Benchmarks
// ============================================================================

/**
 * @brief Benchmark AES-256-GCM encryption throughput
 * Target: >= 1 GB/s
 */
static void BM_AES_GCM_Encryption(::benchmark::State& state) {
    auto* engine = get_fixture().get_engine();
    if (!engine || !engine->has_key()) {
        state.SkipWithError("Encryption engine not initialized");
        return;
    }

    const auto data_size = static_cast<std::size_t>(state.range(0));
    auto plaintext = test_data_generator::generate_random_data(data_size, 42);

    for (auto _ : state) {
        auto result = engine->encrypt(std::span<const std::byte>(plaintext));
        if (!result.has_value()) {
            state.SkipWithError("Encryption failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetBytesProcessed(static_cast<int64_t>(data_size) *
                           static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark AES-256-GCM decryption throughput
 * Target: >= 1.5 GB/s
 */
static void BM_AES_GCM_Decryption(::benchmark::State& state) {
    auto* engine = get_fixture().get_engine();
    if (!engine || !engine->has_key()) {
        state.SkipWithError("Encryption engine not initialized");
        return;
    }

    const auto data_size = static_cast<std::size_t>(state.range(0));
    auto plaintext = test_data_generator::generate_random_data(data_size, 42);

    // Pre-encrypt data
    auto encrypt_result = engine->encrypt(std::span<const std::byte>(plaintext));
    if (!encrypt_result.has_value()) {
        state.SkipWithError("Failed to prepare encrypted data");
        return;
    }
    auto encrypted = std::move(encrypt_result.value());

    for (auto _ : state) {
        auto result = engine->decrypt(
            std::span<const std::byte>(encrypted.ciphertext),
            encrypted.metadata);
        if (!result.has_value()) {
            state.SkipWithError("Decryption failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetBytesProcessed(static_cast<int64_t>(data_size) *
                           static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark encryption with AAD (Additional Authenticated Data)
 */
static void BM_AES_GCM_Encryption_With_AAD(::benchmark::State& state) {
    auto* engine = get_fixture().get_engine();
    if (!engine || !engine->has_key()) {
        state.SkipWithError("Encryption engine not initialized");
        return;
    }

    const auto data_size = static_cast<std::size_t>(state.range(0));
    auto plaintext = test_data_generator::generate_random_data(data_size, 42);

    // Standard AAD size (e.g., file metadata)
    std::vector<std::byte> aad(64);
    for (std::size_t i = 0; i < aad.size(); ++i) {
        aad[i] = static_cast<std::byte>(i);
    }

    for (auto _ : state) {
        auto result = engine->encrypt(
            std::span<const std::byte>(plaintext),
            std::span<const std::byte>(aad));
        if (!result.has_value()) {
            state.SkipWithError("Encryption with AAD failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetBytesProcessed(static_cast<int64_t>(data_size) *
                           static_cast<int64_t>(state.iterations()));
}

// ============================================================================
// Chunk-based Encryption Benchmarks
// ============================================================================

/**
 * @brief Benchmark chunk-based encryption (typical file transfer pattern)
 */
static void BM_AES_GCM_Encrypt_Chunk(::benchmark::State& state) {
    auto* engine = get_fixture().get_engine();
    if (!engine || !engine->has_key()) {
        state.SkipWithError("Encryption engine not initialized");
        return;
    }

    const auto chunk_size = static_cast<std::size_t>(state.range(0));
    auto chunk_data = test_data_generator::generate_random_data(chunk_size, 42);

    uint64_t chunk_index = 0;

    for (auto _ : state) {
        auto result = engine->encrypt_chunk(
            std::span<const std::byte>(chunk_data),
            chunk_index++);
        if (!result.has_value()) {
            state.SkipWithError("Chunk encryption failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetBytesProcessed(static_cast<int64_t>(chunk_size) *
                           static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark chunk-based decryption
 */
static void BM_AES_GCM_Decrypt_Chunk(::benchmark::State& state) {
    auto* engine = get_fixture().get_engine();
    if (!engine || !engine->has_key()) {
        state.SkipWithError("Encryption engine not initialized");
        return;
    }

    const auto chunk_size = static_cast<std::size_t>(state.range(0));
    auto chunk_data = test_data_generator::generate_random_data(chunk_size, 42);

    // Pre-encrypt chunk
    auto encrypt_result = engine->encrypt_chunk(
        std::span<const std::byte>(chunk_data), 0);
    if (!encrypt_result.has_value()) {
        state.SkipWithError("Failed to prepare encrypted chunk");
        return;
    }
    auto encrypted = std::move(encrypt_result.value());

    for (auto _ : state) {
        auto result = engine->decrypt_chunk(
            std::span<const std::byte>(encrypted.ciphertext),
            encrypted.metadata,
            0);
        if (!result.has_value()) {
            state.SkipWithError("Chunk decryption failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetBytesProcessed(static_cast<int64_t>(chunk_size) *
                           static_cast<int64_t>(state.iterations()));
}

// ============================================================================
// Streaming Encryption Benchmarks
// ============================================================================

/**
 * @brief Benchmark streaming encryption (for large files)
 */
static void BM_AES_GCM_Stream_Encrypt(::benchmark::State& state) {
    auto* engine = get_fixture().get_engine();
    if (!engine || !engine->has_key()) {
        state.SkipWithError("Encryption engine not initialized");
        return;
    }

    const auto total_size = static_cast<std::size_t>(state.range(0));
    const auto chunk_size = static_cast<std::size_t>(state.range(1));
    auto data = test_data_generator::generate_random_data(total_size, 42);

    for (auto _ : state) {
        auto stream = engine->create_encrypt_stream(total_size);
        if (!stream) {
            state.SkipWithError("Failed to create encrypt stream");
            return;
        }

        std::vector<std::byte> output;
        for (std::size_t offset = 0; offset < total_size; offset += chunk_size) {
            std::size_t size = std::min(chunk_size, total_size - offset);
            auto chunk = std::span<const std::byte>(data.data() + offset, size);

            auto result = stream->process_chunk(chunk);
            if (!result.has_value()) {
                state.SkipWithError("Stream chunk processing failed");
                return;
            }
            output.insert(output.end(), result.value().begin(), result.value().end());
        }

        auto final_result = stream->finalize();
        if (!final_result.has_value()) {
            state.SkipWithError("Stream finalization failed");
            return;
        }
        output.insert(output.end(), final_result.value().begin(), final_result.value().end());

        ::benchmark::DoNotOptimize(output);
    }

    state.SetBytesProcessed(static_cast<int64_t>(total_size) *
                           static_cast<int64_t>(state.iterations()));
}

// ============================================================================
// Overhead Measurement Benchmarks
// ============================================================================

/**
 * @brief Measure encryption overhead (size expansion)
 * Target: <= 10%
 */
static void BM_Encryption_Overhead(::benchmark::State& state) {
    auto* engine = get_fixture().get_engine();
    if (!engine || !engine->has_key()) {
        state.SkipWithError("Encryption engine not initialized");
        return;
    }

    const auto data_size = static_cast<std::size_t>(state.range(0));
    auto plaintext = test_data_generator::generate_random_data(data_size, 42);

    double total_overhead_percent = 0.0;
    int64_t iterations = 0;

    for (auto _ : state) {
        auto result = engine->encrypt(std::span<const std::byte>(plaintext));
        if (!result.has_value()) {
            state.SkipWithError("Encryption failed");
            return;
        }

        state.PauseTiming();
        auto& encrypted = result.value();
        std::size_t total_encrypted_size = encrypted.ciphertext.size() +
                                           encrypted.metadata.iv.size() +
                                           encrypted.metadata.auth_tag.size();
        double overhead = (static_cast<double>(total_encrypted_size) -
                          static_cast<double>(data_size)) /
                         static_cast<double>(data_size) * 100.0;
        total_overhead_percent += overhead;
        ++iterations;
        state.ResumeTiming();

        ::benchmark::DoNotOptimize(encrypted);
    }

    state.SetBytesProcessed(static_cast<int64_t>(data_size) *
                           static_cast<int64_t>(state.iterations()));

    if (iterations > 0) {
        state.counters["overhead_percent"] = ::benchmark::Counter(
            total_overhead_percent / static_cast<double>(iterations));
    }
}

/**
 * @brief Benchmark IV generation performance
 */
static void BM_IV_Generation(::benchmark::State& state) {
    auto* engine = get_fixture().get_engine();
    if (!engine) {
        state.SkipWithError("Encryption engine not initialized");
        return;
    }

    for (auto _ : state) {
        auto result = engine->generate_iv();
        if (!result.has_value()) {
            state.SkipWithError("IV generation failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

// ============================================================================
// Register Benchmarks
// ============================================================================

// Single-shot encryption - various sizes
BENCHMARK(BM_AES_GCM_Encryption)
    ->Arg(static_cast<int64_t>(64 * sizes::KB))
    ->Arg(static_cast<int64_t>(256 * sizes::KB))
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Arg(static_cast<int64_t>(4 * sizes::MB))
    ->Arg(static_cast<int64_t>(16 * sizes::MB))
    ->Arg(static_cast<int64_t>(64 * sizes::MB))
    ->Unit(::benchmark::kMillisecond);

// Single-shot decryption - various sizes
BENCHMARK(BM_AES_GCM_Decryption)
    ->Arg(static_cast<int64_t>(64 * sizes::KB))
    ->Arg(static_cast<int64_t>(256 * sizes::KB))
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Arg(static_cast<int64_t>(4 * sizes::MB))
    ->Arg(static_cast<int64_t>(16 * sizes::MB))
    ->Arg(static_cast<int64_t>(64 * sizes::MB))
    ->Unit(::benchmark::kMillisecond);

// Encryption with AAD
BENCHMARK(BM_AES_GCM_Encryption_With_AAD)
    ->Arg(static_cast<int64_t>(64 * sizes::KB))
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Arg(static_cast<int64_t>(16 * sizes::MB))
    ->Unit(::benchmark::kMillisecond);

// Chunk-based encryption (typical transfer patterns)
BENCHMARK(BM_AES_GCM_Encrypt_Chunk)
    ->Arg(static_cast<int64_t>(64 * sizes::KB))   // Small chunk
    ->Arg(static_cast<int64_t>(256 * sizes::KB))  // Default chunk
    ->Arg(static_cast<int64_t>(1 * sizes::MB))    // Large chunk
    ->Unit(::benchmark::kMicrosecond);

BENCHMARK(BM_AES_GCM_Decrypt_Chunk)
    ->Arg(static_cast<int64_t>(64 * sizes::KB))
    ->Arg(static_cast<int64_t>(256 * sizes::KB))
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Unit(::benchmark::kMicrosecond);

// Streaming encryption (total_size, chunk_size)
BENCHMARK(BM_AES_GCM_Stream_Encrypt)
    ->Args({static_cast<int64_t>(16 * sizes::MB), static_cast<int64_t>(64 * sizes::KB)})
    ->Args({static_cast<int64_t>(16 * sizes::MB), static_cast<int64_t>(256 * sizes::KB)})
    ->Args({static_cast<int64_t>(16 * sizes::MB), static_cast<int64_t>(1 * sizes::MB)})
    ->Unit(::benchmark::kMillisecond);

// Overhead measurement
BENCHMARK(BM_Encryption_Overhead)
    ->Arg(static_cast<int64_t>(64 * sizes::KB))
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Arg(static_cast<int64_t>(16 * sizes::MB))
    ->Unit(::benchmark::kMillisecond);

// IV generation
BENCHMARK(BM_IV_Generation)
    ->Unit(::benchmark::kNanosecond);

}  // namespace kcenon::file_transfer::benchmark

#else  // FILE_TRANS_ENABLE_ENCRYPTION

// Placeholder when encryption is disabled
#include <benchmark/benchmark.h>

static void BM_Encryption_Disabled(::benchmark::State& state) {
    for (auto _ : state) {
        state.SkipWithError("Encryption not enabled");
    }
}

BENCHMARK(BM_Encryption_Disabled);

#endif  // FILE_TRANS_ENABLE_ENCRYPTION
