#include "protocol_peer.h"
#include <kcenon/file_transfer/client/file_transfer_client.h>
#include <kcenon/file_transfer/server/file_transfer_server.h>
#include <kcenon/file_transfer/encryption/transfer_crypto.h>
#include <kcenon/file_transfer/server/server_pipeline.h>
#include <fstream>

namespace kcenon::file_transfer::test {
class PublicEncryptionTest : public ::testing::Test {
protected:
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("fts-crypto-" + transfer_id::generate().to_string());
    std::vector<std::byte> key = std::vector<std::byte>(32, std::byte{0x73});
    std::unique_ptr<file_transfer_server> server;
    uint16_t port{};
    void start(bool password = false, bool required = true) {
        auto builder = file_transfer_server::builder().with_storage_directory(root / "server").with_chunk_size(65536);
        if (password) builder.with_encryption_password("public-integration-test-password");
        else builder.with_encryption_key(key);
        builder.with_required_encryption(required);
        auto built = builder.build(); ASSERT_TRUE(built) << built.error().message;
        server = std::make_unique<file_transfer_server>(std::move(built.value()));
        port = available_port(); ASSERT_TRUE(server->start({"127.0.0.1", port}));
    }
    auto client(bool password = false, bool wrong = false) -> file_transfer_client {
        auto builder = file_transfer_client::builder().with_transfer_timeout(5s);
        if (password) builder.with_encryption_password("public-integration-test-password");
        else { auto secret = key; if (wrong) secret[0] ^= std::byte{1}; builder.with_encryption_key(secret); }
        auto built = builder.build(); if (!built) throw std::runtime_error(built.error().message);
        auto c = std::move(built.value()); if (!c.connect({"127.0.0.1", port})) throw std::runtime_error("connect failed"); return c;
    }
    auto source(std::string name, size_t size) -> std::filesystem::path {
        std::filesystem::create_directories(root);
        auto p = root / name; std::ofstream f(p, std::ios::binary);
        for (size_t i=0; i<size; ++i) f.put(static_cast<char>((i * 17) % 251)); return p;
    }
    void TearDown() override { if (server) (void)server->stop(); server.reset(); std::error_code ec; std::filesystem::remove_all(root, ec); }
};
TEST_F(PublicEncryptionTest, RawKeyAndPasswordRoundTripEmptyAndCompressedChunks) {
    for (bool password : {false, true}) {
        start(password); auto c = client(password);
        for (size_t size : {size_t{0}, size_t{220000}}) {
            auto name = std::to_string(size); auto p = source("source", size);
            upload_options options; options.compression = compression_mode::always;
            auto up = c.upload_file(p, name, options); ASSERT_TRUE(up);
            auto done = up.value().wait_for(10s); ASSERT_TRUE(done); ASSERT_TRUE(done.value().success) << done.value().error_message.value_or("");
            auto out = root / ("out" + name); download_options down; down.overwrite = true;
            auto dl = c.download_file(name, out, down); ASSERT_TRUE(dl);
            auto received = dl.value().wait_for(10s); ASSERT_TRUE(received); ASSERT_TRUE(received.value().success) << received.value().error_message.value_or("");
            EXPECT_EQ(checksum::sha256_file(p).value(), checksum::sha256_file(out).value());
            EXPECT_EQ(checksum::sha256_file(p).value(), checksum::sha256_file(root / "server" / name).value());
        }
        ASSERT_TRUE(c.disconnect()); ASSERT_TRUE(server->stop()); server.reset();
        std::filesystem::remove_all(root / "server");
    }
}
TEST_F(PublicEncryptionTest, WrongKeysFailEvenForEmptyFilesAndDoNotAffectConcurrentValidTransfers) {
    start(); auto good = client(); auto bad = client(false, true); auto p = source("source", 190000);
    auto a = good.upload_file(p, "valid"); auto b = bad.upload_file(p, "invalid"); ASSERT_TRUE(a); ASSERT_TRUE(b);
    auto good_result = a.value().wait_for(10s); auto bad_result = b.value().wait_for(10s);
    ASSERT_TRUE(good_result); ASSERT_TRUE(bad_result); EXPECT_TRUE(good_result.value().success); EXPECT_FALSE(bad_result.value().success);
    EXPECT_FALSE(std::filesystem::exists(root / "server/invalid"));
    auto zero = source("zero", 0); auto empty = bad.upload_file(zero, "empty"); ASSERT_TRUE(empty);
    auto empty_result = empty.value().wait_for(10s); ASSERT_TRUE(empty_result); EXPECT_FALSE(empty_result.value().success);
    auto download = bad.download_file("valid", root / "invalid-download"); ASSERT_TRUE(download);
    auto downloaded = download.value().wait_for(10s); ASSERT_TRUE(downloaded); EXPECT_FALSE(downloaded.value().success);
    EXPECT_FALSE(std::filesystem::exists(root / "invalid-download"));
}
TEST_F(PublicEncryptionTest, PlaintextOverrideIsExplicitAndRequiredPolicyRejectsIt) {
    for (bool required : {false, true}) {
        start(false, required); auto c = client(); auto p = source("plain-source", 20);
        upload_options options; options.encrypt = false;
        auto up = c.upload_file(p, "plain", options); ASSERT_TRUE(up);
        auto done = up.value().wait_for(5s); ASSERT_TRUE(done); EXPECT_EQ(done.value().success, !required);
        ASSERT_TRUE(c.disconnect()); ASSERT_TRUE(server->stop()); server.reset(); std::filesystem::remove_all(root / "server");
    }
}
TEST_F(PublicEncryptionTest, CiphertextIsVisibleOnWireAndAuthenticatesHeaderTagAndPayload) {
    start(); auto c = client(); auto p = source("source", 512);
    auto uploaded = c.upload_file(p, "observed"); ASSERT_TRUE(uploaded); ASSERT_TRUE(uploaded.value().wait_for(5s).value().success);
    protocol_peer peer(port); msg_download_request request{}; request.transfer_id = transfer_id::generate().bytes;
    request.filename = "observed"; request.encryption = wire_encryption_algorithm::aes_256_gcm;
    request.encryption_salt = transfer_crypto::random_salt().value();
    auto initial = transfer_crypto::create(key, "", transfer_id{request.transfer_id}, request.encryption_salt, false).value();
    ASSERT_TRUE(initial->sign(message_type::download_request, request)); peer.send(message_type::download_request, request);
    auto accepted = peer.receive<msg_download_accept>(message_type::download_accept);
    auto crypto = transfer_crypto::create(key, "", transfer_id{request.transfer_id}, request.encryption_salt, false, &accepted.encryption_salt).value();
    ASSERT_TRUE(crypto->verify(message_type::download_accept, accepted));
    auto data = peer.receive<chunk>(message_type::chunk_data); EXPECT_TRUE(is_encrypted(data.header.flags));
    EXPECT_EQ(data.data.size(), 512 + 28); EXPECT_NE(checksum::sha256(data.data), checksum::sha256_file(p).value());
    for (int mutation : {0, 1, 2}) {
        auto tampered = data;
        if (mutation == 0) tampered.data[12] ^= std::byte{1};
        if (mutation == 1) tampered.data.back() ^= std::byte{1};
        if (mutation == 2) ++tampered.header.chunk_offset;
        EXPECT_FALSE(crypto->open(tampered));
    }
    ASSERT_TRUE(crypto->open(data)); EXPECT_EQ(checksum::sha256(data.data), checksum::sha256_file(p).value());
    msg_chunk_ack ack{request.transfer_id, 0}; ASSERT_TRUE(crypto->sign(message_type::chunk_ack, ack)); peer.send(message_type::chunk_ack, ack);
    auto completed = peer.receive<msg_download_complete>(message_type::download_complete);
    ASSERT_TRUE(crypto->verify(message_type::download_complete, completed));
}
TEST_F(PublicEncryptionTest, TamperedUploadNeverCommitsOutput) {
    start();
    for (int mutation : {0, 1, 2}) {
        protocol_peer peer(port); std::vector<std::byte> bytes(400, std::byte{0x55});
        auto request = upload_request_for("tampered" + std::to_string(mutation), bytes);
        request.encryption = wire_encryption_algorithm::aes_256_gcm; request.encryption_salt = transfer_crypto::random_salt().value();
        auto initial = transfer_crypto::create(key, "", transfer_id{request.transfer_id}, request.encryption_salt, true).value();
        ASSERT_TRUE(initial->sign(message_type::upload_request, request)); peer.send(message_type::upload_request, request);
        auto accepted = peer.receive<msg_upload_accept>(message_type::upload_accept);
        auto crypto = transfer_crypto::create(key, "", transfer_id{request.transfer_id}, request.encryption_salt, true, &accepted.encryption_salt).value();
        ASSERT_TRUE(crypto->verify(message_type::upload_accept, accepted));
        chunk data; data.header.id.bytes=request.transfer_id; data.header.original_size=bytes.size(); data.header.compressed_size=bytes.size();
        data.header.flags=chunk_flags::first_chunk | chunk_flags::last_chunk; data.header.checksum=checksum::crc32(bytes); data.data=bytes;
        ASSERT_TRUE(crypto->seal(data));
        if (mutation == 0) data.data[12] ^= std::byte{1};
        if (mutation == 1) data.data.back() ^= std::byte{1};
        if (mutation == 2) ++data.header.checksum;
        peer.send(message_type::chunk_data, data); EXPECT_EQ(peer.receive().type, message_type::error);
        EXPECT_FALSE(std::filesystem::exists(root / "server" / request.filename));
    }
}
TEST(EncryptionPolicyTest, InvalidOrMissingSecretsFailBeforeTransfer) {
    EXPECT_FALSE(file_transfer_client::builder().with_encryption(true).build());
    EXPECT_FALSE(file_transfer_client::builder().with_encryption_key(std::vector<std::byte>(31)).build());
    EXPECT_FALSE(file_transfer_server::builder().with_storage_directory(std::filesystem::temp_directory_path()).with_required_encryption().build());
    pipeline_config config; config.enable_encryption = true;
    auto pipeline = server_pipeline::create(config); ASSERT_TRUE(pipeline); EXPECT_FALSE(pipeline.value().start());
}
} // namespace kcenon::file_transfer::test
