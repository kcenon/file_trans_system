#include "protocol_peer.h"
#include <kcenon/file_transfer/client/file_transfer_client.h>
#include <kcenon/file_transfer/server/file_transfer_server.h>
#include <kcenon/network/core/messaging_server.h>
#include <fstream>
#include <random>

namespace kcenon::file_transfer::test {
class PublicUploadTest : public ::testing::Test {
protected:
    std::filesystem::path root;
    std::unique_ptr<file_transfer_server> server;
    std::unique_ptr<file_transfer_client> client;
    std::mutex mutex;
    std::condition_variable cv;
    unsigned callbacks{};
    std::atomic<unsigned> intermediate{0};
    void SetUp() override {
        root = std::filesystem::temp_directory_path() / ("fts-public-upload-" + transfer_id::generate().to_string());
        auto built = file_transfer_server::builder().with_storage_directory(root / "server").with_chunk_size(64 * 1024).build();
        ASSERT_TRUE(built); server = std::make_unique<file_transfer_server>(std::move(built.value()));
        auto port = available_port(); ASSERT_TRUE(server->start({"127.0.0.1", port}));
        auto c = file_transfer_client::builder().with_transfer_timeout(2s).build(); ASSERT_TRUE(c);
        client = std::make_unique<file_transfer_client>(std::move(c.value()));
        ASSERT_TRUE(client->connect({"127.0.0.1", port}));
        client->on_complete([this](const auto&) { std::lock_guard lock(mutex); ++callbacks; cv.notify_all(); });
        client->on_progress([this](const auto& p) { if (p.bytes_transferred < p.total_bytes) ++intermediate; });
    }
    void TearDown() override {
        client.reset();
        if (server && server->is_running()) (void)server->stop();
        server.reset();
        std::error_code ec; std::filesystem::remove_all(root, ec);
    }
    auto source(std::string name, std::size_t size, bool compressible = false) -> std::filesystem::path {
        std::vector<char> bytes(size, 'a');
        if (!compressible) { std::mt19937 generator(42); for (auto& byte : bytes) byte = static_cast<char>(generator()); }
        auto path = root / name;
        std::ofstream out(path, std::ios::binary); out.write(bytes.data(), bytes.size()); out.close();
        return path;
    }
};

TEST_F(PublicUploadTest, EmptySingleAndMultipleChunksPersistExactHashes) {
    unsigned expected = 0;
    for (auto mode : {compression_mode::none, compression_mode::always, compression_mode::adaptive}) {
        for (auto size : {std::size_t{0}, std::size_t{1024}, std::size_t{300000}}) {
            auto name = "upload-" + std::to_string(expected++);
            auto path = source(name, size, mode == compression_mode::always);
            upload_options options; options.compression = mode;
            auto upload = client->upload_file(path, name, options); ASSERT_TRUE(upload);
            auto result = upload.value().wait_for(5s); ASSERT_TRUE(result) << result.error().message;
            ASSERT_TRUE(result.value().success) << result.value().error_message.value_or("");
            EXPECT_EQ(result.value().bytes_transferred, size);
            ASSERT_TRUE(std::filesystem::exists(root / "server" / name));
            EXPECT_EQ(checksum::sha256_file(path).value(), checksum::sha256_file(root / "server" / name).value());
            std::unique_lock lock(mutex); ASSERT_TRUE(cv.wait_for(lock, 2s, [&] { return callbacks == expected; }));
        }
    }
    EXPECT_GT(intermediate, 0);
    EXPECT_EQ(client->get_statistics().total_files_uploaded, expected);
    EXPECT_EQ(server->get_statistics().total_files_uploaded, expected);
}

TEST_F(PublicUploadTest, RejectedUploadFailsWithoutSuccessfulCountersOrFile) {
    server->on_upload_request([](const auto&) { return false; });
    auto upload = client->upload_file(source("denied", 1000), "denied"); ASSERT_TRUE(upload);
    auto result = upload.value().wait_for(5s); ASSERT_TRUE(result); EXPECT_FALSE(result.value().success);
    EXPECT_EQ(upload.value().get_status(), transfer_status::failed);
    EXPECT_EQ(client->get_statistics().total_files_uploaded, 0);
    EXPECT_FALSE(std::filesystem::exists(root / "server/denied"));
    std::unique_lock lock(mutex); ASSERT_TRUE(cv.wait_for(lock, 2s, [&] { return callbacks == 1; }));
}

TEST_F(PublicUploadTest, ConnectionLossAfterProgressTerminatesTheHandle) {
    std::atomic<bool> stopped{false};
    client->on_progress([&](const auto&) { if (!stopped.exchange(true)) EXPECT_TRUE(server->stop()); });
    auto upload = client->upload_file(source("interrupted", 500000), "interrupted"); ASSERT_TRUE(upload);
    auto result = upload.value().wait_for(5s); ASSERT_TRUE(result); EXPECT_FALSE(result.value().success);
    EXPECT_EQ(upload.value().get_status(), transfer_status::failed);
    EXPECT_EQ(client->get_statistics().total_files_uploaded, 0);
    EXPECT_FALSE(std::filesystem::exists(root / "server/interrupted"));
    std::unique_lock lock(mutex); ASSERT_TRUE(cv.wait_for(lock, 2s, [&] { return callbacks == 1; }));
}

TEST_F(PublicUploadTest, CancellationFromProgressCompletesExactlyOnce) {
    std::atomic<uint64_t> id{};
    client->on_progress([&](const auto&) { if (auto handle = id.load()) (void)client->cancel_transfer(handle); });
    auto upload = client->upload_file(source("cancelled", 400000), "cancelled"); ASSERT_TRUE(upload);
    id = upload.value().get_id();
    auto result = upload.value().wait_for(5s); ASSERT_TRUE(result); EXPECT_FALSE(result.value().success);
    EXPECT_EQ(upload.value().get_status(), transfer_status::cancelled);
    std::unique_lock lock(mutex); ASSERT_TRUE(cv.wait_for(lock, 2s, [&] { return callbacks == 1; }));
}

TEST(PublicUploadDeadlineTest, SilentPeerProducesAnExplicitFailedResult) {
    auto server = std::make_shared<network::core::messaging_server>("silent-peer");
    auto port = available_port(); ASSERT_TRUE(server->start_server(port).is_ok());
    auto built = file_transfer_client::builder().with_transfer_timeout(100ms).build(); ASSERT_TRUE(built);
    auto& client = built.value(); ASSERT_TRUE(client.connect({"127.0.0.1", port}));
    auto path = std::filesystem::temp_directory_path() / ("fts-deadline-" + transfer_id::generate().to_string());
    { std::ofstream file(path); file << "deadline"; }
    auto upload = client.upload_file(path, "deadline"); ASSERT_TRUE(upload);
    auto result = upload.value().wait_for(2s); ASSERT_TRUE(result); EXPECT_FALSE(result.value().success);
    EXPECT_NE(result.value().error_message.value_or("").find("deadline"), std::string::npos);
    EXPECT_TRUE(client.disconnect()); EXPECT_TRUE(server->stop_server().is_ok());
    std::error_code ec; std::filesystem::remove(path, ec);
}
} // namespace kcenon::file_transfer::test
