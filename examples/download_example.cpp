/**
 * @file download_example.cpp
 * @brief Detailed file download example with verification and error handling
 *
 * This example demonstrates:
 * - Downloading files with hash verification
 * - Configuring overwrite policies
 * - Using progress callbacks to monitor download status
 * - Comprehensive error handling patterns
 * - Verifying downloaded file integrity
 */

#include <kcenon/file_transfer/client/file_transfer_client.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

using namespace kcenon::file_transfer;

namespace {

/**
 * @brief Format bytes into human-readable string
 */
auto format_bytes(uint64_t bytes) -> std::string {
    constexpr uint64_t KB = 1024;
    constexpr uint64_t MB = KB * 1024;
    constexpr uint64_t GB = MB * 1024;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    if (bytes >= GB) {
        oss << static_cast<double>(bytes) / static_cast<double>(GB) << " GB";
    } else if (bytes >= MB) {
        oss << static_cast<double>(bytes) / static_cast<double>(MB) << " MB";
    } else if (bytes >= KB) {
        oss << static_cast<double>(bytes) / static_cast<double>(KB) << " KB";
    } else {
        oss << bytes << " bytes";
    }
    return oss.str();
}

/**
 * @brief Format transfer rate
 */
auto format_rate(double bytes_per_second) -> std::string {
    return format_bytes(static_cast<uint64_t>(bytes_per_second)) + "/s";
}

/**
 * @brief Progress tracking state
 */
struct progress_tracker {
    std::chrono::steady_clock::time_point start_time;
    uint64_t last_bytes = 0;
    std::chrono::steady_clock::time_point last_update;
    double current_rate = 0.0;

    progress_tracker()
        : start_time(std::chrono::steady_clock::now())
        , last_update(start_time) {}
};

/**
 * @brief Verify downloaded file exists and has expected size
 */
auto verify_downloaded_file(const std::filesystem::path& path, uint64_t expected_size) -> bool {
    if (!std::filesystem::exists(path)) {
        std::cerr << "Verification failed: File does not exist" << std::endl;
        return false;
    }

    auto actual_size = std::filesystem::file_size(path);
    if (actual_size != expected_size) {
        std::cerr << "Verification failed: Size mismatch" << std::endl;
        std::cerr << "  Expected: " << expected_size << " bytes" << std::endl;
        std::cerr << "  Actual: " << actual_size << " bytes" << std::endl;
        return false;
    }

    return true;
}

}  // namespace

void print_usage(const char* program) {
    std::cout << "Download Example - File Transfer System" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << program << " [options] <remote_name> <local_file>" << std::endl;
    std::cout << "   or: " << program << " --list [pattern]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --host <host>       Server hostname (default: localhost)" << std::endl;
    std::cout << "  -p, --port <port>       Server port (default: 8080)" << std::endl;
    std::cout << "  -o, --overwrite         Overwrite existing local file" << std::endl;
    std::cout << "  --no-verify             Skip hash verification after download" << std::endl;
    std::cout << "  --list [pattern]        List files on server (default pattern: *)" << std::endl;
    std::cout << "  --help                  Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << " remote_file.txt local_copy.txt" << std::endl;
    std::cout << "  " << program << " -h server.local -p 9000 data.bin ./downloads/data.bin" << std::endl;
    std::cout << "  " << program << " --overwrite backup.zip restore.zip" << std::endl;
    std::cout << "  " << program << " --list \"*.txt\"" << std::endl;
}

