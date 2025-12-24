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

// TODO: Re-enable once backpressure is implemented with thread_pool
// thread_pool uses unbounded queue, so bounded_job_queue backpressure doesn't apply
TEST_F(ServerPipelineTest, DISABLED_BackpressureWithSmallQueue) {
    pipeline_config config;
    config.io_workers = 1;
    config.compression_workers = 1;
    config.network_workers = 1;
    config.queue_size = 2;  // Very small queue

    auto pipeline_result = server_pipeline::create(config);
    ASSERT_TRUE(pipeline_result.has_value());
    auto& pipeline = pipeline_result.value();

    ASSERT_TRUE(pipeline.start().has_value());

    // Use multiple threads to overwhelm the queue and trigger backpressure
    std::atomic<int> submitted{0};
    std::atomic<int> rejected{0};
    auto id = transfer_id::generate();

    constexpr int num_threads = 8;
    constexpr int chunks_per_thread = 50;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < chunks_per_thread; ++i) {
                std::vector<std::byte> data(100, std::byte{0x42});
                auto chunk = create_pipeline_chunk(
                    id, static_cast<uint64_t>(t * chunks_per_thread + i), data);
                if (pipeline.try_submit_upload_chunk(std::move(chunk))) {
                    submitted++;
                } else {
                    rejected++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Should hit backpressure: either some were rejected or backpressure events recorded
    // With 8 threads submitting 50 chunks each (400 total) to a queue of size 2,
    // backpressure should definitely occur
    EXPECT_TRUE(rejected > 0 || pipeline.stats().backpressure_events.load() > 0)
        << "Expected backpressure but got: submitted=" << submitted
        << ", rejected=" << rejected
        << ", backpressure_events=" << pipeline.stats().backpressure_events.load();

    (void)pipeline.stop();
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

}  // namespace
}  // namespace kcenon::file_transfer
