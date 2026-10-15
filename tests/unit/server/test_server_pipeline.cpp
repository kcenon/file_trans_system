/**
 * @file test_server_pipeline.cpp
 * @brief Unit tests for server_pipeline
 */

#include "kcenon/file_transfer/server/server_pipeline.h"
#include "kcenon/file_transfer/core/checksum.h"
#include "kcenon/file_transfer/core/compression_engine.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <future>
#include <condition_variable>

namespace kcenon::file_transfer {
namespace {

class ServerPipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "pipeline_test";
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    auto create_test_file(const std::string& name, std::size_t size)
        -> std::filesystem::path {
        auto path = test_dir_ / name;
        std::ofstream file(path, std::ios::binary);

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);

        std::vector<char> data(size);
        for (auto& byte : data) {
            byte = static_cast<char>(dis(gen));
        }
        file.write(data.data(), static_cast<std::streamsize>(size));
        return path;
    }

    auto create_pipeline_chunk(const transfer_id& id, uint64_t index,
                               const std::vector<std::byte>& data,
                               bool compressed = false) -> pipeline_chunk {
        pipeline_chunk chunk;
        chunk.id = id;
        chunk.chunk_index = index;
        chunk.data = data;
        chunk.checksum = checksum::crc32(std::span<const std::byte>(data));
        chunk.is_compressed = compressed;
        chunk.original_size = data.size();
        return chunk;
    }

    std::filesystem::path test_dir_;
};

// pipeline_config tests

TEST_F(ServerPipelineTest, ConfigAutoDetect) {
    auto config = pipeline_config::auto_detect();

    EXPECT_GT(config.io_workers, 0);
    EXPECT_GT(config.compression_workers, 0);
    EXPECT_GT(config.network_workers, 0);
    EXPECT_GT(config.queue_size, 0);
    EXPECT_TRUE(config.is_valid());
}

TEST_F(ServerPipelineTest, ConfigValidation) {
    pipeline_config config;
    EXPECT_TRUE(config.is_valid());

    config.io_workers = 0;
    EXPECT_FALSE(config.is_valid());

    config = pipeline_config{};
    config.compression_workers = 0;
    EXPECT_FALSE(config.is_valid());

    config = pipeline_config{};
    config.queue_size = 0;
    EXPECT_FALSE(config.is_valid());
}

// pipeline_stage tests

TEST_F(ServerPipelineTest, PipelineStageToString) {
    EXPECT_STREQ(to_string(pipeline_stage::network_recv), "network_recv");
    EXPECT_STREQ(to_string(pipeline_stage::decompress), "decompress");
    EXPECT_STREQ(to_string(pipeline_stage::chunk_verify), "chunk_verify");
    EXPECT_STREQ(to_string(pipeline_stage::file_write), "file_write");
    EXPECT_STREQ(to_string(pipeline_stage::network_send), "network_send");
    EXPECT_STREQ(to_string(pipeline_stage::file_read), "file_read");
    EXPECT_STREQ(to_string(pipeline_stage::compress), "compress");
}

// server_pipeline creation tests

TEST_F(ServerPipelineTest, CreateWithDefaultConfig) {
    auto result = server_pipeline::create();
    EXPECT_TRUE(result.has_value());
}

TEST_F(ServerPipelineTest, CreateWithCustomConfig) {
    pipeline_config config;
    config.io_workers = 1;
    config.compression_workers = 2;
    config.network_workers = 1;
    config.queue_size = 32;

    auto result = server_pipeline::create(config);
    EXPECT_TRUE(result.has_value());

    auto& pipeline = result.value();
    EXPECT_EQ(pipeline.config().io_workers, 1);
    EXPECT_EQ(pipeline.config().compression_workers, 2);
    EXPECT_EQ(pipeline.config().queue_size, 32);
}

TEST_F(ServerPipelineTest, CreateWithInvalidConfig) {
    pipeline_config config;
    config.io_workers = 0;

    auto result = server_pipeline::create(config);
    EXPECT_FALSE(result.has_value());
}

// pipeline lifecycle tests

