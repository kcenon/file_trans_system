#include "protocol_peer.h"
#include <kcenon/file_transfer/client/file_transfer_client.h>
#include <kcenon/file_transfer/server/file_transfer_server.h>
#include <kcenon/network/core/messaging_server.h>
#include <kcenon/network/session/messaging_session.h>
#include <fstream>
#include <future>

namespace kcenon::file_transfer::test {
class PublicListingTest : public ::testing::Test {
protected:
    std::filesystem::path root;
    std::unique_ptr<file_transfer_server> server;
    std::unique_ptr<file_transfer_client> client;
    void SetUp() override {
        root = std::filesystem::temp_directory_path() / ("fts-listing-" + transfer_id::generate().to_string());
        auto built = file_transfer_server::builder().with_storage_directory(root).build();
        ASSERT_TRUE(built); server = std::make_unique<file_transfer_server>(std::move(built.value()));
        auto port = available_port(); ASSERT_TRUE(server->start({"127.0.0.1", port}));
        auto c = file_transfer_client::builder().with_transfer_timeout(1s).build(); ASSERT_TRUE(c);
        client = std::make_unique<file_transfer_client>(std::move(c.value())); ASSERT_TRUE(client->connect({"127.0.0.1", port}));
    }
    void TearDown() override {
        client.reset(); if (server) (void)server->stop(); server.reset();
        std::error_code ec; std::filesystem::remove_all(root, ec);
    }
    void seed() {
        for (auto [name, contents] : {std::pair{"z.txt", "z"}, {"a.bin", "alpha"}, {"b.txt", "beta"}, {"c.txt", "charlie"}}) {
            { std::ofstream file(root / name); file << contents; }
            auto modified = std::chrono::system_clock::time_point{std::chrono::seconds{1700000000 + static_cast<int>(std::string(contents).size())}};
            std::filesystem::last_write_time(root / name, std::filesystem::file_time_type::clock::from_sys(modified));
        }
    }
    auto names(const list_options& options = {}) -> std::vector<std::string> {
        auto result = client->list_files(options);
        if (!result) throw std::runtime_error(result.error().message);
        std::vector<std::string> out; for (const auto& file : result.value()) out.push_back(file.filename); return out;
    }
};

TEST_F(PublicListingTest, PopulatedListingIncludesExactMetadata) {
    ASSERT_TRUE(client->list_files()); EXPECT_TRUE(names().empty());
    seed();
    EXPECT_EQ(names(), (std::vector<std::string>{"a.bin", "b.txt", "c.txt", "z.txt"}));
    auto result = client->list_files(); ASSERT_TRUE(result); ASSERT_EQ(result.value().size(), 4);
    for (const auto& file : result.value()) {
        EXPECT_EQ(file.size, std::filesystem::file_size(root / file.filename));
        EXPECT_EQ(file.sha256_hash, checksum::sha256_file(root / file.filename).value());
        EXPECT_EQ(std::chrono::duration_cast<std::chrono::seconds>(file.modified_time.time_since_epoch()).count(), 1700000000 + file.size);
    }
}

TEST_F(PublicListingTest, FiltersOrderingAndPageBoundariesAreDeterministic) {
    seed(); list_options options; options.pattern = "?.txt"; options.limit = 2;
    EXPECT_EQ(names(options), (std::vector<std::string>{"b.txt", "c.txt"}));
    options.offset = 2; EXPECT_EQ(names(options), (std::vector<std::string>{"z.txt"}));
    options.offset = 3; EXPECT_TRUE(names(options).empty());
    options.offset = 100; EXPECT_TRUE(names(options).empty());
    options.offset = 0; options.pattern = "missing*"; EXPECT_TRUE(names(options).empty());
    options.pattern = "*"; options.limit = 0; options.sort_by = list_sort_field::size;
    EXPECT_EQ(names(options), (std::vector<std::string>{"z.txt", "b.txt", "a.bin", "c.txt"}));
    options.sort_by = list_sort_field::time; options.sort_order = list_sort_order::descending;
    EXPECT_EQ(names(options), (std::vector<std::string>{"c.txt", "a.bin", "b.txt", "z.txt"}));
    auto first = std::async(std::launch::async, [&] { return names({"*.txt"}); });
    auto second = std::async(std::launch::async, [&] { return names({"*.bin"}); });
    EXPECT_EQ(first.get(), (std::vector<std::string>{"b.txt", "c.txt", "z.txt"}));
    EXPECT_EQ(second.get(), (std::vector<std::string>{"a.bin"}));
}

TEST_F(PublicListingTest, BackendFailureIsAnErrorRatherThanAnEmptyList) {
    std::filesystem::remove_all(root);
    auto result = client->list_files(); ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, error_code::file_read_error);
    EXPECT_TRUE(client->disconnect()); EXPECT_FALSE(client->list_files());
}

TEST(PublicListingFailureTest, SilentMalformedAndServiceErrorResponsesRemainErrors) {
    for (auto failure : {"silent", "malformed", "denied"}) {
        auto server = std::make_shared<network::core::messaging_server>("list-peer");
        server->set_connection_callback([failure = std::string(failure)](auto session) {
            auto decoder = std::make_shared<wire::stream_decoder>();
            std::weak_ptr<network::session::messaging_session> weak = session;
            session->set_receive_callback([failure, decoder, weak](const auto& bytes) {
                auto peer = weak.lock(); if (!peer || failure == "silent") return;
                auto frames = decoder->feed(bytes); if (!frames) return;
                for (const auto& bytes : frames.value()) {
                    auto frame = wire::decode_frame(bytes); if (!frame || frame.value().type != message_type::list_request) continue;
                    auto request = wire::decode<msg_list_request>(frame.value().payload).value();
                    if (failure == "denied") {
                        auto out = wire::encode(message_type::error, msg_error{static_cast<int32_t>(error_code::file_access_denied), "denied", request.request_id});
                        (void)peer->send(std::move(out.value()));
                    } else {
                        msg_list_response response{}; response.request_id = request.request_id; response.total_count = 2;
                        // An empty page is inconsistent with total_count and requested offset/limit.
                        auto out = wire::encode(message_type::list_response, response); (void)peer->send(std::move(out.value()));
                    }
                }
            });
        });
        auto port = available_port(); ASSERT_TRUE(server->start_server(port).is_ok());
        auto built = file_transfer_client::builder().with_transfer_timeout(100ms).build(); ASSERT_TRUE(built);
        auto& client = built.value(); ASSERT_TRUE(client.connect({"127.0.0.1", port}));
        auto result = client.list_files(); ASSERT_FALSE(result) << failure;
        if (std::string(failure) == "silent") EXPECT_EQ(result.error().code, error_code::transfer_timeout);
        if (std::string(failure) == "denied") EXPECT_EQ(result.error().code, error_code::file_access_denied);
        EXPECT_TRUE(client.disconnect()); EXPECT_TRUE(server->stop_server().is_ok());
    }
}
} // namespace kcenon::file_transfer::test
