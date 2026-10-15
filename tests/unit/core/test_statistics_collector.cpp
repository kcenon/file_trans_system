#include <gtest/gtest.h>
#include <kcenon/file_transfer/core/statistics_collector.h>
#include <thread>
#include <vector>

namespace kcenon::file_transfer {
using namespace std::chrono_literals;
TEST(StatisticsCollectorTest, CountersCompressionCompletionAndResetDescribeActualWork) {
    statistics_collector stats;
    EXPECT_FALSE(stats.is_active()); EXPECT_DOUBLE_EQ(stats.get_compression_ratio(), 1);
    EXPECT_DOUBLE_EQ(stats.get_completion_percentage(), 0); EXPECT_EQ(stats.get_eta(), 0ms);
    stats.start(1000); stats.record_bytes_transferred(200, 100); stats.record_chunk_processed(true);
    stats.record_bytes_transferred(300); stats.record_chunk_processed(false);
    stats.record_error(transfer_error_code::file_read_error);
    auto snapshot = stats.get_snapshot();
    EXPECT_EQ(snapshot.total_bytes, 1000); EXPECT_EQ(snapshot.bytes_transferred, 500);
    EXPECT_EQ(snapshot.bytes_on_wire, 400); EXPECT_EQ(snapshot.chunks_transferred, 2);
    EXPECT_EQ(snapshot.chunks_compressed, 1); EXPECT_EQ(snapshot.error_count, 1);
    EXPECT_DOUBLE_EQ(snapshot.compression_ratio, .8); EXPECT_DOUBLE_EQ(stats.get_completion_percentage(), 50);
    EXPECT_EQ(stats.get_bytes_transferred(), 500); EXPECT_EQ(stats.get_bytes_on_wire(), 400);
    EXPECT_EQ(stats.get_chunks_transferred(), 2); EXPECT_EQ(stats.get_error_count(), 1);
    stats.set_total_bytes(500); EXPECT_DOUBLE_EQ(stats.get_completion_percentage(), 100);
    stats.stop(); EXPECT_FALSE(stats.is_active()); EXPECT_EQ(stats.get_elapsed(), 0ms);
    EXPECT_DOUBLE_EQ(stats.get_average_rate(), 0);
    stats.reset(); snapshot = stats.get_snapshot();
    EXPECT_EQ(snapshot.bytes_transferred, 0); EXPECT_EQ(snapshot.bytes_on_wire, 0);
    EXPECT_EQ(snapshot.total_bytes, 0); EXPECT_EQ(snapshot.error_count, 0);
    EXPECT_EQ(snapshot.chunks_transferred, 0); EXPECT_EQ(snapshot.chunks_compressed, 0);
}

TEST(StatisticsCollectorTest, SampleWindowAndEtaRemainFiniteAndMovePreservesMeasurements) {
    statistics_collector::config config; config.rate_window_size = 2;
    config.rate_sample_interval = 0ms; config.min_eta_update_interval = 0ms;
    statistics_collector stats(config); stats.start(400);
    EXPECT_EQ(stats.get_eta(), 0ms);
    for (int i = 0; i < 3; ++i) { std::this_thread::sleep_for(2ms); stats.record_bytes_transferred(100); }
    EXPECT_GT(stats.get_transfer_rate(), 0); EXPECT_GT(stats.get_average_rate(), 0);
    EXPECT_GE(stats.get_eta(), 0ms); EXPECT_GT(stats.get_elapsed(), 0ms);
    statistics_collector moved(std::move(stats)); EXPECT_EQ(moved.get_bytes_transferred(), 300);
    statistics_collector assigned; assigned = std::move(moved);
    assigned.record_bytes_transferred(100); EXPECT_EQ(assigned.get_eta(), 0ms);
    EXPECT_DOUBLE_EQ(assigned.get_completion_percentage(), 100);
}

TEST(StatisticsCollectorTest, ConcurrentProducersAndObserversPreserveExactCounters) {
    statistics_collector::config config; config.rate_sample_interval = 0ms;
    config.min_eta_update_interval = 0ms;
    statistics_collector stats(config); stats.start(80000);
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) workers.emplace_back([&] {
        for (int j = 0; j < 1000; ++j) {
            stats.record_bytes_transferred(10, 5); stats.record_chunk_processed(true);
            stats.record_error(transfer_error_code::file_read_error);
            (void)stats.get_snapshot();
        }
    });
    for (auto& worker : workers) worker.join();
    auto snapshot = stats.get_snapshot();
    EXPECT_EQ(snapshot.bytes_transferred, 80000); EXPECT_EQ(snapshot.bytes_on_wire, 40000);
    EXPECT_EQ(snapshot.chunks_transferred, 8000); EXPECT_EQ(snapshot.chunks_compressed, 8000);
    EXPECT_EQ(snapshot.error_count, 8000); EXPECT_DOUBLE_EQ(stats.get_completion_percentage(), 100);
}
} // namespace kcenon::file_transfer
