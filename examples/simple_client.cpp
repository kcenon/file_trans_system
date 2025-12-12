/**
 * @file simple_client.cpp
 * @brief Basic file transfer client example
 *
 * This example demonstrates how to:
 * - Create and configure a file transfer client
 * - Connect to a server
 * - Upload and download files
 * - List files on the server
 * - Handle progress callbacks
 */

#include <kcenon/file_transfer/client/file_transfer_client.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace kcenon::file_transfer;

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " <command> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  upload <local_file> <remote_name> [host:port]" << std::endl;
    std::cout << "  download <remote_name> <local_file> [host:port]" << std::endl;
    std::cout << "  list [host:port]" << std::endl;
    std::cout << std::endl;
    std::cout << "Default server: localhost:8080" << std::endl;
}

std::pair<std::string, uint16_t> parse_endpoint(const std::string& addr) {
    auto colon_pos = addr.find(':');
    if (colon_pos == std::string::npos) {
        return {addr, 8080};
    }
    return {
        addr.substr(0, colon_pos),
        static_cast<uint16_t>(std::stoi(addr.substr(colon_pos + 1)))
    };
}

void create_test_file(const std::string& path, size_t size) {
    std::ofstream file(path, std::ios::binary);
    std::vector<char> data(size);
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<char>('A' + (i % 26));
    }
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    std::cout << "Created test file: " << path << " (" << size << " bytes)" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    // Build client with configuration
    auto client_result = file_transfer_client::builder()
        .with_compression(compression_mode::adaptive)
        .with_compression_level(compression_level::fast)
        .with_auto_reconnect(true)
        .with_connect_timeout(std::chrono::milliseconds{5000})
        .build();

    if (!client_result.has_value()) {
        std::cerr << "Failed to create client: "
                  << client_result.error().message << std::endl;
        return 1;
    }

    auto& client = client_result.value();

    // Register progress callback
    client.on_progress([](const transfer_progress& progress) {
        std::cout << "\r[Progress] " << progress.filename
                  << ": " << static_cast<int>(progress.percentage) << "%"
                  << " (" << progress.bytes_transferred << "/" << progress.total_bytes << " bytes)"
                  << std::flush;
        if (progress.percentage >= 100.0) {
            std::cout << std::endl;
        }
    });

    // Register completion callback
    client.on_complete([](const transfer_result& result) {
        if (result.success) {
            std::cout << "[Complete] " << result.filename
                      << " - " << result.bytes_transferred << " bytes transferred"
                      << std::endl;
        } else {
            std::cout << "[Failed] " << result.filename
                      << " - " << result.error_message << std::endl;
        }
    });

    // Register connection state callback
    client.on_connection_state_changed([](connection_state state) {
        std::cout << "[Connection] State changed to: " << to_string(state) << std::endl;
    });

    // Parse server address
    std::string host = "localhost";
    uint16_t port = 8080;

    // Handle commands
    if (command == "upload") {
        if (argc < 4) {
            std::cerr << "Usage: " << argv[0] << " upload <local_file> <remote_name> [host:port]"
                      << std::endl;
            return 1;
        }

        std::string local_path = argv[2];
        std::string remote_name = argv[3];
        if (argc >= 5) {
            std::tie(host, port) = parse_endpoint(argv[4]);
        }

        // Check if file exists, create test file if it doesn't
        if (!std::filesystem::exists(local_path)) {
            std::cout << "File not found, creating test file..." << std::endl;
            create_test_file(local_path, 1024 * 1024);  // 1MB test file
        }

        std::cout << "=== File Upload ===" << std::endl;
        std::cout << "Server: " << host << ":" << port << std::endl;
        std::cout << "Local: " << local_path << std::endl;
        std::cout << "Remote: " << remote_name << std::endl;
        std::cout << std::endl;

        // Connect to server
        std::cout << "Connecting to server..." << std::endl;
        auto connect_result = client.connect(endpoint{host, port});
        if (!connect_result.has_value()) {
            std::cerr << "Failed to connect: " << connect_result.error().message << std::endl;
            return 1;
        }
        std::cout << "Connected!" << std::endl;

        // Upload file
        std::cout << "Starting upload..." << std::endl;
        auto upload_result = client.upload_file(local_path, remote_name);
        if (!upload_result.has_value()) {
            std::cerr << "Upload failed: " << upload_result.error().message << std::endl;
            (void)client.disconnect();
            return 1;
        }

        std::cout << "Upload initiated with handle: " << upload_result.value().get_id() << std::endl;

        // Disconnect
        auto disconnect_result = client.disconnect();
        if (!disconnect_result.has_value()) {
            std::cerr << "Disconnect error: " << disconnect_result.error().message << std::endl;
        }

    } else if (command == "download") {
        if (argc < 4) {
            std::cerr << "Usage: " << argv[0] << " download <remote_name> <local_file> [host:port]"
                      << std::endl;
            return 1;
        }

        std::string remote_name = argv[2];
        std::string local_path = argv[3];
        if (argc >= 5) {
            std::tie(host, port) = parse_endpoint(argv[4]);
        }

        std::cout << "=== File Download ===" << std::endl;
        std::cout << "Server: " << host << ":" << port << std::endl;
        std::cout << "Remote: " << remote_name << std::endl;
        std::cout << "Local: " << local_path << std::endl;
        std::cout << std::endl;

        // Connect to server
        std::cout << "Connecting to server..." << std::endl;
        auto connect_result = client.connect(endpoint{host, port});
        if (!connect_result.has_value()) {
            std::cerr << "Failed to connect: " << connect_result.error().message << std::endl;
            return 1;
        }
        std::cout << "Connected!" << std::endl;

        // Download file
        std::cout << "Starting download..." << std::endl;
        download_options options;
        options.overwrite = true;
        options.verify_hash = true;

        auto download_result = client.download_file(remote_name, local_path, options);
        if (!download_result.has_value()) {
            std::cerr << "Download failed: " << download_result.error().message << std::endl;
            (void)client.disconnect();
            return 1;
        }

        std::cout << "Download initiated with handle: " << download_result.value().get_id() << std::endl;

        // Disconnect
        auto disconnect_result = client.disconnect();
        if (!disconnect_result.has_value()) {
            std::cerr << "Disconnect error: " << disconnect_result.error().message << std::endl;
        }

    } else if (command == "list") {
        if (argc >= 3) {
            std::tie(host, port) = parse_endpoint(argv[2]);
        }

        std::cout << "=== List Files ===" << std::endl;
        std::cout << "Server: " << host << ":" << port << std::endl;
        std::cout << std::endl;

        // Connect to server
        std::cout << "Connecting to server..." << std::endl;
        auto connect_result = client.connect(endpoint{host, port});
        if (!connect_result.has_value()) {
            std::cerr << "Failed to connect: " << connect_result.error().message << std::endl;
            return 1;
        }
        std::cout << "Connected!" << std::endl;

        // List files
        std::cout << "Fetching file list..." << std::endl;
        list_options options;
        options.pattern = "*";
        options.limit = 100;

        auto list_result = client.list_files(options);
        if (!list_result.has_value()) {
            std::cerr << "List failed: " << list_result.error().message << std::endl;
            (void)client.disconnect();
            return 1;
        }

        auto& files = list_result.value();
        std::cout << std::endl;
        std::cout << "Files on server (" << files.size() << "):" << std::endl;
        std::cout << std::string(60, '-') << std::endl;
        std::cout << "Name                                    Size        Hash" << std::endl;
        std::cout << std::string(60, '-') << std::endl;

        for (const auto& file : files) {
            std::cout << file.filename;
            if (file.filename.length() < 40) {
                std::cout << std::string(40 - file.filename.length(), ' ');
            }
            std::cout << file.size << " bytes  ";
            if (file.sha256_hash.length() > 16) {
                std::cout << file.sha256_hash.substr(0, 16) << "...";
            } else {
                std::cout << file.sha256_hash;
            }
            std::cout << std::endl;
        }

        if (files.empty()) {
            std::cout << "(No files)" << std::endl;
        }
        std::cout << std::string(60, '-') << std::endl;

        // Disconnect
        auto disconnect_result = client.disconnect();
        if (!disconnect_result.has_value()) {
            std::cerr << "Disconnect error: " << disconnect_result.error().message << std::endl;
        }

    } else {
        std::cerr << "Unknown command: " << command << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    // Print final statistics
    auto stats = client.get_statistics();
    auto compression = client.get_compression_stats();

    std::cout << std::endl;
    std::cout << "=== Statistics ===" << std::endl;
    std::cout << "Bytes uploaded: " << stats.total_bytes_uploaded << std::endl;
    std::cout << "Bytes downloaded: " << stats.total_bytes_downloaded << std::endl;
    std::cout << "Files uploaded: " << stats.total_files_uploaded << std::endl;
    std::cout << "Files downloaded: " << stats.total_files_downloaded << std::endl;
    std::cout << "Compression ratio: " << compression.compression_ratio() << std::endl;

    std::cout << std::endl;
    std::cout << "Done." << std::endl;

    return 0;
}
