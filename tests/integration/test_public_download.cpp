#include "protocol_peer.h"
#include <kcenon/file_transfer/client/file_transfer_client.h>
#include <kcenon/file_transfer/server/file_transfer_server.h>
#include <kcenon/network/core/messaging_server.h>
#include <kcenon/network/session/messaging_session.h>
#include <fstream>
#include <random>

namespace kcenon::file_transfer::test {
class PublicDownloadTest : public ::testing::Test {
protected:
    std::filesystem::path root;
    std::unique_ptr<file_transfer_server> server;
    std::unique_ptr<file_transfer_client> client;
    uint16_t port{};
    void SetUp() override {
        root = std::filesystem::temp_directory_path() / ("fts-download-" + transfer_id::generate().to_string());
        auto built = file_transfer_server::builder().with_storage_directory(root / "server").with_chunk_size(65536).build();
        ASSERT_TRUE(built); server = std::make_unique<file_transfer_server>(std::move(built.value()));
        port = available_port(); ASSERT_TRUE(server->start({"127.0.0.1", port}));
        connect(compression_mode::adaptive);
    }
    void connect(compression_mode mode) {
        client.reset();
        auto built = file_transfer_client::builder().with_compression(mode).with_transfer_timeout(1s).build();
        ASSERT_TRUE(built); client = std::make_unique<file_transfer_client>(std::move(built.value()));
        ASSERT_TRUE(client->connect({"127.0.0.1", port}));
    }
    void TearDown() override {
        client.reset(); if (server) (void)server->stop(); server.reset();
        std::error_code ec; std::filesystem::remove_all(root, ec);
    }
    auto seed(std::string name, std::size_t size, bool compressible = false) -> std::vector<std::byte> {
        std::vector<std::byte> bytes(size, std::byte{'a'});
        if (!compressible) { std::mt19937 random(77); for (auto& b : bytes) b = static_cast<std::byte>(random()); }
        std::ofstream file(root / "server" / name, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); file.close();
        return bytes;
    }
    void no_parts() {
        for (const auto& entry : std::filesystem::directory_iterator(root)) EXPECT_NE(entry.path().extension(), ".part");
    }
};

TEST_F(PublicDownloadTest, EmptySingleAndMultipleChunksHaveExactContentsAndHash) {
    unsigned name = 0;
    for (auto compression : {compression_mode::none, compression_mode::always, compression_mode::adaptive}) {
        connect(compression);
        for (auto size : {0, 1024, 300000}) {
            auto filename = "file-" + std::to_string(name++);
            auto bytes = seed(filename, size, compression == compression_mode::always);
            auto download = client->download_file(filename, root / filename); ASSERT_TRUE(download);
            auto result = download.value().wait_for(5s); ASSERT_TRUE(result);
            ASSERT_TRUE(result.value().success) << result.value().error_message.value_or("");
            EXPECT_EQ(download.value().get_status(), transfer_status::completed);
            EXPECT_EQ(result.value().bytes_transferred, bytes.size());
            std::ifstream in(root / filename, std::ios::binary);
            std::vector<char> actual{std::istreambuf_iterator<char>{in}, {}};
            ASSERT_EQ(actual.size(), bytes.size());
            EXPECT_TRUE(std::equal(actual.begin(), actual.end(), bytes.begin(), [](char a, std::byte b) { return static_cast<unsigned char>(a) == std::to_integer<unsigned char>(b); }));
            EXPECT_EQ(checksum::sha256_file(root / filename).value(), checksum::sha256(bytes));
            no_parts();
        }
        EXPECT_EQ(client->get_statistics().total_files_downloaded, 3);
    }
}

TEST_F(PublicDownloadTest, MissingAndRejectedFilesFailWithinDeadline) {
    seed("denied", 100);
    server->on_download_request([](const auto& request) { return request.filename != "denied"; });
    for (auto name : {"absent", "denied"}) {
        auto download = client->download_file(name, root / name); ASSERT_TRUE(download);
        auto result = download.value().wait_for(3s); ASSERT_TRUE(result); EXPECT_FALSE(result.value().success);
        EXPECT_FALSE(std::filesystem::exists(root / name)); no_parts();
    }
    EXPECT_EQ(client->get_statistics().total_files_downloaded, 0);
}

TEST_F(PublicDownloadTest, OverwriteIsExplicitAndDestinationRaceDoesNotClobber) {
    auto bytes = seed("source", 200000);
    auto path = root / "destination";
    { std::ofstream file(path); file << "existing"; }
    EXPECT_FALSE(client->download_file("source", path));
    download_options options; options.overwrite = true;
    auto download = client->download_file("source", path, options); ASSERT_TRUE(download);
    auto result = download.value().wait_for(5s); ASSERT_TRUE(result); ASSERT_TRUE(result.value().success);
    EXPECT_EQ(checksum::sha256_file(path).value(), checksum::sha256(bytes));
    auto race = root / "race";
    client->on_progress([&](const auto&) { std::ofstream file(race); file << "created-during-download"; });
    download = client->download_file("source", race); ASSERT_TRUE(download);
    result = download.value().wait_for(5s); ASSERT_TRUE(result); EXPECT_FALSE(result.value().success);
    std::ifstream file(race); std::string contents; file >> contents; EXPECT_EQ(contents, "created-during-download");
    no_parts();
}

TEST_F(PublicDownloadTest, DisconnectAfterProgressFailsAndCleansPartialFile) {
    seed("interrupted", 300000);
    std::atomic<bool> stopped{false};
    client->on_progress([&](const auto&) { if (!stopped.exchange(true)) (void)server->stop(); });
    auto download = client->download_file("interrupted", root / "interrupted"); ASSERT_TRUE(download);
    auto result = download.value().wait_for(3s); ASSERT_TRUE(result); EXPECT_FALSE(result.value().success);
    EXPECT_FALSE(std::filesystem::exists(root / "interrupted")); no_parts();
}

TEST_F(PublicDownloadTest, BandwidthWaitIsCancellableAndNotifiesOnce) {
    seed("limited", 300000);
    client.reset();
    auto built = file_transfer_client::builder().with_download_bandwidth_limit(32768).build(); ASSERT_TRUE(built);
    client = std::make_unique<file_transfer_client>(std::move(built.value()));
    ASSERT_TRUE(client->connect({"127.0.0.1", port}));
    std::mutex mutex; std::condition_variable cv; unsigned callbacks = 0;
    client->on_complete([&](const auto&) { std::lock_guard lock(mutex); ++callbacks; cv.notify_all(); });
    auto download = client->download_file("limited", root / "limited"); ASSERT_TRUE(download);
    auto pending = download.value().wait_for(100ms); EXPECT_FALSE(pending);
    ASSERT_TRUE(download.value().cancel());
    auto result = download.value().wait_for(1s); ASSERT_TRUE(result); EXPECT_FALSE(result.value().success);
    EXPECT_EQ(download.value().get_status(), transfer_status::cancelled);
    { std::unique_lock lock(mutex); ASSERT_TRUE(cv.wait_for(lock, 1s, [&] { return callbacks == 1; })); }
    client->on_complete({}); no_parts();
}

class InvalidDownloadTest : public ::testing::TestWithParam<std::string> {};
TEST_P(InvalidDownloadTest, InvalidStreamNeverCommitsAFile) {
    auto failure = GetParam();
    auto server = std::make_shared<network::core::messaging_server>("invalid-download");
    server->set_connection_callback([failure](auto session) {
        auto decoder = std::make_shared<wire::stream_decoder>();
        std::weak_ptr<network::session::messaging_session> weak = session;
        session->set_receive_callback([failure, decoder, weak](const auto& bytes) {
            auto peer = weak.lock(); if (!peer) return;
            auto frames = decoder->feed(bytes); if (!frames) return;
            for (const auto& bytes : frames.value()) {
                auto frame = wire::decode_frame(bytes);
                if (!frame || frame.value().type != message_type::download_request || failure == "silent") continue;
                auto request = wire::decode<msg_download_request>(frame.value().payload).value();
                auto send = [&](message_type type, const auto& payload) { auto encoded = wire::encode(type, payload); if (encoded) (void)peer->send(std::move(encoded.value())); };
                chunk data; data.data.resize(65536, std::byte{42}); data.header.id.bytes = request.transfer_id;
                data.header.original_size = data.header.compressed_size = data.data.size();
                data.header.flags = chunk_flags::first_chunk | chunk_flags::last_chunk;
                data.header.checksum = checksum::crc32(data.data);
                msg_download_accept accepted{request.transfer_id, data.data.size(), sha_bytes(data.data), request.compression,
                    wire_encryption_algorithm::none, 65536, 1, 0, 0};
                if (failure == "hash") accepted.sha256_hash[0] ^= 1;
                if (failure != "before-metadata") send(message_type::download_accept, accepted);
                if (failure == "crc") data.header.checksum ^= 1;
                if (failure == "offset") data.header.chunk_offset = 1;
                if (failure == "truncated") { data.data.pop_back(); --data.header.compressed_size; }
                send(message_type::chunk_data, data);
                if (failure == "duplicate") send(message_type::chunk_data, data);
                if (failure != "incomplete") send(message_type::download_complete,
                    msg_download_complete{request.transfer_id, 1, 65536, data.data.size()});
            }
        });
    });
    auto port = available_port(); ASSERT_TRUE(server->start_server(port).is_ok());
    auto built = file_transfer_client::builder().with_transfer_timeout(150ms).build(); ASSERT_TRUE(built);
    auto& client = built.value(); ASSERT_TRUE(client.connect({"127.0.0.1", port}));
    auto root = std::filesystem::temp_directory_path() / ("fts-invalid-" + transfer_id::generate().to_string());
    auto download = client.download_file("invalid", root / "destination"); ASSERT_TRUE(download);
    auto result = download.value().wait_for(3s); ASSERT_TRUE(result); EXPECT_FALSE(result.value().success);
    EXPECT_EQ(download.value().get_status(), transfer_status::failed);
    EXPECT_TRUE(std::filesystem::is_empty(root));
    EXPECT_EQ(client.get_statistics().total_files_downloaded, 0);
    EXPECT_TRUE(client.disconnect()); EXPECT_TRUE(server->stop_server().is_ok());
    std::error_code ec; std::filesystem::remove_all(root, ec);
}
INSTANTIATE_TEST_SUITE_P(Protocol, InvalidDownloadTest,
    ::testing::Values("crc", "hash", "offset", "truncated", "duplicate", "before-metadata", "incomplete", "silent"));
} // namespace kcenon::file_transfer::test
