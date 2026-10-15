#include <gtest/gtest.h>
#include <asio.hpp>
#include <kcenon/file_transfer/cloud/cloud_http_client.h>

#include <future>
#include <sstream>
#include <thread>

namespace kcenon::file_transfer {
namespace {

// One real HTTP exchange, with bounded input and teardown even if the client fails.
class http_peer {
public:
    http_peer(int status, const std::string& response_body)
        : listener_(io_, {asio::ip::address_v4::loopback(), 0}), socket_(io_),
          input_(64 * 1024), received_(request_.get_future()) {
        response_ = "HTTP/1.1 " + std::to_string(status) + " Test\r\n"
            "Content-Length: " + std::to_string(response_body.size()) +
            "\r\nX-Request-Id: loopback\r\nConnection: close\r\n\r\n" + response_body;
        listener_.async_accept(socket_, [this](auto error) {
            if (error) return;
            asio::async_read_until(socket_, input_, "\r\n\r\n", [this](auto error, auto size) {
                if (error) return;
                std::istream stream(&input_);
                headers_.resize(size);
                stream.read(headers_.data(), size);
                std::istringstream lines(headers_);
                std::string line;
                std::size_t length = 0;
                while (std::getline(lines, line)) {
                    if (line.starts_with("Content-Length: ")) {
                        length = std::stoul(line.substr(16));
                    }
                }
                if (length > 32 * 1024) return;
                if (input_.size() >= length) return respond(length);
                asio::async_read(socket_, input_, asio::transfer_exactly(length - input_.size()),
                    [this, length](auto error, auto) { if (!error) respond(length); });
            });
        });
        thread_ = std::thread([this] { io_.run(); });
    }

    ~http_peer() { io_.stop(); thread_.join(); }

    std::string url() const {
        return "http://127.0.0.1:" + std::to_string(listener_.local_endpoint().port()) + "/object";
    }

    std::future<std::pair<std::string, std::string>>& received() { return received_; }

private:
    void respond(std::size_t length) {
        std::string body(length, '\0');
        std::istream stream(&input_);
        stream.read(body.data(), length);
        request_.set_value({headers_, body});
        asio::async_write(socket_, asio::buffer(response_), [this](auto, auto) {
            asio::error_code ignored;
            socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
            socket_.close(ignored);
        });
    }

    asio::io_context io_;
    asio::ip::tcp::acceptor listener_;
    asio::ip::tcp::socket socket_;
    asio::streambuf input_;
    std::string response_, headers_;
    std::promise<std::pair<std::string, std::string>> request_;
    std::future<std::pair<std::string, std::string>> received_;
    std::thread thread_;
};

TEST(CloudHttpLoopbackTest, AllVerbsPreserveWireBodiesHeadersAndStatus) {
    const std::string payload("binary\0\xff\r\n", 10);
    const std::vector<uint8_t> binary(payload.begin(), payload.end());
    auto client = make_cloud_http_client(std::chrono::milliseconds(1500));
    ASSERT_TRUE(client->is_available());
    const std::map<std::string, std::string> headers{{"X-Test-Token", "test-token"}};
    for (const std::string verb : {"GET", "POST", "POST-binary", "PUT", "PUT-binary", "DELETE", "HEAD"}) {
        SCOPED_TRACE(verb);
        const std::string reply = verb == "HEAD" ? "" : payload;
        http_peer peer(verb == "DELETE" ? 403 : 200, reply);
        auto response = [&]() -> result<http_response_base> {
            if (verb == "GET") return client->get(peer.url(), {{"key", "space + &"}}, headers);
            if (verb == "POST") return client->post(peer.url(), payload, headers);
            if (verb == "POST-binary") return client->post(peer.url(), binary, headers);
            if (verb == "PUT") return client->put(peer.url(), payload, headers);
            if (verb == "PUT-binary") return client->put(peer.url(), binary, headers);
            if (verb == "DELETE") return client->del(peer.url(), headers);
            return client->head(peer.url(), headers);
        }();
        ASSERT_TRUE(response) << response.error().message;
        EXPECT_EQ(response.value().status_code, verb == "DELETE" ? 403 : 200);
        EXPECT_EQ(response.value().body, std::vector<uint8_t>(reply.begin(), reply.end()));
        EXPECT_EQ(response.value().headers.at("X-Request-Id"), "loopback");
        ASSERT_EQ(peer.received().wait_for(std::chrono::seconds(2)), std::future_status::ready);
        const auto [request_headers, body] = peer.received().get();
        const auto method = verb.substr(0, verb.find('-'));
        EXPECT_TRUE(request_headers.starts_with(method + " /object"));
        EXPECT_NE(request_headers.find("X-Test-Token: test-token\r\n"), std::string::npos);
        if (verb == "GET") EXPECT_NE(request_headers.find("key=space%20%2B%20%26"), std::string::npos);
        EXPECT_EQ(body, method == "POST" || method == "PUT" ? payload : "");
    }
}

TEST(CloudHttpLoopbackTest, ConnectionFailuresPropagateForEveryVerbAndRetriesAreBounded) {
    asio::io_context io;
    // Reserve a port without listening, so another process cannot take the failed endpoint.
    asio::ip::tcp::acceptor reserved(io);
    reserved.open(asio::ip::tcp::v4());
    reserved.bind({asio::ip::address_v4::loopback(), 0});
    const auto url = "http://127.0.0.1:" + std::to_string(reserved.local_endpoint().port()) + "/object";
    cloud_http_client client(std::chrono::milliseconds(200));
    EXPECT_FALSE(client.get(url, {}, {}));
    EXPECT_FALSE(client.post(url, std::string("data"), {}));
    EXPECT_FALSE(client.post(url, std::vector<uint8_t>{0, 255}, {}));
    EXPECT_FALSE(client.put(url, std::string("data"), {}));
    EXPECT_FALSE(client.put(url, std::vector<uint8_t>{0, 255}, {}));
    EXPECT_FALSE(client.del(url, {}));
    EXPECT_FALSE(client.head(url, {}));

    cloud_retry_policy retry;
    retry.max_attempts = 3;
    retry.initial_delay = retry.max_delay = std::chrono::milliseconds(1);
    retry.use_jitter = false;
    int attempts = 0;
    auto failed = client.execute_with_retry([&] { ++attempts; return client.get(url, {}, {}); }, retry);
    EXPECT_FALSE(failed);
    EXPECT_EQ(attempts, 3);

    http_peer peer(200, "recovered");
    attempts = 0;
    auto recovered = client.execute_with_retry([&] {
        return client.get(++attempts == 1 ? url : peer.url(), {}, {});
    }, retry);
    ASSERT_TRUE(recovered);
    EXPECT_EQ(attempts, 2);
    EXPECT_EQ(recovered.value().body, (std::vector<uint8_t>{'r','e','c','o','v','e','r','e','d'}));
}

}  // namespace
}  // namespace kcenon::file_transfer