TEST_F(ServerPipelineTest, StartAndStop) {
    auto pipeline_result = server_pipeline::create();
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    EXPECT_FALSE(pipeline.is_running());

    auto start_result = pipeline.start();
    EXPECT_TRUE(start_result.has_value());
    EXPECT_TRUE(pipeline.is_running());

    auto stop_result = pipeline.stop();
    EXPECT_TRUE(stop_result.has_value());
    EXPECT_FALSE(pipeline.is_running());
}

TEST_F(ServerPipelineTest, StartTwiceFails) {
    auto pipeline_result = server_pipeline::create();
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    EXPECT_TRUE(pipeline.start().has_value());
    EXPECT_FALSE(pipeline.start().has_value());
}

TEST_F(ServerPipelineTest, StopWithoutStartFails) {
    auto pipeline_result = server_pipeline::create();
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    EXPECT_FALSE(pipeline.stop().has_value());
}

// Upload pipeline tests

TEST_F(ServerPipelineTest, SubmitUploadChunkWhenNotRunning) {
    auto pipeline_result = server_pipeline::create();
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    auto id = transfer_id::generate();
    std::vector<std::byte> data(100, std::byte{0x42});
    auto chunk = create_pipeline_chunk(id, 0, data);

    auto result = pipeline.submit_upload_chunk(std::move(chunk));
    EXPECT_FALSE(result.has_value());
}

TEST_F(ServerPipelineTest, UploadChunkProcessing) {
    pipeline_config config;
    config.io_workers = 1;
    config.compression_workers = 1;
    config.network_workers = 1;
    config.queue_size = 16;

    auto pipeline_result = server_pipeline::create(config);
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    std::atomic<int> upload_complete_count{0};
    pipeline.on_upload_complete([&](const transfer_id&, uint64_t) {
        upload_complete_count++;
    });

    ASSERT_TRUE(pipeline.start().has_value());

    // Submit a chunk
    auto id = transfer_id::generate();
    std::vector<std::byte> data(1024);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::byte>(i & 0xFF);
    }
    auto chunk = create_pipeline_chunk(id, 0, data, false);

    auto result = pipeline.submit_upload_chunk(std::move(chunk));
    EXPECT_TRUE(result.has_value());

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_GE(upload_complete_count.load(), 1);

    (void)pipeline.stop();
}

TEST_F(ServerPipelineTest, TrySubmitUploadChunk) {
    auto pipeline_result = server_pipeline::create();
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    ASSERT_TRUE(pipeline.start().has_value());

    auto id = transfer_id::generate();
    std::vector<std::byte> data(100, std::byte{0x42});
    auto chunk = create_pipeline_chunk(id, 0, data);

    bool submitted = pipeline.try_submit_upload_chunk(std::move(chunk));
    EXPECT_TRUE(submitted);

    (void)pipeline.stop();
}

// Download pipeline tests

TEST_F(ServerPipelineTest, SubmitDownloadRequestWhenNotRunning) {
    auto pipeline_result = server_pipeline::create();
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    auto id = transfer_id::generate();
    auto result = pipeline.submit_download_request(
        id, 0, test_dir_ / "nonexistent.txt", 0, 1024);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ServerPipelineTest, DownloadChunkProcessing) {
    pipeline_config config;
    config.io_workers = 1;
    config.compression_workers = 1;
    config.network_workers = 1;
    config.queue_size = 16;

    auto pipeline_result = server_pipeline::create(config);
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    // Create test file
    auto file_path = create_test_file("download_test.bin", 4096);

    std::atomic<int> download_ready_count{0};
    pipeline.on_download_ready([&](const pipeline_chunk&) {
        download_ready_count++;
    });

    ASSERT_TRUE(pipeline.start().has_value());

    // Submit download request
    auto id = transfer_id::generate();
    auto result = pipeline.submit_download_request(id, 0, file_path, 0, 1024);
    EXPECT_TRUE(result.has_value());

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_GE(download_ready_count.load(), 1);

    (void)pipeline.stop();
}

// Statistics tests

TEST_F(ServerPipelineTest, StatsInitialState) {
    auto pipeline_result = server_pipeline::create();
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    auto& stats = pipeline.stats();
    EXPECT_EQ(stats.chunks_processed, 0);
    EXPECT_EQ(stats.bytes_processed, 0);
    EXPECT_EQ(stats.compression_saved_bytes, 0);
    EXPECT_EQ(stats.stalls_detected, 0);
    EXPECT_EQ(stats.backpressure_events, 0);
}

