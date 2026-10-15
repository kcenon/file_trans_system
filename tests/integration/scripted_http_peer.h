#pragma once
#include <asio.hpp>
#include <future>
#include <map>
#include <sstream>
#include <thread>
#include <vector>

namespace kcenon::file_transfer::test {
struct http_reply {
    int status;
    std::string body;
    std::map<std::string, std::string> headers;
};
struct http_request { std::string headers, body; };

// Bounded real HTTP exchanges. Destruction stops a pending accept/read even
// when the client returns early; the future requires every expected request.
class scripted_http_peer {
public:
    explicit scripted_http_peer(std::vector<http_reply> replies)
        : listener_(io_, {asio::ip::address_v4::loopback(), 0}), socket_(io_),
          input_(128 * 1024), replies_(std::move(replies)), received_(promise_.get_future()) {
        accept(); worker_ = std::thread([this] { io_.run(); });
    }
    ~scripted_http_peer() { io_.stop(); worker_.join(); }
    std::string endpoint() const {
        return "http://127.0.0.1:" + std::to_string(listener_.local_endpoint().port());
    }
    std::future<std::vector<http_request>>& received() { return received_; }
private:
    void accept() {
        listener_.async_accept(socket_, [this](auto error) {
            if (error) return;
            asio::async_read_until(socket_, input_, "\r\n\r\n", [this](auto error, auto count) {
                if (error) return;
                request_ = {}; request_.headers.resize(count);
                std::istream stream(&input_); stream.read(request_.headers.data(), count);
                std::istringstream lines(request_.headers); std::string line; std::size_t length = 0;
                while (std::getline(lines, line)) {
                    if (line.starts_with("Content-Length: ")) length = std::stoul(line.substr(16));
                }
                if (length > 64 * 1024) return;
                if (input_.size() >= length) return respond(length);
                asio::async_read(socket_, input_, asio::transfer_exactly(length - input_.size()),
                    [this, length](auto error, auto) { if (!error) respond(length); });
            });
        });
    }
    void respond(std::size_t length) {
        request_.body.resize(length); std::istream stream(&input_); stream.read(request_.body.data(), length);
        requests_.push_back(std::move(request_));
        auto reply = replies_.at(next_++);
        reply.headers.try_emplace("Content-Length", std::to_string(reply.body.size()));
        reply.headers["Connection"] = "close";
        response_ = "HTTP/1.1 " + std::to_string(reply.status) + " Test\r\n";
        for (const auto& [key, value] : reply.headers) response_ += key + ": " + value + "\r\n";
        response_ += "\r\n" + reply.body;
        asio::async_write(socket_, asio::buffer(response_), [this](auto error, auto) {
            if (error) return;
            asio::error_code ignored; socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
            socket_.close(ignored);
            if (next_ == replies_.size()) promise_.set_value(std::move(requests_));
            else accept();
        });
    }
    asio::io_context io_;
    asio::ip::tcp::acceptor listener_;
    asio::ip::tcp::socket socket_;
    asio::streambuf input_;
    std::vector<http_reply> replies_;
    std::vector<http_request> requests_;
    http_request request_;
    std::string response_;
    std::size_t next_ = 0;
    std::promise<std::vector<http_request>> promise_;
    std::future<std::vector<http_request>> received_;
    std::thread worker_;
};
} // namespace kcenon::file_transfer::test
