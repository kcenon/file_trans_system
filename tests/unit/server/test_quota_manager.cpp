/**
 * @file test_quota_manager.cpp
 * @brief Unit tests for quota manager
 */

#include <gtest/gtest.h>

#include <kcenon/file_transfer/server/quota_manager.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace kcenon::file_transfer::test {

class QuotaManagerTest : public ::testing::Test {
protected:
    static constexpr uint64_t MB = 1024 * 1024;
    static constexpr uint64_t KB = 1024;
    static constexpr uint64_t GB = 1024 * MB;

    void SetUp() override {
        // Create temporary test directory
        test_dir_ = std::filesystem::temp_directory_path() / "quota_manager_test";
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        // Clean up test directory
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    void create_test_file(const std::string& name, std::size_t size) {
        std::ofstream file(test_dir_ / name, std::ios::binary);
        std::vector<char> data(size, 'x');
        file.write(data.data(), static_cast<std::streamsize>(size));
    }

    std::filesystem::path test_dir_;
};

// Basic construction tests

TEST_F(QuotaManagerTest, Create_WithValidPath) {
    auto result = quota_manager::create(test_dir_, 100 * MB);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().get_total_quota(), 100 * MB);
}

TEST_F(QuotaManagerTest, Create_WithZeroQuotaMeansUnlimited) {
    auto result = quota_manager::create(test_dir_, 0);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().get_total_quota(), 0);
}

TEST_F(QuotaManagerTest, Create_CreatesDirectoryIfNotExists) {
    auto new_dir = test_dir_ / "new_subdir";
    ASSERT_FALSE(std::filesystem::exists(new_dir));

    auto result = quota_manager::create(new_dir, 100 * MB);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::filesystem::exists(new_dir));
}

TEST_F(QuotaManagerTest, Create_FailsForInvalidPath) {
    // Try to create in a file path instead of directory
    create_test_file("not_a_dir", 100);
    auto file_path = test_dir_ / "not_a_dir";

    auto result = quota_manager::create(file_path, 100 * MB);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::invalid_file_path);
}

// Quota configuration tests

TEST_F(QuotaManagerTest, SetTotalQuota_UpdatesQuota) {
    auto result = quota_manager::create(test_dir_, 100 * MB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    manager.set_total_quota(200 * MB);
    EXPECT_EQ(manager.get_total_quota(), 200 * MB);
}

TEST_F(QuotaManagerTest, SetMaxFileSize_UpdatesLimit) {
    auto result = quota_manager::create(test_dir_, 100 * MB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    manager.set_max_file_size(10 * MB);
    EXPECT_EQ(manager.get_max_file_size(), 10 * MB);
}

// Usage tracking tests

TEST_F(QuotaManagerTest, GetUsage_EmptyDirectory) {
    auto result = quota_manager::create(test_dir_, 100 * MB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    auto usage = manager.get_usage();
    EXPECT_EQ(usage.total_quota, 100 * MB);
    EXPECT_EQ(usage.used_bytes, 0);
    EXPECT_EQ(usage.available_bytes, 100 * MB);
    EXPECT_DOUBLE_EQ(usage.usage_percent, 0.0);
    EXPECT_EQ(usage.file_count, 0);
}

TEST_F(QuotaManagerTest, GetUsage_WithFiles) {
    // Create some test files first
    create_test_file("file1.txt", 10 * KB);
    create_test_file("file2.txt", 20 * KB);

    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    auto usage = manager.get_usage();
    EXPECT_EQ(usage.total_quota, 100 * KB);
    EXPECT_EQ(usage.used_bytes, 30 * KB);
    EXPECT_EQ(usage.available_bytes, 70 * KB);
    EXPECT_DOUBLE_EQ(usage.usage_percent, 30.0);
    EXPECT_EQ(usage.file_count, 2);
}

TEST_F(QuotaManagerTest, RecordBytesAdded_UpdatesUsage) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    manager.record_bytes_added(30 * KB);
    auto usage = manager.get_usage();
    EXPECT_EQ(usage.used_bytes, 30 * KB);
    EXPECT_DOUBLE_EQ(usage.usage_percent, 30.0);
}

TEST_F(QuotaManagerTest, RecordBytesRemoved_UpdatesUsage) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    manager.record_bytes_added(50 * KB);
    manager.record_bytes_removed(20 * KB);

    auto usage = manager.get_usage();
    EXPECT_EQ(usage.used_bytes, 30 * KB);
}

TEST_F(QuotaManagerTest, RecordFileCount_UpdatesUsage) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    manager.record_file_added();
    manager.record_file_added();
    EXPECT_EQ(manager.get_usage().file_count, 2);

    manager.record_file_removed();
    EXPECT_EQ(manager.get_usage().file_count, 1);
}