TEST_F(ServerPipelineTest, ResetStats) {
    auto pipeline_result = server_pipeline::create();
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    // Manually increment (would normally be done by pipeline)
    const_cast<pipeline_stats&>(pipeline.stats()).chunks_processed = 10;

    pipeline.reset_stats();

    EXPECT_EQ(pipeline.stats().chunks_processed, 0);
}

// Queue size monitoring tests

TEST_F(ServerPipelineTest, QueueSizes) {
    auto pipeline_result = server_pipeline::create();
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    auto sizes = pipeline.queue_sizes();
    EXPECT_GT(sizes.size(), 0);

    for (const auto& [stage, size] : sizes) {
        EXPECT_EQ(size, 0);  // Initially empty
    }
}

// Callback tests

TEST_F(ServerPipelineTest, ErrorCallback) {
    pipeline_config config;
    config.io_workers = 1;
    config.compression_workers = 1;
    config.network_workers = 1;
    config.queue_size = 16;

    auto pipeline_result = server_pipeline::create(config);
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    std::atomic<int> error_count{0};
    pipeline.on_error([&](pipeline_stage, const std::string&) {
        error_count++;
    });

    ASSERT_TRUE(pipeline.start().has_value());

    // Submit chunk with bad checksum
    auto id = transfer_id::generate();
    std::vector<std::byte> data(100, std::byte{0x42});
    pipeline_chunk chunk;
    chunk.id = id;
    chunk.chunk_index = 0;
    chunk.data = data;
    chunk.checksum = 0xDEADBEEF;  // Invalid checksum
    chunk.is_compressed = false;
    chunk.original_size = data.size();

    pipeline.submit_upload_chunk(std::move(chunk));

    // Wait for error
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_GE(error_count.load(), 1);

    (void)pipeline.stop();
}

TEST_F(ServerPipelineTest, StageCompleteCallback) {
    pipeline_config config;
    config.io_workers = 1;
    config.compression_workers = 1;
    config.network_workers = 1;
    config.queue_size = 16;

    auto pipeline_result = server_pipeline::create(config);
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    std::atomic<int> stage_complete_count{0};
    pipeline.on_stage_complete([&](pipeline_stage, const pipeline_chunk&) {
        stage_complete_count++;
    });

    ASSERT_TRUE(pipeline.start().has_value());

    auto id = transfer_id::generate();
    std::vector<std::byte> data(100, std::byte{0x42});
    auto chunk = create_pipeline_chunk(id, 0, data);

    pipeline.submit_upload_chunk(std::move(chunk));

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Should have multiple stage completions
    EXPECT_GE(stage_complete_count.load(), 2);

    (void)pipeline.stop();
}

// Backpressure tests

struct consumer_gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered{}, released{};
    void block() {
        std::unique_lock lock(mutex); entered = true; cv.notify_all();
        cv.wait(lock, [&] { return released; });
    }
    bool wait() { std::unique_lock lock(mutex); return cv.wait_for(lock, std::chrono::seconds(3), [&] { return entered; }); }
    void release() { std::lock_guard lock(mutex); released = true; cv.notify_all(); }
    ~consumer_gate() { release(); }
};

