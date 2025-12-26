/**
 * @file test_s3_integration.cpp
 * @brief Integration tests for AWS S3 storage with MinIO
 *
 * These tests require a running MinIO server or S3-compatible endpoint.
 *
 * Environment variables:
 *   MINIO_ENDPOINT      - MinIO endpoint URL (e.g., http://localhost:9000)
 *   MINIO_ACCESS_KEY    - MinIO access key (default: minioadmin)
 *   MINIO_SECRET_KEY    - MinIO secret key (default: minioadmin)
 *   MINIO_BUCKET        - Test bucket name (default: test-bucket)
 *
 * Running MinIO locally:
 *   docker run -p 9000:9000 -p 9001:9001 \
 *     -e MINIO_ROOT_USER=minioadmin \
 *     -e MINIO_ROOT_PASSWORD=minioadmin \
 *     minio/minio server /data --console-address ":9001"
 *
 * Create test bucket:
 *   mc alias set local http://localhost:9000 minioadmin minioadmin
 *   mc mb local/test-bucket
 */

#include <gtest/gtest.h>

#include "kcenon/file_transfer/cloud/s3_storage.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <vector>

namespace kcenon::file_transfer {
namespace {

/**
 * @brief Get environment variable with default value
 */
auto get_env(const char* name, const char* default_value = nullptr) -> std::optional<std::string> {
    const char* value = std::getenv(name);
    if (value != nullptr) {
        return std::string(value);
    }
    if (default_value != nullptr) {
        return std::string(default_value);
    }
    return std::nullopt;
}

/**
 * @brief Check if MinIO integration tests should run
 */
auto should_run_minio_tests() -> bool {
    return get_env("MINIO_ENDPOINT").has_value();
}

/**
 * @brief MinIO configuration from environment
 */
struct minio_config {
    std::string endpoint;
    std::string access_key;
    std::string secret_key;
    std::string bucket;

    static auto from_environment() -> std::optional<minio_config> {
        auto endpoint = get_env("MINIO_ENDPOINT");
        if (!endpoint.has_value()) {
            return std::nullopt;
        }

        return minio_config{
            .endpoint = endpoint.value(),
            .access_key = get_env("MINIO_ACCESS_KEY", "minioadmin").value(),
            .secret_key = get_env("MINIO_SECRET_KEY", "minioadmin").value(),
            .bucket = get_env("MINIO_BUCKET", "test-bucket").value()
        };
    }
};

// ============================================================================
// MinIO Integration Test Fixture
// ============================================================================

class MinIOIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto config_opt = minio_config::from_environment();
        if (!config_opt.has_value()) {
            GTEST_SKIP() << "MinIO not configured. Set MINIO_ENDPOINT to run.";
        }
        minio_config_ = config_opt.value();

        // Create credentials
        static_credentials creds;
        creds.access_key_id = minio_config_.access_key;
        creds.secret_access_key = minio_config_.secret_key;
        provider_ = s3_credential_provider::create(creds);
        ASSERT_NE(provider_, nullptr);

        // Create S3 storage with MinIO endpoint
        auto config = cloud_config_builder::s3()
            .with_bucket(minio_config_.bucket)
            .with_region("us-east-1")
            .with_endpoint(minio_config_.endpoint)
            .with_path_style(true)
            .with_ssl(false, false)
            .build_s3();

        storage_ = s3_storage::create(config, provider_);
        ASSERT_NE(storage_, nullptr);

        // Connect to MinIO
        auto connect_result = storage_->connect();
        ASSERT_TRUE(connect_result.has_value())
            << "Failed to connect to MinIO: "
            << (connect_result.has_value() ? "" : connect_result.error().message);

        // Create temporary directory for test files
        temp_dir_ = std::filesystem::temp_directory_path() /
                    ("s3_integration_test_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(temp_dir_);

        // Generate unique prefix for this test run
        test_prefix_ = "test_" + std::to_string(std::random_device{}()) + "/";
    }

