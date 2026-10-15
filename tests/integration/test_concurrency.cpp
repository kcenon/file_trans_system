/** Real concurrent transfers, connection admission and exact persisted contents. */
#include "test_fixtures.h"
#include <future>
#include <set>

namespace kcenon::file_transfer::test {
class ConcurrentConnectionTest : public TempDirectoryFixture {
protected:
    std::unique_ptr<file_transfer_server> server_;
    uint16_t server_port_{};
    void SetUp() override {
        TempDirectoryFixture::SetUp();
        auto built = file_transfer_server::builder().with_storage_directory(storage_dir_)
            .with_max_connections(150).with_chunk_size(65536).build();
        ASSERT_TRUE(built); server_ = std::make_unique<file_transfer_server>(std::move(built.value()));
        server_port_ = ServerFixture::get_available_port();
        ASSERT_TRUE(server_->start({"127.0.0.1", server_port_}));
    }
    auto create_client() -> std::unique_ptr<file_transfer_client> {
        auto built = file_transfer_client::builder().with_auto_reconnect(false)
            .with_connect_timeout(2s).with_transfer_timeout(5s).build();
        if (!built) throw std::runtime_error(built.error().message);
        auto client = std::make_unique<file_transfer_client>(std::move(built.value()));
        auto connected = client->connect({"127.0.0.1", server_port_});
        if (!connected) throw std::runtime_error(connected.error().message);
        return client;
    }
    void TearDown() override {
        if (server_ && server_->is_running()) (void)server_->stop();
        server_.reset(); TempDirectoryFixture::TearDown();
    }
};

TEST_F(ConcurrentConnectionTest, TenConcurrentClientConnections) {
    std::mutex mutex; std::set<uint64_t> ids; std::atomic<unsigned> disconnected{};
    server_->on_client_connected([&](const auto& info) { std::lock_guard lock(mutex); ids.insert(info.id.value); });
    server_->on_client_disconnected([&](const auto&) { ++disconnected; });
    std::vector<std::future<std::unique_ptr<file_transfer_client>>> pending;
    for (unsigned i = 0; i < 10; ++i)
        pending.push_back(std::async(std::launch::async, [&] { return create_client(); }));
    std::vector<std::unique_ptr<file_transfer_client>> clients;
    for (auto& future : pending) {
        clients.push_back(future.get()); ASSERT_TRUE(clients.back()->list_files());
    }
    EXPECT_EQ(server_->get_statistics().active_connections, 10);
    { std::lock_guard lock(mutex); EXPECT_EQ(ids.size(), 10); }
    clients.clear();
    EXPECT_TRUE(eventually([&] { return disconnected == 10 && server_->get_statistics().active_connections == 0; }));
    ASSERT_TRUE(server_->stop()); // Join callbacks before captured locals die.
}

TEST_F(ConcurrentConnectionTest, VerifyIndependentClientOperations) {
    std::vector<std::filesystem::path> sources;
    for (unsigned i = 0; i < 10; ++i) sources.push_back(create_test_file("source-" + std::to_string(i), 180000 + i));
    std::vector<std::future<bool>> pending;
    for (unsigned i = 0; i < 10; ++i) pending.push_back(std::async(std::launch::async, [&, i] {
        auto client = create_client(); auto name = "remote-" + std::to_string(i);
        auto transfer = client->upload_file(sources[i], name);
        return transfer_matches(transfer, sources[i], storage_dir_ / name);
    }));
    unsigned matched = 0; for (auto& future : pending) matched += future.get();
    EXPECT_EQ(matched, 10); EXPECT_EQ(server_->get_statistics().total_files_uploaded, 10);
}

TEST_F(ConcurrentConnectionTest, SameFileConcurrentDownload) {
    auto source = create_test_file("source", 800000);
    std::filesystem::copy_file(source, storage_dir_ / "shared");
    std::vector<std::future<bool>> pending;
    for (unsigned i = 0; i < 10; ++i) pending.push_back(std::async(std::launch::async, [&, i] {
        auto client = create_client(); auto destination = download_dir_ / std::to_string(i);
        auto transfer = client->download_file("shared", destination);
        return transfer_matches(transfer, source, destination);
    }));
    unsigned matched = 0; for (auto& future : pending) matched += future.get();
    EXPECT_EQ(matched, 10);
    EXPECT_TRUE(eventually([&] { return server_->get_statistics().total_files_downloaded == 10; }));
}

TEST_F(ConcurrentConnectionTest, ServerLoadTest100Connections) {
    std::vector<std::unique_ptr<file_transfer_client>> clients;
    for (unsigned i = 0; i < 100; ++i) {
        clients.push_back(create_client()); ASSERT_TRUE(clients.back()->list_files());
    }
    EXPECT_EQ(server_->get_statistics().active_connections, 100);
    std::vector<std::future<bool>> pending;
    for (auto& client : clients) pending.push_back(std::async(std::launch::async, [ptr = client.get()] {
        auto listing = ptr->list_files(); return listing && listing.value().empty();
    }));
    for (auto& future : pending) EXPECT_TRUE(future.get());
    clients.clear();
    EXPECT_TRUE(eventually([&] { return server_->get_statistics().active_connections == 0; }));
}

TEST_F(ConcurrentConnectionTest, ServerConnectionLimitEnforcement) {
    ASSERT_TRUE(server_->stop()); server_.reset();
    auto built = file_transfer_server::builder().with_storage_directory(storage_dir_).with_max_connections(2).build();
    ASSERT_TRUE(built); server_ = std::make_unique<file_transfer_server>(std::move(built.value()));
    ASSERT_TRUE(server_->start({"127.0.0.1", server_port_}));
    auto first = create_client(), second = create_client();
    ASSERT_TRUE(first->list_files()); ASSERT_TRUE(second->list_files());
    EXPECT_EQ(server_->get_statistics().active_connections, 2);
    auto extra = file_transfer_client::builder().with_connect_timeout(500ms).with_transfer_timeout(500ms)
        .with_auto_reconnect(false).build(); ASSERT_TRUE(extra);
    auto connection = extra.value().connect({"127.0.0.1", server_port_});
    if (connection) EXPECT_FALSE(extra.value().list_files());
    EXPECT_EQ(server_->get_statistics().active_connections, 2);
    ASSERT_TRUE(first->list_files()); ASSERT_TRUE(second->list_files());
}

class ConcurrentStressTest : public ConcurrentConnectionTest {};
TEST_F(ConcurrentStressTest, ConcurrentUploadDownloadMixed) {
    auto source = create_test_file("source", 300000); std::filesystem::copy_file(source, storage_dir_ / "seed");
    std::vector<std::future<bool>> pending;
    for (unsigned i = 0; i < 10; ++i) pending.push_back(std::async(std::launch::async, [&, i] {
        auto client = create_client(); auto name = std::to_string(i);
        if (i % 2) {
            auto transfer = client->upload_file(source, name);
            return transfer_matches(transfer, source, storage_dir_ / name);
        }
        auto transfer = client->download_file("seed", download_dir_ / name);
        return transfer_matches(transfer, source, download_dir_ / name);
    }));
    unsigned matched = 0; for (auto& future : pending) matched += future.get();
    EXPECT_EQ(matched, 10);
    EXPECT_TRUE(eventually([&] {
        auto stats = server_->get_statistics();
        return stats.total_files_uploaded == 5 && stats.total_files_downloaded == 5 && stats.active_transfers == 0;
    }));
}

TEST_F(ConcurrentStressTest, RapidConnectDisconnectConcurrent) {
    std::vector<std::future<unsigned>> pending;
    for (unsigned i = 0; i < 8; ++i) pending.push_back(std::async(std::launch::async, [&] {
        unsigned completed = 0;
        for (unsigned j = 0; j < 15; ++j) {
            auto client = create_client();
            if (client->list_files() && client->disconnect()) ++completed;
        }
        return completed;
    }));
    unsigned completed = 0; for (auto& future : pending) completed += future.get();
    EXPECT_EQ(completed, 120);
    EXPECT_TRUE(eventually([&] { return server_->get_statistics().active_connections == 0; }));
}

TEST_F(ConcurrentStressTest, RepeatedTransfersReleaseActiveServerResources) {
    auto source = create_test_file("source", 8000);
    for (unsigned round = 0; round < 5; ++round) {
        std::vector<std::future<bool>> pending;
        for (unsigned i = 0; i < 10; ++i) pending.push_back(std::async(std::launch::async, [&, i, round] {
            auto client = create_client(); auto name = std::to_string(round) + "-" + std::to_string(i);
            auto up = client->upload_file(source, name);
            return transfer_matches(up, source, storage_dir_ / name);
        }));
        for (auto& future : pending) EXPECT_TRUE(future.get());
        EXPECT_TRUE(eventually([&] {
            auto stats = server_->get_statistics(); return stats.active_connections == 0 && stats.active_transfers == 0;
        }));
    }
    EXPECT_EQ(server_->get_statistics().total_files_uploaded, 50);
}

TEST_F(ConcurrentStressTest, NoDataCorruptionUnderConcurrency) {
    constexpr unsigned count = 10;
    std::vector<std::filesystem::path> sources;
    for (unsigned i = 0; i < count; ++i) sources.push_back(create_test_file("original-" + std::to_string(i), 200000 + 13 * i));
    std::vector<std::future<bool>> uploads;
    for (unsigned i = 0; i < count; ++i) uploads.push_back(std::async(std::launch::async, [&, i] {
        auto client = create_client(); auto name = "checksum-" + std::to_string(i);
        auto up = client->upload_file(sources[i], name);
        return transfer_matches(up, sources[i], storage_dir_ / name);
    }));
    unsigned uploaded = 0; for (auto& future : uploads) uploaded += future.get();
    ASSERT_EQ(uploaded, count);
    std::vector<std::future<bool>> downloads;
    for (unsigned i = 0; i < count; ++i) downloads.push_back(std::async(std::launch::async, [&, i] {
        auto client = create_client(); auto destination = download_dir_ / std::to_string(i);
        auto down = client->download_file("checksum-" + std::to_string(i), destination);
        return transfer_matches(down, sources[i], destination);
    }));
    unsigned matched = 0; for (auto& future : downloads) matched += future.get();
    EXPECT_EQ(matched, count) << "Every downloaded file must match; zero matches is a failure";
}
} // namespace kcenon::file_transfer::test