TEST_F(ServerPipelineTest, BackpressureWithSmallQueue) {
    using namespace std::chrono_literals;
    pipeline_config config; config.worker_threads = 1; config.queue_size = 2;
    auto built = server_pipeline::create(config); ASSERT_TRUE(built); auto& pipeline = built.value();
    consumer_gate gate;
    pipeline.on_stage_complete([&](auto stage, const auto& chunk) {
        if (stage == pipeline_stage::decompress && chunk.chunk_index == 0) gate.block();
    });
    ASSERT_TRUE(pipeline.start());
    auto id = transfer_id::generate(); std::vector<std::byte> bytes(100, std::byte{0x42});
    ASSERT_TRUE(pipeline.submit_upload_chunk(create_pipeline_chunk(id, 0, bytes)));
    EXPECT_TRUE(gate.wait());
    EXPECT_TRUE(pipeline.try_submit_upload_chunk(create_pipeline_chunk(id, 1, bytes)));
    EXPECT_FALSE(pipeline.try_submit_upload_chunk(create_pipeline_chunk(id, 2, bytes)));
    EXPECT_EQ(pipeline.stats().backpressure_events, 1);
    EXPECT_EQ(pipeline.stats().admitted_chunks, 2);
    EXPECT_EQ(pipeline.stats().reserved_bytes, 2 * (4 * bytes.size() + 4096));
    for (const auto& [stage, count] : pipeline.queue_sizes())
        EXPECT_EQ(count, stage == pipeline_stage::decompress ? 1 : 0);
    auto timed = pipeline.submit_upload_chunk(create_pipeline_chunk(id, 2, bytes), 15ms);
    EXPECT_FALSE(timed); EXPECT_EQ(timed.error().code, error_code::transfer_timeout);
    auto blocked = std::async(std::launch::async, [&] {
        return pipeline.submit_upload_chunk(create_pipeline_chunk(id, 2, bytes));
    });
    EXPECT_EQ(blocked.wait_for(20ms), std::future_status::timeout);
    gate.release();
    EXPECT_TRUE(blocked.get()); ASSERT_TRUE(pipeline.stop(true));
    EXPECT_EQ(pipeline.stats().chunks_processed, 3); EXPECT_EQ(pipeline.stats().backpressure_events, 3);
    EXPECT_EQ(pipeline.stats().peak_admitted_chunks, 2);
    EXPECT_EQ(pipeline.stats().admitted_chunks, 0); EXPECT_EQ(pipeline.stats().reserved_bytes, 0);
    for (const auto& [stage, count] : pipeline.queue_sizes()) EXPECT_EQ(count, 0);
}

TEST_F(ServerPipelineTest, CancellationWakesBlockedProducerWithoutReleasingOthersCapacity) {
    using namespace std::chrono_literals;
    pipeline_config config; config.worker_threads = 1; config.queue_size = 1;
    auto built = server_pipeline::create(config); ASSERT_TRUE(built); auto& pipeline = built.value();
    consumer_gate gate;
    pipeline.on_stage_complete([&](auto stage, const auto&) { if (stage == pipeline_stage::decompress) gate.block(); });
    ASSERT_TRUE(pipeline.start());
    auto id = transfer_id::generate(); std::vector<std::byte> bytes(100);
    ASSERT_TRUE(pipeline.submit_upload_chunk(create_pipeline_chunk(id, 0, bytes))); EXPECT_TRUE(gate.wait());
    std::stop_source cancellation;
    auto blocked = std::async(std::launch::async, [&] {
        return pipeline.submit_upload_chunk(create_pipeline_chunk(id, 1, bytes), std::chrono::milliseconds::max(), cancellation.get_token());
    });
    EXPECT_EQ(blocked.wait_for(20ms), std::future_status::timeout); cancellation.request_stop();
    EXPECT_EQ(blocked.wait_for(1s), std::future_status::ready); EXPECT_FALSE(blocked.get());
    EXPECT_EQ(pipeline.stats().admitted_chunks, 1); EXPECT_EQ(pipeline.stats().backpressure_events, 1);
    gate.release(); EXPECT_TRUE(pipeline.stop()); EXPECT_EQ(pipeline.stats().admitted_chunks, 0);
}

