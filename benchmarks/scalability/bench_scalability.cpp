/**
 * @file bench_scalability.cpp
 * @brief Benchmarks for system scalability
 *
 * Performance Targets:
 * - Support >= 100 concurrent connections
 * - Linear performance scaling with concurrent connections
 * - Consistent performance across file sizes
 * - Long-running memory stability
 */

#include <benchmark/benchmark.h>

#include <kcenon/file_transfer/file_transfer.h>

#include "utils/benchmark_helpers.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
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

    long page_size = sysconf(_SC_PAGESIZE);
    return resident * static_cast<std::size_t>(page_size);
#else
    return 0;
#endif
}

/**
 * @brief Helper class for scalability benchmark fixture
 */
class scalability_benchmark_fixture {
public:
    scalability_benchmark_fixture() = default;
    ~scalability_benchmark_fixture() { cleanup(); }

    scalability_benchmark_fixture(const scalability_benchmark_fixture&) = delete;
    auto operator=(const scalability_benchmark_fixture&)
        -> scalability_benchmark_fixture& = delete;

    auto setup_server(std::size_t max_connections = 150) -> bool {
        if (server_) return true;

        // Create temp directory
        base_dir_ = std::filesystem::temp_directory_path() /
                    ("bench_scalability_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(base_dir_);
        storage_dir_ = base_dir_ / "storage";
        std::filesystem::create_directories(storage_dir_);

        // Create server with high connection limit
        auto server_result = file_transfer_server::builder()
            .with_storage_directory(storage_dir_)
            .with_max_connections(max_connections)
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
            .with_connect_timeout(std::chrono::milliseconds(10000))
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
        static std::atomic<uint16_t> port_counter{54000};
        return port_counter++;
    }

    std::unique_ptr<file_transfer_server> server_;
    std::filesystem::path base_dir_;
    std::filesystem::path storage_dir_;
    uint16_t port_{0};
};

}  // namespace

/**
 * @brief Measure performance vs concurrent connection count
 *
 * Tests: 10, 50, 100 connections
 * Target: >= 100 concurrent connections supported
 */
