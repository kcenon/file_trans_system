/**
 * @file bench_latency.cpp
 * @brief Benchmarks for connection and response latency
 *
 * Performance Targets:
 * - Connection setup: < 100ms
 * - File list response (10K files): < 100ms
 */

#include <benchmark/benchmark.h>

#include <kcenon/file_transfer/file_transfer.h>

#include "utils/benchmark_helpers.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

namespace kcenon::file_transfer::benchmark {

namespace {

/**
 * @brief Helper class for managing benchmark server/client setup
 */
class benchmark_fixture {
public:
    benchmark_fixture() = default;
    ~benchmark_fixture() { cleanup(); }

    benchmark_fixture(const benchmark_fixture&) = delete;
    auto operator=(const benchmark_fixture&) -> benchmark_fixture& = delete;

    auto setup_server() -> bool {
        if (server_) return true;

        // Create temp directory
        base_dir_ = std::filesystem::temp_directory_path() /
                    ("bench_latency_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(base_dir_);
        storage_dir_ = base_dir_ / "storage";
        std::filesystem::create_directories(storage_dir_);

        // Create server
        auto server_result = file_transfer_server::builder()
            .with_storage_directory(storage_dir_)
            .with_max_connections(100)
            .build();

        if (!server_result.has_value()) {
            return false;
        }

        server_ = std::make_unique<file_transfer_server>(std::move(server_result.value()));

        // Start server
        port_ = get_available_port();
        auto start_result = server_->start(endpoint{port_});
        return start_result.has_value();
    }

    auto create_client() -> std::unique_ptr<file_transfer_client> {
        auto client_result = file_transfer_client::builder()
            .with_compression(compression_mode::none)
            .with_auto_reconnect(false)
            .with_connect_timeout(std::chrono::milliseconds(5000))
            .build();

        if (!client_result.has_value()) {
            return nullptr;
        }

        return std::make_unique<file_transfer_client>(std::move(client_result.value()));
    }

    void create_test_files(std::size_t count) {
        if (!server_) return;

        for (std::size_t i = 0; i < count; ++i) {
            auto filename = "test_file_" + std::to_string(i) + ".dat";
            auto path = storage_dir_ / filename;
            std::ofstream file(path, std::ios::binary);
            // Create small files (100 bytes each)
            std::vector<char> data(100, static_cast<char>(i % 256));
            file.write(data.data(), static_cast<std::streamsize>(data.size()));
        }
    }

    void cleanup() {
        if (server_ && server_->is_running()) {
            (void)server_->stop();
        }
        server_.reset();

        if (!base_dir_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(base_dir_, ec);
        }
    }

    [[nodiscard]] auto port() const -> uint16_t { return port_; }
    [[nodiscard]] auto storage_dir() const -> const std::filesystem::path& {
        return storage_dir_;
    }

private:
    static auto get_available_port() -> uint16_t {
        static std::atomic<uint16_t> port_counter{51000};
        return port_counter++;
    }

    std::unique_ptr<file_transfer_server> server_;
    std::filesystem::path base_dir_;
    std::filesystem::path storage_dir_;
    uint16_t port_{0};
};

// Global fixture for benchmarks that need persistent server
benchmark_fixture g_fixture;

}  // namespace

/**
 * @brief Benchmark for connection setup time
 *
 * Target: < 100ms
 */
static void BM_Connection_Setup(::benchmark::State& state) {
    if (!g_fixture.setup_server()) {
        state.SkipWithError("Failed to setup server");
        return;
    }

    for (auto _ : state) {
        auto client = g_fixture.create_client();
        if (!client) {
            state.SkipWithError("Failed to create client");
            return;
        }

        auto start = std::chrono::high_resolution_clock::now();
        auto result = client->connect(endpoint{"127.0.0.1", g_fixture.port()});
        auto end = std::chrono::high_resolution_clock::now();

        if (!result) {
            state.SkipWithError("Connection failed");
            return;
        }

        auto duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        state.SetIterationTime(duration_ms / 1000.0);

        // Disconnect for next iteration
        (void)client->disconnect();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark for connection teardown time
 */
static void BM_Connection_Teardown(::benchmark::State& state) {
    if (!g_fixture.setup_server()) {
        state.SkipWithError("Failed to setup server");
        return;
    }

    for (auto _ : state) {
        auto client = g_fixture.create_client();
        if (!client) {
            state.SkipWithError("Failed to create client");
            return;
        }

        auto connect_result = client->connect(endpoint{"127.0.0.1", g_fixture.port()});
        if (!connect_result) {
            state.SkipWithError("Connection failed");
            return;
        }

        state.PauseTiming();
        state.ResumeTiming();

        auto start = std::chrono::high_resolution_clock::now();
        auto result = client->disconnect();
        auto end = std::chrono::high_resolution_clock::now();

        if (!result) {
            state.SkipWithError("Disconnect failed");
            return;
        }

        auto duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        state.SetIterationTime(duration_ms / 1000.0);
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark for file list response time
 *
 * Target: < 100ms for 10K files
 */
static void BM_FileList_Response(::benchmark::State& state) {
    const auto file_count = static_cast<std::size_t>(state.range(0));

    // Setup server with test files
    benchmark_fixture fixture;
    if (!fixture.setup_server()) {
        state.SkipWithError("Failed to setup server");
        return;
    }

    fixture.create_test_files(file_count);

    // Create and connect client
    auto client = fixture.create_client();
    if (!client) {
        state.SkipWithError("Failed to create client");
        return;
    }

    auto connect_result = client->connect(endpoint{"127.0.0.1", fixture.port()});
    if (!connect_result) {
        state.SkipWithError("Connection failed");
        return;
    }

    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();
        auto result = client->list_files();
        auto end = std::chrono::high_resolution_clock::now();

        if (!result) {
            state.SkipWithError("List files failed");
            return;
        }

        auto duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        state.SetIterationTime(duration_ms / 1000.0);

        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetItemsProcessed(static_cast<int64_t>(file_count) *
                           static_cast<int64_t>(state.iterations()));

    // Report latency in ms
    state.counters["files"] = ::benchmark::Counter(
        static_cast<double>(file_count));
}

/**
 * @brief Benchmark for protocol round-trip time (simple ping-pong)
 *
 * Measures the time for a minimal request/response cycle using list_files
 * with zero files.
 */
static void BM_Protocol_RTT(::benchmark::State& state) {
    benchmark_fixture fixture;
    if (!fixture.setup_server()) {
        state.SkipWithError("Failed to setup server");
        return;
    }

    // No files created - minimal response

    auto client = fixture.create_client();
    if (!client) {
        state.SkipWithError("Failed to create client");
        return;
    }

    auto connect_result = client->connect(endpoint{"127.0.0.1", fixture.port()});
    if (!connect_result) {
        state.SkipWithError("Connection failed");
        return;
    }

    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();
        auto result = client->list_files();
        auto end = std::chrono::high_resolution_clock::now();

        if (!result) {
            state.SkipWithError("Request failed");
            return;
        }

        auto duration_us = std::chrono::duration<double, std::micro>(end - start).count();
        state.SetIterationTime(duration_us / 1000000.0);

        ::benchmark::DoNotOptimize(result.value());
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark for time to first byte (TTFB) - upload
 *
 * Measures the time from starting an upload until the first chunk is sent
 */
static void BM_Upload_TTFB(::benchmark::State& state) {
    const auto file_size = static_cast<std::size_t>(state.range(0));

    benchmark_fixture fixture;
    if (!fixture.setup_server()) {
        state.SkipWithError("Failed to setup server");
        return;
    }

    // Create test file
    temp_file_manager temp_files;
    auto test_file = temp_files.create_random_file("ttfb_test.bin", file_size, 42);

    auto client = fixture.create_client();
    if (!client) {
        state.SkipWithError("Failed to create client");
        return;
    }

    auto connect_result = client->connect(endpoint{"127.0.0.1", fixture.port()});
    if (!connect_result) {
        state.SkipWithError("Connection failed");
        return;
    }

    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();
        auto result = client->upload_file(
            test_file,
            "ttfb_upload_" + std::to_string(state.iterations()) + ".bin");

        if (!result) {
            state.SkipWithError("Upload initiation failed");
            return;
        }

        // Wait for completion to get accurate timing
        auto handle = std::move(result.value());
        auto wait_result = handle.wait();
        auto end = std::chrono::high_resolution_clock::now();

        if (!wait_result) {
            state.SkipWithError("Upload failed");
            return;
        }

        auto duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        state.SetIterationTime(duration_ms / 1000.0);
    }

    state.SetBytesProcessed(static_cast<int64_t>(file_size) *
                           static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark for time to first byte (TTFB) - download
 */
static void BM_Download_TTFB(::benchmark::State& state) {
    const auto file_size = static_cast<std::size_t>(state.range(0));

    benchmark_fixture fixture;
    if (!fixture.setup_server()) {
        state.SkipWithError("Failed to setup server");
        return;
    }

    // Create file in storage directory
    {
        std::ofstream file(fixture.storage_dir() / "ttfb_download.bin", std::ios::binary);
        std::mt19937 gen(42);
        std::uniform_int_distribution<> dis(0, 255);
        for (std::size_t i = 0; i < file_size; ++i) {
            char byte = static_cast<char>(dis(gen));
            file.write(&byte, 1);
        }
    }

    auto client = fixture.create_client();
    if (!client) {
        state.SkipWithError("Failed to create client");
        return;
    }

    auto connect_result = client->connect(endpoint{"127.0.0.1", fixture.port()});
    if (!connect_result) {
        state.SkipWithError("Connection failed");
        return;
    }

    temp_file_manager temp_files;

    for (auto _ : state) {
        auto download_path = temp_files.base_dir() /
                             ("download_" + std::to_string(state.iterations()) + ".bin");

        auto start = std::chrono::high_resolution_clock::now();
        auto result = client->download_file("ttfb_download.bin", download_path);

        if (!result) {
            state.SkipWithError("Download initiation failed");
            return;
        }

        auto handle = std::move(result.value());
        auto wait_result = handle.wait();
        auto end = std::chrono::high_resolution_clock::now();

        if (!wait_result) {
            state.SkipWithError("Download failed");
            return;
        }

        auto duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        state.SetIterationTime(duration_ms / 1000.0);

        // Cleanup downloaded file
        std::filesystem::remove(download_path);
    }

    state.SetBytesProcessed(static_cast<int64_t>(file_size) *
                           static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark for concurrent connection setup
 */
static void BM_Concurrent_Connections(::benchmark::State& state) {
    const auto num_clients = static_cast<std::size_t>(state.range(0));

    benchmark_fixture fixture;
    if (!fixture.setup_server()) {
        state.SkipWithError("Failed to setup server");
        return;
    }

    for (auto _ : state) {
        std::vector<std::unique_ptr<file_transfer_client>> clients;
        clients.reserve(num_clients);

        auto start = std::chrono::high_resolution_clock::now();

        // Create and connect all clients
        for (std::size_t i = 0; i < num_clients; ++i) {
            auto client = fixture.create_client();
            if (!client) {
                state.SkipWithError("Failed to create client");
                return;
            }

            auto result = client->connect(endpoint{"127.0.0.1", fixture.port()});
            if (!result) {
                state.SkipWithError("Connection failed");
                return;
            }

            clients.push_back(std::move(client));
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        state.SetIterationTime(duration_ms / 1000.0);

        // Cleanup - disconnect all clients
        state.PauseTiming();
        for (auto& client : clients) {
            (void)client->disconnect();
        }
        clients.clear();
        state.ResumeTiming();
    }

    state.SetItemsProcessed(static_cast<int64_t>(num_clients) *
                           static_cast<int64_t>(state.iterations()));

    state.counters["clients"] = ::benchmark::Counter(
        static_cast<double>(num_clients));
}

// Register latency benchmarks

// Connection benchmarks
BENCHMARK(BM_Connection_Setup)
    ->Unit(::benchmark::kMillisecond)
    ->UseManualTime()
    ->Iterations(50);

BENCHMARK(BM_Connection_Teardown)
    ->Unit(::benchmark::kMillisecond)
    ->UseManualTime()
    ->Iterations(50);

// File list benchmarks - various file counts
BENCHMARK(BM_FileList_Response)
    ->Arg(100)      // 100 files
    ->Arg(1000)     // 1K files
    ->Arg(5000)     // 5K files
    ->Arg(10000)    // 10K files (target)
    ->Unit(::benchmark::kMillisecond)
    ->UseManualTime();

// Protocol RTT benchmark
BENCHMARK(BM_Protocol_RTT)
    ->Unit(::benchmark::kMicrosecond)
    ->UseManualTime()
    ->Iterations(100);

// TTFB benchmarks
BENCHMARK(BM_Upload_TTFB)
    ->Arg(static_cast<int64_t>(64 * sizes::KB))
    ->Arg(static_cast<int64_t>(256 * sizes::KB))
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Unit(::benchmark::kMillisecond)
    ->UseManualTime();

BENCHMARK(BM_Download_TTFB)
    ->Arg(static_cast<int64_t>(64 * sizes::KB))
    ->Arg(static_cast<int64_t>(256 * sizes::KB))
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Unit(::benchmark::kMillisecond)
    ->UseManualTime();

// Concurrent connections benchmark
BENCHMARK(BM_Concurrent_Connections)
    ->Arg(5)
    ->Arg(10)
    ->Arg(25)
    ->Arg(50)
    ->Unit(::benchmark::kMillisecond)
    ->UseManualTime();

}  // namespace kcenon::file_transfer::benchmark
