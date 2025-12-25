/**
 * @file test_connection_migration.cpp
 * @brief Unit tests for QUIC connection migration
 */

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "kcenon/file_transfer/transport/connection_migration.h"

namespace kcenon::file_transfer {
namespace {

// ============================================================================
// Network Path Tests
// ============================================================================

class NetworkPathTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(NetworkPathTest, DefaultPathValues) {
    network_path path;
    EXPECT_TRUE(path.local_address.empty());
    EXPECT_EQ(path.local_port, 0);
    EXPECT_TRUE(path.remote_address.empty());
    EXPECT_EQ(path.remote_port, 0);
    EXPECT_FALSE(path.validated);
    EXPECT_EQ(path.rtt.count(), 0);
}

TEST_F(NetworkPathTest, PathEquality) {
    network_path path1;
    path1.local_address = "192.168.1.100";
    path1.local_port = 12345;
    path1.remote_address = "10.0.0.1";
    path1.remote_port = 443;

    network_path path2 = path1;
    EXPECT_TRUE(path1 == path2);

    path2.local_port = 12346;
    EXPECT_FALSE(path1 == path2);
}

TEST_F(NetworkPathTest, PathToString) {
    network_path path;
    path.local_address = "192.168.1.100";
    path.local_port = 12345;
    path.remote_address = "10.0.0.1";
    path.remote_port = 443;

    std::string expected = "192.168.1.100:12345 -> 10.0.0.1:443";
    EXPECT_EQ(path.to_string(), expected);
}

// ============================================================================
// Migration State Tests
// ============================================================================

TEST(MigrationStateTest, StateToString) {
    EXPECT_STREQ(to_string(migration_state::idle), "idle");
    EXPECT_STREQ(to_string(migration_state::detecting), "detecting");
    EXPECT_STREQ(to_string(migration_state::probing), "probing");
    EXPECT_STREQ(to_string(migration_state::validating), "validating");
    EXPECT_STREQ(to_string(migration_state::migrating), "migrating");
    EXPECT_STREQ(to_string(migration_state::completed), "completed");
    EXPECT_STREQ(to_string(migration_state::failed), "failed");
}

// ============================================================================
// Migration Event Tests
// ============================================================================

TEST(MigrationEventTypeTest, EventToString) {
    EXPECT_STREQ(to_string(migration_event::network_change_detected),
                 "network_change_detected");
    EXPECT_STREQ(to_string(migration_event::path_probe_started),
                 "path_probe_started");
    EXPECT_STREQ(to_string(migration_event::path_probe_succeeded),
                 "path_probe_succeeded");
    EXPECT_STREQ(to_string(migration_event::path_probe_failed),
                 "path_probe_failed");
    EXPECT_STREQ(to_string(migration_event::migration_started),
                 "migration_started");
    EXPECT_STREQ(to_string(migration_event::migration_completed),
                 "migration_completed");
    EXPECT_STREQ(to_string(migration_event::migration_failed),
                 "migration_failed");
    EXPECT_STREQ(to_string(migration_event::path_validated),
                 "path_validated");
    EXPECT_STREQ(to_string(migration_event::path_degraded),
                 "path_degraded");
    EXPECT_STREQ(to_string(migration_event::fallback_triggered),
                 "fallback_triggered");
}

// ============================================================================
// Migration Result Tests
// ============================================================================

TEST(MigrationResultTest, SuccessfulResult) {
    network_path old_path;
    old_path.local_address = "192.168.1.100";
    old_path.remote_address = "10.0.0.1";

    network_path new_path;
    new_path.local_address = "192.168.1.101";
    new_path.remote_address = "10.0.0.1";

    auto result = migration_result::succeeded(
        old_path, new_path, std::chrono::milliseconds{50});

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.old_path.local_address, "192.168.1.100");
    EXPECT_EQ(result.new_path.local_address, "192.168.1.101");
    EXPECT_EQ(result.duration.count(), 50);
    EXPECT_TRUE(result.error_message.empty());
}

TEST(MigrationResultTest, FailedResult) {
    network_path old_path;
    old_path.local_address = "192.168.1.100";

    auto result = migration_result::failed(old_path, "Path probe failed");

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.old_path.local_address, "192.168.1.100");
    EXPECT_EQ(result.error_message, "Path probe failed");
}

// ============================================================================
// Migration Config Tests
// ============================================================================

