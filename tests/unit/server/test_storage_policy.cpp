/**
 * @file test_storage_policy.cpp
 * @brief Unit tests for storage policy
 */

#include <gtest/gtest.h>

#include <kcenon/file_transfer/server/storage_policy.h>
#include <kcenon/file_transfer/server/storage_manager.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace kcenon::file_transfer::test {

class StoragePolicyTest : public ::testing::Test {
protected:
    static constexpr uint64_t MB = 1024 * 1024;
    static constexpr uint64_t KB = 1024;

    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "storage_policy_test";
        std::filesystem::create_directories(test_dir_);

        // Create storage manager for testing
        auto backend = local_storage_backend::create(test_dir_);

        storage_manager_config config;
        config.primary_backend = std::move(backend);
        config.track_access = true;

        manager_ = storage_manager::create(config);
        manager_->initialize();
    }

    void TearDown() override {
        if (manager_) {
            manager_->shutdown();
        }
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    auto create_test_data(std::size_t size) -> std::vector<std::byte> {
        std::vector<std::byte> data(size);
        for (std::size_t i = 0; i < size; ++i) {
            data[i] = static_cast<std::byte>(i % 256);
        }
        return data;
    }

    std::filesystem::path test_dir_;
    std::unique_ptr<storage_manager> manager_;
};

// ============================================================================
// Builder tests
// ============================================================================

TEST_F(StoragePolicyTest, Builder_CreateEmptyPolicy) {
    auto policy = storage_policy::builder().build();
    ASSERT_NE(policy, nullptr);
    EXPECT_FALSE(policy->has_access_pattern_tiering());
    EXPECT_FALSE(policy->has_age_tiering());
    EXPECT_FALSE(policy->has_size_tiering());
}

TEST_F(StoragePolicyTest, Builder_WithAgeTiering) {
    age_tiering_config config;
    config.hot_to_warm_age = std::chrono::hours{24 * 7};

    auto policy = storage_policy::builder()
        .with_age_tiering(config)
        .build();

    ASSERT_NE(policy, nullptr);
    EXPECT_TRUE(policy->has_age_tiering());
}

TEST_F(StoragePolicyTest, Builder_WithSizeTiering) {
    size_tiering_config config;
    config.hot_max_size = 1 * MB;
    config.warm_max_size = 10 * MB;

    auto policy = storage_policy::builder()
        .with_size_tiering(config)
        .build();

    ASSERT_NE(policy, nullptr);
    EXPECT_TRUE(policy->has_size_tiering());
}

TEST_F(StoragePolicyTest, Builder_WithAccessPatternTiering) {
    access_pattern_config config;
    config.hot_min_access_count = 5;

    auto policy = storage_policy::builder()
        .with_access_pattern_tiering(config)
        .build();

    ASSERT_NE(policy, nullptr);
    EXPECT_TRUE(policy->has_access_pattern_tiering());
}

TEST_F(StoragePolicyTest, Builder_WithRule) {
    tiering_rule rule;
    rule.name = "archive_old";
    rule.trigger = tiering_trigger::age;
    rule.min_age = std::chrono::hours{24 * 30};
    rule.target_tier = storage_tier::archive;

    auto policy = storage_policy::builder()
        .with_rule(rule)
        .build();

    ASSERT_NE(policy, nullptr);
    EXPECT_EQ(policy->rules().size(), 1);
    EXPECT_EQ(policy->rules()[0].name, "archive_old");
}

TEST_F(StoragePolicyTest, Builder_WithMultipleRules) {
    tiering_rule rule1;
    rule1.name = "rule1";
    rule1.priority = 10;

    tiering_rule rule2;
    rule2.name = "rule2";
    rule2.priority = 20;

    auto policy = storage_policy::builder()
        .with_rule(rule1)
        .with_rule(rule2)
        .build();

    ASSERT_NE(policy, nullptr);
    EXPECT_EQ(policy->rules().size(), 2);
    // Rules should be sorted by priority (highest first)
    EXPECT_EQ(policy->rules()[0].name, "rule2");
    EXPECT_EQ(policy->rules()[1].name, "rule1");
}

TEST_F(StoragePolicyTest, Builder_WithRetention) {
    retention_policy retention;
    retention.min_retention = std::chrono::hours{24 * 30};
    retention.legal_hold = false;

    auto policy = storage_policy::builder()
        .with_retention(retention)
        .build();

    ASSERT_NE(policy, nullptr);
    EXPECT_EQ(policy->retention().min_retention.count(), std::chrono::hours{24 * 30}.count());
}

TEST_F(StoragePolicyTest, Builder_WithDryRun) {
    auto policy = storage_policy::builder()
        .with_dry_run(true)
        .build();

    ASSERT_NE(policy, nullptr);
    EXPECT_TRUE(policy->is_dry_run());
}

// ============================================================================
// Attachment tests
// ============================================================================

TEST_F(StoragePolicyTest, AttachDetach) {
    auto policy = storage_policy::builder().build();

    EXPECT_FALSE(policy->is_attached());

    policy->attach(*manager_);
    EXPECT_TRUE(policy->is_attached());

    policy->detach();
    EXPECT_FALSE(policy->is_attached());
}

