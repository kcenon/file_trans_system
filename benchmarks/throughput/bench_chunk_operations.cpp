/**
 * @file bench_chunk_operations.cpp
 * @brief Benchmarks for chunk splitting and assembling operations
 */

#include <benchmark/benchmark.h>

#include <kcenon/file_transfer/core/chunk_assembler.h>
#include <kcenon/file_transfer/core/chunk_splitter.h>
#include <kcenon/file_transfer/core/checksum.h>

#include "utils/benchmark_helpers.h"

#include <filesystem>
#include <fstream>
#include <memory>

namespace kcenon::file_transfer::benchmark {

/**
 * @brief Benchmark for chunk_splitter with various file sizes
 */
static void BM_ChunkSplitter_Split(::benchmark::State& state) {
    const auto file_size = static_cast<std::size_t>(state.range(0));
    const auto chunk_size = static_cast<std::size_t>(state.range(1));

    temp_file_manager temp_files;
    auto test_file = temp_files.create_random_file("split_test.bin", file_size, 42);

    chunk_config config(chunk_size);
    chunk_splitter splitter(config);

    for (auto _ : state) {
        state.PauseTiming();
        auto id = transfer_id::generate();
        state.ResumeTiming();

        auto result = splitter.split(test_file, id);
        if (!result) {
            state.SkipWithError("Failed to create splitter iterator");
            return;
        }

        auto& iterator = result.value();
        while (iterator.has_next()) {
            auto chunk_result = iterator.next();
            if (!chunk_result) {
                state.SkipWithError("Failed to get chunk");
                return;
            }
            ::benchmark::DoNotOptimize(chunk_result.value());
        }
    }

    state.SetBytesProcessed(static_cast<int64_t>(file_size) *
                           static_cast<int64_t>(state.iterations()));
    state.SetItemsProcessed(static_cast<int64_t>((file_size + chunk_size - 1) / chunk_size) *
                           static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark for chunk_assembler processing
 */
static void BM_ChunkAssembler_Process(::benchmark::State& state) {
    const auto file_size = static_cast<std::size_t>(state.range(0));
    const auto chunk_size = static_cast<std::size_t>(state.range(1));

    temp_file_manager temp_files;
    auto test_file = temp_files.create_random_file("assemble_source.bin", file_size, 42);

    // Pre-generate all chunks
    chunk_config config(chunk_size);
    chunk_splitter splitter(config);
    std::vector<chunk> chunks;

    auto id = transfer_id::generate();
    auto split_result = splitter.split(test_file, id);
    if (!split_result) {
        state.SkipWithError("Failed to create splitter");
        return;
    }

    auto& iterator = split_result.value();
    while (iterator.has_next()) {
        auto chunk_result = iterator.next();
        if (chunk_result) {
            chunks.push_back(std::move(chunk_result.value()));
        }
    }

    for (auto _ : state) {
        state.PauseTiming();
        auto output_dir = std::filesystem::temp_directory_path() / "bench_assembler_output";
        std::filesystem::create_directories(output_dir);
        chunk_assembler assembler(output_dir);

        auto new_id = transfer_id::generate();
        // Update chunk IDs for new session
        std::vector<chunk> session_chunks;
        session_chunks.reserve(chunks.size());
        for (const auto& c : chunks) {
            chunk new_chunk = c;
            new_chunk.header.id = new_id;
            session_chunks.push_back(std::move(new_chunk));
        }

        auto start_result = assembler.start_session(
            new_id, "output.bin", file_size, chunks.size());
        if (!start_result) {
            state.SkipWithError("Failed to start session");
            std::filesystem::remove_all(output_dir);
            return;
        }
        state.ResumeTiming();

        for (auto& chunk : session_chunks) {
            auto process_result = assembler.process_chunk(chunk);
            if (!process_result) {
                state.SkipWithError("Failed to process chunk");
                break;
            }
        }

        state.PauseTiming();
        assembler.cancel_session(new_id);
        std::filesystem::remove_all(output_dir);
        state.ResumeTiming();
    }

    state.SetBytesProcessed(static_cast<int64_t>(file_size) *
                           static_cast<int64_t>(state.iterations()));
    state.SetItemsProcessed(static_cast<int64_t>(chunks.size()) *
                           static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark for CRC32 checksum calculation
 */
static void BM_Checksum_CRC32(::benchmark::State& state) {
    const auto data_size = static_cast<std::size_t>(state.range(0));
    auto data = test_data_generator::generate_random_data(data_size, 42);

    for (auto _ : state) {
        auto crc = checksum::crc32(data);
        ::benchmark::DoNotOptimize(crc);
    }

    state.SetBytesProcessed(static_cast<int64_t>(data_size) *
                           static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark for SHA-256 hash calculation
 */
static void BM_Checksum_SHA256(::benchmark::State& state) {
    const auto data_size = static_cast<std::size_t>(state.range(0));
    auto data = test_data_generator::generate_random_data(data_size, 42);

    for (auto _ : state) {
        auto hash = checksum::sha256(data);
        ::benchmark::DoNotOptimize(hash);
    }

    state.SetBytesProcessed(static_cast<int64_t>(data_size) *
                           static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark for SHA-256 file hash calculation
 */
static void BM_Checksum_SHA256_File(::benchmark::State& state) {
    const auto file_size = static_cast<std::size_t>(state.range(0));

    temp_file_manager temp_files;
    auto test_file = temp_files.create_random_file("sha256_test.bin", file_size, 42);

    for (auto _ : state) {
        auto result = checksum::sha256_file(test_file);
        if (!result) {
            state.SkipWithError("Failed to calculate file hash");
            return;
        }
        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetBytesProcessed(static_cast<int64_t>(file_size) *
                           static_cast<int64_t>(state.iterations()));
}

// Register benchmarks with various sizes

// Chunk Splitter benchmarks
BENCHMARK(BM_ChunkSplitter_Split)
    ->Args({static_cast<int64_t>(sizes::small_file), static_cast<int64_t>(sizes::default_chunk)})
    ->Args({static_cast<int64_t>(sizes::medium_file), static_cast<int64_t>(sizes::default_chunk)})
    ->Args({static_cast<int64_t>(sizes::large_file), static_cast<int64_t>(sizes::default_chunk)})
    ->Args({static_cast<int64_t>(sizes::large_file), static_cast<int64_t>(sizes::min_chunk)})
    ->Args({static_cast<int64_t>(sizes::large_file), static_cast<int64_t>(sizes::max_chunk)})
    ->Unit(::benchmark::kMillisecond);

// Chunk Assembler benchmarks
BENCHMARK(BM_ChunkAssembler_Process)
    ->Args({static_cast<int64_t>(sizes::small_file), static_cast<int64_t>(sizes::default_chunk)})
    ->Args({static_cast<int64_t>(sizes::medium_file), static_cast<int64_t>(sizes::default_chunk)})
    ->Args({static_cast<int64_t>(sizes::large_file), static_cast<int64_t>(sizes::default_chunk)})
    ->Unit(::benchmark::kMillisecond);

// CRC32 benchmarks
BENCHMARK(BM_Checksum_CRC32)
    ->Arg(static_cast<int64_t>(1 * sizes::KB))
    ->Arg(static_cast<int64_t>(64 * sizes::KB))
    ->Arg(static_cast<int64_t>(256 * sizes::KB))
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Unit(::benchmark::kMicrosecond);

// SHA-256 benchmarks
BENCHMARK(BM_Checksum_SHA256)
    ->Arg(static_cast<int64_t>(1 * sizes::KB))
    ->Arg(static_cast<int64_t>(64 * sizes::KB))
    ->Arg(static_cast<int64_t>(256 * sizes::KB))
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Unit(::benchmark::kMicrosecond);

// SHA-256 File benchmarks
BENCHMARK(BM_Checksum_SHA256_File)
    ->Arg(static_cast<int64_t>(sizes::small_file))
    ->Arg(static_cast<int64_t>(sizes::medium_file))
    ->Arg(static_cast<int64_t>(sizes::large_file))
    ->Unit(::benchmark::kMillisecond);

}  // namespace kcenon::file_transfer::benchmark
