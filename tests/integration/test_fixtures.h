/**
 * @file test_fixtures.h
 * @brief Test fixtures for integration tests
 */

#ifndef KCENON_FILE_TRANSFER_TEST_FIXTURES_H
#define KCENON_FILE_TRANSFER_TEST_FIXTURES_H

#include <gtest/gtest.h>

#include <kcenon/file_transfer/file_transfer.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <vector>

namespace kcenon::file_transfer::test {

/**
 * @brief Test fixture for temporary directory management
 */
class TempDirectoryFixture : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() /
                    ("file_trans_test_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(test_dir_);
        storage_dir_ = test_dir_ / "storage";
        std::filesystem::create_directories(storage_dir_);
        download_dir_ = test_dir_ / "downloads";
        std::filesystem::create_directories(download_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    auto create_test_file(const std::string& name, std::size_t size)
        -> std::filesystem::path {
        auto path = test_dir_ / name;
        std::ofstream file(path, std::ios::binary);

        std::mt19937 gen(42);  // Fixed seed for reproducibility
        std::uniform_int_distribution<> dis(0, 255);

        for (std::size_t i = 0; i < size; ++i) {
            char byte = static_cast<char>(dis(gen));
            file.write(&byte, 1);
        }

        return path;
    }

    auto create_text_file(const std::string& name, std::size_t size)
        -> std::filesystem::path {
        auto path = test_dir_ / name;
        std::ofstream file(path);

        // Create highly compressible text content
        const std::string pattern = "The quick brown fox jumps over the lazy dog. ";
        std::size_t written = 0;
        while (written < size) {
            file << pattern;
            written += pattern.size();
        }

        return path;
    }

    auto create_binary_file(const std::string& name, std::size_t size)
        -> std::filesystem::path {
        auto path = test_dir_ / name;
        std::ofstream file(path, std::ios::binary);

        // Create random binary content (low compressibility)
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);

        for (std::size_t i = 0; i < size; ++i) {
            char byte = static_cast<char>(dis(gen));
            file.write(&byte, 1);
        }

        return path;
    }

    std::filesystem::path test_dir_;
    std::filesystem::path storage_dir_;
    std::filesystem::path download_dir_;
};

/**
 * @brief Test fixture for server tests
 */
class ServerFixture : public TempDirectoryFixture {
protected:
    void SetUp() override {
        TempDirectoryFixture::SetUp();

        auto server_result = file_transfer_server::builder()
            .with_storage_directory(storage_dir_)
            .with_max_connections(10)
            .with_max_file_size(100 * 1024 * 1024)  // 100MB
            .build();

        ASSERT_TRUE(server_result.has_value()) << "Failed to create server";
        server_ = std::make_unique<file_transfer_server>(std::move(server_result.value()));
    }

    void TearDown() override {
        if (server_ && server_->is_running()) {
            (void)server_->stop();
        }
        server_.reset();
        TempDirectoryFixture::TearDown();
    }

    auto start_server(uint16_t port = 0) -> uint16_t {
        // If port is 0, find an available port
        if (port == 0) {
            port = get_available_port();
        }

        auto result = server_->start(endpoint{port});
        EXPECT_TRUE(result.has_value()) << "Failed to start server";
        return port;
    }

    std::unique_ptr<file_transfer_server> server_;

public:
    static auto get_available_port() -> uint16_t {
        // Return a port in the dynamic range
        static std::atomic<uint16_t> port_counter{50000};
        return port_counter++;
    }
};

/**
 * @brief Test fixture for client tests
 */
class ClientFixture : public TempDirectoryFixture {
protected:
    void SetUp() override {
        TempDirectoryFixture::SetUp();

        auto client_result = file_transfer_client::builder()
            .with_compression(compression_mode::adaptive)
            .with_auto_reconnect(false)
            .build();

        ASSERT_TRUE(client_result.has_value()) << "Failed to create client";
        client_ = std::make_unique<file_transfer_client>(std::move(client_result.value()));
    }

    void TearDown() override {
        if (client_ && client_->is_connected()) {
            (void)client_->disconnect();
        }
        client_.reset();
        TempDirectoryFixture::TearDown();
    }

    std::unique_ptr<file_transfer_client> client_;
};

/**
 * @brief Test fixture for server-client integration tests
 */
class IntegrationFixture : public TempDirectoryFixture {
protected:
    void SetUp() override {
        TempDirectoryFixture::SetUp();

        // Create server
        auto server_result = file_transfer_server::builder()
            .with_storage_directory(storage_dir_)
            .with_max_connections(10)
            .build();

        ASSERT_TRUE(server_result.has_value()) << "Failed to create server";
        server_ = std::make_unique<file_transfer_server>(std::move(server_result.value()));

        // Create client
        auto client_result = file_transfer_client::builder()
            .with_compression(compression_mode::adaptive)
            .with_auto_reconnect(false)
            .build();

        ASSERT_TRUE(client_result.has_value()) << "Failed to create client";
        client_ = std::make_unique<file_transfer_client>(std::move(client_result.value()));

        // Start server
        server_port_ = ServerFixture::get_available_port();
        auto start_result = server_->start(endpoint{server_port_});
        ASSERT_TRUE(start_result.has_value()) << "Failed to start server";
    }

    void TearDown() override {
        if (client_ && client_->is_connected()) {
            (void)client_->disconnect();
        }
        client_.reset();

        if (server_ && server_->is_running()) {
            (void)server_->stop();
        }
        server_.reset();

        TempDirectoryFixture::TearDown();
    }

    auto connect_client() -> bool {
        auto result = client_->connect(endpoint{"127.0.0.1", server_port_});
        return result.has_value();
    }

    std::unique_ptr<file_transfer_server> server_;
    std::unique_ptr<file_transfer_client> client_;
    uint16_t server_port_{0};
};

/**
 * @brief Test data sizes
 */
namespace test_data {
    constexpr std::size_t small_file_size = 1024;           // 1KB
    constexpr std::size_t medium_file_size = 10 * 1024 * 1024;  // 10MB
    constexpr std::size_t large_file_size = 100 * 1024 * 1024;  // 100MB
}

}  // namespace kcenon::file_transfer::test

#endif  // KCENON_FILE_TRANSFER_TEST_FIXTURES_H
