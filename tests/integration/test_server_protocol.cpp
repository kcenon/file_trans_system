#include "protocol_peer.h"
#include "cloud_storage_stub.h"
#include <kcenon/file_transfer/server/file_transfer_server.h>
#include <kcenon/file_transfer/core/compression_engine.h>
#include <fstream>

namespace kcenon::file_transfer::test {

class ServerProtocolTest : public ::testing::Test {
protected:
    std::filesystem::path root;
    std::unique_ptr<file_transfer_server> server;
    std::unique_ptr<protocol_peer> peer;
    std::mutex event_mutex;
    std::condition_variable event_cv;
    unsigned connected{}, disconnected{}, completed{};
    std::vector<transfer_result> results;

    void SetUp() override {
        root = std::filesystem::temp_directory_path() / ("fts-protocol-test-" + transfer_id::generate().to_string());
        auto built = file_transfer_server::builder().with_storage_directory(root)
            .with_chunk_size(64 * 1024).with_storage_quota(1024 * 1024).build();
        ASSERT_TRUE(built) << built.error().message;
        server = std::make_unique<file_transfer_server>(std::move(built.value()));
        server->on_client_connected([this](const auto&) { std::lock_guard lock(event_mutex); ++connected; event_cv.notify_all(); });
        server->on_client_disconnected([this](const auto&) { std::lock_guard lock(event_mutex); ++disconnected; event_cv.notify_all(); });
        server->on_transfer_complete([this](const auto& value) {
            // Reentrant status queries must not acquire a lock held by the callback invoker.
            (void)server->get_statistics();
            std::lock_guard lock(event_mutex); ++completed; results.push_back(value); event_cv.notify_all();
        });
        auto port = available_port();
        auto started = server->start({"127.0.0.1", port});
        ASSERT_TRUE(started) << started.error().message;
        peer = std::make_unique<protocol_peer>(port);
        std::unique_lock lock(event_mutex);
        ASSERT_TRUE(event_cv.wait_for(lock, 5s, [&] { return connected == 1; }));
    }
    void TearDown() override {
        peer.reset();
        if (server && server->is_running()) (void)server->stop();
        server.reset();
        std::error_code ec; std::filesystem::remove_all(root, ec);
    }
    void upload(const std::string& name, const std::vector<std::byte>& bytes, bool compressed = false) {
        auto request = upload_request_for(name, bytes);
        request.compression = compressed ? wire_compression_mode::lz4 : wire_compression_mode::none;
        peer->send(message_type::upload_request, request);
        auto accepted = peer->receive<msg_upload_accept>(message_type::upload_accept);
        ASSERT_EQ(accepted.transfer_id, request.transfer_id);
        uint64_t index = 0, wire_bytes = 0;
        for (std::size_t offset = 0; offset < bytes.size(); offset += accepted.chunk_size, ++index) {
            chunk data;
            data.header.id = transfer_id{request.transfer_id}; data.header.chunk_index = index; data.header.chunk_offset = offset;
            auto length = std::min<std::size_t>(accepted.chunk_size, bytes.size() - offset);
            data.data.assign(bytes.begin() + offset, bytes.begin() + offset + length);
            data.header.original_size = length; data.header.checksum = checksum::crc32(data.data);
            if (compressed) {
                compression_engine engine;
                auto encoded = engine.compress(data.data);
                ASSERT_TRUE(encoded);
                data.data = std::move(encoded.value()); data.header.flags |= chunk_flags::compressed;
            }
            data.header.compressed_size = data.data.size(); wire_bytes += data.data.size();
            peer->send(message_type::chunk_data, data);
            auto ack = peer->receive<msg_chunk_ack>(message_type::chunk_ack);
            ASSERT_EQ(ack.chunk_index, index);
        }
        peer->send(message_type::upload_complete, msg_upload_complete{request.transfer_id, index, bytes.size(), wire_bytes});
        auto ack = peer->receive<msg_upload_ack>(message_type::upload_ack);
        ASSERT_EQ(ack.verified, 1);
        ASSERT_EQ(ack.stored_path, name);
        EXPECT_EQ(checksum::sha256_file(root / name).value(), checksum::sha256(bytes));
    }
};

TEST_F(ServerProtocolTest, UploadListDownloadPersistsExactBytesAndCallbacks) {
    std::vector<std::byte> bytes(150000);
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = std::byte(i * 17 % 251);
    upload("roundtrip.bin", bytes);
    msg_list_request listing{};
    listing.request_id = transfer_id::generate().bytes; listing.pattern = "*.bin"; listing.limit = 10;
    peer->send(message_type::list_request, listing);
    auto files = peer->receive<msg_list_response>(message_type::list_response);
    ASSERT_EQ(files.entries.size(), 1); EXPECT_EQ(files.entries[0].filename, "roundtrip.bin");
    EXPECT_EQ(files.entries[0].file_size, bytes.size());
    msg_download_request request{};
    request.transfer_id = transfer_id::generate().bytes; request.filename = "roundtrip.bin";
    peer->send(message_type::download_request, request);
    auto metadata = peer->receive<msg_download_accept>(message_type::download_accept);
    EXPECT_EQ(metadata.sha256_hash, sha_bytes(bytes));
    std::vector<std::byte> received;
    for (uint64_t i = 0; i < metadata.total_chunks; ++i) {
        auto data = peer->receive<chunk>(message_type::chunk_data);
        ASSERT_EQ(data.header.chunk_index, i);
        ASSERT_EQ(data.header.chunk_offset, received.size());
        ASSERT_EQ(checksum::crc32(data.data), data.header.checksum);
        received.insert(received.end(), data.data.begin(), data.data.end());
        peer->send(message_type::chunk_ack, msg_chunk_ack{request.transfer_id, i});
    }
    auto complete = peer->receive<msg_download_complete>(message_type::download_complete);
    ASSERT_EQ(received, bytes); EXPECT_EQ(complete.bytes_sent, bytes.size());
    peer->send(message_type::download_ack, msg_download_ack{request.transfer_id, 1, received.size()});
    {
        std::unique_lock lock(event_mutex);
        ASSERT_TRUE(event_cv.wait_for(lock, 5s, [&] { return completed == 2; }));
        ASSERT_EQ(results.size(), 2); EXPECT_TRUE(results[0].success); EXPECT_TRUE(results[1].success);
    }
    EXPECT_EQ(server->get_statistics().active_transfers, 0);
    EXPECT_EQ(server->get_statistics().total_files_uploaded, 1);
    EXPECT_EQ(server->get_statistics().total_files_downloaded, 1);
    peer->disconnect();
    std::unique_lock lock(event_mutex);
    ASSERT_TRUE(event_cv.wait_for(lock, 5s, [&] { return disconnected == 1; }));
    EXPECT_EQ(server->get_statistics().active_connections, 0);
}

TEST_F(ServerProtocolTest, EmptyAndCompressedUploadsAreVerified) {
    upload("empty.bin", {});
    upload("compressed.bin", std::vector<std::byte>(140000, std::byte{42}), true);
}

TEST_F(ServerProtocolTest, ApprovalRejectionAndMissingFileReturnErrors) {
    server->on_upload_request([](const auto&) { return false; });
    peer->send(message_type::upload_request, upload_request_for("rejected", {}));
    EXPECT_EQ(peer->receive<msg_upload_reject>(message_type::upload_reject).reason_code,
              static_cast<int>(error_code::file_access_denied));
    msg_download_request request{}; request.transfer_id = transfer_id::generate().bytes; request.filename = "absent";
    peer->send(message_type::download_request, request);
    EXPECT_EQ(peer->receive<msg_download_reject>(message_type::download_reject).reason_code,
              static_cast<int>(error_code::file_not_found));
    EXPECT_EQ(server->get_statistics().active_transfers, 0);
    EXPECT_FALSE(std::filesystem::exists(root / "rejected"));
}

TEST_F(ServerProtocolTest, PathsAndMalformedFramesAreRejected) {
    for (const auto& name : {"../escape", "/absolute", "..\\escape", "C:escape"}) {
        peer->send(message_type::upload_request, upload_request_for(name, {}));
        EXPECT_EQ(peer->receive<msg_upload_reject>(message_type::upload_reject).reason_code,
                  static_cast<int>(error_code::invalid_file_path));
    }
    auto encoded = wire::encode(message_type::upload_request, upload_request_for("valid", {}));
    ASSERT_TRUE(encoded); encoded.value().back() ^= 1; peer->raw(std::move(encoded.value()));
    EXPECT_FALSE(peer->receive<msg_error>(message_type::error).message.empty());
}

TEST_F(ServerProtocolTest, QuotaReservationsAreReleasedOnCancellation) {
    auto first = upload_request_for("first", std::vector<std::byte>(700000));
    auto second = upload_request_for("second", std::vector<std::byte>(700000));
    peer->send(message_type::upload_request, first);
    (void)peer->receive<msg_upload_accept>(message_type::upload_accept);
    peer->send(message_type::upload_request, second);
    EXPECT_EQ(peer->receive<msg_upload_reject>(message_type::upload_reject).reason_code,
              static_cast<int>(error_code::quota_exceeded));
    peer->send(message_type::transfer_cancel, msg_transfer_cancel{first.transfer_id, 0, "test"});
    peer->send(message_type::upload_request, second);
    EXPECT_EQ(peer->receive<msg_upload_accept>(message_type::upload_accept).transfer_id, second.transfer_id);
    EXPECT_EQ(server->get_statistics().active_uploads, 1);
}

TEST_F(ServerProtocolTest, CorruptionNeverCommitsAnUpload) {
    std::vector<std::byte> bytes(1024, std::byte{42});
    auto request = upload_request_for("corrupt", bytes);
    peer->send(message_type::upload_request, request);
    (void)peer->receive<msg_upload_accept>(message_type::upload_accept);
    chunk data;
    data.header.id = transfer_id{request.transfer_id}; data.header.original_size = bytes.size();
    data.header.compressed_size = bytes.size(); data.header.checksum = checksum::crc32(bytes) ^ 1; data.data = bytes;
    peer->send(message_type::chunk_data, data);
    EXPECT_FALSE(peer->receive<msg_error>(message_type::error).message.empty());
    EXPECT_FALSE(std::filesystem::exists(root / "corrupt"));
    EXPECT_EQ(server->get_statistics().total_files_uploaded, 0);
}

TEST_F(ServerProtocolTest, ConcurrentClientsCannotOverbookQuota) {
    protocol_peer other(server->port());
    auto first = upload_request_for("first", std::vector<std::byte>(700000));
    auto second = upload_request_for("second", std::vector<std::byte>(700000));
    auto sent = std::async(std::launch::async, [&] { peer->send(message_type::upload_request, first); });
    other.send(message_type::upload_request, second); sent.get();
    auto a = peer->receive(); auto b = other.receive();
    EXPECT_TRUE((a.type == message_type::upload_accept && b.type == message_type::upload_reject) ||
                (b.type == message_type::upload_accept && a.type == message_type::upload_reject));
    EXPECT_EQ(server->get_statistics().active_uploads, 1);
}

TEST(ProtocolCodecTest, RejectsTruncationTrailingBytesAndInvalidListAllocation) {
    auto request = upload_request_for("small", std::vector<std::byte>(1));
    auto encoded = wire::encode(message_type::upload_request, request);
    ASSERT_TRUE(encoded);
    for (std::size_t length = 0; length < encoded.value().size(); ++length)
        EXPECT_FALSE(wire::decode_frame(std::span(encoded.value()).first(length)));
    auto frame = wire::decode_frame(encoded.value()); ASSERT_TRUE(frame);
    auto decoded = wire::decode<msg_upload_request>(frame.value().payload); ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value().filename, request.filename); EXPECT_EQ(decoded.value().file_size, 1);
    frame.value().payload.push_back(0); EXPECT_FALSE(wire::decode<msg_upload_request>(frame.value().payload));
    std::vector<uint8_t> invalid(msg_list_response::min_serialized_size, 0xff);
    EXPECT_FALSE(wire::decode<msg_list_response>(invalid));
}

