/**
 * @file test_pipeline_jobs.cpp
 * @brief Unit tests for pipeline job types
 */

#include "kcenon/file_transfer/server/pipeline_jobs.h"
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

class PipelineJobsTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "pipeline_jobs_test";
        std::filesystem::create_directories(test_dir_);

        // Create shared pipeline context
        context_ = std::make_shared<pipeline_context>();
        stats_.chunks_processed = 0;
        stats_.bytes_processed = 0;
        stats_.compression_saved_bytes = 0;
        stats_.stalls_detected = 0;
        stats_.backpressure_events = 0;
        context_->stats = &stats_;

        // Create compression engine
        engine_ = std::make_shared<compression_engine>(compression_level::fast);

        // Check if LZ4 is available at runtime
        lz4_available_ = check_lz4_availability();
    }

    auto check_lz4_availability() -> bool {
        // Try to compress a small test buffer to check if LZ4 is enabled
        std::vector<std::byte> test_data(64, std::byte{0x41});
        auto result = engine_->compress(std::span<const std::byte>(test_data));
        return result.has_value();
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

    auto create_test_chunk(std::size_t size = 1024) -> pipeline_chunk {
        pipeline_chunk chunk;
        chunk.id = transfer_id::generate();
        chunk.chunk_index = 0;
        chunk.data.resize(size);
        for (std::size_t i = 0; i < size; ++i) {
            chunk.data[i] = static_cast<std::byte>(i & 0xFF);
        }
        chunk.checksum = checksum::crc32(std::span<const std::byte>(chunk.data));
        chunk.is_compressed = false;
        chunk.original_size = size;
        return chunk;
    }

    auto create_compressible_chunk(std::size_t size = 1024) -> pipeline_chunk {
        pipeline_chunk chunk;
        chunk.id = transfer_id::generate();
        chunk.chunk_index = 0;
        chunk.data.resize(size);
        // Fill with repetitive data that compresses well
        for (std::size_t i = 0; i < size; ++i) {
            chunk.data[i] = static_cast<std::byte>('A' + (i % 4));
        }
        chunk.checksum = checksum::crc32(std::span<const std::byte>(chunk.data));
        chunk.is_compressed = false;
        chunk.original_size = size;
        return chunk;
    }

    std::filesystem::path test_dir_;
    std::shared_ptr<pipeline_context> context_;
    std::shared_ptr<compression_engine> engine_;
    pipeline_stats stats_;
    bool lz4_available_ = false;
};

// ----------------------------------------------------------------------------
// pipeline_context tests
// ----------------------------------------------------------------------------

TEST_F(PipelineJobsTest, ContextReportError) {
    std::atomic<int> error_count{0};
    std::string last_error;

    context_->on_error = [&](pipeline_stage stage, const std::string& msg) {
        error_count++;
        last_error = msg;
        EXPECT_EQ(stage, pipeline_stage::decompress);
    };

    context_->report_error(pipeline_stage::decompress, "Test error");

    EXPECT_EQ(error_count.load(), 1);
    EXPECT_EQ(last_error, "Test error");
}

TEST_F(PipelineJobsTest, ContextReportStageComplete) {
    std::atomic<int> complete_count{0};
    pipeline_stage last_stage{};

    context_->on_stage_complete = [&](pipeline_stage stage, const pipeline_chunk&) {
        complete_count++;
        last_stage = stage;
    };

    auto chunk = create_test_chunk();
    context_->report_stage_complete(pipeline_stage::compress, chunk);

    EXPECT_EQ(complete_count.load(), 1);
    EXPECT_EQ(last_stage, pipeline_stage::compress);
}

TEST_F(PipelineJobsTest, ContextNullCallbacksDoNotCrash) {
    // Context with null callbacks should not crash
    auto empty_context = std::make_shared<pipeline_context>();

    empty_context->report_error(pipeline_stage::decompress, "Test");
    auto chunk = create_test_chunk();
    empty_context->report_stage_complete(pipeline_stage::chunk_verify, chunk);

    // Should not crash
    SUCCEED();
}

