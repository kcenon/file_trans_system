/**
 * @file bench_memory_usage.cpp
 * @brief Benchmarks for memory usage measurement
 *
 * Performance Targets:
 * - Server baseline memory: < 100 MB
 * - Client baseline memory: < 50 MB
 * - Per-connection overhead: < 1 MB
 * - Memory usage constant regardless of file size
 */

#include <benchmark/benchmark.h>

#include <kcenon/file_transfer/file_transfer.h>

#include "utils/benchmark_helpers.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <vector>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/task.h>
#elif defined(__linux__)
#include <fstream>
#include <sstream>
#include <unistd.h>
#endif

namespace kcenon::file_transfer::benchmark {

namespace {

/**
 * @brief Get current process memory usage in bytes
 * @return Resident memory size in bytes, or 0 on failure
 */
auto get_memory_usage() -> std::size_t {
#ifdef __APPLE__
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return info.resident_size;
    }
    return 0;
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    if (!statm.is_open()) {
        return 0;
    }

    std::size_t size = 0;
    std::size_t resident = 0;
    statm >> size >> resident;

    // resident is in pages, convert to bytes
    long page_size = sysconf(_SC_PAGESIZE);
    return resident * static_cast<std::size_t>(page_size);
#else
    return 0;
#endif
}

/**
 * @brief Helper class for memory benchmark fixture
 */
class memory_benchmark_fixture {
public:
    memory_benchmark_fixture() = default;
    ~memory_benchmark_fixture() { cleanup(); }

    memory_benchmark_fixture(const memory_benchmark_fixture&) = delete;
    auto operator=(const memory_benchmark_fixture&)
        -> memory_benchmark_fixture& = delete;

    auto setup_server() -> bool {
        if (server_) return true;

        // Create temp directory
        base_dir_ = std::filesystem::temp_directory_path() /
                    ("bench_memory_" + std::to_string(std::random_device{}()));
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

        server_ = std::make_unique<file_transfer_server>(
            std::move(server_result.value()));

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

        return std::make_unique<file_transfer_client>(
            std::move(client_result.value()));
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
        static std::atomic<uint16_t> port_counter{52000};
        return port_counter++;
    }

    std::unique_ptr<file_transfer_server> server_;
    std::filesystem::path base_dir_;
    std::filesystem::path storage_dir_;
    uint16_t port_{0};
};

}  // namespace

/**
 * @brief Measure baseline process memory (no server/client)
 *
 * Establishes baseline memory usage before any components are created.
 */
static void BM_Memory_Baseline(::benchmark::State& state) {
    for (auto _ : state) {
        auto memory = get_memory_usage();
        ::benchmark::DoNotOptimize(memory);

        state.counters["memory_MB"] = ::benchmark::Counter(
            static_cast<double>(memory) / sizes::MB,
            ::benchmark::Counter::kDefaults);
    }
}

/**
 * @brief Measure server baseline memory usage
 *
 * Target: < 100 MB
 */