TEST(ProtocolCodecTest, DecodesEverySplitAndCoalescedMessages) {
    auto a = wire::encode(message_type::upload_request, upload_request_for("one", {})).value();
    auto b = wire::encode(message_type::upload_request, upload_request_for("two", {})).value();
    a.insert(a.end(), b.begin(), b.end());
    for (std::size_t split = 0; split <= a.size(); ++split) {
        wire::stream_decoder decoder;
        auto first = decoder.feed(std::span(a).first(split));
        auto second = decoder.feed(std::span(a).subspan(split));
        ASSERT_TRUE(first); ASSERT_TRUE(second);
        EXPECT_EQ(first.value().size() + second.value().size(), 2);
    }
}

class recording_cloud : public cloud_storage_stub {
public:
    bool fail_upload = false;
    bool connected = false;
    std::string last_key;
    std::vector<std::byte> stored;
    auto connect() -> result<void> override { connected = true; return {}; }
    auto disconnect() -> result<void> override { connected = false; return {}; }
    auto is_connected() const -> bool override { return connected; }
    auto list_objects(const list_objects_options&) -> result<list_objects_result> override { return list_objects_result{}; }
    auto exists(const std::string&) -> result<bool> override { return false; }
    auto upload_file(const std::filesystem::path& path, const std::string& key,
                     const cloud_transfer_options&) -> result<upload_result> override {
        last_key = key;
        if (fail_upload) return unexpected{error{error_code::file_write_error, "Injected backend failure"}};
        std::ifstream input(path, std::ios::binary);
        std::vector<char> bytes{std::istreambuf_iterator<char>(input), {}};
        for (auto byte : bytes) stored.push_back(std::byte(static_cast<unsigned char>(byte)));
        upload_result result; result.bytes_uploaded = bytes.size(); return result;
    }
};