TEST_F(StoragePolicyTest, EvaluateWithoutAttach) {
    auto policy = storage_policy::builder().build();

    auto result = policy->evaluate("test.txt");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

// ============================================================================
// Evaluation tests
// ============================================================================

TEST_F(StoragePolicyTest, EvaluateNoRulesMatch) {
    auto policy = storage_policy::builder().build();
    policy->attach(*manager_);

    // Store a file
    manager_->store("test.txt", create_test_data(100));

    auto result = policy->evaluate("test.txt");
    ASSERT_TRUE(result.has_value());

    // No rules, so no tier change recommended
    EXPECT_EQ(result.value().current_tier, result.value().target_tier);
}

TEST_F(StoragePolicyTest, EvaluateSizeRule) {
    // Rule: files larger than 100KB should go to warm tier
    tiering_rule rule;
    rule.name = "large_to_warm";
    rule.trigger = tiering_trigger::size;
    rule.min_size = 100 * KB;
    rule.target_tier = storage_tier::warm;
    rule.action = tiering_action::move;

    auto policy = storage_policy::builder()
        .with_rule(rule)
        .build();
    policy->attach(*manager_);

    // Store a large file
    manager_->store("large.bin", create_test_data(150 * KB));

    auto result = policy->evaluate("large.bin");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().matched_rule, "large_to_warm");
    EXPECT_EQ(result.value().target_tier, storage_tier::warm);
}

TEST_F(StoragePolicyTest, EvaluateKeyPattern) {
    // Rule: files matching "logs/*" pattern
    tiering_rule rule;
    rule.name = "archive_logs";
    rule.trigger = tiering_trigger::age;
    rule.key_pattern = "logs/*";
    rule.target_tier = storage_tier::archive;
    rule.min_age = std::chrono::hours{0};  // Immediate for testing

    auto policy = storage_policy::builder()
        .with_rule(rule)
        .build();
    policy->attach(*manager_);

    // Store files
    std::filesystem::create_directories(test_dir_ / "logs");
    manager_->store("logs/app.log", create_test_data(100));
    manager_->store("data.txt", create_test_data(100));

    // Log file should match
    auto log_result = policy->evaluate("logs/app.log");
    ASSERT_TRUE(log_result.has_value());
    EXPECT_EQ(log_result.value().matched_rule, "archive_logs");

    // Data file should not match
    auto data_result = policy->evaluate("data.txt");
    ASSERT_TRUE(data_result.has_value());
    EXPECT_TRUE(data_result.value().matched_rule.empty());
}

TEST_F(StoragePolicyTest, EvaluateAll) {
    tiering_rule rule;
    rule.name = "size_rule";
    rule.trigger = tiering_trigger::size;
    rule.min_size = 500;
    rule.target_tier = storage_tier::warm;

    auto policy = storage_policy::builder()
        .with_rule(rule)
        .build();
    policy->attach(*manager_);

    // Store files of different sizes
    manager_->store("small.txt", create_test_data(100));
    manager_->store("large.txt", create_test_data(1000));

    auto results = policy->evaluate_all();
    ASSERT_TRUE(results.has_value());
    EXPECT_EQ(results.value().size(), 2);

    // Count matched rules
    int matched = 0;
    for (const auto& r : results.value()) {
        if (!r.matched_rule.empty()) matched++;
    }
    EXPECT_EQ(matched, 1);  // Only large.txt should match
}

// ============================================================================
// Rule management tests
// ============================================================================

TEST_F(StoragePolicyTest, AddRule) {
    auto policy = storage_policy::builder().build();

    tiering_rule rule;
    rule.name = "dynamic_rule";
    rule.priority = 100;

    policy->add_rule(rule);

    EXPECT_EQ(policy->rules().size(), 1);
    EXPECT_EQ(policy->rules()[0].name, "dynamic_rule");
}

TEST_F(StoragePolicyTest, RemoveRule) {
    tiering_rule rule;
    rule.name = "to_remove";

    auto policy = storage_policy::builder()
        .with_rule(rule)
        .build();

    EXPECT_EQ(policy->rules().size(), 1);

    bool removed = policy->remove_rule("to_remove");
    EXPECT_TRUE(removed);
    EXPECT_EQ(policy->rules().size(), 0);
}

TEST_F(StoragePolicyTest, RemoveNonexistentRule) {
    auto policy = storage_policy::builder().build();

    bool removed = policy->remove_rule("nonexistent");
    EXPECT_FALSE(removed);
}

TEST_F(StoragePolicyTest, SetRuleEnabled) {
    tiering_rule rule;
    rule.name = "toggleable";
    rule.enabled = true;

    auto policy = storage_policy::builder()
        .with_rule(rule)
        .build();

    EXPECT_TRUE(policy->rules()[0].enabled);

    policy->set_rule_enabled("toggleable", false);
    EXPECT_FALSE(policy->rules()[0].enabled);

    policy->set_rule_enabled("toggleable", true);
    EXPECT_TRUE(policy->rules()[0].enabled);
}

