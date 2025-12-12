/**
 * @file bench_lz4_compression.cpp
 * @brief Benchmarks for LZ4 compression and decompression performance
 *
 * Performance Targets:
 * - LZ4 compression: >= 400 MB/s
 * - LZ4 decompression: >= 1.5 GB/s
 */

#include <benchmark/benchmark.h>

#include <kcenon/file_transfer/core/compression_engine.h>

#include "utils/benchmark_helpers.h"

#include <cstddef>
#include <vector>

namespace kcenon::file_transfer::benchmark {

/**
 * @brief Benchmark for LZ4 compression with fast level
 *
 * Target: >= 400 MB/s
 */
static void BM_LZ4_Compression_Fast(::benchmark::State& state) {
    const auto data_size = static_cast<std::size_t>(state.range(0));
    auto data = test_data_generator::generate_text_data(data_size, 42);

    compression_engine engine(compression_level::fast);

    for (auto _ : state) {
        auto result = engine.compress(data);
        if (!result) {
            state.SkipWithError("Compression failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetBytesProcessed(static_cast<int64_t>(data_size) *
                           static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark for LZ4 compression with high level (HC)
 */
static void BM_LZ4_Compression_High(::benchmark::State& state) {
    const auto data_size = static_cast<std::size_t>(state.range(0));
    auto data = test_data_generator::generate_text_data(data_size, 42);

    compression_engine engine(compression_level::high);

    for (auto _ : state) {
        auto result = engine.compress(data);
        if (!result) {
            state.SkipWithError("Compression failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetBytesProcessed(static_cast<int64_t>(data_size) *
                           static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark for LZ4 decompression
 *
 * Target: >= 1.5 GB/s
 */
static void BM_LZ4_Decompression(::benchmark::State& state) {
    const auto data_size = static_cast<std::size_t>(state.range(0));
    auto original_data = test_data_generator::generate_text_data(data_size, 42);

    compression_engine engine(compression_level::fast);
    auto compress_result = engine.compress(original_data);
    if (!compress_result) {
        state.SkipWithError("Failed to prepare compressed data");
        return;
    }
    auto compressed_data = std::move(compress_result.value());

    for (auto _ : state) {
        auto result = engine.decompress(compressed_data, data_size);
        if (!result) {
            state.SkipWithError("Decompression failed");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    // Report decompressed bytes (original size)
    state.SetBytesProcessed(static_cast<int64_t>(data_size) *
                           static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark compression ratio for text data
 */
static void BM_Compression_Ratio_Text(::benchmark::State& state) {
    const auto data_size = static_cast<std::size_t>(state.range(0));
    auto data = test_data_generator::generate_text_data(data_size, 42);

    compression_engine engine(compression_level::fast);

    double total_ratio = 0.0;
    int64_t iterations = 0;

    for (auto _ : state) {
        state.PauseTiming();
        engine.reset_stats();
        state.ResumeTiming();

        auto result = engine.compress(data);
        if (!result) {
            state.SkipWithError("Compression failed");
            return;
        }

        state.PauseTiming();
        auto compressed_size = result.value().size();
        double ratio = static_cast<double>(data_size) / static_cast<double>(compressed_size);
        total_ratio += ratio;
        ++iterations;
        state.ResumeTiming();

        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetBytesProcessed(static_cast<int64_t>(data_size) *
                           static_cast<int64_t>(state.iterations()));

    if (iterations > 0) {
        state.counters["avg_ratio"] =
            ::benchmark::Counter(total_ratio / static_cast<double>(iterations));
    }
}

/**
 * @brief Benchmark compression ratio for binary/random data
 */
static void BM_Compression_Ratio_Binary(::benchmark::State& state) {
    const auto data_size = static_cast<std::size_t>(state.range(0));
    auto data = test_data_generator::generate_random_data(data_size, 42);

    compression_engine engine(compression_level::fast);

    double total_ratio = 0.0;
    int64_t iterations = 0;

    for (auto _ : state) {
        state.PauseTiming();
        engine.reset_stats();
        state.ResumeTiming();

        auto result = engine.compress(data);
        if (!result) {
            state.SkipWithError("Compression failed");
            return;
        }

        state.PauseTiming();
        auto compressed_size = result.value().size();
        double ratio = static_cast<double>(data_size) / static_cast<double>(compressed_size);
        total_ratio += ratio;
        ++iterations;
        state.ResumeTiming();

        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetBytesProcessed(static_cast<int64_t>(data_size) *
                           static_cast<int64_t>(state.iterations()));

    if (iterations > 0) {
        state.counters["avg_ratio"] =
            ::benchmark::Counter(total_ratio / static_cast<double>(iterations));
    }
}

/**
 * @brief Benchmark adaptive compression overhead (is_compressible check)
 */
static void BM_Adaptive_Compression_Check(::benchmark::State& state) {
    const auto data_size = static_cast<std::size_t>(state.range(0));
    auto data = test_data_generator::generate_text_data(data_size, 42);

    compression_engine engine(compression_level::fast);

    for (auto _ : state) {
        auto is_compressible = engine.is_compressible(data);
        ::benchmark::DoNotOptimize(is_compressible);
    }

    state.SetBytesProcessed(static_cast<int64_t>(std::min(data_size, std::size_t{4096})) *
                           static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark adaptive compression with pre-compressed data detection
 */
static void BM_Adaptive_Compression_Skip(::benchmark::State& state) {
    const auto data_size = static_cast<std::size_t>(state.range(0));

    // Generate data that looks like already compressed (random bytes)
    auto data = test_data_generator::generate_random_data(data_size, 42);

    compression_engine engine(compression_level::fast);

    uint64_t skipped_count = 0;
    uint64_t total_count = 0;

    for (auto _ : state) {
        ++total_count;
        if (!engine.is_compressible(data)) {
            ++skipped_count;
            // Skip compression for incompressible data
            ::benchmark::DoNotOptimize(data);
        } else {
            auto result = engine.compress(data);
            if (result) {
                ::benchmark::DoNotOptimize(result.value());
            }
        }
    }

    state.SetBytesProcessed(static_cast<int64_t>(data_size) *
                           static_cast<int64_t>(state.iterations()));

    if (total_count > 0) {
        state.counters["skip_rate"] = ::benchmark::Counter(
            static_cast<double>(skipped_count) / static_cast<double>(total_count) * 100.0);
    }
}

/**
 * @brief Benchmark full adaptive compression pipeline
 */
static void BM_Adaptive_Compression_Full(::benchmark::State& state) {
    const auto data_size = static_cast<std::size_t>(state.range(0));
    const auto compressibility = state.range(1) / 100.0;  // 0-100 to 0.0-1.0

    auto data = test_data_generator::generate_data_with_compressibility(
        data_size, compressibility, 42);

    compression_engine engine(compression_level::fast);

    for (auto _ : state) {
        if (engine.is_compressible(data)) {
            auto result = engine.compress(data);
            if (result) {
                ::benchmark::DoNotOptimize(result.value());
            }
        } else {
            ::benchmark::DoNotOptimize(data);
        }
    }

    state.SetBytesProcessed(static_cast<int64_t>(data_size) *
                           static_cast<int64_t>(state.iterations()));
}

// Register compression benchmarks

// LZ4 Fast Compression - various sizes
BENCHMARK(BM_LZ4_Compression_Fast)
    ->Arg(static_cast<int64_t>(64 * sizes::KB))
    ->Arg(static_cast<int64_t>(256 * sizes::KB))
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Arg(static_cast<int64_t>(4 * sizes::MB))
    ->Arg(static_cast<int64_t>(16 * sizes::MB))
    ->Unit(::benchmark::kMillisecond);

// LZ4 High Compression - various sizes
BENCHMARK(BM_LZ4_Compression_High)
    ->Arg(static_cast<int64_t>(64 * sizes::KB))
    ->Arg(static_cast<int64_t>(256 * sizes::KB))
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Arg(static_cast<int64_t>(4 * sizes::MB))
    ->Unit(::benchmark::kMillisecond);

// LZ4 Decompression - various sizes
BENCHMARK(BM_LZ4_Decompression)
    ->Arg(static_cast<int64_t>(64 * sizes::KB))
    ->Arg(static_cast<int64_t>(256 * sizes::KB))
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Arg(static_cast<int64_t>(4 * sizes::MB))
    ->Arg(static_cast<int64_t>(16 * sizes::MB))
    ->Unit(::benchmark::kMillisecond);

// Compression ratio benchmarks
BENCHMARK(BM_Compression_Ratio_Text)
    ->Arg(static_cast<int64_t>(256 * sizes::KB))
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Arg(static_cast<int64_t>(4 * sizes::MB))
    ->Unit(::benchmark::kMillisecond);

BENCHMARK(BM_Compression_Ratio_Binary)
    ->Arg(static_cast<int64_t>(256 * sizes::KB))
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Arg(static_cast<int64_t>(4 * sizes::MB))
    ->Unit(::benchmark::kMillisecond);

// Adaptive compression benchmarks
BENCHMARK(BM_Adaptive_Compression_Check)
    ->Arg(static_cast<int64_t>(4 * sizes::KB))
    ->Arg(static_cast<int64_t>(64 * sizes::KB))
    ->Arg(static_cast<int64_t>(256 * sizes::KB))
    ->Unit(::benchmark::kMicrosecond);

BENCHMARK(BM_Adaptive_Compression_Skip)
    ->Arg(static_cast<int64_t>(256 * sizes::KB))
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Unit(::benchmark::kMillisecond);

// Full adaptive compression with different compressibility ratios
BENCHMARK(BM_Adaptive_Compression_Full)
    ->Args({static_cast<int64_t>(1 * sizes::MB), 0})    // Random (incompressible)
    ->Args({static_cast<int64_t>(1 * sizes::MB), 50})   // Medium compressibility
    ->Args({static_cast<int64_t>(1 * sizes::MB), 100})  // Highly compressible
    ->Unit(::benchmark::kMillisecond);

}  // namespace kcenon::file_transfer::benchmark