TEST(ServerStorageRoutingTest, CloudOnlyAndHybridUseConfiguredBackendAndPrefix) {
    for (auto mode : {storage_mode::cloud_only, storage_mode::hybrid}) {
        auto root = std::filesystem::temp_directory_path() / ("fts-cloud-route-" + transfer_id::generate().to_string());
        auto cloud = std::make_shared<recording_cloud>();
        auto built = file_transfer_server::builder().with_storage_directory(root)
            .with_storage_mode(mode).with_cloud_storage(cloud).with_cloud_key_prefix("objects/")
            .with_cloud_cache(false).build();
        ASSERT_TRUE(built);
        auto& server = built.value(); auto port = available_port();
        auto started = server.start({"127.0.0.1", port}); ASSERT_TRUE(started) << started.error().message;
        {
            protocol_peer peer(port);
            auto request = upload_request_for("empty.bin", {});
            peer.send(message_type::upload_request, request);
            (void)peer.receive<msg_upload_accept>(message_type::upload_accept);
            peer.send(message_type::upload_complete, msg_upload_complete{request.transfer_id, 0, 0, 0});
            EXPECT_EQ(peer.receive<msg_upload_ack>(message_type::upload_ack).verified, 1);
            EXPECT_EQ(cloud->last_key, "objects/empty.bin");
            EXPECT_EQ(std::filesystem::exists(root / "empty.bin"), mode == storage_mode::hybrid);
        }
        EXPECT_TRUE(server.stop());
        std::error_code ec; std::filesystem::remove_all(root, ec);
    }
}

