/**
 * @file test_batch_transfer.cpp
 * @brief Unit tests for batch transfer (upload_files/download_files)
 */

#include <gtest/gtest.h>

#include <kcenon/file_transfer/client/file_transfer_client.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace kcenon::file_transfer::test {

class BatchTransferTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() /
                    ("file_trans_test_batch_" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()));
        std::filesystem::create_directories(test_dir_);

        // Create multiple test files
        for (int i = 0; i < 5; ++i) {
            auto path = test_dir_ / ("test_file_" + std::to_string(i) + ".txt");
            std::ofstream file(path);
            file << "Test file content " << i << " for batch testing.";
            file.close();
            test_files_.push_back(path);
        }
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    auto create_client() -> result<file_transfer_client> {
        return file_transfer_client::builder()
            .with_compression(compression_mode::none)
            .with_chunk_size(64 * 1024)
            .build();
    }

    std::filesystem::path test_dir_;
    std::vector<std::filesystem::path> test_files_;
};

// ============================================================================
// Batch Type Tests
// ============================================================================

TEST_F(BatchTransferTest, UploadEntry_DefaultConstruction) {
    upload_entry entry;
    EXPECT_TRUE(entry.local_path.empty());
    EXPECT_TRUE(entry.remote_name.empty());
}

TEST_F(BatchTransferTest, UploadEntry_ConstructWithPath) {
    upload_entry entry{"/path/to/file.txt"};
    EXPECT_EQ(entry.local_path, "/path/to/file.txt");
    EXPECT_TRUE(entry.remote_name.empty());
}

TEST_F(BatchTransferTest, UploadEntry_ConstructWithPathAndName) {
    upload_entry entry{"/path/to/local.txt", "remote.txt"};
    EXPECT_EQ(entry.local_path, "/path/to/local.txt");
    EXPECT_EQ(entry.remote_name, "remote.txt");
}

TEST_F(BatchTransferTest, DownloadEntry_DefaultConstruction) {
    download_entry entry;
    EXPECT_TRUE(entry.remote_name.empty());
    EXPECT_TRUE(entry.local_path.empty());
}

TEST_F(BatchTransferTest, DownloadEntry_ConstructWithValues) {
    download_entry entry{"remote.txt", "/path/to/local.txt"};
    EXPECT_EQ(entry.remote_name, "remote.txt");
    EXPECT_EQ(entry.local_path, "/path/to/local.txt");
}

TEST_F(BatchTransferTest, BatchProgress_DefaultValues) {
    batch_progress progress;
    EXPECT_EQ(progress.total_files, 0u);
    EXPECT_EQ(progress.completed_files, 0u);
    EXPECT_EQ(progress.failed_files, 0u);
    EXPECT_EQ(progress.in_progress_files, 0u);
    EXPECT_EQ(progress.total_bytes, 0u);
    EXPECT_EQ(progress.transferred_bytes, 0u);
    EXPECT_DOUBLE_EQ(progress.overall_rate, 0.0);
}

TEST_F(BatchTransferTest, BatchProgress_CompletionPercentage) {
    batch_progress progress;
    progress.total_bytes = 1000;
    progress.transferred_bytes = 500;

    EXPECT_DOUBLE_EQ(progress.completion_percentage(), 50.0);
}

TEST_F(BatchTransferTest, BatchProgress_CompletionPercentageZeroTotal) {
    batch_progress progress;
    progress.total_bytes = 0;
    progress.transferred_bytes = 100;

    EXPECT_DOUBLE_EQ(progress.completion_percentage(), 0.0);
}

TEST_F(BatchTransferTest, BatchProgress_PendingFiles) {
    batch_progress progress;
    progress.total_files = 10;
    progress.completed_files = 3;
    progress.failed_files = 2;
    progress.in_progress_files = 2;

    EXPECT_EQ(progress.pending_files(), 3u);
}

TEST_F(BatchTransferTest, BatchResult_DefaultValues) {
    batch_result result;
    EXPECT_EQ(result.total_files, 0u);
    EXPECT_EQ(result.succeeded, 0u);
    EXPECT_EQ(result.failed, 0u);
    EXPECT_EQ(result.total_bytes, 0u);
    EXPECT_EQ(result.elapsed.count(), 0);
    EXPECT_TRUE(result.file_results.empty());
}

TEST_F(BatchTransferTest, BatchResult_AllSucceeded) {
    batch_result result;
    result.total_files = 5;
    result.succeeded = 5;
    result.failed = 0;

    EXPECT_TRUE(result.all_succeeded());
}

TEST_F(BatchTransferTest, BatchResult_NotAllSucceeded) {
    batch_result result;
    result.total_files = 5;
    result.succeeded = 4;
    result.failed = 1;

    EXPECT_FALSE(result.all_succeeded());
}

TEST_F(BatchTransferTest, BatchOptions_DefaultValues) {
    batch_options options;
    EXPECT_EQ(options.max_concurrent, 4u);
    EXPECT_TRUE(options.continue_on_error);
    EXPECT_FALSE(options.overwrite);
    EXPECT_FALSE(options.compression.has_value());
}