// Quota check tests

TEST_F(QuotaManagerTest, CheckQuota_SucceedsWhenEnoughSpace) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    auto check_result = manager.check_quota(50 * KB);
    EXPECT_TRUE(check_result.has_value());
}

TEST_F(QuotaManagerTest, CheckQuota_FailsWhenNotEnoughSpace) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    manager.record_bytes_added(80 * KB);
    auto check_result = manager.check_quota(30 * KB);

    EXPECT_FALSE(check_result.has_value());
    EXPECT_EQ(check_result.error().code, error_code::quota_exceeded);
}

TEST_F(QuotaManagerTest, CheckQuota_SucceedsWithUnlimitedQuota) {
    auto result = quota_manager::create(test_dir_, 0);  // Unlimited
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    auto check_result = manager.check_quota(1 * GB);
    EXPECT_TRUE(check_result.has_value());
}

TEST_F(QuotaManagerTest, CheckFileSize_SucceedsWhenWithinLimit) {
    auto result = quota_manager::create(test_dir_, 100 * MB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    manager.set_max_file_size(10 * MB);
    auto check_result = manager.check_file_size(5 * MB);
    EXPECT_TRUE(check_result.has_value());
}

TEST_F(QuotaManagerTest, CheckFileSize_FailsWhenExceedsLimit) {
    auto result = quota_manager::create(test_dir_, 100 * MB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    manager.set_max_file_size(10 * MB);
    auto check_result = manager.check_file_size(15 * MB);

    EXPECT_FALSE(check_result.has_value());
    EXPECT_EQ(check_result.error().code, error_code::file_too_large);
}

TEST_F(QuotaManagerTest, CheckFileSize_SucceedsWithNoLimit) {
    auto result = quota_manager::create(test_dir_, 100 * MB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    // max_file_size defaults to 0 (no limit)
    auto check_result = manager.check_file_size(1 * GB);
    EXPECT_TRUE(check_result.has_value());
}

// Warning threshold tests

TEST_F(QuotaManagerTest, SetWarningThresholds_UpdatesThresholds) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    manager.set_warning_thresholds({70.0, 85.0, 95.0});
    auto thresholds = manager.get_warning_thresholds();

    ASSERT_EQ(thresholds.size(), 3);
    EXPECT_DOUBLE_EQ(thresholds[0].percentage, 70.0);
    EXPECT_DOUBLE_EQ(thresholds[1].percentage, 85.0);
    EXPECT_DOUBLE_EQ(thresholds[2].percentage, 95.0);
}

TEST_F(QuotaManagerTest, WarningCallback_InvokedWhenThresholdReached) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    manager.set_warning_thresholds({50.0});

    std::atomic<bool> callback_invoked{false};
    manager.on_quota_warning([&callback_invoked](const quota_usage&) {
        callback_invoked = true;
    });

    // Add bytes to trigger threshold
    manager.record_bytes_added(60 * KB);  // 60% usage

    EXPECT_TRUE(callback_invoked);
}

TEST_F(QuotaManagerTest, WarningCallback_NotInvokedBelowThreshold) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    manager.set_warning_thresholds({80.0});

    std::atomic<int> callback_count{0};
    manager.on_quota_warning([&callback_count](const quota_usage&) {
        callback_count++;
    });

    // Add bytes but stay below threshold
    manager.record_bytes_added(50 * KB);  // 50% usage

    EXPECT_EQ(callback_count, 0);
}

TEST_F(QuotaManagerTest, WarningCallback_OnlyTriggeredOnce) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    manager.set_warning_thresholds({50.0});

    std::atomic<int> callback_count{0};
    manager.on_quota_warning([&callback_count](const quota_usage&) {
        callback_count++;
    });

    // Trigger threshold multiple times
    manager.record_bytes_added(60 * KB);
    manager.record_bytes_added(10 * KB);
    manager.record_bytes_added(5 * KB);

    // Should only trigger once
    EXPECT_EQ(callback_count, 1);
}