// ----------------------------------------------------------------------------
// decompress_job tests
// ----------------------------------------------------------------------------

TEST_F(PipelineJobsTest, DecompressJobUncompressedChunk) {
    auto chunk = create_test_chunk();
    chunk.is_compressed = false;

    std::atomic<int> stage_complete{0};
    context_->on_stage_complete = [&](pipeline_stage stage, const pipeline_chunk&) {
        EXPECT_EQ(stage, pipeline_stage::decompress);
        stage_complete++;
    };

    auto job = std::make_shared<decompress_job>(std::move(chunk), engine_, context_);
    auto result = job->do_work();

    EXPECT_TRUE(result.is_ok());
    EXPECT_FALSE(job->get_chunk().is_compressed);
    EXPECT_EQ(stage_complete.load(), 1);
}

TEST_F(PipelineJobsTest, DecompressJobCompressedChunk) {
    if (!lz4_available_) {
        GTEST_SKIP() << "LZ4 compression not available";
    }

    // First compress some data
    auto original_chunk = create_compressible_chunk(4096);
    auto compress_result = engine_->compress(
        std::span<const std::byte>(original_chunk.data));
    ASSERT_TRUE(compress_result.has_value());

    pipeline_chunk compressed_chunk;
    compressed_chunk.id = transfer_id::generate();
    compressed_chunk.chunk_index = 0;
    compressed_chunk.data = std::move(compress_result.value());
    compressed_chunk.is_compressed = true;
    compressed_chunk.original_size = original_chunk.data.size();
    compressed_chunk.checksum = original_chunk.checksum;

    std::atomic<int> stage_complete{0};
    context_->on_stage_complete = [&](pipeline_stage stage, const pipeline_chunk&) {
        EXPECT_EQ(stage, pipeline_stage::decompress);
        stage_complete++;
    };

    auto job = std::make_shared<decompress_job>(
        std::move(compressed_chunk), engine_, context_);
    auto result = job->do_work();

    EXPECT_TRUE(result.is_ok());
    EXPECT_FALSE(job->get_chunk().is_compressed);
    EXPECT_EQ(job->get_chunk().data.size(), original_chunk.data.size());
    EXPECT_EQ(stage_complete.load(), 1);
}

TEST_F(PipelineJobsTest, DecompressJobCancelled) {
    auto chunk = create_test_chunk();

    auto job = std::make_shared<decompress_job>(std::move(chunk), engine_, context_);

    // Create and set a cancelled token
    thread::cancellation_token token;
    token.cancel();
    job->set_cancellation_token(token);

    auto result = job->do_work();

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(thread::get_error_code(result.error()),
              thread::error_code::operation_canceled);
}

TEST_F(PipelineJobsTest, DecompressJobName) {
    auto chunk = create_test_chunk();
    auto job = std::make_shared<decompress_job>(std::move(chunk), engine_, context_);

    EXPECT_EQ(job->get_name(), "decompress_job");
}

// ----------------------------------------------------------------------------
// verify_job tests
// ----------------------------------------------------------------------------

TEST_F(PipelineJobsTest, VerifyJobValidChecksum) {
    auto chunk = create_test_chunk();

    std::atomic<int> stage_complete{0};
    context_->on_stage_complete = [&](pipeline_stage stage, const pipeline_chunk&) {
        EXPECT_EQ(stage, pipeline_stage::chunk_verify);
        stage_complete++;
    };

    auto job = std::make_shared<verify_job>(std::move(chunk), context_);
    auto result = job->do_work();

    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(stage_complete.load(), 1);
    EXPECT_EQ(stats_.chunks_processed.load(), 1);
}