TEST_F(BatchTransferTest, BatchFileResult_DefaultValues) {
    batch_file_result result;
    EXPECT_TRUE(result.filename.empty());
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.bytes_transferred, 0u);
    EXPECT_EQ(result.elapsed.count(), 0);
    EXPECT_FALSE(result.error_message.has_value());
}

// ============================================================================
// batch_transfer_handle Tests
// ============================================================================

TEST_F(BatchTransferTest, BatchTransferHandle_DefaultConstruction) {
    batch_transfer_handle handle;
    EXPECT_EQ(handle.get_id(), 0u);
    EXPECT_FALSE(handle.is_valid());
}

TEST_F(BatchTransferTest, BatchTransferHandle_InvalidHandle_GetTotalFiles) {
    batch_transfer_handle handle;
    EXPECT_EQ(handle.get_total_files(), 0u);
}

TEST_F(BatchTransferTest, BatchTransferHandle_InvalidHandle_GetCompletedFiles) {
    batch_transfer_handle handle;
    EXPECT_EQ(handle.get_completed_files(), 0u);
}

TEST_F(BatchTransferTest, BatchTransferHandle_InvalidHandle_GetFailedFiles) {
    batch_transfer_handle handle;
    EXPECT_EQ(handle.get_failed_files(), 0u);
}

TEST_F(BatchTransferTest, BatchTransferHandle_InvalidHandle_GetIndividualHandles) {
    batch_transfer_handle handle;
    auto handles = handle.get_individual_handles();
    EXPECT_TRUE(handles.empty());
}

TEST_F(BatchTransferTest, BatchTransferHandle_InvalidHandle_GetProgress) {
    batch_transfer_handle handle;
    auto progress = handle.get_batch_progress();
    EXPECT_EQ(progress.total_files, 0u);
}

TEST_F(BatchTransferTest, BatchTransferHandle_InvalidHandle_PauseAll) {
    batch_transfer_handle handle;
    auto result = handle.pause_all();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(BatchTransferTest, BatchTransferHandle_InvalidHandle_ResumeAll) {
    batch_transfer_handle handle;
    auto result = handle.resume_all();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(BatchTransferTest, BatchTransferHandle_InvalidHandle_CancelAll) {
    batch_transfer_handle handle;
    auto result = handle.cancel_all();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(BatchTransferTest, BatchTransferHandle_InvalidHandle_Wait) {
    batch_transfer_handle handle;
    auto result = handle.wait();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(BatchTransferTest, BatchTransferHandle_InvalidHandle_WaitFor) {
    batch_transfer_handle handle;
    auto result = handle.wait_for(std::chrono::milliseconds{100});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

// ============================================================================
// upload_files Tests
// ============================================================================

TEST_F(BatchTransferTest, UploadFiles_NotConnected) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    std::vector<upload_entry> files{
        {test_files_[0], "remote1.txt"},
        {test_files_[1], "remote2.txt"}
    };

    auto result = client.upload_files(files);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(BatchTransferTest, UploadFiles_EmptyFileList) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    // Connect first
    (void)client.connect({"localhost", 8080});

    std::vector<upload_entry> files;
    auto result = client.upload_files(files);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::invalid_file_path);
}

TEST_F(BatchTransferTest, UploadFiles_FileNotFound) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    (void)client.connect({"localhost", 8080});

    std::vector<upload_entry> files{
        {test_dir_ / "nonexistent.txt", "remote.txt"}
    };

    auto result = client.upload_files(files);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::file_not_found);
}

TEST_F(BatchTransferTest, UploadFiles_ValidBatch) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    (void)client.connect({"localhost", 8080});

    std::vector<upload_entry> files{
        {test_files_[0], "remote1.txt"},
        {test_files_[1], "remote2.txt"},
        {test_files_[2]}  // Use local filename
    };

    auto result = client.upload_files(files);
    EXPECT_TRUE(result.has_value());

    auto& handle = result.value();
    EXPECT_TRUE(handle.is_valid());
    EXPECT_EQ(handle.get_total_files(), 3u);
}

TEST_F(BatchTransferTest, UploadFiles_WithCustomOptions) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    (void)client.connect({"localhost", 8080});

    std::vector<upload_entry> files{
        {test_files_[0], "remote1.txt"},
        {test_files_[1], "remote2.txt"}
    };

    batch_options options;
    options.max_concurrent = 2;
    options.overwrite = true;
    options.compression = compression_mode::always;

    auto result = client.upload_files(files, options);
    EXPECT_TRUE(result.has_value());
}

// ============================================================================
// download_files Tests
// ============================================================================

TEST_F(BatchTransferTest, DownloadFiles_NotConnected) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    std::vector<download_entry> files{
        {"remote1.txt", test_dir_ / "local1.txt"},
        {"remote2.txt", test_dir_ / "local2.txt"}
    };

    auto result = client.download_files(files);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(BatchTransferTest, DownloadFiles_EmptyFileList) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    (void)client.connect({"localhost", 8080});

    std::vector<download_entry> files;
    auto result = client.download_files(files);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::invalid_file_path);
}

