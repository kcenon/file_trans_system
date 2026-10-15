#include "protocol_peer.h"
#include <kcenon/file_transfer/client/file_transfer_client.h>
#include <kcenon/file_transfer/server/file_transfer_server.h>
#include <fstream>
namespace kcenon::file_transfer::test {
class PublicTransportTest : public ::testing::TestWithParam<bool> {};
TEST_P(PublicTransportTest, UploadListDownloadUsesSelectedTransportAndPersistsExactBytes) {
    auto root = std::filesystem::temp_directory_path() / ("fts-transport-" + transfer_id::generate().to_string());
    struct cleanup { std::filesystem::path path; ~cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    auto server_builder = file_transfer_server::builder().with_storage_directory(root / "server");
    auto client_builder = file_transfer_client::builder().with_connect_timeout(1500ms).with_transfer_timeout(3s);
    if (GetParam()) {
        auto data = std::filesystem::path(FILE_TRANS_TEST_DATA_DIR) / "quic";
        quic_transport_config server_config; server_config.enable_0rtt=false;
        server_config.cert_path=(data / "server-cert.pem").string(); server_config.key_path=(data / "server-key.pem").string();
        server_builder.with_transport(server_config);
        quic_transport_config client_config; client_config.enable_0rtt=false; client_config.ca_path=server_config.cert_path;
        client_config.server_name="localhost"; client_builder.with_transport(client_config);
    }
    auto built = server_builder.build(); ASSERT_TRUE(built) << built.error().message; auto& server=built.value();
    auto port=available_port(); ASSERT_TRUE(server.start({"127.0.0.1",port}));
    auto c=client_builder.build(); ASSERT_TRUE(c); auto& client=c.value();
    auto connected=client.connect({"127.0.0.1",port}); ASSERT_TRUE(connected) << connected.error().message;
    EXPECT_EQ(client.transport_name(), GetParam() ? "quic" : "tcp"); EXPECT_EQ(server.transport_name(), client.transport_name());
    auto source=root / "source"; { std::ofstream f(source,std::ios::binary); for(int i=0;i<200000;++i) f.put(static_cast<char>(i%251)); }
    auto up=client.upload_file(source,"data"); ASSERT_TRUE(up); auto uploaded=up.value().wait_for(5s);
    ASSERT_TRUE(uploaded); ASSERT_TRUE(uploaded.value().success) << uploaded.value().error_message.value_or("");
    auto listed=client.list_files(); ASSERT_TRUE(listed); ASSERT_EQ(listed.value().size(),1); EXPECT_EQ(listed.value()[0].filename,"data");
    auto down=client.download_file("data",root / "download"); ASSERT_TRUE(down); auto downloaded=down.value().wait_for(5s);
    ASSERT_TRUE(downloaded); ASSERT_TRUE(downloaded.value().success) << downloaded.value().error_message.value_or("");
    EXPECT_EQ(checksum::sha256_file(source).value(),checksum::sha256_file(root / "server/data").value());
    EXPECT_EQ(checksum::sha256_file(source).value(),checksum::sha256_file(root / "download").value());
    auto bytes=client.network_bytes(); EXPECT_GT(bytes.first,0); EXPECT_GT(bytes.second,0);
    EXPECT_TRUE(client.disconnect()); EXPECT_TRUE(server.stop());
}
INSTANTIATE_TEST_SUITE_P(Tcp,PublicTransportTest,::testing::Values(false));
#ifdef FILE_TRANS_ENABLE_QUIC
INSTANTIATE_TEST_SUITE_P(Quic,PublicTransportTest,::testing::Values(true));
TEST(PublicTransportPolicyTest, QuicHandshakeTimeoutCanBeRepeatedAndDestroyedSafely) {
    // A bound UDP endpoint that never answers makes the deadline deterministic.
    asio::io_context context;
    asio::ip::udp::socket silent_peer(context, {asio::ip::udp::v4(), 0});
    auto port = silent_peer.local_endpoint().port();
    for (int attempt = 0; attempt < 3; ++attempt) {
        quic_transport_config config; config.enable_0rtt = false;
        auto built = file_transfer_client::builder().with_transport(config)
            .with_connect_timeout(100ms).build();
        ASSERT_TRUE(built);
        auto start = std::chrono::steady_clock::now();
        EXPECT_FALSE(built.value().connect({"127.0.0.1", port}));
        EXPECT_FALSE(built.value().is_connected());
        EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);
        EXPECT_TRUE(built.value().disconnect());
    }
}

#else
TEST(PublicTransportPolicyTest, DisabledQuicCannotSilentlyOpenTcp) {
    EXPECT_FALSE(file_transfer_client::builder().with_transport(quic_transport_config{}).build());
    EXPECT_FALSE(file_transfer_server::builder().with_storage_directory(std::filesystem::temp_directory_path()).with_transport(quic_transport_config{}).build());
}
#endif
TEST(PublicTransportPolicyTest, UnavailableTcpPeerFailsWithinDeadline) {
    auto built=file_transfer_client::builder().with_connect_timeout(100ms).build(); ASSERT_TRUE(built);
    auto start=std::chrono::steady_clock::now(); EXPECT_FALSE(built.value().connect({"127.0.0.1",available_port()}));
    EXPECT_LT(std::chrono::steady_clock::now()-start,2s); EXPECT_FALSE(built.value().is_connected());
}
} // namespace kcenon::file_transfer::test