TEST_F(ServerPipelineTest, SaturatedGracefulAndImmediateShutdownWakeProducersAndReleaseCapacity) {
    using namespace std::chrono_literals;
    for (bool graceful : {false, true}) {
        pipeline_config config; config.worker_threads = 1; config.queue_size = 2;
        auto built = server_pipeline::create(config); ASSERT_TRUE(built); auto& pipeline = built.value();
        consumer_gate gate;
        pipeline.on_stage_complete([&](auto stage, const auto&) { if (stage == pipeline_stage::decompress) gate.block(); });
        ASSERT_TRUE(pipeline.start());
        auto id = transfer_id::generate(); std::vector<std::byte> bytes(100);
        EXPECT_TRUE(pipeline.submit_upload_chunk(create_pipeline_chunk(id, 0, bytes))); EXPECT_TRUE(gate.wait());
        EXPECT_TRUE(pipeline.submit_upload_chunk(create_pipeline_chunk(id, 1, bytes)));
        auto blocked = std::async(std::launch::async, [&] { return pipeline.submit_upload_chunk(create_pipeline_chunk(id, 2, bytes)); });
        EXPECT_EQ(blocked.wait_for(20ms), std::future_status::timeout);
        auto stopping = std::async(std::launch::async, [&] { return pipeline.stop(graceful); });
        EXPECT_EQ(blocked.wait_for(1s), std::future_status::ready); EXPECT_FALSE(blocked.get());
        gate.release(); EXPECT_EQ(stopping.wait_for(2s), std::future_status::ready); EXPECT_TRUE(stopping.get());
        EXPECT_EQ(pipeline.stats().admitted_chunks, 0); EXPECT_EQ(pipeline.stats().reserved_bytes, 0);
        if (graceful) EXPECT_EQ(pipeline.stats().chunks_processed, 2);
        for (const auto& [stage, count] : pipeline.queue_sizes()) EXPECT_EQ(count, 0);
    }
}

TEST_F(ServerPipelineTest, SingleWorkerStreamsSixtyFourMiBAgainstHalfMiBBufferBudget) {
    constexpr std::size_t chunk_size = 65536;
    constexpr std::size_t charge = 4 * chunk_size + 4096;
    pipeline_config config; config.worker_threads = 1; config.queue_size = 4;
    config.max_buffered_bytes = 2 * charge; config.max_memory_per_transfer = 2 * charge;
    auto built = server_pipeline::create(config); ASSERT_TRUE(built); auto& pipeline = built.value();
    auto source = create_test_file("bounded-source", 1024 * 1024);
    std::atomic<unsigned> completed{}; std::atomic<uint64_t> bytes{};
    pipeline.on_download_ready([&](const auto& chunk) {
        EXPECT_LE(pipeline.stats().reserved_bytes, config.max_buffered_bytes);
        EXPECT_LE(pipeline.stats().admitted_chunks, 2);
        if (chunk.is_compressed) {
            compression_engine engine;
            auto decoded = engine.decompress(chunk.data, chunk.original_size); ASSERT_TRUE(decoded);
            EXPECT_EQ(checksum::crc32(decoded.value()), chunk.checksum);
        } else EXPECT_EQ(checksum::crc32(chunk.data), chunk.checksum);
        bytes += chunk.original_size; ++completed;
    });
    ASSERT_TRUE(pipeline.start()); auto id = transfer_id::generate();
    for (unsigned i = 0; i < 1024; ++i)
        ASSERT_TRUE(pipeline.submit_download_request(id, i, source, (i % 16) * chunk_size, chunk_size));
    ASSERT_TRUE(pipeline.stop());
    EXPECT_EQ(completed, 1024); EXPECT_EQ(bytes, 64 * 1024 * 1024);
    EXPECT_GT(pipeline.stats().backpressure_events, 0);
    EXPECT_LE(pipeline.stats().peak_reserved_bytes, config.max_buffered_bytes);
    EXPECT_EQ(pipeline.stats().reserved_bytes, 0);
}

TEST_F(ServerPipelineTest, PerTransferBudgetAndOversizedChunksAreEnforcedBeforeAllocation) {
    pipeline_config config; config.worker_threads = 1; config.queue_size = 10;
    config.max_memory_per_transfer = 4496; config.max_buffered_bytes = 8992;
    auto built = server_pipeline::create(config); ASSERT_TRUE(built); auto& pipeline = built.value();
    consumer_gate gate;
    pipeline.on_stage_complete([&](auto stage, const auto&) { if (stage == pipeline_stage::decompress) gate.block(); });
    ASSERT_TRUE(pipeline.start()); auto first = transfer_id::generate(); std::vector<std::byte> bytes(100);
    EXPECT_TRUE(pipeline.submit_upload_chunk(create_pipeline_chunk(first, 0, bytes))); EXPECT_TRUE(gate.wait());
    EXPECT_FALSE(pipeline.try_submit_upload_chunk(create_pipeline_chunk(first, 1, bytes)));
    EXPECT_TRUE(pipeline.try_submit_upload_chunk(create_pipeline_chunk(transfer_id::generate(), 0, bytes)));
    EXPECT_FALSE(pipeline.submit_download_request(first, 1, test_dir_ / "irrelevant", 0, std::numeric_limits<std::size_t>::max()));
    gate.release(); EXPECT_TRUE(pipeline.stop()); EXPECT_EQ(pipeline.stats().reserved_bytes, 0);
}