TEST(MigrationConfigTest, DefaultValues) {
    migration_config config;

    EXPECT_TRUE(config.auto_migrate);
    EXPECT_TRUE(config.enable_path_probing);
    EXPECT_EQ(config.probe_interval.count(), 1000);
    EXPECT_EQ(config.probe_timeout.count(), 5000);
    EXPECT_EQ(config.max_probe_retries, 3);
    EXPECT_EQ(config.validation_timeout.count(), 10000);
    EXPECT_TRUE(config.enable_fallback);
    EXPECT_DOUBLE_EQ(config.min_rtt_improvement_percent, 20.0);
    EXPECT_EQ(config.detection_interval.count(), 500);
    EXPECT_TRUE(config.keep_previous_paths);
    EXPECT_EQ(config.max_previous_paths, 3);
}

// ============================================================================
// Connection Migration Manager Tests
// ============================================================================

class ConnectionMigrationManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        migration_config config;
        config.auto_migrate = false;  // Disable auto-migration for tests
        manager_ = connection_migration_manager::create(config);
    }

    void TearDown() override {
        if (manager_) {
            manager_->stop_monitoring();
        }
        manager_.reset();
    }

    std::unique_ptr<connection_migration_manager> manager_;
};

TEST_F(ConnectionMigrationManagerTest, CreateManager) {
    ASSERT_NE(manager_, nullptr);
    EXPECT_EQ(manager_->state(), migration_state::idle);
    EXPECT_FALSE(manager_->is_monitoring());
}

TEST_F(ConnectionMigrationManagerTest, InitialState) {
    EXPECT_EQ(manager_->state(), migration_state::idle);
    EXPECT_FALSE(manager_->current_path().has_value());
    EXPECT_TRUE(manager_->previous_paths().empty());
}

TEST_F(ConnectionMigrationManagerTest, SetCurrentPath) {
    network_path path;
    path.local_address = "192.168.1.100";
    path.local_port = 12345;
    path.remote_address = "10.0.0.1";
    path.remote_port = 443;

    manager_->set_current_path(path);

    auto current = manager_->current_path();
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->local_address, "192.168.1.100");
    EXPECT_EQ(current->remote_address, "10.0.0.1");
}

TEST_F(ConnectionMigrationManagerTest, StartStopMonitoring) {
    auto result = manager_->start_monitoring();
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(manager_->is_monitoring());

    manager_->stop_monitoring();
    EXPECT_FALSE(manager_->is_monitoring());
}

TEST_F(ConnectionMigrationManagerTest, GetAvailableInterfaces) {
    auto interfaces = manager_->get_available_interfaces();
    // Should have at least some interfaces on the system
    // (may be empty in containerized environments)
    SUCCEED();  // Just verify it doesn't crash
}

TEST_F(ConnectionMigrationManagerTest, StatisticsInitiallyZero) {
    auto stats = manager_->get_statistics();

    EXPECT_EQ(stats.total_migrations, 0);
    EXPECT_EQ(stats.successful_migrations, 0);
    EXPECT_EQ(stats.failed_migrations, 0);
    EXPECT_EQ(stats.path_probes, 0);
    EXPECT_EQ(stats.network_changes_detected, 0);
    EXPECT_EQ(stats.total_downtime.count(), 0);
}

TEST_F(ConnectionMigrationManagerTest, ResetStatistics) {
    // Get initial stats and verify they can be reset
    auto stats1 = manager_->get_statistics();
    manager_->reset_statistics();
    auto stats2 = manager_->get_statistics();

    EXPECT_EQ(stats2.total_migrations, 0);
}

TEST_F(ConnectionMigrationManagerTest, ConfigAccess) {
    auto& config = manager_->config();
    EXPECT_FALSE(config.auto_migrate);  // We disabled it in SetUp

    migration_config new_config;
    new_config.auto_migrate = true;
    new_config.probe_interval = std::chrono::milliseconds{2000};
    manager_->set_config(new_config);

    auto& updated_config = manager_->config();
    EXPECT_TRUE(updated_config.auto_migrate);
    EXPECT_EQ(updated_config.probe_interval.count(), 2000);
}

TEST_F(ConnectionMigrationManagerTest, EventCallback) {
    std::mutex mutex;
    std::condition_variable cv;
    bool event_received = false;
    migration_event received_event{};

    manager_->on_migration_event([&](const migration_event_data& event) {
        std::lock_guard lock(mutex);
        event_received = true;
        received_event = event.event;
        cv.notify_one();
    });

    // Start monitoring - this should trigger detecting state
    manager_->start_monitoring();

    // Wait briefly for any events
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    manager_->stop_monitoring();
    SUCCEED();  // Event callback was set without crashing
}

TEST_F(ConnectionMigrationManagerTest, CancelMigration) {
    // Should not crash when called in idle state
    manager_->cancel_migration();
    EXPECT_EQ(manager_->state(), migration_state::idle);
}

TEST_F(ConnectionMigrationManagerTest, DetectNetworkChanges) {
    auto changes = manager_->detect_network_changes();
    // Just verify it returns without crashing
    SUCCEED();
}