static void BM_Scalability_ConcurrentConnections(::benchmark::State& state) {
    const auto num_connections = static_cast<std::size_t>(state.range(0));

    scalability_benchmark_fixture fixture;
    if (!fixture.setup_server(num_connections + 50)) {
        state.SkipWithError("Failed to setup server");
        return;
    }

    for (auto _ : state) {
        std::vector<std::unique_ptr<file_transfer_client>> clients;
        clients.reserve(num_connections);

        std::atomic<std::size_t> successful_connections{0};
        std::atomic<std::size_t> failed_connections{0};

        auto start = std::chrono::high_resolution_clock::now();

        // Create and connect all clients concurrently
        std::vector<std::future<std::unique_ptr<file_transfer_client>>> futures;
        futures.reserve(num_connections);

        for (std::size_t i = 0; i < num_connections; ++i) {
            futures.push_back(std::async(std::launch::async,
                [&fixture, &successful_connections, &failed_connections]()
                    -> std::unique_ptr<file_transfer_client> {
                    auto client = fixture.create_client();
                    if (!client) {
                        ++failed_connections;
                        return nullptr;
                    }

                    auto result = client->connect(
                        endpoint{"127.0.0.1", fixture.port()});
                    if (!result) {
                        ++failed_connections;
                        return nullptr;
                    }

                    ++successful_connections;
                    return client;
                }));
        }

        // Collect results
        for (auto& future : futures) {
            auto client = future.get();
            if (client) {
                clients.push_back(std::move(client));
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

        state.SetIterationTime(duration_ms / 1000.0);

        state.counters["requested_connections"] = ::benchmark::Counter(
            static_cast<double>(num_connections),
            ::benchmark::Counter::kDefaults);

        state.counters["successful_connections"] = ::benchmark::Counter(
            static_cast<double>(successful_connections.load()),
            ::benchmark::Counter::kDefaults);

        state.counters["failed_connections"] = ::benchmark::Counter(
            static_cast<double>(failed_connections.load()),
            ::benchmark::Counter::kDefaults);

        state.counters["success_rate"] = ::benchmark::Counter(
            static_cast<double>(successful_connections.load()) /
            static_cast<double>(num_connections) * 100.0,
            ::benchmark::Counter::kDefaults);

        state.counters["time_per_connection_ms"] = ::benchmark::Counter(
            duration_ms / static_cast<double>(successful_connections.load()),
            ::benchmark::Counter::kDefaults);

        // Cleanup
        state.PauseTiming();
        for (auto& client : clients) {
            if (client) {
                (void)client->disconnect();
            }
        }
        clients.clear();
        state.ResumeTiming();
    }

    state.SetItemsProcessed(static_cast<int64_t>(num_connections) *
                           static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Measure throughput performance vs file size
 *
 * Tests: 1MB, 100MB, 1GB, 10GB
 */
static void BM_Scalability_FileSize(::benchmark::State& state) {
    const auto file_size = static_cast<std::size_t>(state.range(0));

    scalability_benchmark_fixture fixture;
    if (!fixture.setup_server()) {
        state.SkipWithError("Failed to setup server");
        return;
    }

    // Create test file
    temp_file_manager temp_files;
    auto test_file = temp_files.create_random_file("scale_test.bin", file_size, 42);

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
            "scale_upload_" + std::to_string(state.iterations()) + ".bin");

        if (!result) {
            state.SkipWithError("Upload initiation failed");
            return;
        }

        auto handle = std::move(result.value());
        auto wait_result = handle.wait();

        auto end = std::chrono::high_resolution_clock::now();

        if (!wait_result) {
            state.SkipWithError("Upload failed");
            return;
        }

        auto duration_s = std::chrono::duration<double>(end - start).count();
        auto throughput_mbps = (static_cast<double>(file_size) / sizes::MB) / duration_s;

        state.SetIterationTime(duration_s);

        state.counters["file_size_MB"] = ::benchmark::Counter(
            static_cast<double>(file_size) / sizes::MB,
            ::benchmark::Counter::kDefaults);

        state.counters["throughput_MB_s"] = ::benchmark::Counter(
            throughput_mbps,
            ::benchmark::Counter::kDefaults);
    }

    state.SetBytesProcessed(static_cast<int64_t>(file_size) *
                           static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Test stability with 100 concurrent connections
 *
 * Target: System remains stable with >= 100 connections
 */
static void BM_Scalability_100Connections_Stability(::benchmark::State& state) {
    constexpr std::size_t target_connections = 100;

    scalability_benchmark_fixture fixture;
    if (!fixture.setup_server(150)) {
        state.SkipWithError("Failed to setup server");
        return;
    }

    // Create test files in storage
    temp_file_manager temp_files;
    auto test_file = temp_files.create_random_file("stability_test.bin", sizes::MB, 42);

    for (auto _ : state) {
        std::vector<std::unique_ptr<file_transfer_client>> clients;
        clients.reserve(target_connections);

        std::atomic<std::size_t> successful_ops{0};
        std::atomic<std::size_t> failed_ops{0};

        // First, establish all connections
        for (std::size_t i = 0; i < target_connections; ++i) {
            auto client = fixture.create_client();
            if (!client) continue;

            auto result = client->connect(endpoint{"127.0.0.1", fixture.port()});
            if (!result) continue;

            clients.push_back(std::move(client));
        }

        if (clients.size() < target_connections / 2) {
            state.SkipWithError("Too few connections established");
            return;
        }

        auto start = std::chrono::high_resolution_clock::now();

        // Perform concurrent operations (list files) from all clients
        std::vector<std::future<bool>> futures;
        futures.reserve(clients.size());

        for (auto& client : clients) {
            futures.push_back(std::async(std::launch::async,
                [&client, &successful_ops, &failed_ops]() -> bool {
                    auto result = client->list_files();
                    if (result) {
                        ++successful_ops;
                        return true;
                    }
                    ++failed_ops;
                    return false;
                }));
        }

        // Wait for all operations
        for (auto& future : futures) {
            future.wait();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

        state.SetIterationTime(duration_ms / 1000.0);

        state.counters["active_connections"] = ::benchmark::Counter(
            static_cast<double>(clients.size()),
            ::benchmark::Counter::kDefaults);

        state.counters["successful_ops"] = ::benchmark::Counter(
            static_cast<double>(successful_ops.load()),
            ::benchmark::Counter::kDefaults);

        state.counters["failed_ops"] = ::benchmark::Counter(
            static_cast<double>(failed_ops.load()),
            ::benchmark::Counter::kDefaults);

        state.counters["success_rate"] = ::benchmark::Counter(
            static_cast<double>(successful_ops.load()) /
            static_cast<double>(clients.size()) * 100.0,
            ::benchmark::Counter::kDefaults);

        // Cleanup
        state.PauseTiming();
        for (auto& client : clients) {
            if (client) {
                (void)client->disconnect();
            }
        }
        clients.clear();
        state.ResumeTiming();
    }

    state.SetItemsProcessed(static_cast<int64_t>(target_connections) *
                           static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Measure long-running memory stability
 *
 * Run multiple transfer cycles and verify memory doesn't grow unboundedly.
 */
static void BM_Scalability_MemoryStability(::benchmark::State& state) {
    const auto num_cycles = static_cast<std::size_t>(state.range(0));

    scalability_benchmark_fixture fixture;
    if (!fixture.setup_server()) {
        state.SkipWithError("Failed to setup server");
        return;
    }

    // Create test file
    temp_file_manager temp_files;
    auto test_file = temp_files.create_random_file(
        "stability_test.bin", 10 * sizes::MB, 42);

    for (auto _ : state) {
        auto initial_memory = get_memory_usage();

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

        auto start = std::chrono::high_resolution_clock::now();

        std::size_t peak_memory = initial_memory;

        // Perform multiple transfer cycles
        for (std::size_t i = 0; i < num_cycles; ++i) {
            auto result = client->upload_file(
                test_file,
                "stability_upload_" + std::to_string(i) + ".bin");

            if (!result) {
                state.SkipWithError("Upload failed");
                return;
            }

            auto handle = std::move(result.value());
            auto wait_result = handle.wait();

            if (!wait_result) {
                state.SkipWithError("Upload wait failed");
                return;
            }

            // Track peak memory
            auto current_memory = get_memory_usage();
            if (current_memory > peak_memory) {
                peak_memory = current_memory;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration_s = std::chrono::duration<double>(end - start).count();

        auto final_memory = get_memory_usage();
        auto memory_growth = (final_memory > initial_memory)
            ? (final_memory - initial_memory) : 0;

        state.SetIterationTime(duration_s);

        state.counters["cycles"] = ::benchmark::Counter(
            static_cast<double>(num_cycles),
            ::benchmark::Counter::kDefaults);

        state.counters["initial_memory_MB"] = ::benchmark::Counter(
            static_cast<double>(initial_memory) / sizes::MB,
            ::benchmark::Counter::kDefaults);

        state.counters["peak_memory_MB"] = ::benchmark::Counter(
            static_cast<double>(peak_memory) / sizes::MB,
            ::benchmark::Counter::kDefaults);

        state.counters["final_memory_MB"] = ::benchmark::Counter(
            static_cast<double>(final_memory) / sizes::MB,
            ::benchmark::Counter::kDefaults);

        state.counters["memory_growth_MB"] = ::benchmark::Counter(
            static_cast<double>(memory_growth) / sizes::MB,
            ::benchmark::Counter::kDefaults);

        // Memory growth should be minimal (< 10 MB over many cycles)
        state.counters["memory_stable"] = ::benchmark::Counter(
            memory_growth < 10 * sizes::MB ? 1.0 : 0.0,
            ::benchmark::Counter::kDefaults);

        // Cleanup
        state.PauseTiming();
        (void)client->disconnect();
        state.ResumeTiming();
    }
}

/**
 * @brief Measure concurrent upload throughput
 *
 * Multiple clients uploading simultaneously.
 */
static void BM_Scalability_ConcurrentUploads(::benchmark::State& state) {
    const auto num_clients = static_cast<std::size_t>(state.range(0));
    constexpr std::size_t file_size = sizes::MB;  // 1 MB per client

    scalability_benchmark_fixture fixture;
    if (!fixture.setup_server(num_clients + 10)) {
        state.SkipWithError("Failed to setup server");
        return;
    }

    // Create test files for each client
    std::vector<std::filesystem::path> test_files;
    test_files.reserve(num_clients);
    temp_file_manager temp_files;

    for (std::size_t i = 0; i < num_clients; ++i) {
        test_files.push_back(temp_files.create_random_file(
            "concurrent_" + std::to_string(i) + ".bin",
            file_size,
            static_cast<uint32_t>(i)));
    }

    for (auto _ : state) {
        std::vector<std::unique_ptr<file_transfer_client>> clients;
        clients.reserve(num_clients);

        // Create and connect all clients
        for (std::size_t i = 0; i < num_clients; ++i) {
            auto client = fixture.create_client();
            if (!client) continue;

            auto result = client->connect(endpoint{"127.0.0.1", fixture.port()});
            if (!result) continue;

            clients.push_back(std::move(client));
        }

        if (clients.size() < num_clients) {
            state.SkipWithError("Failed to create all clients");
            return;
        }

        std::atomic<std::size_t> successful_uploads{0};
        std::atomic<uint64_t> total_bytes{0};

        auto start = std::chrono::high_resolution_clock::now();

        // Start concurrent uploads
        std::vector<std::future<bool>> futures;
        futures.reserve(num_clients);

        for (std::size_t i = 0; i < clients.size(); ++i) {
            futures.push_back(std::async(std::launch::async,
                [&clients, &test_files, &successful_uploads, &total_bytes,
                 i, file_size]() -> bool {
                    auto result = clients[i]->upload_file(
                        test_files[i],
                        "concurrent_upload_" + std::to_string(i) + ".bin");

                    if (!result) return false;

                    auto handle = std::move(result.value());
                    auto wait_result = handle.wait();

                    if (wait_result) {
                        ++successful_uploads;
                        total_bytes += file_size;
                        return true;
                    }
                    return false;
                }));
        }

        // Wait for all uploads
        for (auto& future : futures) {
            future.wait();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration_s = std::chrono::duration<double>(end - start).count();

        auto aggregate_throughput_mbps =
            (static_cast<double>(total_bytes.load()) / sizes::MB) / duration_s;

        state.SetIterationTime(duration_s);

        state.counters["clients"] = ::benchmark::Counter(
            static_cast<double>(num_clients),
            ::benchmark::Counter::kDefaults);

        state.counters["successful_uploads"] = ::benchmark::Counter(
            static_cast<double>(successful_uploads.load()),
            ::benchmark::Counter::kDefaults);

        state.counters["aggregate_throughput_MB_s"] = ::benchmark::Counter(
            aggregate_throughput_mbps,
            ::benchmark::Counter::kDefaults);

        state.counters["per_client_throughput_MB_s"] = ::benchmark::Counter(
            aggregate_throughput_mbps / static_cast<double>(successful_uploads.load()),
            ::benchmark::Counter::kDefaults);

        // Cleanup
        state.PauseTiming();
        for (auto& client : clients) {
            if (client) {
                (void)client->disconnect();
            }
        }
        clients.clear();
        state.ResumeTiming();
    }

    state.SetBytesProcessed(static_cast<int64_t>(num_clients * file_size) *
                           static_cast<int64_t>(state.iterations()));
}

// Register scalability benchmarks

BENCHMARK(BM_Scalability_ConcurrentConnections)
    ->Arg(10)
    ->Arg(50)
    ->Arg(100)
    ->Unit(::benchmark::kMillisecond)
    ->UseManualTime()
    ->Iterations(3);

BENCHMARK(BM_Scalability_FileSize)
    ->Arg(static_cast<int64_t>(1 * sizes::MB))      // 1 MB
    ->Arg(static_cast<int64_t>(100 * sizes::MB))    // 100 MB
    ->Unit(::benchmark::kMillisecond)
    ->UseManualTime()
    ->Iterations(3);

// Note: 1GB and 10GB tests are disabled by default due to time constraints
// Uncomment for comprehensive testing:
// BENCHMARK(BM_Scalability_FileSize)
//     ->Arg(static_cast<int64_t>(1 * sizes::GB))     // 1 GB
//     ->Arg(static_cast<int64_t>(10 * sizes::GB))    // 10 GB
//     ->Unit(::benchmark::kSecond)
//     ->UseManualTime()
//     ->Iterations(1);

BENCHMARK(BM_Scalability_100Connections_Stability)
    ->Unit(::benchmark::kMillisecond)
    ->UseManualTime()
    ->Iterations(3);

BENCHMARK(BM_Scalability_MemoryStability)
    ->Arg(5)      // 5 cycles
    ->Arg(10)     // 10 cycles
    ->Arg(20)     // 20 cycles
    ->Unit(::benchmark::kSecond)
    ->UseManualTime()
    ->Iterations(1);

BENCHMARK(BM_Scalability_ConcurrentUploads)
    ->Arg(2)
    ->Arg(5)
    ->Arg(10)
    ->Unit(::benchmark::kMillisecond)
    ->UseManualTime()
    ->Iterations(3);

}  // namespace kcenon::file_transfer::benchmark