TEST_F(QuotaManagerTest, ResetThresholdTriggers_AllowsReTriggering) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    manager.set_warning_thresholds({50.0});

    std::atomic<int> callback_count{0};
    manager.on_quota_warning([&callback_count](const quota_usage&) {
        callback_count++;
    });

    manager.record_bytes_added(60 * KB);
    EXPECT_EQ(callback_count, 1);

    manager.reset_threshold_triggers();
    manager.record_bytes_added(1 * KB);  // Still above threshold

    EXPECT_EQ(callback_count, 2);
}

// Quota exceeded callback tests

TEST_F(QuotaManagerTest, ExceededCallback_InvokedWhenQuotaExceeded) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    std::atomic<bool> callback_invoked{false};
    manager.on_quota_exceeded([&callback_invoked](const quota_usage&) {
        callback_invoked = true;
    });

    // Exceed quota
    manager.record_bytes_added(110 * KB);

    EXPECT_TRUE(callback_invoked);
}

// Cleanup policy tests

TEST_F(QuotaManagerTest, SetCleanupPolicy_UpdatesPolicy) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    cleanup_policy policy;
    policy.enabled = true;
    policy.trigger_threshold = 85.0;
    policy.target_threshold = 70.0;
    policy.delete_oldest_first = true;

    manager.set_cleanup_policy(policy);

    auto retrieved = manager.get_cleanup_policy();
    EXPECT_TRUE(retrieved.enabled);
    EXPECT_DOUBLE_EQ(retrieved.trigger_threshold, 85.0);
    EXPECT_DOUBLE_EQ(retrieved.target_threshold, 70.0);
}

TEST_F(QuotaManagerTest, ShouldCleanup_ReturnsTrueWhenAboveThreshold) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    cleanup_policy policy;
    policy.enabled = true;
    policy.trigger_threshold = 80.0;
    manager.set_cleanup_policy(policy);

    manager.record_bytes_added(85 * KB);  // 85% usage

    EXPECT_TRUE(manager.should_cleanup());
}

TEST_F(QuotaManagerTest, ShouldCleanup_ReturnsFalseWhenBelowThreshold) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    cleanup_policy policy;
    policy.enabled = true;
    policy.trigger_threshold = 80.0;
    manager.set_cleanup_policy(policy);

    manager.record_bytes_added(50 * KB);  // 50% usage

    EXPECT_FALSE(manager.should_cleanup());
}

TEST_F(QuotaManagerTest, ShouldCleanup_ReturnsFalseWhenDisabled) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    cleanup_policy policy;
    policy.enabled = false;
    policy.trigger_threshold = 50.0;
    manager.set_cleanup_policy(policy);

    manager.record_bytes_added(85 * KB);  // Above threshold but disabled

    EXPECT_FALSE(manager.should_cleanup());
}