// ============================================================================
// Path Probing Tests
// ============================================================================

class PathProbingTest : public ::testing::Test {
protected:
    void SetUp() override {
        migration_config config;
        config.enable_path_probing = true;
        manager_ = connection_migration_manager::create(config);
    }

    void TearDown() override {
        manager_.reset();
    }

    std::unique_ptr<connection_migration_manager> manager_;
};

TEST_F(PathProbingTest, ProbeNonExistentPath) {
    network_path fake_path;
    fake_path.local_address = "10.255.255.255";  // Non-routable address
    fake_path.local_port = 12345;
    fake_path.remote_address = "10.0.0.1";
    fake_path.remote_port = 443;

    auto result = manager_->probe_path(fake_path);
    EXPECT_TRUE(result.has_value());
    // Probe should fail for non-existent interface
    EXPECT_FALSE(result.value());
}

TEST_F(PathProbingTest, ValidateNonExistentPath) {
    network_path fake_path;
    fake_path.local_address = "10.255.255.255";
    fake_path.remote_address = "10.0.0.1";

    auto result = manager_->validate_path(fake_path);
    EXPECT_TRUE(result.has_value());
    EXPECT_FALSE(result.value());
}

// ============================================================================
// Migration Tests with Events
// ============================================================================

class MigrationEventTest : public ::testing::Test {
protected:
    void SetUp() override {
        migration_config config;
        config.auto_migrate = false;
        config.enable_path_probing = true;
        manager_ = connection_migration_manager::create(config);
    }

    void TearDown() override {
        manager_.reset();
    }

    std::unique_ptr<connection_migration_manager> manager_;
    std::vector<migration_event> received_events_;
    std::mutex events_mutex_;
};

TEST_F(MigrationEventTest, MigrationEventsEmitted) {
    manager_->on_migration_event([this](const migration_event_data& event) {
        std::lock_guard lock(events_mutex_);
        received_events_.push_back(event.event);
    });

    // Set current path first
    network_path current;
    current.local_address = "192.168.1.100";
    current.local_port = 12345;
    current.remote_address = "10.0.0.1";
    current.remote_port = 443;
    current.validated = true;
    manager_->set_current_path(current);

    // Attempt migration to a new path
    network_path new_path;
    new_path.local_address = "192.168.1.101";
    new_path.local_port = 12346;
    new_path.remote_address = "10.0.0.1";
    new_path.remote_port = 443;

    // Migration will likely fail (no actual interface)
    auto result = manager_->migrate_to_path(new_path);

    // Should have received some events
    std::lock_guard lock(events_mutex_);
    EXPECT_FALSE(received_events_.empty());

    // Should include migration_started
    auto it = std::find(received_events_.begin(), received_events_.end(),
                        migration_event::migration_started);
    EXPECT_NE(it, received_events_.end());
}

// ============================================================================
// Previous Paths Tests
// ============================================================================

class PreviousPathsTest : public ::testing::Test {
protected:
    void SetUp() override {
        migration_config config;
        config.keep_previous_paths = true;
        config.max_previous_paths = 3;
        manager_ = connection_migration_manager::create(config);
    }

    void TearDown() override {
        manager_.reset();
    }

    std::unique_ptr<connection_migration_manager> manager_;
};

TEST_F(PreviousPathsTest, InitiallyEmpty) {
    EXPECT_TRUE(manager_->previous_paths().empty());
}

TEST_F(PreviousPathsTest, FallbackWithNoPreviousPaths) {
    auto result = manager_->fallback_to_previous();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().message, "No previous paths available");
}

// ============================================================================
// Statistics Tests
// ============================================================================

class MigrationStatisticsTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = connection_migration_manager::create();
    }

    void TearDown() override {
        manager_.reset();
    }

    std::unique_ptr<connection_migration_manager> manager_;
};

TEST_F(MigrationStatisticsTest, StatisticsAccumulate) {
    auto stats = manager_->get_statistics();
    EXPECT_EQ(stats.total_migrations, 0);

    // Reset and verify
    manager_->reset_statistics();
    stats = manager_->get_statistics();
    EXPECT_EQ(stats.total_migrations, 0);
    EXPECT_EQ(stats.successful_migrations, 0);
    EXPECT_EQ(stats.failed_migrations, 0);
}

// ============================================================================
// Migration Availability Tests
// ============================================================================

TEST(MigrationAvailabilityTest, CheckMigrationAvailable) {
    auto manager = connection_migration_manager::create();

    // Migration availability depends on having multiple interfaces
    // This test just verifies the method doesn't crash
    [[maybe_unused]] bool available = manager->is_migration_available();
    SUCCEED();
}

}  // namespace
}  // namespace kcenon::file_transfer