TEST_F(BatchTransferTest, DownloadFiles_ValidBatch) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    (void)client.connect({"localhost", 8080});

    auto download_dir = test_dir_ / "downloads";
    std::filesystem::create_directories(download_dir);

    std::vector<download_entry> files{
        {"remote1.txt", download_dir / "local1.txt"},
        {"remote2.txt", download_dir / "local2.txt"},
        {"remote3.txt", download_dir / "local3.txt"}
    };

    auto result = client.download_files(files);
    EXPECT_TRUE(result.has_value());

    auto& handle = result.value();
    EXPECT_TRUE(handle.is_valid());
    EXPECT_EQ(handle.get_total_files(), 3u);
}

// ============================================================================
// Batch Control Tests
// ============================================================================

TEST_F(BatchTransferTest, BatchControl_PauseBatchNotFound) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    auto result = client.pause_batch(999);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::transfer_not_found);
}

TEST_F(BatchTransferTest, BatchControl_ResumeBatchNotFound) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    auto result = client.resume_batch(999);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::transfer_not_found);
}

TEST_F(BatchTransferTest, BatchControl_CancelBatchNotFound) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    auto result = client.cancel_batch(999);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::transfer_not_found);
}

TEST_F(BatchTransferTest, BatchControl_WaitForBatchNotFound) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    auto result = client.wait_for_batch(999);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::transfer_not_found);
}

TEST_F(BatchTransferTest, BatchControl_GetProgressNotFound) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    auto progress = client.get_batch_progress(999);
    EXPECT_EQ(progress.total_files, 0u);
}

TEST_F(BatchTransferTest, BatchControl_PauseBatchValid) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    (void)client.connect({"localhost", 8080});

    std::vector<upload_entry> files{
        {test_files_[0], "remote1.txt"}
    };

    auto batch_result = client.upload_files(files);
    ASSERT_TRUE(batch_result.has_value());

    auto pause_result = batch_result.value().pause_all();
    EXPECT_TRUE(pause_result.has_value());
}

TEST_F(BatchTransferTest, BatchControl_CancelBatchValid) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    (void)client.connect({"localhost", 8080});

    std::vector<upload_entry> files{
        {test_files_[0], "remote1.txt"}
    };

    auto batch_result = client.upload_files(files);
    ASSERT_TRUE(batch_result.has_value());

    auto cancel_result = batch_result.value().cancel_all();
    EXPECT_TRUE(cancel_result.has_value());
}

// ============================================================================
// Batch Progress Tests
// ============================================================================

TEST_F(BatchTransferTest, BatchProgress_TrackProgress) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    (void)client.connect({"localhost", 8080});

    std::vector<upload_entry> files;
    for (const auto& path : test_files_) {
        files.emplace_back(path, path.filename().string());
    }

    auto batch_result = client.upload_files(files);
    ASSERT_TRUE(batch_result.has_value());

    auto& handle = batch_result.value();
    auto progress = handle.get_batch_progress();

    EXPECT_EQ(progress.total_files, test_files_.size());
    EXPECT_GE(progress.total_bytes, 0u);
}

TEST_F(BatchTransferTest, BatchProgress_GetIndividualHandles) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    (void)client.connect({"localhost", 8080});

    std::vector<upload_entry> files{
        {test_files_[0], "remote1.txt"},
        {test_files_[1], "remote2.txt"}
    };

    auto batch_result = client.upload_files(files);
    ASSERT_TRUE(batch_result.has_value());

    auto& handle = batch_result.value();
    auto individual_handles = handle.get_individual_handles();

    // Should have handles for successfully started transfers
    EXPECT_LE(individual_handles.size(), files.size());
}

// ============================================================================
// Handle Copy/Move Tests
// ============================================================================

TEST_F(BatchTransferTest, BatchTransferHandle_Copy) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    (void)client.connect({"localhost", 8080});

    std::vector<upload_entry> files{
        {test_files_[0], "remote1.txt"}
    };

    auto batch_result = client.upload_files(files);
    ASSERT_TRUE(batch_result.has_value());

    auto& handle1 = batch_result.value();
    batch_transfer_handle handle2 = handle1;  // Copy

    EXPECT_EQ(handle1.get_id(), handle2.get_id());
    EXPECT_TRUE(handle2.is_valid());
}

TEST_F(BatchTransferTest, BatchTransferHandle_Move) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    (void)client.connect({"localhost", 8080});

    std::vector<upload_entry> files{
        {test_files_[0], "remote1.txt"}
    };

    auto batch_result = client.upload_files(files);
    ASSERT_TRUE(batch_result.has_value());

    auto handle1_id = batch_result.value().get_id();
    batch_transfer_handle handle2 = std::move(batch_result.value());

    EXPECT_EQ(handle2.get_id(), handle1_id);
    EXPECT_TRUE(handle2.is_valid());
}

}  // namespace kcenon::file_transfer::test