TEST_F(ServerPipelineTest, ErrorsAndThrowingCallbacksReleaseCapacityAndCallbackAdmissionCannotDeadlock) {
    pipeline_config config; config.worker_threads = 1; config.queue_size = 1;
    auto built = server_pipeline::create(config); ASSERT_TRUE(built); auto& pipeline = built.value();
    std::atomic<unsigned> errors{}, callbacks{};
    auto id = transfer_id::generate(); std::vector<std::byte> bytes(100);
    pipeline.on_error([&](auto, const auto&) { ++errors; });
    pipeline.on_stage_complete([&](auto stage, const auto& chunk) {
        if (stage == pipeline_stage::decompress && chunk.chunk_index == 2) {
            EXPECT_FALSE(pipeline.submit_upload_chunk(create_pipeline_chunk(id, 3, bytes)));
            EXPECT_FALSE(pipeline.try_submit_upload_chunk(create_pipeline_chunk(id, 3, bytes)));
            EXPECT_FALSE(pipeline.stop());
            ++callbacks; throw std::runtime_error("Intentional callback exception");
        }
    });
    ASSERT_TRUE(pipeline.start());
    auto bad = create_pipeline_chunk(id, 0, bytes); ++bad.checksum;
    ASSERT_TRUE(pipeline.submit_upload_chunk(std::move(bad)));
    ASSERT_TRUE(pipeline.submit_download_request(id, 1, test_dir_ / "missing", 0, 100));
    ASSERT_TRUE(pipeline.submit_upload_chunk(create_pipeline_chunk(id, 2, bytes)));
    ASSERT_TRUE(pipeline.submit_upload_chunk(create_pipeline_chunk(id, 4, bytes)));
    ASSERT_TRUE(pipeline.stop());
    EXPECT_EQ(errors, 2); EXPECT_EQ(callbacks, 1); EXPECT_EQ(pipeline.stats().chunks_processed, 1);
    EXPECT_EQ(pipeline.stats().admitted_chunks, 0); EXPECT_EQ(pipeline.stats().reserved_bytes, 0);
}

// Move semantics tests

TEST_F(ServerPipelineTest, MoveConstruction) {
    auto pipeline_result = server_pipeline::create();
    ASSERT_TRUE(pipeline_result.has_value());

    server_pipeline moved_pipeline = std::move(pipeline_result.value());
    EXPECT_FALSE(moved_pipeline.is_running());

    EXPECT_TRUE(moved_pipeline.start().has_value());
    EXPECT_TRUE(moved_pipeline.is_running());

    (void)moved_pipeline.stop();
}

TEST_F(ServerPipelineTest, MoveAssignment) {
    auto pipeline1_result = server_pipeline::create();
    auto pipeline2_result = server_pipeline::create();
    ASSERT_TRUE(pipeline1_result.has_value());
    ASSERT_TRUE(pipeline2_result.has_value());

    auto& pipeline1 = pipeline1_result.value();
    auto& pipeline2 = pipeline2_result.value();

    ASSERT_TRUE(pipeline1.start().has_value());
    pipeline2 = std::move(pipeline1);

    // After move, pipeline2 should be running
    EXPECT_TRUE(pipeline2.is_running());

    (void)pipeline2.stop();
}

// stage_result tests

TEST_F(ServerPipelineTest, StageResultOk) {
    auto id = transfer_id::generate();
    std::vector<std::byte> data(100, std::byte{0x42});

    pipeline_chunk chunk;
    chunk.id = id;
    chunk.chunk_index = 0;
    chunk.data = data;

    auto result = stage_result::ok(std::move(chunk));
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.error_message.empty());
    EXPECT_EQ(result.chunk.chunk_index, 0);
}

TEST_F(ServerPipelineTest, StageResultFail) {
    auto result = stage_result::fail("Test error");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_message, "Test error");
}