// ============================================================================
// Retention tests
// ============================================================================

TEST_F(StoragePolicyTest, RetentionBlocksDelete) {
    retention_policy retention;
    retention.min_retention = std::chrono::hours{24 * 365};  // 1 year

    auto policy = storage_policy::builder()
        .with_retention(retention)
        .build();
    policy->attach(*manager_);

    // Store a file (just now, so not old enough)
    manager_->store("protected.txt", create_test_data(100));

    auto can_delete = policy->can_delete("protected.txt");
    ASSERT_TRUE(can_delete.has_value());
    EXPECT_FALSE(can_delete.value());  // Can't delete, retention not met
}

TEST_F(StoragePolicyTest, LegalHoldBlocksAll) {
    retention_policy retention;
    retention.legal_hold = true;

    auto policy = storage_policy::builder()
        .with_retention(retention)
        .build();
    policy->attach(*manager_);

    manager_->store("held.txt", create_test_data(100));

    auto can_delete = policy->can_delete("held.txt");
    ASSERT_TRUE(can_delete.has_value());
    EXPECT_FALSE(can_delete.value());

    auto can_modify = policy->can_modify("held.txt");
    ASSERT_TRUE(can_modify.has_value());
    EXPECT_FALSE(can_modify.value());
}

TEST_F(StoragePolicyTest, RetentionExclusions) {
    retention_policy retention;
    retention.legal_hold = true;
    retention.exclusions = {"temp/*"};

    auto policy = storage_policy::builder()
        .with_retention(retention)
        .build();
    policy->attach(*manager_);

    std::filesystem::create_directories(test_dir_ / "temp");
    manager_->store("temp/cache.txt", create_test_data(100));

    // Should be excluded from retention
    auto can_delete = policy->can_delete("temp/cache.txt");
    ASSERT_TRUE(can_delete.has_value());
    EXPECT_TRUE(can_delete.value());
}

// ============================================================================
// Execution tests
// ============================================================================

TEST_F(StoragePolicyTest, ExecuteDryRun) {
    tiering_rule rule;
    rule.name = "test_rule";
    rule.trigger = tiering_trigger::size;
    rule.min_size = 50;
    rule.target_tier = storage_tier::cold;

    auto policy = storage_policy::builder()
        .with_rule(rule)
        .with_dry_run(true)
        .build();
    policy->attach(*manager_);

    manager_->store("dry_run.txt", create_test_data(100));

    bool action_called = false;
    policy->on_action([&action_called](const std::string&, tiering_action,
                                       storage_tier, storage_tier) {
        action_called = true;
    });

    auto result = policy->execute("dry_run.txt");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(action_called);  // Action callback should still be called

    // But file should still exist (dry run)
    auto exists = manager_->exists("dry_run.txt");
    EXPECT_TRUE(exists.value());
}

// ============================================================================
// Statistics tests
// ============================================================================

TEST_F(StoragePolicyTest, Statistics) {
    auto policy = storage_policy::builder().build();
    policy->attach(*manager_);

    manager_->store("stats_test.txt", create_test_data(100));

    auto initial_stats = policy->get_statistics();
    EXPECT_EQ(initial_stats.objects_evaluated, 0);

    policy->evaluate("stats_test.txt");

    auto stats = policy->get_statistics();
    EXPECT_EQ(stats.objects_evaluated, 1);
}

TEST_F(StoragePolicyTest, ResetStatistics) {
    auto policy = storage_policy::builder().build();
    policy->attach(*manager_);

    manager_->store("reset_test.txt", create_test_data(100));
    policy->evaluate("reset_test.txt");

    policy->reset_statistics();

    auto stats = policy->get_statistics();
    EXPECT_EQ(stats.objects_evaluated, 0);
}

// ============================================================================
// Callback tests
// ============================================================================

TEST_F(StoragePolicyTest, EvaluationCallback) {
    tiering_rule rule;
    rule.name = "callback_rule";
    rule.trigger = tiering_trigger::size;
    rule.min_size = 50;
    rule.target_tier = storage_tier::warm;

    auto policy = storage_policy::builder()
        .with_rule(rule)
        .build();
    policy->attach(*manager_);

    bool callback_called = false;
    std::string callback_key;

    policy->on_evaluation([&](const policy_evaluation_result& result) {
        callback_called = true;
        callback_key = result.key;
    });

    manager_->store("callback.txt", create_test_data(100));
    policy->evaluate("callback.txt");

    EXPECT_TRUE(callback_called);
    EXPECT_EQ(callback_key, "callback.txt");
}

TEST_F(StoragePolicyTest, DryRunModeToggle) {
    auto policy = storage_policy::builder()
        .with_dry_run(false)
        .build();

    EXPECT_FALSE(policy->is_dry_run());

    policy->set_dry_run(true);
    EXPECT_TRUE(policy->is_dry_run());

    policy->set_dry_run(false);
    EXPECT_FALSE(policy->is_dry_run());
}

}  // namespace kcenon::file_transfer::test