TEST(ServerStorageRoutingTest, ReplicationFailureIsNotSuccessfulPersistence) {
    auto root = std::filesystem::temp_directory_path() / ("fts-cloud-fail-" + transfer_id::generate().to_string());
    auto cloud = std::make_shared<recording_cloud>(); cloud->fail_upload = true;
    auto built = file_transfer_server::builder().with_storage_directory(root)
        .with_storage_mode(storage_mode::hybrid).with_cloud_storage(cloud).with_cloud_cache(false).build();
    ASSERT_TRUE(built); auto& server = built.value(); auto port = available_port();
    ASSERT_TRUE(server.start({"127.0.0.1", port}));
    {
        protocol_peer peer(port); auto request = upload_request_for("empty.bin", {});
        peer.send(message_type::upload_request, request);
        (void)peer.receive<msg_upload_accept>(message_type::upload_accept);
        peer.send(message_type::upload_complete, msg_upload_complete{request.transfer_id, 0, 0, 0});
        EXPECT_NE(peer.receive<msg_error>(message_type::error).message.find("Injected backend failure"), std::string::npos);
        EXPECT_EQ(server.get_statistics().total_files_uploaded, 0);
    }
    EXPECT_TRUE(server.stop());
    std::error_code ec; std::filesystem::remove_all(root, ec);
}

TEST(ServerResourceLimitTest, ExcessConnectionsAndOversizedFilesAreRejected) {
    auto root = std::filesystem::temp_directory_path() / ("fts-limits-" + transfer_id::generate().to_string());
    auto built = file_transfer_server::builder().with_storage_directory(root).with_max_connections(1).with_max_file_size(10).build();
    ASSERT_TRUE(built); auto& server = built.value(); auto port = available_port();
    ASSERT_TRUE(server.start({"127.0.0.1", port}));
    {
        protocol_peer first(port);
        first.send(message_type::upload_request, upload_request_for("large", std::vector<std::byte>(11)));
        EXPECT_EQ(first.receive<msg_upload_reject>(message_type::upload_reject).reason_code, static_cast<int>(error_code::file_too_large));
        protocol_peer second(port);
        auto response = second.receive<msg_error>(message_type::error);
        EXPECT_EQ(response.error_code, static_cast<int>(error_code::connection_refused));
        EXPECT_EQ(server.get_statistics().active_connections, 1);
    }
    EXPECT_TRUE(server.stop());
    EXPECT_EQ(server.get_statistics().active_connections, 0);
    std::error_code ec; std::filesystem::remove_all(root, ec);
}
} // namespace kcenon::file_transfer::test