// pipeline_chunk constructor from chunk tests

TEST_F(ServerPipelineTest, PipelineChunkFromChunk) {
    chunk c;
    c.header.id = transfer_id::generate();
    c.header.chunk_index = 42;
    c.header.checksum = 0x12345678;
    c.header.original_size = 100;
    c.header.compressed_size = 100;
    c.header.flags = chunk_flags::none;
    c.data = std::vector<std::byte>(100, std::byte{0x55});

    pipeline_chunk pc(c);

    EXPECT_EQ(pc.id, c.header.id);
    EXPECT_EQ(pc.chunk_index, 42);
    EXPECT_EQ(pc.checksum, 0x12345678);
    EXPECT_EQ(pc.original_size, 100);
    EXPECT_FALSE(pc.is_compressed);
    EXPECT_EQ(pc.data.size(), 100);
}

TEST_F(ServerPipelineTest, PipelineChunkFromCompressedChunk) {
    chunk c;
    c.header.id = transfer_id::generate();
    c.header.chunk_index = 0;
    c.header.flags = chunk_flags::compressed;
    c.header.original_size = 200;
    c.header.compressed_size = 100;
    c.data = std::vector<std::byte>(100, std::byte{0xAA});

    pipeline_chunk pc(c);

    EXPECT_TRUE(pc.is_compressed);
    EXPECT_EQ(pc.original_size, 200);
}

// Encryption pipeline tests

TEST_F(ServerPipelineTest, PipelineStageToStringWithEncryption) {
    EXPECT_STREQ(to_string(pipeline_stage::decrypt), "decrypt");
    EXPECT_STREQ(to_string(pipeline_stage::encrypt), "encrypt");
}

TEST_F(ServerPipelineTest, EncryptionConfigDefaults) {
    pipeline_config config;
    EXPECT_FALSE(config.enable_encryption);
    EXPECT_EQ(config.encryption_workers, 2);
}

TEST_F(ServerPipelineTest, CreateWithEncryptionEnabled) {
    pipeline_config config;
    config.io_workers = 1;
    config.compression_workers = 1;
    config.network_workers = 1;
    config.encryption_workers = 1;
    config.enable_encryption = true;
    config.queue_size = 16;

    auto result = server_pipeline::create(config);
    EXPECT_TRUE(result.has_value());

    auto& pipeline = result.value();
    EXPECT_TRUE(pipeline.config().enable_encryption);
    EXPECT_EQ(pipeline.config().encryption_workers, 1);
}

TEST_F(ServerPipelineTest, EncryptionStatsInitialState) {
    pipeline_config config;
    config.enable_encryption = true;

    auto pipeline_result = server_pipeline::create(config);
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    auto& stats = pipeline.stats();
    EXPECT_EQ(stats.chunks_encrypted, 0);
    EXPECT_EQ(stats.chunks_decrypted, 0);
    EXPECT_EQ(stats.encryption_bytes, 0);
}

TEST_F(ServerPipelineTest, EncryptionStatsReset) {
    pipeline_config config;
    config.enable_encryption = true;

    auto pipeline_result = server_pipeline::create(config);
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    // Manually set stats
    const_cast<pipeline_stats&>(pipeline.stats()).chunks_encrypted = 10;
    const_cast<pipeline_stats&>(pipeline.stats()).chunks_decrypted = 5;
    const_cast<pipeline_stats&>(pipeline.stats()).encryption_bytes = 1000;

    pipeline.reset_stats();

    EXPECT_EQ(pipeline.stats().chunks_encrypted, 0);
    EXPECT_EQ(pipeline.stats().chunks_decrypted, 0);
    EXPECT_EQ(pipeline.stats().encryption_bytes, 0);
}

TEST_F(ServerPipelineTest, QueueSizesIncludeEncryption) {
    pipeline_config config;
    config.enable_encryption = true;

    auto pipeline_result = server_pipeline::create(config);
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    auto sizes = pipeline.queue_sizes();

    // Should include decrypt and encrypt queues
    bool has_decrypt = false;
    bool has_encrypt = false;
    for (const auto& [stage, size] : sizes) {
        if (stage == pipeline_stage::decrypt) has_decrypt = true;
        if (stage == pipeline_stage::encrypt) has_encrypt = true;
    }

    EXPECT_TRUE(has_decrypt);
    EXPECT_TRUE(has_encrypt);
}

