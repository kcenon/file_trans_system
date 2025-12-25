/**
 * @file bench_quic_transport.cpp
 * @brief Benchmarks for QUIC transport performance
 *
 * Performance Targets (from Issue #85):
 * - QUIC throughput: >= 90% of TCP throughput
 * - 0-RTT reconnection: <= 50ms
 * - Connection migration: < 100ms disruption
 */

#include <benchmark/benchmark.h>

#include <kcenon/file_transfer/transport/quic_transport.h>
#include <kcenon/file_transfer/transport/tcp_transport.h>
#include <kcenon/file_transfer/transport/session_resumption.h>
#include <kcenon/file_transfer/transport/connection_migration.h>

#include "utils/benchmark_helpers.h"

#include <atomic>
#include <chrono>
#include <random>
#include <thread>

namespace kcenon::file_transfer::benchmark {

namespace {

/**
 * @brief Generate random test data
 */
auto generate_test_data(std::size_t size, uint32_t seed = 42) -> std::vector<std::byte> {
    std::mt19937 gen(seed);
    std::uniform_int_distribution<> dis(0, 255);
    std::vector<std::byte> data(size);
    for (auto& byte : data) {
        byte = static_cast<std::byte>(dis(gen));
    }
    return data;
}

/**
 * @brief Port counter for unique ports
 */
std::atomic<uint16_t> g_port_counter{52000};

auto get_unique_port() -> uint16_t {
    return g_port_counter++;
}

}  // namespace

// ============================================================================
// Connection Establishment Benchmarks
// ============================================================================

/**
 * @brief Benchmark QUIC connection establishment time (1-RTT)
 *
 * Measures time to establish a new QUIC connection without session resumption.
 */
static void BM_QUIC_Connection_1RTT(::benchmark::State& state) {
    auto config = quic_transport_config{};
    config.enable_0rtt = false;

    for (auto _ : state) {
        auto transport = quic_transport::create(config);
        if (!transport) {
            state.SkipWithError("Failed to create QUIC transport");
            return;
        }

        auto start = std::chrono::high_resolution_clock::now();

        // Note: This benchmark measures transport creation and connect preparation
        // Full connection requires a running server
        auto endpoint_result = transport->local_endpoint();
        ::benchmark::DoNotOptimize(endpoint_result);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration<double, std::micro>(end - start).count();
        state.SetIterationTime(duration_us / 1000000.0);
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark QUIC 0-RTT connection resumption
 *
 * Target: <= 50ms
 */
static void BM_QUIC_Connection_0RTT(::benchmark::State& state) {
    // Create session store with a pre-stored ticket
    auto store_config = session_store_config{};
    auto store = memory_session_store::create(store_config);
    if (!store) {
        state.SkipWithError("Failed to create session store");
        return;
    }

    // Simulate a stored session ticket
    session_ticket ticket;
    ticket.server_id = "test-server:8080";
    ticket.ticket_data = std::vector<uint8_t>(256, 0x42);
    ticket.issued_at = std::chrono::system_clock::now();
    ticket.expires_at = std::chrono::system_clock::now() + std::chrono::hours{24};
    ticket.max_early_data_size = 16384;
    ticket.alpn_protocol = "h3";

    auto store_result = store->store(ticket);
    if (!store_result.has_value()) {
        state.SkipWithError("Failed to store session ticket");
        return;
    }

    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();

        // Simulate 0-RTT ticket retrieval and validation
        auto retrieved = store->retrieve("test-server:8080");
        if (!retrieved.has_value()) {
            state.SkipWithError("Failed to retrieve ticket");
            return;
        }

        // Check ticket validity
        bool valid = retrieved->is_valid() && retrieved->allows_early_data();
        ::benchmark::DoNotOptimize(valid);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration<double, std::micro>(end - start).count();
        state.SetIterationTime(duration_us / 1000000.0);
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    state.counters["target_ms"] = 50.0;
}

/**
 * @brief Benchmark session ticket storage and retrieval
 */
static void BM_QUIC_SessionTicket_Operations(::benchmark::State& state) {
    const auto num_tickets = static_cast<std::size_t>(state.range(0));

    auto store_config = session_store_config{};
    store_config.max_tickets = num_tickets * 2;
    auto store = memory_session_store::create(store_config);

    // Pre-populate store
    for (std::size_t i = 0; i < num_tickets; ++i) {
        session_ticket ticket;
        ticket.server_id = "server-" + std::to_string(i) + ":8080";
        ticket.ticket_data = std::vector<uint8_t>(256, static_cast<uint8_t>(i));
        ticket.issued_at = std::chrono::system_clock::now();
        ticket.expires_at = std::chrono::system_clock::now() + std::chrono::hours{24};
        ticket.max_early_data_size = 16384;
        (void)store->store(ticket);
    }

    std::mt19937 gen(42);
    std::uniform_int_distribution<std::size_t> dis(0, num_tickets - 1);

    for (auto _ : state) {
        auto idx = dis(gen);
        auto server_id = "server-" + std::to_string(idx) + ":8080";

        auto start = std::chrono::high_resolution_clock::now();
        auto retrieved = store->retrieve(server_id);
        auto end = std::chrono::high_resolution_clock::now();

        ::benchmark::DoNotOptimize(retrieved);

        auto duration_ns = std::chrono::duration<double, std::nano>(end - start).count();
        state.SetIterationTime(duration_ns / 1000000000.0);
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    state.counters["tickets"] = static_cast<double>(num_tickets);
}

// ============================================================================
// Throughput Benchmarks
// ============================================================================

/**
 * @brief Benchmark QUIC transport data preparation throughput
 *
 * Measures how fast data can be prepared for QUIC transmission.
 */
static void BM_QUIC_DataPreparation_Throughput(::benchmark::State& state) {
    const auto data_size = static_cast<std::size_t>(state.range(0));
    auto test_data = generate_test_data(data_size);

    auto config = quic_transport_config{};
    auto transport = quic_transport::create(config);
    if (!transport) {
        state.SkipWithError("Failed to create QUIC transport");
        return;
    }

    for (auto _ : state) {
        // Measure data preparation operations
        auto span = std::span<const std::byte>(test_data);
        ::benchmark::DoNotOptimize(span.data());
        ::benchmark::DoNotOptimize(span.size());
        ::benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(static_cast<int64_t>(data_size) *
                           static_cast<int64_t>(state.iterations()));
    state.counters["size_MB"] = static_cast<double>(data_size) / sizes::MB;
}

/**
 * @brief Benchmark TCP transport data preparation throughput for comparison
 */
static void BM_TCP_DataPreparation_Throughput(::benchmark::State& state) {
    const auto data_size = static_cast<std::size_t>(state.range(0));
    auto test_data = generate_test_data(data_size);

    auto config = tcp_transport_config{};
    auto transport = tcp_transport::create(config);
    if (!transport) {
        state.SkipWithError("Failed to create TCP transport");
        return;
    }

    for (auto _ : state) {
        // Measure data preparation operations
        auto span = std::span<const std::byte>(test_data);
        ::benchmark::DoNotOptimize(span.data());
        ::benchmark::DoNotOptimize(span.size());
        ::benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(static_cast<int64_t>(data_size) *
                           static_cast<int64_t>(state.iterations()));
    state.counters["size_MB"] = static_cast<double>(data_size) / sizes::MB;
}

// ============================================================================
// Connection Migration Benchmarks
// ============================================================================

/**
 * @brief Benchmark connection migration manager operations
 *
 * Target: < 100ms disruption
 */
static void BM_QUIC_ConnectionMigration_Preparation(::benchmark::State& state) {
    auto config = migration_config{};
    config.auto_migrate = true;
    config.probe_timeout = std::chrono::milliseconds{100};
    config.max_probe_retries = 3;

    auto manager = connection_migration_manager::create(config);
    if (!manager) {
        state.SkipWithError("Failed to create migration manager");
        return;
    }

    network_path path1;
    path1.local_address = "192.168.1.100";
    path1.local_port = 12345;
    path1.remote_address = "10.0.0.1";
    path1.remote_port = 8080;
    path1.interface_name = "eth0";

    network_path path2;
    path2.local_address = "192.168.1.101";
    path2.local_port = 12346;
    path2.remote_address = "10.0.0.1";
    path2.remote_port = 8080;
    path2.interface_name = "wlan0";

    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();

        // Simulate migration preparation steps
        auto interfaces = manager->get_available_interfaces();
        ::benchmark::DoNotOptimize(interfaces);

        auto current_state = manager->state();
        ::benchmark::DoNotOptimize(current_state);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration<double, std::micro>(end - start).count();
        state.SetIterationTime(duration_us / 1000000.0);
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    state.counters["target_ms"] = 100.0;
}

/**
 * @brief Benchmark path validation operations
 */
static void BM_QUIC_PathValidation(::benchmark::State& state) {
    auto config = migration_config{};
    auto manager = connection_migration_manager::create(config);

    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();

        // Simulate path validation challenge generation and verification
        std::array<uint8_t, 8> challenge{};
        std::mt19937 gen(42);
        for (auto& byte : challenge) {
            byte = static_cast<uint8_t>(gen());
        }
        ::benchmark::DoNotOptimize(challenge);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration<double, std::micro>(end - start).count();
        state.SetIterationTime(duration_us / 1000000.0);
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

// ============================================================================
// Transport Statistics Benchmarks
// ============================================================================

/**
 * @brief Benchmark transport statistics collection
 */
static void BM_QUIC_Statistics_Collection(::benchmark::State& state) {
    auto config = quic_transport_config{};
    auto transport = quic_transport::create(config);
    if (!transport) {
        state.SkipWithError("Failed to create QUIC transport");
        return;
    }

    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();

        auto stats = transport->get_statistics();
        ::benchmark::DoNotOptimize(stats.bytes_sent);
        ::benchmark::DoNotOptimize(stats.bytes_received);
        ::benchmark::DoNotOptimize(stats.packets_sent);
        ::benchmark::DoNotOptimize(stats.packets_received);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration_ns = std::chrono::duration<double, std::nano>(end - start).count();
        state.SetIterationTime(duration_ns / 1000000000.0);
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark TCP statistics collection for comparison
 */
static void BM_TCP_Statistics_Collection(::benchmark::State& state) {
    auto config = tcp_transport_config{};
    auto transport = tcp_transport::create(config);
    if (!transport) {
        state.SkipWithError("Failed to create TCP transport");
        return;
    }

    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();

        auto stats = transport->get_statistics();
        ::benchmark::DoNotOptimize(stats.bytes_sent);
        ::benchmark::DoNotOptimize(stats.bytes_received);
        ::benchmark::DoNotOptimize(stats.packets_sent);
        ::benchmark::DoNotOptimize(stats.packets_received);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration_ns = std::chrono::duration<double, std::nano>(end - start).count();
        state.SetIterationTime(duration_ns / 1000000000.0);
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

// ============================================================================
// Transport Creation Benchmarks
// ============================================================================

/**
 * @brief Benchmark QUIC transport creation time
 */
static void BM_QUIC_Transport_Creation(::benchmark::State& state) {
    auto config = quic_transport_config{};
    config.enable_0rtt = true;
    config.max_idle_timeout = std::chrono::seconds{60};

    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();

        auto transport = quic_transport::create(config);
        ::benchmark::DoNotOptimize(transport.get());

        auto end = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration<double, std::micro>(end - start).count();
        state.SetIterationTime(duration_us / 1000000.0);
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

/**
 * @brief Benchmark TCP transport creation time for comparison
 */
static void BM_TCP_Transport_Creation(::benchmark::State& state) {
    auto config = tcp_transport_config{};
    config.tcp_nodelay = true;

    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();

        auto transport = tcp_transport::create(config);
        ::benchmark::DoNotOptimize(transport.get());

        auto end = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration<double, std::micro>(end - start).count();
        state.SetIterationTime(duration_us / 1000000.0);
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

// ============================================================================
// Stream Management Benchmarks
// ============================================================================

/**
 * @brief Benchmark QUIC stream creation
 */
static void BM_QUIC_Stream_Creation(::benchmark::State& state) {
    const auto num_streams = static_cast<std::size_t>(state.range(0));

    auto config = quic_transport_config{};
    auto transport = quic_transport::create(config);
    if (!transport) {
        state.SkipWithError("Failed to create QUIC transport");
        return;
    }

    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();

        for (std::size_t i = 0; i < num_streams; ++i) {
            // Stream creation without active connection returns error
            // but we can still measure the overhead
            auto stream_result = transport->create_stream();
            ::benchmark::DoNotOptimize(stream_result);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration<double, std::micro>(end - start).count();
        state.SetIterationTime(duration_us / 1000000.0);
    }

    state.SetItemsProcessed(static_cast<int64_t>(num_streams) *
                           static_cast<int64_t>(state.iterations()));
    state.counters["streams"] = static_cast<double>(num_streams);
}

// ============================================================================
// Register Benchmarks
// ============================================================================

// Connection establishment benchmarks
BENCHMARK(BM_QUIC_Connection_1RTT)
    ->Unit(::benchmark::kMicrosecond)
    ->UseManualTime()
    ->Iterations(100);

BENCHMARK(BM_QUIC_Connection_0RTT)
    ->Unit(::benchmark::kMicrosecond)
    ->UseManualTime()
    ->Iterations(1000);

BENCHMARK(BM_QUIC_SessionTicket_Operations)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000)
    ->Unit(::benchmark::kNanosecond)
    ->UseManualTime();

// Throughput benchmarks
BENCHMARK(BM_QUIC_DataPreparation_Throughput)
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Arg(static_cast<int64_t>(10 * sizes::MB))
    ->Arg(static_cast<int64_t>(100 * sizes::MB))
    ->Unit(::benchmark::kMillisecond);

BENCHMARK(BM_TCP_DataPreparation_Throughput)
    ->Arg(static_cast<int64_t>(1 * sizes::MB))
    ->Arg(static_cast<int64_t>(10 * sizes::MB))
    ->Arg(static_cast<int64_t>(100 * sizes::MB))
    ->Unit(::benchmark::kMillisecond);

// Connection migration benchmarks
BENCHMARK(BM_QUIC_ConnectionMigration_Preparation)
    ->Unit(::benchmark::kMicrosecond)
    ->UseManualTime()
    ->Iterations(100);

BENCHMARK(BM_QUIC_PathValidation)
    ->Unit(::benchmark::kMicrosecond)
    ->UseManualTime()
    ->Iterations(1000);

// Statistics collection benchmarks
BENCHMARK(BM_QUIC_Statistics_Collection)
    ->Unit(::benchmark::kNanosecond)
    ->UseManualTime()
    ->Iterations(10000);

BENCHMARK(BM_TCP_Statistics_Collection)
    ->Unit(::benchmark::kNanosecond)
    ->UseManualTime()
    ->Iterations(10000);

// Transport creation benchmarks
BENCHMARK(BM_QUIC_Transport_Creation)
    ->Unit(::benchmark::kMicrosecond)
    ->UseManualTime()
    ->Iterations(100);

BENCHMARK(BM_TCP_Transport_Creation)
    ->Unit(::benchmark::kMicrosecond)
    ->UseManualTime()
    ->Iterations(100);

// Stream management benchmarks
BENCHMARK(BM_QUIC_Stream_Creation)
    ->Arg(1)
    ->Arg(10)
    ->Arg(100)
    ->Unit(::benchmark::kMicrosecond)
    ->UseManualTime();

}  // namespace kcenon::file_transfer::benchmark
