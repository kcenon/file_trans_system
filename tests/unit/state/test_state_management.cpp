/**
 * @file test_state_management.cpp
 * @brief Unit tests for state management types (connection_state, server_state)
 */

#include <gtest/gtest.h>

#include <kcenon/file_transfer/client/client_types.h>
#include <kcenon/file_transfer/server/server_types.h>

#include <unordered_map>
#include <unordered_set>

namespace kcenon::file_transfer::test {

// =============================================================================
// connection_state Tests
// =============================================================================

class ConnectionStateTest : public ::testing::Test {};

TEST_F(ConnectionStateTest, ToString_Disconnected) {
    EXPECT_STREQ(to_string(connection_state::disconnected), "disconnected");
}

TEST_F(ConnectionStateTest, ToString_Connecting) {
    EXPECT_STREQ(to_string(connection_state::connecting), "connecting");
}

TEST_F(ConnectionStateTest, ToString_Connected) {
    EXPECT_STREQ(to_string(connection_state::connected), "connected");
}

TEST_F(ConnectionStateTest, ToString_Reconnecting) {
    EXPECT_STREQ(to_string(connection_state::reconnecting), "reconnecting");
}

TEST_F(ConnectionStateTest, ToString_Unknown) {
    auto unknown = static_cast<connection_state>(999);
    EXPECT_STREQ(to_string(unknown), "unknown");
}

TEST_F(ConnectionStateTest, AllStatesAreDifferent) {
    EXPECT_NE(connection_state::disconnected, connection_state::connecting);
    EXPECT_NE(connection_state::connecting, connection_state::connected);
    EXPECT_NE(connection_state::connected, connection_state::reconnecting);
    EXPECT_NE(connection_state::reconnecting, connection_state::disconnected);
}

TEST_F(ConnectionStateTest, StateTransitions_ValidScenarios) {
    // These are logical transition scenarios that should be valid
    // disconnected -> connecting -> connected -> disconnected
    connection_state state = connection_state::disconnected;

    // Start connection attempt
    state = connection_state::connecting;
    EXPECT_EQ(state, connection_state::connecting);

    // Connection established
    state = connection_state::connected;
    EXPECT_EQ(state, connection_state::connected);

    // Disconnect
    state = connection_state::disconnected;
    EXPECT_EQ(state, connection_state::disconnected);
}

TEST_F(ConnectionStateTest, StateTransitions_ReconnectScenario) {
    // connected -> reconnecting -> connected
    connection_state state = connection_state::connected;

    // Connection lost, attempting reconnect
    state = connection_state::reconnecting;
    EXPECT_EQ(state, connection_state::reconnecting);

    // Reconnection successful
    state = connection_state::connected;
    EXPECT_EQ(state, connection_state::connected);
}

TEST_F(ConnectionStateTest, StateTransitions_ReconnectFailure) {
    // connected -> reconnecting -> disconnected
    connection_state state = connection_state::connected;

    // Connection lost, attempting reconnect
    state = connection_state::reconnecting;
    EXPECT_EQ(state, connection_state::reconnecting);

    // Reconnection failed
    state = connection_state::disconnected;
    EXPECT_EQ(state, connection_state::disconnected);
}

// =============================================================================
// server_state Tests
// =============================================================================

class ServerStateTest : public ::testing::Test {};

TEST_F(ServerStateTest, ToString_Stopped) {
    EXPECT_STREQ(to_string(server_state::stopped), "stopped");
}

TEST_F(ServerStateTest, ToString_Starting) {
    EXPECT_STREQ(to_string(server_state::starting), "starting");
}

TEST_F(ServerStateTest, ToString_Running) {
    EXPECT_STREQ(to_string(server_state::running), "running");
}

TEST_F(ServerStateTest, ToString_Stopping) {
    EXPECT_STREQ(to_string(server_state::stopping), "stopping");
}

TEST_F(ServerStateTest, ToString_Unknown) {
    auto unknown = static_cast<server_state>(999);
    EXPECT_STREQ(to_string(unknown), "unknown");
}

TEST_F(ServerStateTest, AllStatesAreDifferent) {
    EXPECT_NE(server_state::stopped, server_state::starting);
    EXPECT_NE(server_state::starting, server_state::running);
    EXPECT_NE(server_state::running, server_state::stopping);
    EXPECT_NE(server_state::stopping, server_state::stopped);
}

TEST_F(ServerStateTest, StateTransitions_NormalStartup) {
    // stopped -> starting -> running
    server_state state = server_state::stopped;

    // Begin startup
    state = server_state::starting;
    EXPECT_EQ(state, server_state::starting);

    // Startup complete
    state = server_state::running;
    EXPECT_EQ(state, server_state::running);
}

TEST_F(ServerStateTest, StateTransitions_NormalShutdown) {
    // running -> stopping -> stopped
    server_state state = server_state::running;

    // Begin shutdown
    state = server_state::stopping;
    EXPECT_EQ(state, server_state::stopping);

    // Shutdown complete
    state = server_state::stopped;
    EXPECT_EQ(state, server_state::stopped);
}

TEST_F(ServerStateTest, StateTransitions_FullCycle) {
    // Full lifecycle: stopped -> starting -> running -> stopping -> stopped
    server_state state = server_state::stopped;

    state = server_state::starting;
    EXPECT_EQ(state, server_state::starting);

    state = server_state::running;
    EXPECT_EQ(state, server_state::running);

    state = server_state::stopping;
    EXPECT_EQ(state, server_state::stopping);

    state = server_state::stopped;
    EXPECT_EQ(state, server_state::stopped);
}

// =============================================================================
// reconnect_policy Tests
// =============================================================================

class ReconnectPolicyTest : public ::testing::Test {};

TEST_F(ReconnectPolicyTest, DefaultValues) {
    reconnect_policy policy;

    EXPECT_EQ(policy.max_attempts, 5);
    EXPECT_EQ(policy.initial_delay.count(), 1000);
    EXPECT_EQ(policy.max_delay.count(), 30000);
    EXPECT_DOUBLE_EQ(policy.backoff_multiplier, 2.0);
}

TEST_F(ReconnectPolicyTest, CustomValues) {
    reconnect_policy policy;
    policy.max_attempts = 10;
    policy.initial_delay = std::chrono::milliseconds{500};
    policy.max_delay = std::chrono::milliseconds{60000};
    policy.backoff_multiplier = 1.5;

    EXPECT_EQ(policy.max_attempts, 10);
    EXPECT_EQ(policy.initial_delay.count(), 500);
    EXPECT_EQ(policy.max_delay.count(), 60000);
    EXPECT_DOUBLE_EQ(policy.backoff_multiplier, 1.5);
}

TEST_F(ReconnectPolicyTest, BackoffCalculation) {
    reconnect_policy policy;
    policy.initial_delay = std::chrono::milliseconds{100};
    policy.backoff_multiplier = 2.0;

    // Simulate exponential backoff calculation
    auto delay = policy.initial_delay;
    EXPECT_EQ(delay.count(), 100);  // Attempt 1

    delay = std::chrono::milliseconds{
        static_cast<long>(delay.count() * policy.backoff_multiplier)};
    EXPECT_EQ(delay.count(), 200);  // Attempt 2

    delay = std::chrono::milliseconds{
        static_cast<long>(delay.count() * policy.backoff_multiplier)};
    EXPECT_EQ(delay.count(), 400);  // Attempt 3
}

// =============================================================================
// client_config Tests
// =============================================================================

class ClientConfigTest : public ::testing::Test {};

TEST_F(ClientConfigTest, DefaultValues) {
    client_config config;

    EXPECT_EQ(config.compression, compression_mode::adaptive);
    EXPECT_EQ(config.comp_level, compression_level::fast);
    EXPECT_EQ(config.chunk_size, 256 * 1024);
    EXPECT_TRUE(config.auto_reconnect);
    EXPECT_FALSE(config.upload_bandwidth_limit.has_value());
    EXPECT_FALSE(config.download_bandwidth_limit.has_value());
    EXPECT_EQ(config.connect_timeout.count(), 30000);
}

TEST_F(ClientConfigTest, CustomValues) {
    client_config config;
    config.compression = compression_mode::always;
    config.comp_level = compression_level::best;
    config.chunk_size = 128 * 1024;
    config.auto_reconnect = false;
    config.upload_bandwidth_limit = 1024 * 1024;
    config.download_bandwidth_limit = 2 * 1024 * 1024;
    config.connect_timeout = std::chrono::milliseconds{60000};

    EXPECT_EQ(config.compression, compression_mode::always);
    EXPECT_EQ(config.comp_level, compression_level::best);
    EXPECT_EQ(config.chunk_size, 128 * 1024);
    EXPECT_FALSE(config.auto_reconnect);
    EXPECT_TRUE(config.upload_bandwidth_limit.has_value());
    EXPECT_EQ(config.upload_bandwidth_limit.value(), 1024 * 1024);
    EXPECT_TRUE(config.download_bandwidth_limit.has_value());
    EXPECT_EQ(config.download_bandwidth_limit.value(), 2 * 1024 * 1024);
    EXPECT_EQ(config.connect_timeout.count(), 60000);
}

// =============================================================================
// server_config Tests
// =============================================================================

class ServerConfigTest : public ::testing::Test {};

TEST_F(ServerConfigTest, DefaultValues) {
    server_config config;

    EXPECT_TRUE(config.storage_directory.empty());
    EXPECT_EQ(config.max_connections, 100);
    EXPECT_EQ(config.max_file_size, 10ULL * 1024 * 1024 * 1024);  // 10GB
    EXPECT_EQ(config.storage_quota, 100ULL * 1024 * 1024 * 1024);  // 100GB
    EXPECT_EQ(config.chunk_size, 256 * 1024);
}

TEST_F(ServerConfigTest, IsValid_EmptyDirectory) {
    server_config config;
    config.storage_directory = "";
    config.max_connections = 100;

    EXPECT_FALSE(config.is_valid());
}

TEST_F(ServerConfigTest, IsValid_ZeroConnections) {
    server_config config;
    config.storage_directory = "/tmp/storage";
    config.max_connections = 0;

    EXPECT_FALSE(config.is_valid());
}

TEST_F(ServerConfigTest, IsValid_ValidConfig) {
    server_config config;
    config.storage_directory = "/tmp/storage";
    config.max_connections = 100;

    EXPECT_TRUE(config.is_valid());
}

TEST_F(ServerConfigTest, IsValid_MinimalValidConfig) {
    server_config config;
    config.storage_directory = "/";
    config.max_connections = 1;

    EXPECT_TRUE(config.is_valid());
}

// =============================================================================
// endpoint Tests
// =============================================================================

class EndpointTest : public ::testing::Test {};

TEST_F(EndpointTest, DefaultConstruction) {
    endpoint ep;
    EXPECT_TRUE(ep.host.empty());
    EXPECT_EQ(ep.port, 0);
}

TEST_F(EndpointTest, ConstructWithHostAndPort) {
    endpoint ep("localhost", 8080);
    EXPECT_EQ(ep.host, "localhost");
    EXPECT_EQ(ep.port, 8080);
}

TEST_F(EndpointTest, ConstructWithPortOnly) {
    endpoint ep(8080);
    EXPECT_EQ(ep.host, "0.0.0.0");
    EXPECT_EQ(ep.port, 8080);
}

TEST_F(EndpointTest, ConstructWithIPv4) {
    endpoint ep("192.168.1.1", 9000);
    EXPECT_EQ(ep.host, "192.168.1.1");
    EXPECT_EQ(ep.port, 9000);
}

// =============================================================================
// client_id Tests
// =============================================================================

class ClientIdTest : public ::testing::Test {};

TEST_F(ClientIdTest, DefaultConstruction) {
    client_id id;
    EXPECT_EQ(id.value, 0);
}

TEST_F(ClientIdTest, ExplicitConstruction) {
    client_id id(12345);
    EXPECT_EQ(id.value, 12345);
}

TEST_F(ClientIdTest, EqualityOperator) {
    client_id id1(100);
    client_id id2(100);
    client_id id3(200);

    EXPECT_TRUE(id1 == id2);
    EXPECT_FALSE(id1 == id3);
}

TEST_F(ClientIdTest, LessThanOperator) {
    client_id id1(100);
    client_id id2(200);

    EXPECT_TRUE(id1 < id2);
    EXPECT_FALSE(id2 < id1);
    EXPECT_FALSE(id1 < id1);
}

TEST_F(ClientIdTest, HashSupport) {
    client_id id1(100);
    client_id id2(100);
    client_id id3(200);

    std::hash<client_id> hasher;
    EXPECT_EQ(hasher(id1), hasher(id2));
    EXPECT_NE(hasher(id1), hasher(id3));
}

TEST_F(ClientIdTest, UseInUnorderedSet) {
    std::unordered_set<client_id> ids;
    ids.insert(client_id(1));
    ids.insert(client_id(2));
    ids.insert(client_id(1));  // Duplicate

    EXPECT_EQ(ids.size(), 2);
}

TEST_F(ClientIdTest, UseInUnorderedMap) {
    std::unordered_map<client_id, std::string> map;
    map[client_id(1)] = "client_one";
    map[client_id(2)] = "client_two";

    EXPECT_EQ(map[client_id(1)], "client_one");
    EXPECT_EQ(map[client_id(2)], "client_two");
}

// =============================================================================
// storage_stats Tests
// =============================================================================

class StorageStatsTest : public ::testing::Test {};

TEST_F(StorageStatsTest, UsagePercent_ZeroCapacity) {
    storage_stats stats;
    stats.total_capacity = 0;
    stats.used_size = 0;

    EXPECT_DOUBLE_EQ(stats.usage_percent(), 0.0);
}

TEST_F(StorageStatsTest, UsagePercent_Empty) {
    storage_stats stats;
    stats.total_capacity = 1000;
    stats.used_size = 0;

    EXPECT_DOUBLE_EQ(stats.usage_percent(), 0.0);
}

TEST_F(StorageStatsTest, UsagePercent_HalfFull) {
    storage_stats stats;
    stats.total_capacity = 1000;
    stats.used_size = 500;

    EXPECT_DOUBLE_EQ(stats.usage_percent(), 50.0);
}

TEST_F(StorageStatsTest, UsagePercent_Full) {
    storage_stats stats;
    stats.total_capacity = 1000;
    stats.used_size = 1000;

    EXPECT_DOUBLE_EQ(stats.usage_percent(), 100.0);
}

TEST_F(StorageStatsTest, UsagePercent_PartialUsage) {
    storage_stats stats;
    stats.total_capacity = 3;
    stats.used_size = 1;

    EXPECT_NEAR(stats.usage_percent(), 33.333, 0.01);
}

// =============================================================================
// compression_statistics Tests
// =============================================================================

class CompressionStatisticsTest : public ::testing::Test {};

TEST_F(CompressionStatisticsTest, CompressionRatio_NoData) {
    compression_statistics stats;
    stats.total_compressed_bytes = 0;
    stats.total_uncompressed_bytes = 0;

    EXPECT_DOUBLE_EQ(stats.compression_ratio(), 1.0);
}

TEST_F(CompressionStatisticsTest, CompressionRatio_NoCompression) {
    compression_statistics stats;
    stats.total_compressed_bytes = 1000;
    stats.total_uncompressed_bytes = 1000;

    EXPECT_DOUBLE_EQ(stats.compression_ratio(), 1.0);
}

TEST_F(CompressionStatisticsTest, CompressionRatio_GoodCompression) {
    compression_statistics stats;
    stats.total_compressed_bytes = 500;
    stats.total_uncompressed_bytes = 1000;

    EXPECT_DOUBLE_EQ(stats.compression_ratio(), 0.5);
}

TEST_F(CompressionStatisticsTest, CompressionRatio_PoorCompression) {
    compression_statistics stats;
    stats.total_compressed_bytes = 900;
    stats.total_uncompressed_bytes = 1000;

    EXPECT_DOUBLE_EQ(stats.compression_ratio(), 0.9);
}

// =============================================================================
// transfer_handle Tests
// =============================================================================

class TransferHandleTest : public ::testing::Test {};

TEST_F(TransferHandleTest, DefaultConstruction) {
    transfer_handle handle;
    EXPECT_EQ(handle.id, 0);
    EXPECT_FALSE(handle.is_valid());
}

TEST_F(TransferHandleTest, ExplicitConstruction) {
    transfer_handle handle(12345);
    EXPECT_EQ(handle.id, 12345);
    EXPECT_TRUE(handle.is_valid());
}

TEST_F(TransferHandleTest, IsValid_Zero) {
    transfer_handle handle(0);
    EXPECT_FALSE(handle.is_valid());
}

TEST_F(TransferHandleTest, IsValid_NonZero) {
    transfer_handle handle(1);
    EXPECT_TRUE(handle.is_valid());
}

// =============================================================================
// upload_options Tests
// =============================================================================

class UploadOptionsTest : public ::testing::Test {};

TEST_F(UploadOptionsTest, DefaultValues) {
    upload_options opts;
    EXPECT_FALSE(opts.compression.has_value());
    EXPECT_FALSE(opts.overwrite);
}

TEST_F(UploadOptionsTest, CustomValues) {
    upload_options opts;
    opts.compression = compression_mode::always;
    opts.overwrite = true;

    EXPECT_TRUE(opts.compression.has_value());
    EXPECT_EQ(opts.compression.value(), compression_mode::always);
    EXPECT_TRUE(opts.overwrite);
}

// =============================================================================
// download_options Tests
// =============================================================================

class DownloadOptionsTest : public ::testing::Test {};

TEST_F(DownloadOptionsTest, DefaultValues) {
    download_options opts;
    EXPECT_FALSE(opts.overwrite);
    EXPECT_TRUE(opts.verify_hash);
}

TEST_F(DownloadOptionsTest, CustomValues) {
    download_options opts;
    opts.overwrite = true;
    opts.verify_hash = false;

    EXPECT_TRUE(opts.overwrite);
    EXPECT_FALSE(opts.verify_hash);
}

// =============================================================================
// list_options Tests
// =============================================================================

class ListOptionsTest : public ::testing::Test {};

TEST_F(ListOptionsTest, DefaultValues) {
    list_options opts;
    EXPECT_EQ(opts.pattern, "*");
    EXPECT_EQ(opts.offset, 0);
    EXPECT_EQ(opts.limit, 1000);
}

TEST_F(ListOptionsTest, CustomValues) {
    list_options opts;
    opts.pattern = "*.txt";
    opts.offset = 10;
    opts.limit = 50;

    EXPECT_EQ(opts.pattern, "*.txt");
    EXPECT_EQ(opts.offset, 10);
    EXPECT_EQ(opts.limit, 50);
}

}  // namespace kcenon::file_transfer::test