TEST_F(PipelineJobsTest, VerifyJobInvalidChecksum) {
    auto chunk = create_test_chunk();
    chunk.checksum = 0xDEADBEEF;  // Invalid checksum

    std::atomic<int> error_count{0};
    context_->on_error = [&](pipeline_stage stage, const std::string&) {
        EXPECT_EQ(stage, pipeline_stage::chunk_verify);
        error_count++;
    };

    auto job = std::make_shared<verify_job>(std::move(chunk), context_);
    auto result = job->do_work();

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(error_count.load(), 1);
}

TEST_F(PipelineJobsTest, VerifyJobCancelled) {
    auto chunk = create_test_chunk();
    auto job = std::make_shared<verify_job>(std::move(chunk), context_);

    thread::cancellation_token token;
    token.cancel();
    job->set_cancellation_token(token);

    auto result = job->do_work();

    EXPECT_TRUE(result.is_err());
}

TEST_F(PipelineJobsTest, VerifyJobName) {
    auto chunk = create_test_chunk();
    auto job = std::make_shared<verify_job>(std::move(chunk), context_);

    EXPECT_EQ(job->get_name(), "verify_job");
}

// ----------------------------------------------------------------------------
// write_job tests
// ----------------------------------------------------------------------------

TEST_F(PipelineJobsTest, WriteJobSuccess) {
    auto chunk = create_test_chunk();
    auto expected_id = chunk.id;
    auto expected_size = chunk.data.size();

    std::atomic<int> stage_complete{0};
    context_->on_stage_complete = [&](pipeline_stage stage, const pipeline_chunk&) {
        EXPECT_EQ(stage, pipeline_stage::file_write);
        stage_complete++;
    };

    std::atomic<int> upload_complete{0};
    context_->on_upload_complete = [&](const transfer_id& id, uint64_t bytes) {
        EXPECT_EQ(id, expected_id);
        EXPECT_EQ(bytes, expected_size);
        upload_complete++;
    };

    auto job = std::make_shared<write_job>(std::move(chunk), context_);
    auto result = job->do_work();

    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(stage_complete.load(), 1);
    EXPECT_EQ(upload_complete.load(), 1);
}

TEST_F(PipelineJobsTest, WriteJobCancelled) {
    auto chunk = create_test_chunk();
    auto job = std::make_shared<write_job>(std::move(chunk), context_);

    thread::cancellation_token token;
    token.cancel();
    job->set_cancellation_token(token);

    auto result = job->do_work();

    EXPECT_TRUE(result.is_err());
}

TEST_F(PipelineJobsTest, WriteJobName) {
    auto chunk = create_test_chunk();
    auto job = std::make_shared<write_job>(std::move(chunk), context_);

    EXPECT_EQ(job->get_name(), "write_job");
}

// ----------------------------------------------------------------------------
// read_job tests
// ----------------------------------------------------------------------------

TEST_F(PipelineJobsTest, ReadJobSuccess) {
    // Create a test file
    auto file_path = create_test_file("read_test.bin", 1024);

    download_request request;
    request.id = transfer_id::generate();
    request.chunk_index = 0;
    request.file_path = file_path;
    request.offset = 0;
    request.size = 512;

    std::atomic<int> stage_complete{0};
    context_->on_stage_complete = [&](pipeline_stage stage, const pipeline_chunk&) {
        EXPECT_EQ(stage, pipeline_stage::file_read);
        stage_complete++;
    };

    auto job = std::make_shared<read_job>(std::move(request), context_);
    auto result = job->do_work();

    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(job->get_chunk().data.size(), 512);
    EXPECT_EQ(job->get_chunk().chunk_index, 0);
    EXPECT_FALSE(job->get_chunk().is_compressed);
    EXPECT_EQ(stage_complete.load(), 1);
}

