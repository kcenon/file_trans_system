/**
 * @file bench_single_file_throughput.cpp
 * @brief Benchmarks for single file transfer throughput
 *
 * Measures end-to-end throughput for file splitting and assembly operations,
 * targeting >= 500 MB/s for LAN transfers.
 */

#include <benchmark/benchmark.h>

#include <kcenon/file_transfer/core/chunk_assembler.h>
#include <kcenon/file_transfer/core/chunk_splitter.h>

#include "utils/benchmark_helpers.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace kcenon::file_transfer::benchmark {

/**
 * @brief Benchmark for complete file splitting throughput
 *
 * Measures throughput of splitting files into chunks, including:
 * - File I/O
 * - CRC32 calculation per chunk
 * - Memory operations
 */
static void BM_SingleFile_SplitThroughput(::benchmark::State& state) {
    const auto file_size = static_cast<std::size_t>(state.range(0));

    temp_file_manager temp_files;
    auto test_file = temp_files.create_random_file("throughput_test.bin", file_size, 42);

    chunk_config config(sizes::default_chunk);
    chunk_splitter splitter(config);

    uint64_t total_chunks = 0;

    for (auto _ : state) {
        auto id = transfer_id::generate();
        auto result = splitter.split(test_file, id);

        if (!result) {
            state.SkipWithError("Failed to create splitter iterator");
            return;
        }

        auto& iterator = result.value();
        uint64_t chunks_processed = 0;

        while (iterator.has_next()) {
            auto chunk_result = iterator.next();
            if (!chunk_result) {
                state.SkipWithError("Failed to get chunk");
                return;
            }
            ++chunks_processed;
            ::benchmark::DoNotOptimize(chunk_result.value());
        }

        total_chunks = chunks_processed;
    }

    state.SetBytesProcessed(static_cast<int64_t>(file_size) *
                           static_cast<int64_t>(state.iterations()));
    state.counters["chunks"] = static_cast<double>(total_chunks);
    state.counters["throughput_MB_s"] =
        ::benchmark::Counter(static_cast<double>(file_size) / sizes::MB,
                            ::benchmark::Counter::kIsIterationInvariantRate);
}

/**
 * @brief Benchmark for complete file assembly throughput
 *
 * Measures throughput of assembling chunks into files, including:
 * - File I/O
 * - CRC32 verification per chunk
 * - Memory operations
 */
static void BM_SingleFile_AssemblyThroughput(::benchmark::State& state) {
    const auto file_size = static_cast<std::size_t>(state.range(0));

    temp_file_manager temp_files;
    auto source_file = temp_files.create_random_file("assembly_source.bin", file_size, 42);

    // Pre-split the file
    chunk_config config(sizes::default_chunk);
    chunk_splitter splitter(config);
    std::vector<chunk> chunks;

    auto id = transfer_id::generate();
    auto split_result = splitter.split(source_file, id);
    if (!split_result) {
        state.SkipWithError("Failed to split source file");
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
        auto output_dir = std::filesystem::temp_directory_path() / "bench_throughput_output";
        std::filesystem::create_directories(output_dir);
        chunk_assembler assembler(output_dir);

        auto new_id = transfer_id::generate();
        std::vector<chunk> session_chunks;
        session_chunks.reserve(chunks.size());
        for (const auto& c : chunks) {
            chunk new_chunk = c;
            new_chunk.header.id = new_id;
            session_chunks.push_back(std::move(new_chunk));
        }

        auto start_result = assembler.start_session(
            new_id, "assembled_output.bin", file_size, chunks.size());
        if (!start_result) {
            state.SkipWithError("Failed to start assembly session");
            std::filesystem::remove_all(output_dir);
            return;
        }
        state.ResumeTiming();

        for (auto& chunk : session_chunks) {
            auto process_result = assembler.process_chunk(chunk);
            if (!process_result) {
                state.PauseTiming();
                assembler.cancel_session(new_id);
                std::filesystem::remove_all(output_dir);
                state.ResumeTiming();
                state.SkipWithError("Failed to process chunk");
                return;
            }
        }

        state.PauseTiming();
        assembler.cancel_session(new_id);
        std::filesystem::remove_all(output_dir);
        state.ResumeTiming();
    }

    state.SetBytesProcessed(static_cast<int64_t>(file_size) *
                           static_cast<int64_t>(state.iterations()));
    state.counters["chunks"] = static_cast<double>(chunks.size());
    state.counters["throughput_MB_s"] =
        ::benchmark::Counter(static_cast<double>(file_size) / sizes::MB,
                            ::benchmark::Counter::kIsIterationInvariantRate);
}

/**
 * @brief Benchmark for round-trip throughput (split + assemble)
 *
 * Measures end-to-end throughput including splitting and assembly
 */