static void BM_Memory_ServerBaseline(::benchmark::State& state) {
    for (auto _ : state) {
        auto before = get_memory_usage();

        // Create server
        auto base_dir = std::filesystem::temp_directory_path() /
                        ("bench_server_mem_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(base_dir);
        auto storage_dir = base_dir / "storage";
        std::filesystem::create_directories(storage_dir);

        auto server_result = file_transfer_server::builder()
            .with_storage_directory(storage_dir)
            .with_max_connections(100)
            .build();

        if (!server_result.has_value()) {
            state.SkipWithError("Failed to create server");
            return;
        }

        auto server = std::make_unique<file_transfer_server>(
            std::move(server_result.value()));

        // Start server
        static std::atomic<uint16_t> port_counter{53000};
        auto port = port_counter++;
        auto start_result = server->start(endpoint{port});
        if (!start_result.has_value()) {
            state.SkipWithError("Failed to start server");
            return;
        }

        // Allow server to stabilize
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto after = get_memory_usage();
        auto server_memory = (after > before) ? (after - before) : 0;

        ::benchmark::DoNotOptimize(server_memory);

        state.counters["server_memory_MB"] = ::benchmark::Counter(
            static_cast<double>(server_memory) / sizes::MB,
            ::benchmark::Counter::kDefaults);

        state.counters["target_MB"] = ::benchmark::Counter(
            static_cast<double>(targets::server_memory) / sizes::MB,
            ::benchmark::Counter::kDefaults);

        state.counters["passes_target"] = ::benchmark::Counter(
            server_memory < targets::server_memory ? 1.0 : 0.0,
            ::benchmark::Counter::kDefaults);

        // Cleanup
        (void)server->stop();
        server.reset();
        std::error_code ec;
        std::filesystem::remove_all(base_dir, ec);
    }
}

/**
 * @brief Measure client baseline memory usage
 *
 * Target: < 50 MB
 */
static void BM_Memory_ClientBaseline(::benchmark::State& state) {
    memory_benchmark_fixture fixture;
    if (!fixture.setup_server()) {
        state.SkipWithError("Failed to setup server");
        return;
    }

    for (auto _ : state) {
        auto before = get_memory_usage();

        auto client = fixture.create_client();
        if (!client) {
            state.SkipWithError("Failed to create client");
            return;
        }

        auto connect_result = client->connect(
            endpoint{"127.0.0.1", fixture.port()});
        if (!connect_result) {
            state.SkipWithError("Connection failed");
            return;
        }

        // Allow client to stabilize
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto after = get_memory_usage();
        auto client_memory = (after > before) ? (after - before) : 0;

        ::benchmark::DoNotOptimize(client_memory);

        state.counters["client_memory_MB"] = ::benchmark::Counter(
            static_cast<double>(client_memory) / sizes::MB,
            ::benchmark::Counter::kDefaults);

        state.counters["target_MB"] = ::benchmark::Counter(
            static_cast<double>(targets::client_memory) / sizes::MB,
            ::benchmark::Counter::kDefaults);

        state.counters["passes_target"] = ::benchmark::Counter(
            client_memory < targets::client_memory ? 1.0 : 0.0,
            ::benchmark::Counter::kDefaults);

        // Cleanup
        (void)client->disconnect();
    }
}

/**
 * @brief Measure memory overhead per connection
 *
 * Target: < 1 MB per connection
 */
static void BM_Memory_PerConnection(::benchmark::State& state) {
    const auto num_connections = static_cast<std::size_t>(state.range(0));

    memory_benchmark_fixture fixture;
    if (!fixture.setup_server()) {
        state.SkipWithError("Failed to setup server");
        return;
    }

    for (auto _ : state) {
        auto before = get_memory_usage();

        std::vector<std::unique_ptr<file_transfer_client>> clients;
        clients.reserve(num_connections);

        for (std::size_t i = 0; i < num_connections; ++i) {
            auto client = fixture.create_client();
            if (!client) {
                state.SkipWithError("Failed to create client");
                return;
            }

            auto connect_result = client->connect(
                endpoint{"127.0.0.1", fixture.port()});
            if (!connect_result) {
                state.SkipWithError("Connection failed");
                return;
            }

            clients.push_back(std::move(client));
        }

        // Allow connections to stabilize
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto after = get_memory_usage();
        auto total_memory = (after > before) ? (after - before) : 0;
        auto per_connection_memory = total_memory / num_connections;

        ::benchmark::DoNotOptimize(per_connection_memory);

        state.counters["total_memory_MB"] = ::benchmark::Counter(
            static_cast<double>(total_memory) / sizes::MB,
            ::benchmark::Counter::kDefaults);

        state.counters["per_connection_KB"] = ::benchmark::Counter(
            static_cast<double>(per_connection_memory) / sizes::KB,
            ::benchmark::Counter::kDefaults);

        state.counters["target_KB"] = ::benchmark::Counter(
            static_cast<double>(targets::per_connection) / sizes::KB,
            ::benchmark::Counter::kDefaults);

        state.counters["passes_target"] = ::benchmark::Counter(
            per_connection_memory < targets::per_connection ? 1.0 : 0.0,
            ::benchmark::Counter::kDefaults);

        state.counters["connections"] = ::benchmark::Counter(
            static_cast<double>(num_connections),
            ::benchmark::Counter::kDefaults);

        // Cleanup
        for (auto& client : clients) {
            (void)client->disconnect();
        }
        clients.clear();
    }
}

/**
 * @brief Verify memory usage is constant regardless of file size
 *
 * Memory should not grow proportionally with file size (streaming design).
 */
static void BM_Memory_FileSize_Constant(::benchmark::State& state) {
    const auto file_size = static_cast<std::size_t>(state.range(0));

    memory_benchmark_fixture fixture;
    if (!fixture.setup_server()) {
        state.SkipWithError("Failed to setup server");
        return;
    }

    // Create test file
    temp_file_manager temp_files;
    auto test_file = temp_files.create_random_file("memory_test.bin", file_size, 42);

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

    // Allow initial connection to stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    for (auto _ : state) {
        auto before = get_memory_usage();

        auto result = client->upload_file(
            test_file,
            "memory_upload_" + std::to_string(state.iterations()) + ".bin");

        if (!result) {
            state.SkipWithError("Upload initiation failed");
            return;
        }

        auto handle = std::move(result.value());
        auto wait_result = handle.wait();

        if (!wait_result) {
            state.SkipWithError("Upload failed");
            return;
        }

        auto after = get_memory_usage();
        auto memory_during_transfer = (after > before) ? (after - before) : 0;

        ::benchmark::DoNotOptimize(memory_during_transfer);

        state.counters["file_size_MB"] = ::benchmark::Counter(
            static_cast<double>(file_size) / sizes::MB,
            ::benchmark::Counter::kDefaults);

        state.counters["memory_overhead_MB"] = ::benchmark::Counter(
            static_cast<double>(memory_during_transfer) / sizes::MB,
            ::benchmark::Counter::kDefaults);
    }

    state.SetBytesProcessed(static_cast<int64_t>(file_size) *
                           static_cast<int64_t>(state.iterations()));
}

// Register memory benchmarks

BENCHMARK(BM_Memory_Baseline)
    ->Unit(::benchmark::kMillisecond)
    ->Iterations(10);

BENCHMARK(BM_Memory_ServerBaseline)
    ->Unit(::benchmark::kMillisecond)
    ->Iterations(5);

BENCHMARK(BM_Memory_ClientBaseline)
    ->Unit(::benchmark::kMillisecond)
    ->Iterations(10);

BENCHMARK(BM_Memory_PerConnection)
    ->Arg(1)
    ->Arg(5)
    ->Arg(10)
    ->Arg(25)
    ->Arg(50)
    ->Unit(::benchmark::kMillisecond)
    ->Iterations(3);

BENCHMARK(BM_Memory_FileSize_Constant)
    ->Arg(static_cast<int64_t>(1 * sizes::MB))     // 1 MB
    ->Arg(static_cast<int64_t>(10 * sizes::MB))    // 10 MB
    ->Arg(static_cast<int64_t>(100 * sizes::MB))   // 100 MB
    ->Unit(::benchmark::kMillisecond)
    ->Iterations(3);

}  // namespace kcenon::file_transfer::benchmark