TEST_F(PipelineJobsTest, ReadJobFileNotFound) {
    download_request request;
    request.id = transfer_id::generate();
    request.chunk_index = 0;
    request.file_path = test_dir_ / "nonexistent.bin";
    request.offset = 0;
    request.size = 1024;

    std::atomic<int> error_count{0};
    context_->on_error = [&](pipeline_stage stage, const std::string&) {
        EXPECT_EQ(stage, pipeline_stage::file_read);
        error_count++;
    };

    auto job = std::make_shared<read_job>(std::move(request), context_);
    auto result = job->do_work();

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(error_count.load(), 1);
}

TEST_F(PipelineJobsTest, ReadJobPartialRead) {
    // Create a small file
    auto file_path = create_test_file("small_file.bin", 100);

    download_request request;
    request.id = transfer_id::generate();
    request.chunk_index = 0;
    request.file_path = file_path;
    request.offset = 0;
    request.size = 1024;  // Request more than file size

    auto job = std::make_shared<read_job>(std::move(request), context_);
    auto result = job->do_work();

    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(job->get_chunk().data.size(), 100);  // Should only read what's available
}

TEST_F(PipelineJobsTest, ReadJobCancelled) {
    auto file_path = create_test_file("cancelled_test.bin", 1024);

    download_request request;
    request.id = transfer_id::generate();
    request.chunk_index = 0;
    request.file_path = file_path;
    request.offset = 0;
    request.size = 512;

    auto job = std::make_shared<read_job>(std::move(request), context_);

    thread::cancellation_token token;
    token.cancel();
    job->set_cancellation_token(token);

    auto result = job->do_work();

    EXPECT_TRUE(result.is_err());
}

TEST_F(PipelineJobsTest, ReadJobName) {
    download_request request;
    request.file_path = test_dir_ / "test.bin";
    auto job = std::make_shared<read_job>(std::move(request), context_);

    EXPECT_EQ(job->get_name(), "read_job");
}

// ----------------------------------------------------------------------------
// compress_job tests
// ----------------------------------------------------------------------------

TEST_F(PipelineJobsTest, CompressJobCompressibleData) {
    if (!lz4_available_) {
        GTEST_SKIP() << "LZ4 compression not available";
    }

    auto chunk = create_compressible_chunk(4096);
    auto original_size = chunk.data.size();

    std::atomic<int> stage_complete{0};
    context_->on_stage_complete = [&](pipeline_stage stage, const pipeline_chunk&) {
        EXPECT_EQ(stage, pipeline_stage::compress);
        stage_complete++;
    };

    auto job = std::make_shared<compress_job>(std::move(chunk), engine_, context_);
    auto result = job->do_work();

    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(job->get_chunk().is_compressed);
    EXPECT_LT(job->get_chunk().data.size(), original_size);
    EXPECT_EQ(job->get_chunk().original_size, original_size);
    EXPECT_EQ(stage_complete.load(), 1);
}

TEST_F(PipelineJobsTest, CompressJobRandomData) {
    // Random data doesn't compress well
    auto chunk = create_test_chunk(1024);

    std::atomic<int> stage_complete{0};
    context_->on_stage_complete = [&](pipeline_stage stage, const pipeline_chunk&) {
        EXPECT_EQ(stage, pipeline_stage::compress);
        stage_complete++;
    };

    auto job = std::make_shared<compress_job>(std::move(chunk), engine_, context_);
    auto result = job->do_work();

    EXPECT_TRUE(result.is_ok());
    // Random data may not compress well but job should still complete
    EXPECT_EQ(stage_complete.load(), 1);
}

TEST_F(PipelineJobsTest, CompressJobCancelled) {
    auto chunk = create_test_chunk();
    auto job = std::make_shared<compress_job>(std::move(chunk), engine_, context_);

    thread::cancellation_token token;
    token.cancel();
    job->set_cancellation_token(token);

    auto result = job->do_work();

    EXPECT_TRUE(result.is_err());
}

