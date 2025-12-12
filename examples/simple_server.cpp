/**
 * @file simple_server.cpp
 * @brief Basic file transfer server example
 *
 * This example demonstrates how to:
 * - Create and configure a file transfer server
 * - Register event callbacks
 * - Start the server and handle connections
 * - Gracefully shut down the server
 */

#include <kcenon/file_transfer/server/file_transfer_server.h>

#include <csignal>
#include <iostream>
#include <string>
#include <thread>

using namespace kcenon::file_transfer;

// Global flag for graceful shutdown
static std::atomic<bool> running{true};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nShutdown signal received..." << std::endl;
        running = false;
    }
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    uint16_t port = 8080;
    std::string storage_dir = "./server_storage";

    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }
    if (argc >= 3) {
        storage_dir = argv[2];
    }

    std::cout << "=== File Transfer Server Example ===" << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "Storage: " << storage_dir << std::endl;
    std::cout << std::endl;

    // Build server with configuration
    auto server_result = file_transfer_server::builder()
        .with_storage_directory(storage_dir)
        .with_max_connections(100)
        .with_max_file_size(10ULL * 1024 * 1024 * 1024)  // 10GB
        .with_storage_quota(100ULL * 1024 * 1024 * 1024)  // 100GB
        .with_chunk_size(256 * 1024)  // 256KB
        .build();

    if (!server_result.has_value()) {
        std::cerr << "Failed to create server: "
                  << server_result.error().message << std::endl;
        return 1;
    }

    auto& server = server_result.value();

    // Register callbacks
    server.on_client_connected([](const client_info& info) {
        std::cout << "[Connected] Client " << info.id.value
                  << " from " << info.address << ":" << info.port << std::endl;
    });

    server.on_client_disconnected([](const client_info& info) {
        std::cout << "[Disconnected] Client " << info.id.value << std::endl;
    });

    server.on_upload_request([](const upload_request& req) {
        std::cout << "[Upload Request] File: " << req.filename
                  << ", Size: " << req.file_size << " bytes" << std::endl;
        return true;  // Accept the upload
    });

    server.on_download_request([](const download_request& req) {
        std::cout << "[Download Request] File: " << req.filename << std::endl;
        return true;  // Accept the download
    });

    server.on_transfer_complete([](const transfer_result& result) {
        if (result.success) {
            std::cout << "[Transfer Complete] File: " << result.filename
                      << ", Bytes: " << result.bytes_transferred << std::endl;
        } else {
            std::cout << "[Transfer Failed] File: " << result.filename
                      << ", Error: " << result.error_message << std::endl;
        }
    });

    server.on_progress([](const transfer_progress& progress) {
        std::cout << "\r[Progress] " << progress.filename
                  << ": " << static_cast<int>(progress.percentage) << "%"
                  << std::flush;
        if (progress.percentage >= 100.0) {
            std::cout << std::endl;
        }
    });

    // Start the server
    auto start_result = server.start(endpoint{port});
    if (!start_result.has_value()) {
        std::cerr << "Failed to start server: "
                  << start_result.error().message << std::endl;
        return 1;
    }

    std::cout << "Server started on port " << port << std::endl;
    std::cout << "Press Ctrl+C to stop..." << std::endl;
    std::cout << std::endl;

    // Set up signal handlers for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Main loop - wait for shutdown signal
    while (running && server.is_running()) {
        // Print statistics periodically
        auto stats = server.get_statistics();
        auto storage = server.get_storage_stats();

        std::cout << "\r[Stats] Connections: " << stats.active_connections
                  << " | Transfers: " << stats.active_transfers
                  << " | Files: " << storage.file_count
                  << " | Storage: " << (storage.used_size / (1024 * 1024)) << "MB"
                  << std::flush;

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    std::cout << std::endl;

    // Graceful shutdown
    std::cout << "Stopping server..." << std::endl;
    auto stop_result = server.stop();
    if (!stop_result.has_value()) {
        std::cerr << "Error during shutdown: "
                  << stop_result.error().message << std::endl;
    }

    std::cout << "Server stopped." << std::endl;

    // Print final statistics
    auto final_stats = server.get_statistics();
    std::cout << std::endl;
    std::cout << "=== Final Statistics ===" << std::endl;
    std::cout << "Total bytes received: " << final_stats.total_bytes_received << std::endl;
    std::cout << "Total bytes sent: " << final_stats.total_bytes_sent << std::endl;
    std::cout << "Total files uploaded: " << final_stats.total_files_uploaded << std::endl;
    std::cout << "Total files downloaded: " << final_stats.total_files_downloaded << std::endl;

    return 0;
}