    void TearDown() override {
        // Cleanup uploaded objects
        if (storage_ && storage_->is_connected()) {
            for (const auto& key : uploaded_keys_) {
                (void)storage_->delete_object(key);
            }
            storage_->disconnect();
        }

        // Cleanup temp directory
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }

    auto create_test_data(std::size_t size) -> std::vector<std::byte> {
        std::vector<std::byte> data(size);
        std::mt19937 gen(42);
        std::uniform_int_distribution<> dis(0, 255);
        for (auto& byte : data) {
            byte = static_cast<std::byte>(dis(gen));
        }
        return data;
    }

    auto create_test_file(const std::string& name, std::size_t size)
        -> std::filesystem::path {
        auto path = temp_dir_ / name;
        std::ofstream file(path, std::ios::binary);

        std::mt19937 gen(42);
        std::uniform_int_distribution<> dis(0, 255);

        for (std::size_t i = 0; i < size; ++i) {
            char byte = static_cast<char>(dis(gen));
            file.write(&byte, 1);
        }

        return path;
    }

    auto test_key(const std::string& name) -> std::string {
        return test_prefix_ + name;
    }

    void track_upload(const std::string& key) {
        uploaded_keys_.push_back(key);
    }

    minio_config minio_config_;
    std::shared_ptr<credential_provider> provider_;
    std::unique_ptr<s3_storage> storage_;
    std::filesystem::path temp_dir_;
    std::string test_prefix_;
    std::vector<std::string> uploaded_keys_;
};

// ============================================================================
// Connection Tests
// ============================================================================

TEST_F(MinIOIntegrationTest, ConnectionState) {
    ASSERT_TRUE(storage_->is_connected());
    EXPECT_EQ(storage_->state(), cloud_storage_state::connected);
    EXPECT_EQ(storage_->provider(), cloud_provider::aws_s3);
    EXPECT_EQ(storage_->provider_name(), "aws-s3");
}

TEST_F(MinIOIntegrationTest, EndpointConfiguration) {
    EXPECT_EQ(storage_->endpoint_url(), minio_config_.endpoint);
    EXPECT_EQ(storage_->bucket(), minio_config_.bucket);
    EXPECT_FALSE(storage_->is_transfer_acceleration_enabled());
}

TEST_F(MinIOIntegrationTest, DisconnectAndReconnect) {
    auto disconnect_result = storage_->disconnect();
    EXPECT_TRUE(disconnect_result.has_value());
    EXPECT_FALSE(storage_->is_connected());

    auto reconnect_result = storage_->connect();
    EXPECT_TRUE(reconnect_result.has_value());
    EXPECT_TRUE(storage_->is_connected());
}

// ============================================================================
// Upload Tests
// ============================================================================

TEST_F(MinIOIntegrationTest, UploadSmallData) {
    const auto key = test_key("small_data.bin");
    auto data = create_test_data(1024);

    auto result = storage_->upload(key, data);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    track_upload(key);

    EXPECT_EQ(result.value().key, key);
    EXPECT_EQ(result.value().bytes_uploaded, 1024);
    EXPECT_FALSE(result.value().etag.empty());
}

TEST_F(MinIOIntegrationTest, UploadMediumData) {
    const auto key = test_key("medium_data.bin");
    auto data = create_test_data(1024 * 1024);  // 1MB

    auto result = storage_->upload(key, data);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    track_upload(key);

    EXPECT_EQ(result.value().key, key);
    EXPECT_EQ(result.value().bytes_uploaded, 1024 * 1024);
}

TEST_F(MinIOIntegrationTest, UploadWithContentType) {
    const auto key = test_key("document.json");
    auto data = create_test_data(256);

    cloud_transfer_options options;
    options.content_type = "application/json";

    auto result = storage_->upload(key, data, options);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    track_upload(key);

    // Verify content type was set by checking metadata
    auto metadata_result = storage_->get_metadata(key);
    ASSERT_TRUE(metadata_result.has_value());
    EXPECT_EQ(metadata_result.value().content_type, "application/json");
}

TEST_F(MinIOIntegrationTest, UploadFile) {
    const auto key = test_key("uploaded_file.bin");
    auto file_path = create_test_file("upload_source.bin", 4096);

    auto result = storage_->upload_file(file_path, key);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    track_upload(key);

    EXPECT_EQ(result.value().key, key);
    EXPECT_EQ(result.value().bytes_uploaded, 4096);
}

TEST_F(MinIOIntegrationTest, UploadAsync) {
    const auto key = test_key("async_data.bin");
    auto data = create_test_data(2048);

    auto future = storage_->upload_async(key, data);
    auto result = future.get();

    ASSERT_TRUE(result.has_value()) << result.error().message;
    track_upload(key);

    EXPECT_EQ(result.value().key, key);
}

// ============================================================================
// Download Tests
// ============================================================================

TEST_F(MinIOIntegrationTest, DownloadData) {
    const auto key = test_key("download_test.bin");
    auto original_data = create_test_data(2048);

    // Upload first
    auto upload_result = storage_->upload(key, original_data);
    ASSERT_TRUE(upload_result.has_value()) << upload_result.error().message;
    track_upload(key);

    // Download
    auto download_result = storage_->download(key);
    ASSERT_TRUE(download_result.has_value()) << download_result.error().message;

    EXPECT_EQ(download_result.value().size(), original_data.size());
    EXPECT_EQ(download_result.value(), original_data);
}

TEST_F(MinIOIntegrationTest, DownloadFile) {
    const auto key = test_key("download_file_test.bin");
    auto original_data = create_test_data(4096);

    // Upload first
    auto upload_result = storage_->upload(key, original_data);
    ASSERT_TRUE(upload_result.has_value()) << upload_result.error().message;
    track_upload(key);

    // Download to file
    auto download_path = temp_dir_ / "downloaded_file.bin";
    auto download_result = storage_->download_file(key, download_path);
    ASSERT_TRUE(download_result.has_value()) << download_result.error().message;

    EXPECT_EQ(download_result.value().bytes_downloaded, 4096);
    EXPECT_TRUE(std::filesystem::exists(download_path));
    EXPECT_EQ(std::filesystem::file_size(download_path), 4096);
}

TEST_F(MinIOIntegrationTest, DownloadNonExistent) {
    const auto key = test_key("non_existent_file.bin");

    auto result = storage_->download(key);
    EXPECT_FALSE(result.has_value());
}

TEST_F(MinIOIntegrationTest, DownloadAsync) {
    const auto key = test_key("async_download_test.bin");
    auto original_data = create_test_data(1024);

    // Upload first
    auto upload_result = storage_->upload(key, original_data);
    ASSERT_TRUE(upload_result.has_value()) << upload_result.error().message;
    track_upload(key);

    // Async download
    auto future = storage_->download_async(key);
    auto download_result = future.get();

    ASSERT_TRUE(download_result.has_value()) << download_result.error().message;
    EXPECT_EQ(download_result.value(), original_data);
}

// ============================================================================
// Object Operations Tests
// ============================================================================

TEST_F(MinIOIntegrationTest, ObjectExists) {
    const auto key = test_key("exists_test.bin");
    auto data = create_test_data(256);

    // Before upload - should not exist
    auto exists_before = storage_->exists(key);
    ASSERT_TRUE(exists_before.has_value());
    EXPECT_FALSE(exists_before.value());

    // Upload
    auto upload_result = storage_->upload(key, data);
    ASSERT_TRUE(upload_result.has_value()) << upload_result.error().message;
    track_upload(key);

    // After upload - should exist
    auto exists_after = storage_->exists(key);
    ASSERT_TRUE(exists_after.has_value());
    EXPECT_TRUE(exists_after.value());
}

TEST_F(MinIOIntegrationTest, GetMetadata) {
    const auto key = test_key("metadata_test.bin");
    auto data = create_test_data(512);

    auto upload_result = storage_->upload(key, data);
    ASSERT_TRUE(upload_result.has_value()) << upload_result.error().message;
    track_upload(key);

    auto metadata_result = storage_->get_metadata(key);
    ASSERT_TRUE(metadata_result.has_value()) << metadata_result.error().message;

    const auto& metadata = metadata_result.value();
    EXPECT_EQ(metadata.key, key);
    EXPECT_EQ(metadata.size, 512);
    EXPECT_FALSE(metadata.etag.empty());
}

TEST_F(MinIOIntegrationTest, DeleteObject) {
    const auto key = test_key("delete_test.bin");
    auto data = create_test_data(256);

    // Upload
    auto upload_result = storage_->upload(key, data);
    ASSERT_TRUE(upload_result.has_value()) << upload_result.error().message;

    // Verify exists
    auto exists_result = storage_->exists(key);
    ASSERT_TRUE(exists_result.has_value());
    EXPECT_TRUE(exists_result.value());

    // Delete
    auto delete_result = storage_->delete_object(key);
    ASSERT_TRUE(delete_result.has_value()) << delete_result.error().message;
    EXPECT_EQ(delete_result.value().key, key);

    // Verify deleted
    exists_result = storage_->exists(key);
    ASSERT_TRUE(exists_result.has_value());
    EXPECT_FALSE(exists_result.value());
}

TEST_F(MinIOIntegrationTest, ListObjects) {
    // Upload multiple objects
    std::vector<std::string> keys;
    for (int i = 0; i < 5; ++i) {
        auto key = test_key("list_test_" + std::to_string(i) + ".bin");
        auto data = create_test_data(128);
        auto result = storage_->upload(key, data);
        ASSERT_TRUE(result.has_value()) << result.error().message;
        keys.push_back(key);
        track_upload(key);
    }

    // List objects with prefix
    list_objects_options list_options;
    list_options.prefix = test_prefix_;
    list_options.max_keys = 10;

    auto list_result = storage_->list_objects(list_options);
    ASSERT_TRUE(list_result.has_value()) << list_result.error().message;

    const auto& objects = list_result.value().objects;
    EXPECT_GE(objects.size(), 5);

    // Verify all uploaded keys are in the list
    for (const auto& expected_key : keys) {
        bool found = false;
        for (const auto& obj : objects) {
            if (obj.key == expected_key) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Key not found in list: " << expected_key;
    }
}

TEST_F(MinIOIntegrationTest, CopyObject) {
    const auto source_key = test_key("copy_source.bin");
    const auto dest_key = test_key("copy_dest.bin");
    auto data = create_test_data(512);

    // Upload source
    auto upload_result = storage_->upload(source_key, data);
    ASSERT_TRUE(upload_result.has_value()) << upload_result.error().message;
    track_upload(source_key);

    // Copy object
    auto copy_result = storage_->copy_object(source_key, dest_key);
    ASSERT_TRUE(copy_result.has_value()) << copy_result.error().message;
    track_upload(dest_key);

    // Verify copy exists
    auto exists_result = storage_->exists(dest_key);
    ASSERT_TRUE(exists_result.has_value());
    EXPECT_TRUE(exists_result.value());

    // Verify content
    auto download_result = storage_->download(dest_key);
    ASSERT_TRUE(download_result.has_value());
    EXPECT_EQ(download_result.value(), data);
}

// ============================================================================
// Streaming Upload Tests
// ============================================================================

TEST_F(MinIOIntegrationTest, StreamingUpload) {
    const auto key = test_key("streaming_test.bin");

    // Create upload stream
    auto stream = storage_->create_upload_stream(key);
    ASSERT_NE(stream, nullptr);
    EXPECT_TRUE(stream->upload_id().has_value());

    // Write chunks
    std::size_t total_bytes = 0;
    for (int i = 0; i < 5; ++i) {
        auto chunk = create_test_data(1024);
        auto write_result = stream->write(chunk);
        ASSERT_TRUE(write_result.has_value()) << write_result.error().message;
        EXPECT_EQ(write_result.value(), 1024);
        total_bytes += 1024;
    }

    EXPECT_EQ(stream->bytes_written(), total_bytes);

    // Finalize
    auto finalize_result = stream->finalize();
    ASSERT_TRUE(finalize_result.has_value()) << finalize_result.error().message;
    track_upload(key);

    EXPECT_EQ(finalize_result.value().key, key);
    EXPECT_EQ(finalize_result.value().bytes_uploaded, total_bytes);

    // Verify object exists
    auto exists_result = storage_->exists(key);
    ASSERT_TRUE(exists_result.has_value());
    EXPECT_TRUE(exists_result.value());
}

TEST_F(MinIOIntegrationTest, StreamingUploadAbort) {
    const auto key = test_key("aborted_stream.bin");

    // Create upload stream
    auto stream = storage_->create_upload_stream(key);
    ASSERT_NE(stream, nullptr);

    // Write some data
    auto chunk = create_test_data(512);
    auto write_result = stream->write(chunk);
    ASSERT_TRUE(write_result.has_value());

    // Abort
    auto abort_result = stream->abort();
    EXPECT_TRUE(abort_result.has_value());

    // Verify object does not exist
    auto exists_result = storage_->exists(key);
    ASSERT_TRUE(exists_result.has_value());
    EXPECT_FALSE(exists_result.value());
}

// ============================================================================
// Presigned URL Tests
// ============================================================================

#ifdef FILE_TRANS_ENABLE_ENCRYPTION

TEST_F(MinIOIntegrationTest, GeneratePresignedGetUrl) {
    const auto key = test_key("presigned_get.bin");
    auto data = create_test_data(256);

    // Upload object
    auto upload_result = storage_->upload(key, data);
    ASSERT_TRUE(upload_result.has_value()) << upload_result.error().message;
    track_upload(key);

    // Generate presigned URL
    presigned_url_options options;
    options.method = "GET";
    options.expiration = std::chrono::seconds{3600};

    auto url_result = storage_->generate_presigned_url(key, options);
    ASSERT_TRUE(url_result.has_value()) << url_result.error().message;

    const auto& url = url_result.value();
    EXPECT_TRUE(url.find(minio_config_.endpoint) != std::string::npos ||
                url.find(minio_config_.bucket) != std::string::npos);
    EXPECT_TRUE(url.find("X-Amz-Signature=") != std::string::npos);
}

TEST_F(MinIOIntegrationTest, GeneratePresignedPutUrl) {
    const auto key = test_key("presigned_put.bin");

    presigned_url_options options;
    options.method = "PUT";
    options.expiration = std::chrono::seconds{300};
    options.content_type = "application/octet-stream";

    auto url_result = storage_->generate_presigned_url(key, options);
    ASSERT_TRUE(url_result.has_value()) << url_result.error().message;

    const auto& url = url_result.value();
    EXPECT_TRUE(url.find("X-Amz-Signature=") != std::string::npos);
}

#endif  // FILE_TRANS_ENABLE_ENCRYPTION

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(MinIOIntegrationTest, StatisticsTracking) {
    // Reset statistics
    storage_->reset_statistics();

    // Perform operations
    const auto key = test_key("stats_test.bin");
    auto data = create_test_data(512);

    auto upload_result = storage_->upload(key, data);
    ASSERT_TRUE(upload_result.has_value());
    track_upload(key);

    auto download_result = storage_->download(key);
    ASSERT_TRUE(download_result.has_value());

    list_objects_options list_options;
    list_options.prefix = test_prefix_;
    (void)storage_->list_objects(list_options);

    // Check statistics
    auto stats = storage_->get_statistics();
    EXPECT_EQ(stats.bytes_uploaded, 512);
    EXPECT_EQ(stats.bytes_downloaded, 512);
    EXPECT_EQ(stats.upload_count, 1);
    EXPECT_EQ(stats.download_count, 1);
    EXPECT_EQ(stats.list_count, 1);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(MinIOIntegrationTest, DownloadNonExistentObject) {
    const auto key = test_key("definitely_does_not_exist.bin");

    auto result = storage_->download(key);
    EXPECT_FALSE(result.has_value());
}

TEST_F(MinIOIntegrationTest, DeleteNonExistentObject) {
    const auto key = test_key("delete_non_existent.bin");

    auto result = storage_->delete_object(key);
    // MinIO may return success for deleting non-existent objects (idempotent delete)
    // This is valid S3 behavior
}

TEST_F(MinIOIntegrationTest, UploadEmptyData) {
    const auto key = test_key("empty_file.bin");
    std::vector<std::byte> empty_data;

    auto result = storage_->upload(key, empty_data);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    track_upload(key);

    EXPECT_EQ(result.value().bytes_uploaded, 0);
}

// ============================================================================
// Progress Callback Tests
// ============================================================================

TEST_F(MinIOIntegrationTest, UploadProgressCallback) {
    const auto key = test_key("progress_test.bin");
    auto data = create_test_data(10 * 1024);  // 10KB

    std::vector<upload_progress> progress_updates;
    storage_->on_upload_progress([&progress_updates](const upload_progress& p) {
        progress_updates.push_back(p);
    });

    auto result = storage_->upload(key, data);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    track_upload(key);

    // Should have received at least one progress update
    EXPECT_GE(progress_updates.size(), 1);
    if (!progress_updates.empty()) {
        EXPECT_EQ(progress_updates.back().total_bytes, 10 * 1024);
    }
}

// ============================================================================
// Concurrency Tests
// ============================================================================

TEST_F(MinIOIntegrationTest, ConcurrentUploads) {
    constexpr int num_uploads = 5;
    std::vector<std::future<result<upload_result>>> futures;

    // Launch concurrent uploads
    for (int i = 0; i < num_uploads; ++i) {
        auto key = test_key("concurrent_" + std::to_string(i) + ".bin");
        auto data = create_test_data(1024);
        futures.push_back(storage_->upload_async(key, data));
        track_upload(key);
    }

    // Wait for all uploads
    int success_count = 0;
    for (auto& future : futures) {
        auto result = future.get();
        if (result.has_value()) {
            ++success_count;
        }
    }

    EXPECT_EQ(success_count, num_uploads);
}

TEST_F(MinIOIntegrationTest, ConcurrentUploadAndDownload) {
    const auto key = test_key("concurrent_up_down.bin");
    auto original_data = create_test_data(2048);

    // Upload first
    auto upload_result = storage_->upload(key, original_data);
    ASSERT_TRUE(upload_result.has_value()) << upload_result.error().message;
    track_upload(key);

    // Concurrent downloads
    constexpr int num_downloads = 5;
    std::vector<std::future<result<std::vector<std::byte>>>> futures;

    for (int i = 0; i < num_downloads; ++i) {
        futures.push_back(storage_->download_async(key));
    }

    // Verify all downloads succeeded and have correct data
    for (auto& future : futures) {
        auto result = future.get();
        ASSERT_TRUE(result.has_value()) << result.error().message;
        EXPECT_EQ(result.value(), original_data);
    }
}

// ============================================================================
// Large File Tests
// ============================================================================

TEST_F(MinIOIntegrationTest, LargeFileUploadDownload) {
    const auto key = test_key("large_file.bin");
    constexpr std::size_t file_size = 10 * 1024 * 1024;  // 10MB
    auto data = create_test_data(file_size);

    // Upload
    auto upload_result = storage_->upload(key, data);
    ASSERT_TRUE(upload_result.has_value()) << upload_result.error().message;
    track_upload(key);

    EXPECT_EQ(upload_result.value().bytes_uploaded, file_size);

    // Download
    auto download_result = storage_->download(key);
    ASSERT_TRUE(download_result.has_value()) << download_result.error().message;

    EXPECT_EQ(download_result.value().size(), file_size);
    EXPECT_EQ(download_result.value(), data);
}

}  // namespace
}  // namespace kcenon::file_transfer
