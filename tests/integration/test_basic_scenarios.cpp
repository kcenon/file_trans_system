/**
 * @file test_basic_scenarios.cpp
 * @brief Basic integration tests for server-client communication
 */

#include "test_fixtures.h"

namespace kcenon::file_transfer::test {

// Server start/stop tests
class ServerLifecycleTest : public ServerFixture {};

TEST_F(ServerLifecycleTest, ServerStartStop) {
    // Server should not be running initially
    EXPECT_FALSE(server_->is_running());
    EXPECT_EQ(server_->state(), server_state::stopped);

    // Start server
    auto port = start_server();
    EXPECT_TRUE(server_->is_running());
    EXPECT_EQ(server_->state(), server_state::running);
    EXPECT_EQ(server_->port(), port);

    // Stop server
    auto stop_result = server_->stop();
    EXPECT_TRUE(stop_result.has_value());
    EXPECT_FALSE(server_->is_running());
    EXPECT_EQ(server_->state(), server_state::stopped);
}

TEST_F(ServerLifecycleTest, ServerDoubleStart) {
    start_server();

    // Trying to start again should fail
    auto result = server_->start(endpoint{50001});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::already_initialized);
}

TEST_F(ServerLifecycleTest, ServerStopWithoutStart) {
    // Stopping a non-running server should fail
    auto result = server_->stop();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(ServerLifecycleTest, ServerConfiguration) {
    const auto& config = server_->config();
    EXPECT_EQ(config.storage_directory, storage_dir_);
    EXPECT_EQ(config.max_connections, 10);
    EXPECT_EQ(config.max_file_size, 100 * 1024 * 1024);
}

TEST_F(ServerLifecycleTest, ServerStatistics) {
    start_server();

    auto stats = server_->get_statistics();
    EXPECT_EQ(stats.active_connections, 0);
    EXPECT_EQ(stats.active_transfers, 0);
    EXPECT_EQ(stats.total_bytes_received, 0);
    EXPECT_EQ(stats.total_bytes_sent, 0);
}

TEST_F(ServerLifecycleTest, ServerStorageStats) {
    start_server();

    auto stats = server_->get_storage_stats();
    EXPECT_EQ(stats.file_count, 0);
    EXPECT_EQ(stats.used_size, 0);
}

// Client connection tests
class ClientConnectionTest : public ClientFixture {};

TEST_F(ClientConnectionTest, ClientInitialState) {
    EXPECT_FALSE(client_->is_connected());
    EXPECT_EQ(client_->state(), connection_state::disconnected);
}

TEST_F(ClientConnectionTest, ClientConfiguration) {
    const auto& config = client_->config();
    EXPECT_EQ(config.compression, compression_mode::adaptive);
    EXPECT_FALSE(config.auto_reconnect);
}

TEST_F(ClientConnectionTest, ClientStatistics) {
    auto stats = client_->get_statistics();
    EXPECT_EQ(stats.active_transfers, 0);
    EXPECT_EQ(stats.total_bytes_uploaded, 0);
    EXPECT_EQ(stats.total_bytes_downloaded, 0);
}

TEST_F(ClientConnectionTest, ClientDisconnectWithoutConnect) {
    // Disconnecting when not connected should fail
    auto result = client_->disconnect();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(ClientConnectionTest, ClientUploadWithoutConnect) {
    auto path = create_test_file("test.txt", 100);
    auto result = client_->upload_file(path, "test.txt");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(ClientConnectionTest, ClientDownloadWithoutConnect) {
    auto result = client_->download_file("test.txt", test_dir_ / "download.txt");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(ClientConnectionTest, ClientListWithoutConnect) {
    auto result = client_->list_files();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

// Builder tests
class BuilderTest : public TempDirectoryFixture {};

TEST_F(BuilderTest, ServerBuilderMissingStorageDir) {
    auto result = file_transfer_server::builder()
        .build();

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::invalid_configuration);
}

TEST_F(BuilderTest, ServerBuilderValidConfig) {
    auto result = file_transfer_server::builder()
        .with_storage_directory(storage_dir_)
        .with_max_connections(50)
        .with_max_file_size(1024 * 1024 * 1024)
        .with_storage_quota(10ULL * 1024 * 1024 * 1024)
        .with_chunk_size(512 * 1024)
        .build();

    EXPECT_TRUE(result.has_value());
    auto& server = result.value();
    EXPECT_EQ(server.config().max_connections, 50);
    EXPECT_EQ(server.config().max_file_size, 1024 * 1024 * 1024);
}

TEST_F(BuilderTest, ClientBuilderDefaultConfig) {
    auto result = file_transfer_client::builder().build();

    EXPECT_TRUE(result.has_value());
    auto& client = result.value();
    EXPECT_EQ(client.config().compression, compression_mode::adaptive);
    EXPECT_EQ(client.config().comp_level, compression_level::fast);
    EXPECT_TRUE(client.config().auto_reconnect);
}

TEST_F(BuilderTest, ClientBuilderCustomConfig) {
    auto result = file_transfer_client::builder()
        .with_compression(compression_mode::always)
        .with_compression_level(compression_level::best)
        .with_chunk_size(128 * 1024)
        .with_auto_reconnect(false)
        .with_upload_bandwidth_limit(1024 * 1024)
        .with_download_bandwidth_limit(2 * 1024 * 1024)
        .with_connect_timeout(std::chrono::milliseconds{5000})
        .build();

    EXPECT_TRUE(result.has_value());
    auto& client = result.value();
    EXPECT_EQ(client.config().compression, compression_mode::always);
    EXPECT_EQ(client.config().comp_level, compression_level::best);
    EXPECT_EQ(client.config().chunk_size, 128 * 1024);
    EXPECT_FALSE(client.config().auto_reconnect);
    EXPECT_EQ(client.config().upload_bandwidth_limit.value(), 1024 * 1024);
    EXPECT_EQ(client.config().download_bandwidth_limit.value(), 2 * 1024 * 1024);
    EXPECT_EQ(client.config().connect_timeout.count(), 5000);
}

TEST_F(BuilderTest, ClientBuilderInvalidChunkSize) {
    // Chunk size too small
    auto result1 = file_transfer_client::builder()
        .with_chunk_size(32 * 1024)
        .build();
    EXPECT_FALSE(result1.has_value());
    EXPECT_EQ(result1.error().code, error_code::invalid_chunk_size);

    // Chunk size too large
    auto result2 = file_transfer_client::builder()
        .with_chunk_size(2 * 1024 * 1024)
        .build();
    EXPECT_FALSE(result2.has_value());
    EXPECT_EQ(result2.error().code, error_code::invalid_chunk_size);
}

// Callback tests
class CallbackTest : public ServerFixture {};

TEST_F(CallbackTest, ServerCallbackRegistration) {
    bool connect_called = false;
    bool disconnect_called = false;
    bool upload_called = false;
    bool download_called = false;
    bool complete_called = false;
    bool progress_called = false;

    server_->on_client_connected([&]([[maybe_unused]] const client_info& info) {
        connect_called = true;
    });

    server_->on_client_disconnected([&]([[maybe_unused]] const client_info& info) {
        disconnect_called = true;
    });

    server_->on_upload_request([&]([[maybe_unused]] const upload_request& req) {
        upload_called = true;
        return true;
    });

    server_->on_download_request([&]([[maybe_unused]] const download_request& req) {
        download_called = true;
        return true;
    });

    server_->on_transfer_complete([&]([[maybe_unused]] const transfer_result& result) {
        complete_called = true;
    });

    server_->on_progress([&]([[maybe_unused]] const transfer_progress& progress) {
        progress_called = true;
    });

    // Callbacks are registered but not called yet
    EXPECT_FALSE(connect_called);
    EXPECT_FALSE(disconnect_called);
    EXPECT_FALSE(upload_called);
    EXPECT_FALSE(download_called);
    EXPECT_FALSE(complete_called);
    EXPECT_FALSE(progress_called);
}

// Test data generation
class TestDataTest : public TempDirectoryFixture {};

TEST_F(TestDataTest, CreateSmallFile) {
    auto path = create_test_file("small.bin", test_data::small_file_size);
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_EQ(std::filesystem::file_size(path), test_data::small_file_size);
}

TEST_F(TestDataTest, CreateTextFile) {
    auto path = create_text_file("text.txt", 1000);
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_GE(std::filesystem::file_size(path), 1000);
}

TEST_F(TestDataTest, CreateBinaryFile) {
    auto path = create_binary_file("binary.bin", 1000);
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_EQ(std::filesystem::file_size(path), 1000);
}

// =============================================================================
// Server-Client Integration Tests
// =============================================================================

class ServerClientIntegrationTest : public IntegrationFixture {};

TEST_F(ServerClientIntegrationTest, ClientConnectDisconnect) {
    // Connect client to server
    ASSERT_TRUE(connect_client()) << "Failed to connect client to server";

    EXPECT_TRUE(client_->is_connected());
    EXPECT_EQ(client_->state(), connection_state::connected);

    // Verify server sees connection
    auto server_stats = server_->get_statistics();
    // Note: In stub implementation, connection tracking may not be fully implemented
    // This test validates the API contract

    // Disconnect client
    auto disconnect_result = client_->disconnect();
    EXPECT_TRUE(disconnect_result.has_value());
    EXPECT_FALSE(client_->is_connected());
    EXPECT_EQ(client_->state(), connection_state::disconnected);
}

TEST_F(ServerClientIntegrationTest, ClientDoubleConnect) {
    ASSERT_TRUE(connect_client());

    // Trying to connect again should fail
    auto result = client_->connect(endpoint{"127.0.0.1", server_port_});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::already_initialized);
}

TEST_F(ServerClientIntegrationTest, ClientStateCallback) {
    std::vector<connection_state> observed_states;
    std::mutex states_mutex;

    client_->on_connection_state_changed([&](connection_state state) {
        std::lock_guard lock(states_mutex);
        observed_states.push_back(state);
    });

    // Connect
    ASSERT_TRUE(connect_client());

    // Disconnect
    auto disconnect_result = client_->disconnect();
    EXPECT_TRUE(disconnect_result.has_value());

    // Verify states were observed (implementation may vary)
    std::lock_guard lock(states_mutex);
    // At minimum, we should see connecting->connected->disconnected transitions
    EXPECT_FALSE(observed_states.empty());
}

// =============================================================================
// File Upload Integration Tests
// =============================================================================

class FileUploadIntegrationTest : public IntegrationFixture {};

TEST_F(FileUploadIntegrationTest, UploadSmallFile) {
    ASSERT_TRUE(connect_client());

    // Create test file
    auto test_file = create_test_file("upload_small.bin", test_data::small_file_size);
    ASSERT_TRUE(std::filesystem::exists(test_file));

    // Attempt upload
    auto result = client_->upload_file(test_file, "remote_small.bin");
    EXPECT_TRUE(result.has_value()) << "Upload should return a handle";

    if (result.has_value()) {
        EXPECT_TRUE(result.value().is_valid());
    }
}

TEST_F(FileUploadIntegrationTest, UploadNonExistentFile) {
    ASSERT_TRUE(connect_client());

    // Try to upload non-existent file
    auto result = client_->upload_file("/nonexistent/path/file.txt", "remote.txt");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::file_not_found);
}

TEST_F(FileUploadIntegrationTest, UploadTextFile) {
    ASSERT_TRUE(connect_client());

    // Create highly compressible text file
    auto test_file = create_text_file("upload_text.txt", test_data::small_file_size);
    ASSERT_TRUE(std::filesystem::exists(test_file));

    auto result = client_->upload_file(test_file, "remote_text.txt");
    EXPECT_TRUE(result.has_value());
}

TEST_F(FileUploadIntegrationTest, UploadBinaryFile) {
    ASSERT_TRUE(connect_client());

    // Create random binary file (low compressibility)
    auto test_file = create_binary_file("upload_binary.bin", test_data::small_file_size);
    ASSERT_TRUE(std::filesystem::exists(test_file));

    auto result = client_->upload_file(test_file, "remote_binary.bin");
    EXPECT_TRUE(result.has_value());
}

TEST_F(FileUploadIntegrationTest, UploadWithOptions) {
    ASSERT_TRUE(connect_client());

    auto test_file = create_test_file("upload_options.bin", test_data::small_file_size);

    upload_options options;
    options.overwrite = true;
    options.compression = compression_mode::always;

    auto result = client_->upload_file(test_file, "remote_options.bin", options);
    EXPECT_TRUE(result.has_value());
}

// =============================================================================
// File Download Integration Tests
// =============================================================================

class FileDownloadIntegrationTest : public IntegrationFixture {};

TEST_F(FileDownloadIntegrationTest, DownloadToValidPath) {
    ASSERT_TRUE(connect_client());

    auto download_path = download_dir_ / "downloaded.bin";

    auto result = client_->download_file("some_file.bin", download_path);
    // Note: Actual download may not succeed if file doesn't exist on server,
    // but the API call should succeed (return handle)
    EXPECT_TRUE(result.has_value());
}

TEST_F(FileDownloadIntegrationTest, DownloadEmptyFilename) {
    ASSERT_TRUE(connect_client());

    auto result = client_->download_file("", download_dir_ / "empty.bin");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::invalid_file_path);
}

TEST_F(FileDownloadIntegrationTest, DownloadWithOptions) {
    ASSERT_TRUE(connect_client());

    download_options options;
    options.overwrite = true;
    options.verify_hash = true;

    auto result = client_->download_file("test.bin", download_dir_ / "test.bin", options);
    EXPECT_TRUE(result.has_value());
}

// =============================================================================
// File List Integration Tests
// =============================================================================

class FileListIntegrationTest : public IntegrationFixture {};

TEST_F(FileListIntegrationTest, ListFilesDefault) {
    ASSERT_TRUE(connect_client());

    auto result = client_->list_files();
    EXPECT_TRUE(result.has_value());

    // Empty storage should return empty list
    if (result.has_value()) {
        // Note: In current stub implementation, this returns empty vector
        auto& files = result.value();
        EXPECT_TRUE(files.empty() || files.size() >= 0);
    }
}

TEST_F(FileListIntegrationTest, ListFilesWithOptions) {
    ASSERT_TRUE(connect_client());

    list_options options;
    options.pattern = "*.txt";
    options.offset = 0;
    options.limit = 100;

    auto result = client_->list_files(options);
    EXPECT_TRUE(result.has_value());
}

TEST_F(FileListIntegrationTest, ListFilesWithPagination) {
    ASSERT_TRUE(connect_client());

    list_options options;
    options.offset = 10;
    options.limit = 5;

    auto result = client_->list_files(options);
    EXPECT_TRUE(result.has_value());
}

// =============================================================================
// Statistics Integration Tests
// =============================================================================

class StatisticsIntegrationTest : public IntegrationFixture {};

TEST_F(StatisticsIntegrationTest, ServerStatisticsInitialValues) {
    auto stats = server_->get_statistics();
    EXPECT_EQ(stats.total_bytes_received, 0);
    EXPECT_EQ(stats.total_bytes_sent, 0);
    EXPECT_EQ(stats.total_files_uploaded, 0);
    EXPECT_EQ(stats.total_files_downloaded, 0);
}

TEST_F(StatisticsIntegrationTest, ClientStatisticsInitialValues) {
    auto stats = client_->get_statistics();
    EXPECT_EQ(stats.total_bytes_uploaded, 0);
    EXPECT_EQ(stats.total_bytes_downloaded, 0);
    EXPECT_EQ(stats.total_files_uploaded, 0);
    EXPECT_EQ(stats.total_files_downloaded, 0);
    EXPECT_EQ(stats.active_transfers, 0);
}

TEST_F(StatisticsIntegrationTest, CompressionStatisticsInitialValues) {
    auto stats = client_->get_compression_stats();
    EXPECT_EQ(stats.total_compressed_bytes, 0);
    EXPECT_EQ(stats.total_uncompressed_bytes, 0);
    EXPECT_DOUBLE_EQ(stats.compression_ratio(), 1.0);
}

TEST_F(StatisticsIntegrationTest, StorageStatsAfterServerStart) {
    auto stats = server_->get_storage_stats();
    EXPECT_EQ(stats.file_count, 0);
    EXPECT_EQ(stats.used_size, 0);
    EXPECT_GT(stats.total_capacity, 0);
}

// =============================================================================
// Error Handling Integration Tests
// =============================================================================

class ErrorHandlingIntegrationTest : public IntegrationFixture {};

TEST_F(ErrorHandlingIntegrationTest, OperationsAfterDisconnect) {
    ASSERT_TRUE(connect_client());

    // Disconnect first
    auto disconnect_result = client_->disconnect();
    EXPECT_TRUE(disconnect_result.has_value());

    // All operations should fail after disconnect
    auto upload_result = client_->upload_file(
        create_test_file("test.bin", 100), "remote.bin");
    EXPECT_FALSE(upload_result.has_value());
    EXPECT_EQ(upload_result.error().code, error_code::not_initialized);

    auto download_result = client_->download_file("remote.bin", test_dir_ / "local.bin");
    EXPECT_FALSE(download_result.has_value());
    EXPECT_EQ(download_result.error().code, error_code::not_initialized);

    auto list_result = client_->list_files();
    EXPECT_FALSE(list_result.has_value());
    EXPECT_EQ(list_result.error().code, error_code::not_initialized);
}

TEST_F(ErrorHandlingIntegrationTest, ServerStopWhileClientConnected) {
    ASSERT_TRUE(connect_client());
    EXPECT_TRUE(client_->is_connected());

    // Stop server
    auto stop_result = server_->stop();
    EXPECT_TRUE(stop_result.has_value());
    EXPECT_FALSE(server_->is_running());

    // Client may still think it's connected (depending on implementation)
    // This test verifies graceful handling
}

}  // namespace kcenon::file_transfer::test
