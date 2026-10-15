#include "protocol_peer.h"
#include <kcenon/file_transfer/client/file_transfer_client.h>
#include <kcenon/file_transfer/server/file_transfer_server.h>
#include <fstream>
#include <future>

namespace kcenon::file_transfer::test {
TEST(PublicReceiveBackpressureTest, OneHundredUploadsCompleteWithExactBytesAndHashes) {
    auto root = std::filesystem::temp_directory_path() / ("fts-receive-" + transfer_id::generate().to_string());
    struct cleanup { std::filesystem::path path; ~cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    auto built = file_transfer_server::builder().with_storage_directory(root / "server")
        .with_max_connections(100).build();
    ASSERT_TRUE(built); auto& server = built.value();
    auto port = available_port(); ASSERT_TRUE(server.start({"127.0.0.1", port}));
    auto source = root / "source";
    { std::ofstream out(source, std::ios::binary); for (std::size_t i = 0; i < 1048576; ++i) out.put(static_cast<char>(i % 251)); }
    auto hash = checksum::sha256_file(source); ASSERT_TRUE(hash);
    std::vector<file_transfer_client> clients;
    for (int i = 0; i < 100; ++i) {
        // This fixture hashes 100 MiB through fully instrumented dependencies.
        // Keep short fault/deadline tests separate from its response budget.
        auto client = file_transfer_client::builder().with_connect_timeout(5s).with_transfer_timeout(90s).build();
        ASSERT_TRUE(client); clients.push_back(std::move(client.value()));
        ASSERT_TRUE(clients.back().connect({"127.0.0.1", port}));
    }
    std::promise<void> release; auto start = release.get_future().share();
    std::vector<std::future<bool>> results;
    for (int i = 0; i < 100; ++i) results.push_back(std::async(std::launch::async, [&, i] {
        start.wait();
        upload_options options; options.compression = compression_mode::none;
        auto upload = clients[i].upload_file(source, "file-" + std::to_string(i), options);
        if (!upload) { ADD_FAILURE() << "client " << i << ": " << upload.error().message; return false; }
        auto result = upload.value().wait_for(120s);
        if (!result) ADD_FAILURE() << "client " << i << ": " << result.error().message;
        else if (!result.value().success) ADD_FAILURE() << "client " << i << ": " << result.value().error_message.value_or("");
        return result && result.value().success && result.value().bytes_transferred == 1048576;
    }));
    release.set_value();
    unsigned succeeded = 0;
    for (auto& result : results) succeeded += result.get();
    ASSERT_EQ(succeeded, 100);
    for (int i = 0; i < 100; ++i) {
        auto path = root / "server" / ("file-" + std::to_string(i));
        ASSERT_EQ(std::filesystem::file_size(path), 1048576);
        auto actual = checksum::sha256_file(path); ASSERT_TRUE(actual);
        EXPECT_EQ(actual.value(), hash.value());
    }
    EXPECT_EQ(server.get_statistics().total_files_uploaded, 100);
    EXPECT_EQ(server.get_statistics().total_bytes_received, 100 * 1048576ULL);
    clients.clear(); EXPECT_TRUE(server.stop());
}
} // namespace kcenon::file_transfer::test
