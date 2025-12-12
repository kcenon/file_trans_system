/**
 * @file test_transfer_control.cpp
 * @brief Unit tests for transfer control (pause/resume/cancel)
 */

#include <gtest/gtest.h>

#include <kcenon/file_transfer/client/file_transfer_client.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace kcenon::file_transfer::test {

class TransferControlTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() /
                    ("file_trans_test_control_" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()));
        std::filesystem::create_directories(test_dir_);

        // Create test file
        test_file_ = test_dir_ / "test_upload.txt";
        std::ofstream file(test_file_);
        file << "Test file content for upload testing.";
        file.close();
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
    std::filesystem::path test_file_;
};

// ============================================================================
// transfer_status Tests
// ============================================================================

TEST_F(TransferControlTest, TransferStatus_ToString) {
    EXPECT_STREQ(to_string(transfer_status::pending), "pending");
    EXPECT_STREQ(to_string(transfer_status::in_progress), "in_progress");
    EXPECT_STREQ(to_string(transfer_status::paused), "paused");
    EXPECT_STREQ(to_string(transfer_status::completing), "completing");
    EXPECT_STREQ(to_string(transfer_status::completed), "completed");
    EXPECT_STREQ(to_string(transfer_status::failed), "failed");
    EXPECT_STREQ(to_string(transfer_status::cancelled), "cancelled");
}

TEST_F(TransferControlTest, TransferStatus_IsTerminal) {
    EXPECT_FALSE(is_terminal_status(transfer_status::pending));
    EXPECT_FALSE(is_terminal_status(transfer_status::in_progress));
    EXPECT_FALSE(is_terminal_status(transfer_status::paused));
    EXPECT_FALSE(is_terminal_status(transfer_status::completing));
    EXPECT_TRUE(is_terminal_status(transfer_status::completed));
    EXPECT_TRUE(is_terminal_status(transfer_status::failed));
    EXPECT_TRUE(is_terminal_status(transfer_status::cancelled));
}

// ============================================================================
// transfer_progress_info Tests
// ============================================================================

TEST_F(TransferControlTest, TransferProgressInfo_DefaultValues) {
    transfer_progress_info info;
    EXPECT_EQ(info.bytes_transferred, 0u);
    EXPECT_EQ(info.total_bytes, 0u);
    EXPECT_EQ(info.chunks_transferred, 0u);
    EXPECT_EQ(info.total_chunks, 0u);
    EXPECT_DOUBLE_EQ(info.transfer_rate, 0.0);
    EXPECT_EQ(info.elapsed.count(), 0);
}

TEST_F(TransferControlTest, TransferProgressInfo_CompletionPercentage) {
    transfer_progress_info info;
    info.total_bytes = 1000;
    info.bytes_transferred = 500;

    EXPECT_DOUBLE_EQ(info.completion_percentage(), 50.0);
}

TEST_F(TransferControlTest, TransferProgressInfo_CompletionPercentageZeroTotal) {
    transfer_progress_info info;
    info.total_bytes = 0;
    info.bytes_transferred = 100;

    EXPECT_DOUBLE_EQ(info.completion_percentage(), 0.0);
}

// ============================================================================
// transfer_result_info Tests
// ============================================================================

TEST_F(TransferControlTest, TransferResultInfo_DefaultValues) {
    transfer_result_info info;
    EXPECT_FALSE(info.success);
    EXPECT_EQ(info.bytes_transferred, 0u);
    EXPECT_EQ(info.elapsed.count(), 0);
    EXPECT_FALSE(info.error_message.has_value());
}

// ============================================================================
// transfer_handle with client Tests
// ============================================================================

TEST_F(TransferControlTest, TransferHandle_InvalidOperationsWithoutClient) {
    transfer_handle handle(123, nullptr);

    // All operations should fail with invalid handle
    auto pause_result = handle.pause();
    EXPECT_FALSE(pause_result.has_value());
    EXPECT_EQ(pause_result.error().code, error_code::not_initialized);

    auto resume_result = handle.resume();
    EXPECT_FALSE(resume_result.has_value());
    EXPECT_EQ(resume_result.error().code, error_code::not_initialized);

    auto cancel_result = handle.cancel();
    EXPECT_FALSE(cancel_result.has_value());
    EXPECT_EQ(cancel_result.error().code, error_code::not_initialized);

    auto wait_result = handle.wait_for(std::chrono::milliseconds(1));
    EXPECT_FALSE(wait_result.has_value());
    EXPECT_EQ(wait_result.error().code, error_code::not_initialized);
}

TEST_F(TransferControlTest, TransferHandle_GetStatusWithNullClient) {
    transfer_handle handle(123, nullptr);
    auto status = handle.get_status();
    EXPECT_EQ(status, transfer_status::failed);
}

TEST_F(TransferControlTest, TransferHandle_GetProgressWithNullClient) {
    transfer_handle handle(123, nullptr);
    auto progress = handle.get_progress();
    EXPECT_EQ(progress.bytes_transferred, 0u);
    EXPECT_EQ(progress.total_bytes, 0u);
}

// ============================================================================
// Client transfer control integration tests
// ============================================================================

TEST_F(TransferControlTest, Client_GetStatusForNonExistentTransfer) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    // Non-existent transfer should return failed status
    auto status = client.get_transfer_status(999);
    EXPECT_EQ(status, transfer_status::failed);
}

TEST_F(TransferControlTest, Client_GetProgressForNonExistentTransfer) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    // Non-existent transfer should return empty progress
    auto progress = client.get_transfer_progress(999);
    EXPECT_EQ(progress.bytes_transferred, 0u);
    EXPECT_EQ(progress.total_bytes, 0u);
}

TEST_F(TransferControlTest, Client_PauseNonExistentTransfer) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    auto result = client.pause_transfer(999);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::transfer_not_found);
}

TEST_F(TransferControlTest, Client_ResumeNonExistentTransfer) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    auto result = client.resume_transfer(999);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::transfer_not_found);
}

TEST_F(TransferControlTest, Client_CancelNonExistentTransfer) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    auto result = client.cancel_transfer(999);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::transfer_not_found);
}

TEST_F(TransferControlTest, Client_WaitForNonExistentTransfer) {
    auto client_result = create_client();
    ASSERT_TRUE(client_result.has_value());
    auto& client = client_result.value();

    auto result = client.wait_for_transfer(999, std::chrono::milliseconds(1));
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::transfer_not_found);
}

// ============================================================================
// Error code tests
// ============================================================================

TEST_F(TransferControlTest, ErrorCode_TransferControlCodes) {
    EXPECT_STREQ(to_string(error_code::invalid_state_transition),
                 "invalid state transition");
    EXPECT_STREQ(to_string(error_code::transfer_not_found),
                 "transfer not found");
    EXPECT_STREQ(to_string(error_code::transfer_already_completed),
                 "transfer already completed");
    EXPECT_STREQ(to_string(error_code::transfer_timeout),
                 "transfer timeout");
}

}  // namespace kcenon::file_transfer::test
