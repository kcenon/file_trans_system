#include "protocol_peer.h"
#include <kcenon/file_transfer/client/file_transfer_client.h>
#include <kcenon/file_transfer/server/file_transfer_server.h>
#include <future>

namespace kcenon::file_transfer::test {
class DownloadControlTest : public ::testing::Test {
protected:
    std::filesystem::path root;
    std::unique_ptr<file_transfer_server> server;
    std::unique_ptr<file_transfer_client> client;
    std::vector<std::byte> bytes = std::vector<std::byte>(1024, std::byte{42});
    void SetUp() override {
        root = std::filesystem::temp_directory_path() / ("fts-download-control-" + transfer_id::generate().to_string());
        auto built = file_transfer_server::builder().with_storage_directory(root / "server").build();
        ASSERT_TRUE(built); server = std::make_unique<file_transfer_server>(std::move(built.value()));
        auto port = available_port(); ASSERT_TRUE(server->start({"127.0.0.1", port}));
        auto c = file_transfer_client::builder().build(); ASSERT_TRUE(c);
        client = std::make_unique<file_transfer_client>(std::move(c.value()));
        ASSERT_TRUE(client->connect({"127.0.0.1", port}));
    }
    void TearDown() override {
        client.reset();
        if (server && server->is_running()) (void)server->stop();
        server.reset();
        std::error_code ec; std::filesystem::remove_all(root, ec);
    }
    auto prepare(std::string name) -> transfer_handle {
        auto created = client->download_file(name, root / name);
        if (!created) throw std::runtime_error(created.error().message);
        auto metadata = client->set_download_metadata(created.value().get_id(), bytes.size(), 1, 64 * 1024, checksum::sha256(bytes));
        if (!metadata) throw std::runtime_error(metadata.error().message);
        return created.value();
    }
    auto last_chunk() -> chunk {
        chunk data; data.data = bytes;
        data.header.original_size = bytes.size(); data.header.compressed_size = bytes.size();
        data.header.checksum = checksum::crc32(bytes); data.header.flags = chunk_flags::first_chunk | chunk_flags::last_chunk;
        return data;
    }
};

TEST_F(DownloadControlTest, FinalChunkCompletesAndObserversCanQueryStatus) {
    auto handle = prepare("final.bin");
    std::atomic<unsigned> callbacks{0};
    client->on_progress([&](const auto&) { (void)handle.get_status(); (void)client->get_statistics(); });
    client->on_complete([&](const auto&) { EXPECT_EQ(handle.get_status(), transfer_status::completed); ++callbacks; });
    auto completed = std::async(std::launch::async, [&] { return client->process_download_chunk(handle.get_id(), last_chunk()); });
    ASSERT_EQ(completed.wait_for(2s), std::future_status::ready);
    ASSERT_TRUE(completed.get());
    auto result = handle.wait_for(2s); ASSERT_TRUE(result); EXPECT_TRUE(result.value().success);
    EXPECT_EQ(callbacks, 1); EXPECT_EQ(checksum::sha256_file(root / "final.bin").value(), checksum::sha256(bytes));
    EXPECT_TRUE(client->finalize_download(handle.get_id())); EXPECT_EQ(callbacks, 1);
}

TEST_F(DownloadControlTest, CancelNotifiesOnceAndCleansTemporaryFile) {
    auto handle = prepare("cancel.bin");
    std::atomic<unsigned> callbacks{0};
    client->on_complete([&](const auto& result) { EXPECT_FALSE(result.success); (void)handle.get_status(); ++callbacks; });
    auto waiter = std::async(std::launch::async, [&] { return handle.wait_for(2s); });
    ASSERT_TRUE(handle.cancel()); ASSERT_TRUE(handle.cancel());
    ASSERT_EQ(waiter.wait_for(2s), std::future_status::ready);
    auto result = waiter.get(); ASSERT_TRUE(result); EXPECT_FALSE(result.value().success);
    EXPECT_EQ(handle.get_status(), transfer_status::cancelled); EXPECT_EQ(callbacks, 1);
    EXPECT_FALSE(std::filesystem::exists(root / "cancel.bin.tmp"));
    EXPECT_FALSE(std::filesystem::exists(root / "cancel.bin"));
    EXPECT_FALSE(client->process_download_chunk(handle.get_id(), last_chunk()));
}

TEST_F(DownloadControlTest, CancellationRacingWithFinalChunkHasOneTerminalOutcome) {
    for (unsigned attempt = 0; attempt < 20; ++attempt) {
        auto handle = prepare("race-" + std::to_string(attempt));
        std::atomic<unsigned> callbacks{0};
        client->on_complete([&](const auto&) { (void)handle.get_status(); ++callbacks; });
        auto completion = std::async(std::launch::async, [&] { return client->process_download_chunk(handle.get_id(), last_chunk()); });
        (void)handle.cancel();
        ASSERT_EQ(completion.wait_for(2s), std::future_status::ready); (void)completion.get();
        auto result = handle.wait_for(2s); ASSERT_TRUE(result);
        EXPECT_EQ(callbacks, 1);
        EXPECT_TRUE(handle.get_status() == transfer_status::completed || handle.get_status() == transfer_status::cancelled);
    }
    client->on_complete({});
}
} // namespace kcenon::file_transfer::test