int main(int argc, char* argv[]) {
    // Default configuration
    std::string host = "localhost";
    uint16_t port = 8080;
    bool overwrite = false;
    bool verify_hash = true;
    bool list_only = false;
    std::string list_pattern = "*";
    std::string remote_name;
    std::string local_path;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-h" || arg == "--host") {
            if (++i >= argc) {
                std::cerr << "Error: --host requires an argument" << std::endl;
                return 1;
            }
            host = argv[i];
        } else if (arg == "-p" || arg == "--port") {
            if (++i >= argc) {
                std::cerr << "Error: --port requires an argument" << std::endl;
                return 1;
            }
            port = static_cast<uint16_t>(std::stoi(argv[i]));
        } else if (arg == "-o" || arg == "--overwrite") {
            overwrite = true;
        } else if (arg == "--no-verify") {
            verify_hash = false;
        } else if (arg == "--list") {
            list_only = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                ++i;
                list_pattern = argv[i];
            }
        } else if (arg[0] != '-') {
            if (remote_name.empty()) {
                remote_name = arg;
            } else if (local_path.empty()) {
                local_path = arg;
            }
        }
    }

    // Build the client
    auto client_result = file_transfer_client::builder()
        .with_compression(compression_mode::adaptive)
        .with_auto_reconnect(true)
        .with_connect_timeout(std::chrono::milliseconds{10000})
        .build();

    if (!client_result.has_value()) {
        std::cerr << "Failed to create client: " << client_result.error().message << std::endl;
        return 1;
    }

    auto& client = client_result.value();

    // Connect to server
    std::cout << "Connecting to " << host << ":" << port << "..." << std::endl;
    auto connect_result = client.connect(endpoint{host, port});
    if (!connect_result.has_value()) {
        std::cerr << "Failed to connect: " << connect_result.error().message << std::endl;
        std::cerr << std::endl;
        std::cerr << "Troubleshooting:" << std::endl;
        std::cerr << "  - Check if the server is running" << std::endl;
        std::cerr << "  - Verify host and port are correct" << std::endl;
        std::cerr << "  - Check firewall settings" << std::endl;
        return 1;
    }
    std::cout << "Connected!" << std::endl;
    std::cout << std::endl;

    // Handle list mode
    if (list_only) {
        std::cout << "========================================" << std::endl;
        std::cout << "       Files on Server" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Pattern: " << list_pattern << std::endl;
        std::cout << std::endl;

        list_options options;
        options.pattern = list_pattern;
        options.limit = 1000;

        auto list_result = client.list_files(options);
        if (!list_result.has_value()) {
            std::cerr << "Failed to list files: " << list_result.error().message << std::endl;
            (void)client.disconnect();
            return 1;
        }

        auto& files = list_result.value();

        if (files.empty()) {
            std::cout << "(No files matching pattern)" << std::endl;
        } else {
            std::cout << std::setw(40) << std::left << "Filename"
                      << std::setw(15) << std::right << "Size"
                      << "  Hash (first 16 chars)" << std::endl;
            std::cout << std::string(75, '-') << std::endl;

            uint64_t total_size = 0;
            for (const auto& file : files) {
                std::cout << std::setw(40) << std::left << file.filename
                          << std::setw(15) << std::right << format_bytes(file.size)
                          << "  ";
                if (file.sha256_hash.length() > 16) {
                    std::cout << file.sha256_hash.substr(0, 16) << "...";
                } else {
                    std::cout << file.sha256_hash;
                }
                std::cout << std::endl;
                total_size += file.size;
            }

            std::cout << std::string(75, '-') << std::endl;
            std::cout << "Total: " << files.size() << " file(s), " << format_bytes(total_size) << std::endl;
        }

        (void)client.disconnect();
        return 0;
    }

    // Validate download arguments
    if (remote_name.empty() || local_path.empty()) {
        std::cerr << "Error: Both remote_name and local_file are required" << std::endl;
        print_usage(argv[0]);
        (void)client.disconnect();
        return 1;
    }

    // Check if local file exists
    if (!overwrite && std::filesystem::exists(local_path)) {
        std::cerr << "Error: Local file already exists: " << local_path << std::endl;
        std::cerr << "Hint: Use --overwrite option to replace existing file" << std::endl;
        (void)client.disconnect();
        return 1;
    }

    // Create parent directories if needed
    std::filesystem::path local_file_path(local_path);
    if (local_file_path.has_parent_path()) {
        std::filesystem::create_directories(local_file_path.parent_path());
    }

    std::cout << "========================================" << std::endl;
    std::cout << "       File Download Example" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Server: " << host << ":" << port << std::endl;
    std::cout << "  Remote file: " << remote_name << std::endl;
    std::cout << "  Local file: " << local_path << std::endl;
    std::cout << "  Overwrite: " << (overwrite ? "yes" : "no") << std::endl;
    std::cout << "  Verify hash: " << (verify_hash ? "yes" : "no") << std::endl;
    std::cout << std::endl;

    // Set up progress tracker
    progress_tracker tracker;

    // Register progress callback
    client.on_progress([&tracker](const transfer_progress& progress) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - tracker.last_update).count();

        // Calculate rate
        if (elapsed >= 100) {
            auto bytes_delta = progress.bytes_transferred - tracker.last_bytes;
            tracker.current_rate = static_cast<double>(bytes_delta) * 1000.0 / static_cast<double>(elapsed);
            tracker.last_bytes = progress.bytes_transferred;
            tracker.last_update = now;
        }

        // Display progress
        constexpr int bar_width = 30;
        int filled = static_cast<int>(progress.percentage / 100.0 * bar_width);

        std::cout << "\r[";
        for (int i = 0; i < bar_width; ++i) {
            if (i < filled) std::cout << "=";
            else if (i == filled) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << std::fixed << std::setprecision(1) << progress.percentage << "%"
                  << " | " << format_bytes(progress.bytes_transferred) << "/" << format_bytes(progress.total_bytes)
                  << " | " << format_rate(tracker.current_rate)
                  << "     " << std::flush;

        if (progress.percentage >= 100.0) {
            std::cout << std::endl;
        }
    });

    // Register completion callback
    client.on_complete([](const transfer_result& result) {
        if (result.success) {
            std::cout << "[Complete] Download successful!" << std::endl;
        } else {
            std::cout << "[Failed] Download failed: " << result.error_message << std::endl;
        }
    });

    // Configure download options
    download_options options;
    options.overwrite = overwrite;
    options.verify_hash = verify_hash;

    // Start download
    std::cout << "[1/3] Starting download..." << std::endl;
    auto download_result = client.download_file(remote_name, local_path, options);
    if (!download_result.has_value()) {
        std::cerr << "Failed to initiate download: " << download_result.error().message << std::endl;

        // Detailed error handling
        auto& error = download_result.error();
        if (error.message.find("not found") != std::string::npos ||
            error.message.find("does not exist") != std::string::npos) {
            std::cerr << "Hint: Use --list to see available files on the server" << std::endl;
        }

        (void)client.disconnect();
        return 1;
    }

    auto& handle = download_result.value();
    std::cout << "Download started with handle ID: " << handle.get_id() << std::endl;
    std::cout << std::endl;

    // Wait for download completion
    std::cout << "[2/3] Downloading file..." << std::endl;
    std::cout << std::endl;

    auto wait_result = handle.wait();
    if (!wait_result.has_value()) {
        std::cerr << "Error while waiting for download: " << wait_result.error().message << std::endl;
        (void)client.disconnect();
        return 1;
    }

    auto& transfer_info = wait_result.value();
    auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - tracker.start_time);

    // Verify downloaded file
    std::cout << std::endl;
    std::cout << "[3/3] Verifying downloaded file..." << std::endl;

    bool verification_passed = false;
    if (transfer_info.success && std::filesystem::exists(local_path)) {
        verification_passed = verify_downloaded_file(local_path, transfer_info.bytes_transferred);
        if (verification_passed) {
            std::cout << "File verification passed!" << std::endl;
        }
    }

    // Print summary
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "       Download Summary" << std::endl;
    std::cout << "========================================" << std::endl;

    if (transfer_info.success) {
        std::cout << "Status: SUCCESS" << std::endl;
        std::cout << "Downloaded to: " << local_path << std::endl;
        std::cout << "File size: " << format_bytes(transfer_info.bytes_transferred) << std::endl;
        std::cout << "Time elapsed: " << total_elapsed.count() << " ms" << std::endl;

        if (total_elapsed.count() > 0) {
            double avg_rate = static_cast<double>(transfer_info.bytes_transferred) * 1000.0 /
                              static_cast<double>(total_elapsed.count());
            std::cout << "Average rate: " << format_rate(avg_rate) << std::endl;
        }

        std::cout << "Verification: " << (verification_passed ? "PASSED" : "SKIPPED") << std::endl;

        // Get compression statistics
        auto comp_stats = client.get_compression_stats();
        if (comp_stats.total_uncompressed_bytes > 0) {
            std::cout << "Compression ratio: " << std::fixed << std::setprecision(2)
                      << comp_stats.compression_ratio() << std::endl;
        }
    } else {
        std::cout << "Status: FAILED" << std::endl;
        if (transfer_info.error_message) {
            std::cout << "Error: " << *transfer_info.error_message << std::endl;
        }

        // Clean up partial download
        if (std::filesystem::exists(local_path)) {
            std::cout << "Cleaning up partial download..." << std::endl;
            std::filesystem::remove(local_path);
        }
    }

    std::cout << std::endl;

    // Disconnect
    auto disconnect_result = client.disconnect();
    if (!disconnect_result.has_value()) {
        std::cerr << "Disconnect error: " << disconnect_result.error().message << std::endl;
    }

    return transfer_info.success ? 0 : 1;
}