TEST_F(QuotaManagerTest, ExecuteCleanup_DeletesOldestFiles) {
    // Create test files with different timestamps
    create_test_file("old_file.txt", 10 * KB);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    create_test_file("new_file.txt", 10 * KB);

    // Total: 20KB used, Quota: 25KB, so usage is 80%
    auto result = quota_manager::create(test_dir_, 25 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    // Set cleanup policy: trigger at 70%, target at 50% (12.5KB)
    cleanup_policy policy;
    policy.enabled = true;
    policy.trigger_threshold = 70.0;
    policy.target_threshold = 50.0;  // Target: 12.5KB used
    policy.delete_oldest_first = true;
    manager.set_cleanup_policy(policy);

    // Execute cleanup - should delete old_file.txt (10KB) to get below 50%
    auto bytes_freed = manager.execute_cleanup();

    // Should have deleted old_file.txt
    EXPECT_GT(bytes_freed, 0);
    EXPECT_FALSE(std::filesystem::exists(test_dir_ / "old_file.txt"));
    EXPECT_TRUE(std::filesystem::exists(test_dir_ / "new_file.txt"));
}

TEST_F(QuotaManagerTest, ExecuteCleanup_RespectsExclusions) {
    create_test_file("important.txt", 30 * KB);
    create_test_file("deletable.txt", 30 * KB);

    auto result = quota_manager::create(test_dir_, 50 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    cleanup_policy policy;
    policy.enabled = true;
    policy.trigger_threshold = 50.0;
    policy.target_threshold = 30.0;
    policy.exclusions = {"important"};
    manager.set_cleanup_policy(policy);

    manager.execute_cleanup();

    // important.txt should still exist
    EXPECT_TRUE(std::filesystem::exists(test_dir_ / "important.txt"));
}

// quota_usage struct tests

TEST_F(QuotaManagerTest, QuotaUsage_IsExceeded_TrueWhenOver) {
    quota_usage usage;
    usage.total_quota = 100 * KB;
    usage.used_bytes = 110 * KB;

    EXPECT_TRUE(usage.is_exceeded());
}

TEST_F(QuotaManagerTest, QuotaUsage_IsExceeded_FalseWhenUnder) {
    quota_usage usage;
    usage.total_quota = 100 * KB;
    usage.used_bytes = 50 * KB;

    EXPECT_FALSE(usage.is_exceeded());
}

TEST_F(QuotaManagerTest, QuotaUsage_IsExceeded_FalseWhenUnlimited) {
    quota_usage usage;
    usage.total_quota = 0;  // Unlimited
    usage.used_bytes = 1 * GB;

    EXPECT_FALSE(usage.is_exceeded());
}

TEST_F(QuotaManagerTest, QuotaUsage_IsThresholdReached) {
    quota_usage usage;
    usage.usage_percent = 85.0;

    EXPECT_TRUE(usage.is_threshold_reached(80.0));
    EXPECT_TRUE(usage.is_threshold_reached(85.0));
    EXPECT_FALSE(usage.is_threshold_reached(90.0));
}

// Available space tests

TEST_F(QuotaManagerTest, GetAvailableSpace_ReturnsCorrectValue) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    EXPECT_EQ(manager.get_available_space(), 100 * KB);

    manager.record_bytes_added(30 * KB);
    EXPECT_EQ(manager.get_available_space(), 70 * KB);
}

// Storage path tests

TEST_F(QuotaManagerTest, StoragePath_ReturnsCorrectPath) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    EXPECT_EQ(manager.storage_path(), test_dir_);
}

// Move semantics tests

TEST_F(QuotaManagerTest, MoveConstruction_TransfersState) {
    auto result = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result.has_value());

    auto& manager1 = result.value();
    manager1.record_bytes_added(30 * KB);

    quota_manager manager2(std::move(manager1));

    EXPECT_EQ(manager2.get_total_quota(), 100 * KB);
    EXPECT_EQ(manager2.get_usage().used_bytes, 30 * KB);
}

TEST_F(QuotaManagerTest, MoveAssignment_TransfersState) {
    auto result1 = quota_manager::create(test_dir_, 100 * KB);
    ASSERT_TRUE(result1.has_value());

    auto new_dir = test_dir_ / "other";
    std::filesystem::create_directories(new_dir);
    auto result2 = quota_manager::create(new_dir, 50 * KB);
    ASSERT_TRUE(result2.has_value());

    result2.value() = std::move(result1.value());

    EXPECT_EQ(result2.value().get_total_quota(), 100 * KB);
}

// Thread safety tests

TEST_F(QuotaManagerTest, ThreadSafety_ConcurrentRecording) {
    auto result = quota_manager::create(test_dir_, 100 * MB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    std::vector<std::thread> threads;
    constexpr int num_threads = 4;
    constexpr int iterations = 1000;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&manager] {
            for (int j = 0; j < iterations; ++j) {
                manager.record_bytes_added(1 * KB);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto usage = manager.get_usage();
    EXPECT_EQ(usage.used_bytes, num_threads * iterations * 1 * KB);
}

TEST_F(QuotaManagerTest, ThreadSafety_ConcurrentReads) {
    auto result = quota_manager::create(test_dir_, 100 * MB);
    ASSERT_TRUE(result.has_value());
    auto& manager = result.value();

    manager.record_bytes_added(50 * MB);

    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&manager, &stop] {
            while (!stop) {
                auto usage = manager.get_usage();
                (void)usage;  // Suppress unused warning
            }
        });
    }

    // Let threads run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop = true;

    for (auto& t : threads) {
        t.join();
    }

    // If we get here without crash, thread safety is working
    SUCCEED();
}

}  // namespace kcenon::file_transfer::test
