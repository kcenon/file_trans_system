/** Real terminal outcomes for failure, quota, size and compression scenarios. */
#include "test_fixtures.h"

namespace kcenon::file_transfer::test {
class ErrorScenarioTest : public TempDirectoryFixture {};
TEST_F(ErrorScenarioTest, ConnectionFailureWhenServerNotRunning) {
    auto client = file_transfer_client::builder().with_connect_timeout(150ms).with_auto_reconnect(false).build();
    ASSERT_TRUE(client); auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(client.value().connect({"127.0.0.1", ServerFixture::get_available_port()}));
    EXPECT_FALSE(client.value().is_connected()); EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}
TEST_F(ErrorScenarioTest, ConnectionTimeoutWithShortTimeout) {
    asio::io_context context;
    asio::ip::tcp::acceptor listener(context, {asio::ip::tcp::v4(), 0});
    // A closed loopback port provides deterministic refusal without depending on
    // an external network route. Scripted timeout policy has separate coverage.
    auto port = listener.local_endpoint().port(); listener.close();
    auto client = file_transfer_client::builder().with_connect_timeout(20ms).build(); ASSERT_TRUE(client);
    auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(client.value().connect({"127.0.0.1", port}));
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}

class InvalidFilenameTest : public IntegrationFixture {};
TEST_F(InvalidFilenameTest, UploadWithEmptyFilenameUsesLocalBasename) {
    ASSERT_TRUE(connect_client()); auto source = create_test_file("basename.bin", 1000);
    auto pending = client_->upload_file(source, "");
    ASSERT_TRUE(transfer_matches(pending, source, storage_dir_ / "basename.bin"));
}
TEST_F(InvalidFilenameTest, UploadWithPathTraversalOrAbsolutePathFailsWithoutCommit) {
    ASSERT_TRUE(connect_client()); auto source = create_test_file("source", 1000);
    for (auto name : {"../escape.bin", "a/../../escape.bin", "/absolute.bin"}) {
        auto pending = client_->upload_file(source, name);
        transfer_fails(pending);
    }
    EXPECT_FALSE(std::filesystem::exists(test_dir_ / "escape.bin"));
    auto files = client_->list_files(); ASSERT_TRUE(files); EXPECT_TRUE(files.value().empty());
}
TEST_F(InvalidFilenameTest, DownloadWithEmptyFilename) {
    ASSERT_TRUE(connect_client());
    auto pending = client_->download_file("", download_dir_ / "empty");
    ASSERT_FALSE(pending); EXPECT_EQ(pending.error().code, error_code::invalid_file_path);
}
TEST_F(InvalidFilenameTest, DownloadNonExistentFile) {
    ASSERT_TRUE(connect_client()); auto pending = client_->download_file("missing", download_dir_ / "missing");
    transfer_fails(pending); EXPECT_FALSE(std::filesystem::exists(download_dir_ / "missing"));
}

class QuotaExceededTest : public IntegrationFixture {
protected:
    void replace_server(uint64_t max_file, uint64_t quota) {
        client_.reset(); ASSERT_TRUE(server_->stop()); server_.reset();
        auto server = file_transfer_server::builder().with_storage_directory(storage_dir_)
            .with_max_file_size(max_file).with_storage_quota(quota).build();
        ASSERT_TRUE(server); server_ = std::make_unique<file_transfer_server>(std::move(server.value()));
        ASSERT_TRUE(server_->start({"127.0.0.1", server_port_}));
        auto client = file_transfer_client::builder().with_auto_reconnect(false).with_transfer_timeout(3s).build();
        ASSERT_TRUE(client); client_ = std::make_unique<file_transfer_client>(std::move(client.value()));
        ASSERT_TRUE(connect_client());
    }
};
TEST_F(QuotaExceededTest, UploadExceedsMaxFileSize) {
    replace_server(1024, 10240);
    auto source = create_test_file("large", 2048); auto pending = client_->upload_file(source, "rejected");
    transfer_fails(pending); EXPECT_FALSE(std::filesystem::exists(storage_dir_ / "rejected"));
    EXPECT_EQ(server_->get_statistics().total_files_uploaded, 0);
}
TEST_F(QuotaExceededTest, UploadExceedsStorageQuota) {
    replace_server(4096, 3000); auto source = create_test_file("source", 2000);
    auto first = client_->upload_file(source, "first"); ASSERT_TRUE(transfer_matches(first, source, storage_dir_ / "first"));
    auto second = client_->upload_file(source, "second"); transfer_fails(second);
    EXPECT_EQ(server_->get_storage_stats().used_size, 2000);
    EXPECT_EQ(server_->get_statistics().total_files_uploaded, 1);
    EXPECT_FALSE(std::filesystem::exists(storage_dir_ / "second"));
}

class ReconnectionTest : public IntegrationFixture {};
TEST_F(ReconnectionTest, ManualReconnectAfterDisconnect) {
    ASSERT_TRUE(connect_client()); ASSERT_TRUE(client_->disconnect());
    ASSERT_TRUE(client_->connect({"127.0.0.1", server_port_})); ASSERT_TRUE(client_->list_files());
    EXPECT_TRUE(client_->is_connected());
}
// Automatic stop/restart, deadlines, backoff and cancellation are exercised by
// PublicReconnectTest, including a virtual clock for the exact retry sequence.

class LargeFileTransferTest : public IntegrationFixture {};
TEST_F(LargeFileTransferTest, UploadLargeFile100MB) {
    // The final response includes hashing 100 MiB; instrumented Debug builds
    // need a larger response deadline than small-message fault tests.
    auto configured = file_transfer_client::builder().with_auto_reconnect(false)
        .with_connect_timeout(2s).with_transfer_timeout(30s).build();
    ASSERT_TRUE(configured);
    client_ = std::make_unique<file_transfer_client>(std::move(configured.value()));
    ASSERT_TRUE(connect_client()); auto source = create_test_file("large", test_data::large_file_size);
    std::atomic<unsigned> progress{};
    client_->on_progress([&](const auto&) { ++progress; });
    auto pending = client_->upload_file(source, "large");
    auto matched = transfer_matches(pending, source, storage_dir_ / "large", 180s);
    client_.reset();
    EXPECT_TRUE(matched); EXPECT_GT(progress, 1);
}
TEST_F(LargeFileTransferTest, DownloadLargeFile) {
    ASSERT_TRUE(connect_client()); auto source = create_test_file("large", test_data::medium_file_size);
    std::filesystem::copy_file(source, storage_dir_ / "large");
    auto pending = client_->download_file("large", download_dir_ / "large");
    ASSERT_TRUE(transfer_matches(pending, source, download_dir_ / "large", 20s));
}

class SequentialTransferTest : public IntegrationFixture {};
TEST_F(SequentialTransferTest, MultipleSequentialUploadsAndDownloads) {
    ASSERT_TRUE(connect_client());
    for (unsigned i = 0; i < 5; ++i) {
        auto name = "sequential-" + std::to_string(i);
        auto source = create_test_file(name, 1000 + 17 * i);
        auto up = client_->upload_file(source, name);
        ASSERT_TRUE(transfer_matches(up, source, storage_dir_ / name));
        auto down = client_->download_file(name, download_dir_ / name);
        ASSERT_TRUE(transfer_matches(down, source, download_dir_ / name));
    }
    auto files = client_->list_files(); ASSERT_TRUE(files); ASSERT_EQ(files.value().size(), 5);
    EXPECT_EQ(client_->get_statistics().total_files_uploaded, 5);
    EXPECT_EQ(client_->get_statistics().total_files_downloaded, 5);
}

class TransferControlTest : public IntegrationFixture {};
TEST_F(TransferControlTest, TransferProgressCallback) {
    ASSERT_TRUE(connect_client()); std::atomic<unsigned> progress{}, completed{};
    client_->on_progress([&](const auto&) { ++progress; });
    client_->on_complete([&](const auto& outcome) { EXPECT_TRUE(outcome.success); ++completed; });
    auto source = create_test_file("progress", 800000); auto pending = client_->upload_file(source, "progress");
    ASSERT_TRUE(transfer_matches(pending, source, storage_dir_ / "progress"));
    EXPECT_TRUE(eventually([&] { return completed == 1; })); EXPECT_GT(progress, 1);
    client_.reset();
}
TEST_F(TransferControlTest, ClientStatisticsAfterTransfer) {
    ASSERT_TRUE(connect_client()); auto source = create_test_file("stats", 1000);
    auto pending = client_->upload_file(source, "stats");
    ASSERT_TRUE(transfer_matches(pending, source, storage_dir_ / "stats"));
    auto stats = client_->get_statistics();
    EXPECT_EQ(stats.total_files_uploaded, 1); EXPECT_EQ(stats.total_bytes_uploaded, 1000);
    EXPECT_EQ(stats.active_transfers, 0);
}

class CompressionIntegrationTest : public IntegrationFixture {};
TEST_F(CompressionIntegrationTest, PerFileModesPersistExactBytesAndChangeWireVolume) {
    ASSERT_TRUE(connect_client()); auto source = create_text_file("compressible", 800000);
    std::vector<uint64_t> volumes;
    for (auto mode : {compression_mode::none, compression_mode::always, compression_mode::adaptive}) {
        auto name = "mode-" + std::to_string(static_cast<int>(mode));
        upload_options options; options.compression = mode;
        auto before = client_->network_bytes().first;
        auto pending = client_->upload_file(source, name, options);
        ASSERT_TRUE(transfer_matches(pending, source, storage_dir_ / name));
        volumes.push_back(client_->network_bytes().first - before);
        download_options download; download.compression = mode;
        auto received = client_->download_file(name, download_dir_ / name, download);
        ASSERT_TRUE(transfer_matches(received, source, download_dir_ / name));
    }
    EXPECT_GT(volumes[0], std::filesystem::file_size(source));
#ifdef FILE_TRANS_ENABLE_LZ4
    EXPECT_LT(volumes[1], volumes[0] / 10); EXPECT_LT(volumes[2], volumes[0] / 10);
#else
    EXPECT_GE(volumes[1], std::filesystem::file_size(source));
#endif
}
TEST_F(CompressionIntegrationTest, CompressionLevelComparison) {
    auto source = create_text_file("source", 200000);
    for (auto level : {compression_level::fast, compression_level::best}) {
        auto client = file_transfer_client::builder().with_compression_level(level).build(); ASSERT_TRUE(client);
        ASSERT_TRUE(client.value().connect({"127.0.0.1", server_port_}));
        auto name = "level-" + std::to_string(static_cast<int>(level));
        auto pending = client.value().upload_file(source, name);
        ASSERT_TRUE(transfer_matches(pending, source, storage_dir_ / name));
    }
}

class StressTest : public IntegrationFixture {};
TEST_F(StressTest, RapidConnectDisconnectLoop) {
    for (unsigned i = 0; i < 20; ++i) {
        ASSERT_TRUE(connect_client()); ASSERT_TRUE(client_->list_files()); ASSERT_TRUE(client_->disconnect());
        ASSERT_TRUE(eventually([&] { return server_->get_statistics().active_connections == 0; }));
    }
}
TEST_F(StressTest, MultipleSmallFileUploads) {
    ASSERT_TRUE(connect_client()); auto source = create_test_file("source", 512);
    for (unsigned i = 0; i < 50; ++i) {
        auto name = "small-" + std::to_string(i);
        auto pending = client_->upload_file(source, name);
        ASSERT_TRUE(transfer_matches(pending, source, storage_dir_ / name));
    }
    auto files = client_->list_files(); ASSERT_TRUE(files); EXPECT_EQ(files.value().size(), 50);
    EXPECT_EQ(server_->get_statistics().total_files_uploaded, 50);
}
} // namespace kcenon::file_transfer::test