TEST_F(PipelineJobsTest, CompressJobName) {
    auto chunk = create_test_chunk();
    auto job = std::make_shared<compress_job>(std::move(chunk), engine_, context_);

    EXPECT_EQ(job->get_name(), "compress_job");
}

// ----------------------------------------------------------------------------
// send_job tests
// ----------------------------------------------------------------------------

TEST_F(PipelineJobsTest, SendJobSuccess) {
    auto chunk = create_test_chunk();
    auto expected_size = chunk.data.size();

    std::atomic<int> stage_complete{0};
    context_->on_stage_complete = [&](pipeline_stage stage, const pipeline_chunk&) {
        EXPECT_EQ(stage, pipeline_stage::network_send);
        stage_complete++;
    };

    std::atomic<int> download_ready{0};
    context_->on_download_ready = [&](const pipeline_chunk& c) {
        EXPECT_EQ(c.data.size(), expected_size);
        download_ready++;
    };

    auto job = std::make_shared<send_job>(std::move(chunk), context_);
    auto result = job->do_work();

    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(stage_complete.load(), 1);
    EXPECT_EQ(download_ready.load(), 1);
    EXPECT_EQ(stats_.chunks_processed.load(), 1);
    EXPECT_EQ(stats_.bytes_processed.load(), expected_size);
}

TEST_F(PipelineJobsTest, SendJobCancelled) {
    auto chunk = create_test_chunk();
    auto job = std::make_shared<send_job>(std::move(chunk), context_);

    thread::cancellation_token token;
    token.cancel();
    job->set_cancellation_token(token);

    auto result = job->do_work();

    EXPECT_TRUE(result.is_err());
}

TEST_F(PipelineJobsTest, SendJobName) {
    auto chunk = create_test_chunk();
    auto job = std::make_shared<send_job>(std::move(chunk), context_);

    EXPECT_EQ(job->get_name(), "send_job");
}

// ----------------------------------------------------------------------------
// Job inheritance tests
// ----------------------------------------------------------------------------

TEST_F(PipelineJobsTest, AllJobsInheritFromJobBase) {
    auto chunk = create_test_chunk();

    // Test that all job types can be used as kcenon::thread::job
    std::shared_ptr<thread::job> decompress =
        std::make_shared<decompress_job>(chunk, engine_, context_);
    std::shared_ptr<thread::job> verify =
        std::make_shared<verify_job>(chunk, context_);
    std::shared_ptr<thread::job> write =
        std::make_shared<write_job>(chunk, context_);

    download_request request;
    request.file_path = test_dir_ / "test.bin";
    std::shared_ptr<thread::job> read =
        std::make_shared<read_job>(std::move(request), context_);

    std::shared_ptr<thread::job> compress =
        std::make_shared<compress_job>(chunk, engine_, context_);
    std::shared_ptr<thread::job> send =
        std::make_shared<send_job>(std::move(chunk), context_);

    // All should have non-empty names
    EXPECT_FALSE(decompress->get_name().empty());
    EXPECT_FALSE(verify->get_name().empty());
    EXPECT_FALSE(write->get_name().empty());
    EXPECT_FALSE(read->get_name().empty());
    EXPECT_FALSE(compress->get_name().empty());
    EXPECT_FALSE(send->get_name().empty());
}

// ----------------------------------------------------------------------------
// Download request tests
// ----------------------------------------------------------------------------

TEST_F(PipelineJobsTest, DownloadRequestFields) {
    download_request request;
    request.id = transfer_id::generate();
    request.chunk_index = 42;
    request.file_path = "/path/to/file.bin";
    request.offset = 1024;
    request.size = 512;

    EXPECT_EQ(request.chunk_index, 42);
    EXPECT_EQ(request.file_path, "/path/to/file.bin");
    EXPECT_EQ(request.offset, 1024);
    EXPECT_EQ(request.size, 512);
}

}  // namespace
}  // namespace kcenon::file_transfer