static void BM_SingleFile_RoundTripThroughput(::benchmark::State& state) {
    const auto file_size = static_cast<std::size_t>(state.range(0));

    temp_file_manager temp_files;
    auto source_file = temp_files.create_random_file("roundtrip_source.bin", file_size, 42);

    chunk_config config(sizes::default_chunk);

    for (auto _ : state) {
        // Split phase
        chunk_splitter splitter(config);
        auto id = transfer_id::generate();
        auto split_result = splitter.split(source_file, id);

        if (!split_result) {
            state.SkipWithError("Failed to create splitter");
            return;
        }

        std::vector<chunk> chunks;
        auto& iterator = split_result.value();
        while (iterator.has_next()) {
            auto chunk_result = iterator.next();
            if (chunk_result) {
                chunks.push_back(std::move(chunk_result.value()));
            }
        }

        // Assembly phase
        state.PauseTiming();
        auto output_dir = std::filesystem::temp_directory_path() / "bench_roundtrip_output";
        std::filesystem::create_directories(output_dir);
        state.ResumeTiming();

        chunk_assembler assembler(output_dir);
        auto start_result = assembler.start_session(
            id, "roundtrip_output.bin", file_size, chunks.size());

        if (!start_result) {
            state.PauseTiming();
            std::filesystem::remove_all(output_dir);
            state.ResumeTiming();
            state.SkipWithError("Failed to start assembly session");
            return;
        }

        for (auto& chunk : chunks) {
            auto process_result = assembler.process_chunk(chunk);
            if (!process_result) {
                state.PauseTiming();
                assembler.cancel_session(id);
                std::filesystem::remove_all(output_dir);
                state.ResumeTiming();
                state.SkipWithError("Failed to process chunk");
                return;
            }
        }

        state.PauseTiming();
        assembler.cancel_session(id);
        std::filesystem::remove_all(output_dir);
        state.ResumeTiming();
    }

    // Report throughput (file size * 2 for split + assembly)
    state.SetBytesProcessed(static_cast<int64_t>(file_size) * 2 *
                           static_cast<int64_t>(state.iterations()));
    state.counters["throughput_MB_s"] =
        ::benchmark::Counter(static_cast<double>(file_size) * 2 / sizes::MB,
                            ::benchmark::Counter::kIsIterationInvariantRate);
}

/**
 * @brief Benchmark for chunk size impact on throughput
 */
static void BM_SingleFile_ChunkSizeImpact(::benchmark::State& state) {
    const auto file_size = sizes::large_file;  // Fixed 100MB file
    const auto chunk_size = static_cast<std::size_t>(state.range(0));

    temp_file_manager temp_files;
    auto test_file = temp_files.create_random_file("chunk_size_test.bin", file_size, 42);

    chunk_config config(chunk_size);
    chunk_splitter splitter(config);

    uint64_t total_chunks = 0;

    for (auto _ : state) {
        auto id = transfer_id::generate();
        auto result = splitter.split(test_file, id);

        if (!result) {
            state.SkipWithError("Failed to create splitter iterator");
            return;
        }

        auto& iterator = result.value();
        uint64_t chunks_processed = 0;

        while (iterator.has_next()) {
            auto chunk_result = iterator.next();
            if (!chunk_result) {
                state.SkipWithError("Failed to get chunk");
                return;
            }
            ++chunks_processed;
            ::benchmark::DoNotOptimize(chunk_result.value());
        }

        total_chunks = chunks_processed;
    }

    state.SetBytesProcessed(static_cast<int64_t>(file_size) *
                           static_cast<int64_t>(state.iterations()));
    state.counters["chunks"] = static_cast<double>(total_chunks);
    state.counters["chunk_size_KB"] = static_cast<double>(chunk_size / sizes::KB);
}

// Register split throughput benchmarks
BENCHMARK(BM_SingleFile_SplitThroughput)
    ->Arg(static_cast<int64_t>(sizes::small_file))    // 100 KB
    ->Arg(static_cast<int64_t>(sizes::medium_file))   // 10 MB
    ->Arg(static_cast<int64_t>(sizes::large_file))    // 100 MB
    ->Unit(::benchmark::kMillisecond)
    ->Iterations(10);

// Register assembly throughput benchmarks
BENCHMARK(BM_SingleFile_AssemblyThroughput)
    ->Arg(static_cast<int64_t>(sizes::small_file))
    ->Arg(static_cast<int64_t>(sizes::medium_file))
    ->Arg(static_cast<int64_t>(sizes::large_file))
    ->Unit(::benchmark::kMillisecond)
    ->Iterations(10);

// Register round-trip throughput benchmarks
BENCHMARK(BM_SingleFile_RoundTripThroughput)
    ->Arg(static_cast<int64_t>(sizes::small_file))
    ->Arg(static_cast<int64_t>(sizes::medium_file))
    ->Arg(static_cast<int64_t>(sizes::large_file))
    ->Unit(::benchmark::kMillisecond)
    ->Iterations(5);

// Register chunk size impact benchmarks
BENCHMARK(BM_SingleFile_ChunkSizeImpact)
    ->Arg(static_cast<int64_t>(sizes::min_chunk))      // 64 KB
    ->Arg(static_cast<int64_t>(128 * sizes::KB))       // 128 KB
    ->Arg(static_cast<int64_t>(sizes::default_chunk))  // 256 KB
    ->Arg(static_cast<int64_t>(512 * sizes::KB))       // 512 KB
    ->Arg(static_cast<int64_t>(sizes::max_chunk))      // 1 MB
    ->Unit(::benchmark::kMillisecond)
    ->Iterations(10);

}  // namespace kcenon::file_transfer::benchmark