TEST_F(ServerPipelineTest, PipelineChunkEncryptionFields) {
    pipeline_chunk chunk;
    chunk.id = transfer_id::generate();
    chunk.chunk_index = 0;
    chunk.data = std::vector<std::byte>(100, std::byte{0x42});

    // Default values
    EXPECT_FALSE(chunk.is_encrypted);
    EXPECT_EQ(chunk.enc_metadata, nullptr);

    // Set encryption
    chunk.is_encrypted = true;
    EXPECT_TRUE(chunk.is_encrypted);
}

TEST_F(ServerPipelineTest, PipelineChunkCopyWithEncryption) {
    pipeline_chunk original;
    original.id = transfer_id::generate();
    original.chunk_index = 42;
    original.data = std::vector<std::byte>(100, std::byte{0x42});
    original.checksum = 0x12345678;
    original.is_compressed = false;
    original.original_size = 100;
    original.is_encrypted = true;

    // Copy constructor
    pipeline_chunk copy(original);

    EXPECT_EQ(copy.id, original.id);
    EXPECT_EQ(copy.chunk_index, original.chunk_index);
    EXPECT_EQ(copy.data.size(), original.data.size());
    EXPECT_EQ(copy.checksum, original.checksum);
    EXPECT_EQ(copy.is_encrypted, original.is_encrypted);
}

TEST_F(ServerPipelineTest, PipelineChunkMoveConstruction) {
    pipeline_chunk original;
    original.id = transfer_id::generate();
    original.chunk_index = 42;
    original.data = std::vector<std::byte>(100, std::byte{0x42});
    original.is_encrypted = true;

    auto orig_id = original.id;
    pipeline_chunk moved(std::move(original));

    EXPECT_EQ(moved.id, orig_id);
    EXPECT_EQ(moved.chunk_index, 42);
    EXPECT_TRUE(moved.is_encrypted);
}

}  // namespace
}  // namespace kcenon::file_transfer

TEST(PipelineEncryptionPolicyTest, MissingTransferKeyFailsWithoutSuccessfulStage) {
    using namespace kcenon::file_transfer;
    pipeline_config config; config.enable_encryption = true;
    config.encryption_provider = [](const transfer_id&) { return std::shared_ptr<encryption_interface>{}; };
    auto built = server_pipeline::create(config); ASSERT_TRUE(built);
    auto& pipeline = built.value();
#ifndef FILE_TRANS_ENABLE_ENCRYPTION
    EXPECT_FALSE(pipeline.start());
#else
    std::mutex mutex; std::condition_variable cv; bool failed=false; unsigned successful=0;
    pipeline.on_error([&](auto, const auto&) { std::lock_guard lock(mutex); failed=true; cv.notify_all(); });
    pipeline.on_stage_complete([&](auto stage, const auto&) { if (stage==pipeline_stage::encrypt || stage==pipeline_stage::decrypt) ++successful; });
    ASSERT_TRUE(pipeline.start());
    pipeline_chunk data; data.id=transfer_id::generate(); data.chunk_index=0; data.checksum=0;
    data.original_size=3; data.is_encrypted=true; data.is_compressed=false; data.data.resize(3);
    ASSERT_TRUE(pipeline.submit_upload_chunk(std::move(data)));
    { std::unique_lock lock(mutex); ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(2), [&] { return failed; })); }
    ASSERT_TRUE(pipeline.stop()); EXPECT_EQ(successful, 0); EXPECT_EQ(pipeline.stats().chunks_processed, 0);
#endif
}

#ifndef FILE_TRANS_ENABLE_ENCRYPTION
#include <kcenon/file_transfer/encryption/transfer_crypto.h>
TEST(PipelineEncryptionPolicyTest, CryptoDisabledRejectsEvenValidSecret) {
    using namespace kcenon::file_transfer;
    EXPECT_FALSE(transfer_crypto::validate_secret(std::vector<std::byte>(32), ""));
    EXPECT_FALSE(transfer_crypto::random_salt());
}
#endif
