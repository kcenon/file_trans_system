/**
 * @file upload_example.cpp
 * @brief Detailed file upload example with progress callbacks and error handling
 *
 * This example demonstrates:
 * - Configuring compression settings for uploads
 * - Using progress callbacks to monitor upload status
 * - Comprehensive error handling patterns
 * - Using transfer handles to control uploads
 * - Waiting for upload completion and verifying results
 */

#include <kcenon/file_transfer/client/file_transfer_client.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

using namespace kcenon::file_transfer;

namespace {

/**
 * @brief Format bytes into human-readable string
 * @param bytes Number of bytes
 * @return Formatted string (e.g., "1.5 MB")
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
 * @brief Format transfer rate into human-readable string
 * @param bytes_per_second Transfer rate in bytes per second
 * @return Formatted string (e.g., "10.5 MB/s")
 */
auto format_rate(double bytes_per_second) -> std::string {
    return format_bytes(static_cast<uint64_t>(bytes_per_second)) + "/s";
}

/**
 * @brief Create a test file with random content for demonstration
 * @param path File path to create
 * @param size File size in bytes
 */
void create_test_file(const std::filesystem::path& path, size_t size) {
    // Create parent directories if they don't exist
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to create file: " + path.string());
    }

    // Write pattern data for testing (compressible data)
    std::vector<char> buffer(std::min(size, size_t{65536}));
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = static_cast<char>('A' + (i % 26));
    }

    size_t remaining = size;
    while (remaining > 0) {
        size_t to_write = std::min(remaining, buffer.size());
        file.write(buffer.data(), static_cast<std::streamsize>(to_write));
        remaining -= to_write;
    }

    std::cout << "Created test file: " << path << " (" << format_bytes(size) << ")" << std::endl;
}

/**
 * @brief Progress tracking state for callback
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

}  // namespace

void print_usage(const char* program) {
    std::cout << "Upload Example - File Transfer System" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << program << " [options] <local_file> <remote_name>" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --host <host>       Server hostname (default: localhost)" << std::endl;
    std::cout << "  -p, --port <port>       Server port (default: 8080)" << std::endl;
    std::cout << "  -c, --compression <mode>  Compression mode: none, always, adaptive (default: adaptive)" << std::endl;
    std::cout << "  -l, --level <level>     Compression level: fast, balanced, best (default: fast)" << std::endl;
    std::cout << "  -o, --overwrite         Overwrite existing file on server" << std::endl;
    std::cout << "  --create-test <size>    Create test file of specified size (e.g., 10M, 1G)" << std::endl;
    std::cout << "  --help                  Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << " myfile.txt remote_file.txt" << std::endl;
    std::cout << "  " << program << " -h server.local -p 9000 data.bin backup.bin" << std::endl;
    std::cout << "  " << program << " -c always -l best large.zip archive.zip" << std::endl;
    std::cout << "  " << program << " --create-test 100M test_data.bin upload.bin" << std::endl;
}

auto parse_size(const std::string& size_str) -> size_t {
    size_t pos = 0;
    double value = std::stod(size_str, &pos);

    if (pos < size_str.size()) {
        char suffix = static_cast<char>(std::toupper(size_str[pos]));
        switch (suffix) {
            case 'K': return static_cast<size_t>(value * 1024);
            case 'M': return static_cast<size_t>(value * 1024 * 1024);
            case 'G': return static_cast<size_t>(value * 1024 * 1024 * 1024);
            default: break;
        }
    }
    return static_cast<size_t>(value);
}

auto parse_compression_mode(const std::string& mode) -> compression_mode {
    if (mode == "none") return compression_mode::none;
    if (mode == "always") return compression_mode::always;
    if (mode == "adaptive") return compression_mode::adaptive;
    throw std::invalid_argument("Invalid compression mode: " + mode);
}

auto parse_compression_level(const std::string& level) -> compression_level {
    if (level == "fast") return compression_level::fast;
    if (level == "balanced") return compression_level::balanced;
    if (level == "best") return compression_level::best;
    throw std::invalid_argument("Invalid compression level: " + level);
}

int main(int argc, char* argv[]) {
    // Default configuration
    std::string host = "localhost";
    uint16_t port = 8080;
    compression_mode comp_mode = compression_mode::adaptive;
    compression_level comp_level = compression_level::fast;
    bool overwrite = false;
    std::string local_path;
    std::string remote_name;
    std::optional<size_t> create_test_size;

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
        } else if (arg == "-c" || arg == "--compression") {
            if (++i >= argc) {
                std::cerr << "Error: --compression requires an argument" << std::endl;
                return 1;
            }
            try {
                comp_mode = parse_compression_mode(argv[i]);
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
                return 1;
            }
        } else if (arg == "-l" || arg == "--level") {
            if (++i >= argc) {
                std::cerr << "Error: --level requires an argument" << std::endl;
                return 1;
            }
            try {
                comp_level = parse_compression_level(argv[i]);
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
                return 1;
            }
        } else if (arg == "-o" || arg == "--overwrite") {
            overwrite = true;
        } else if (arg == "--create-test") {
            if (++i >= argc) {
                std::cerr << "Error: --create-test requires a size argument" << std::endl;
                return 1;
            }
            create_test_size = parse_size(argv[i]);
        } else if (arg[0] != '-') {
            if (local_path.empty()) {
                local_path = arg;
            } else if (remote_name.empty()) {
                remote_name = arg;
            }
        }
    }

    // Validate arguments
    if (local_path.empty() || remote_name.empty()) {
        std::cerr << "Error: Both local_file and remote_name are required" << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    // Create test file if requested
    if (create_test_size) {
        try {
            create_test_file(local_path, *create_test_size);
        } catch (const std::exception& e) {
            std::cerr << "Error creating test file: " << e.what() << std::endl;
            return 1;
        }
    }

    // Verify local file exists
    if (!std::filesystem::exists(local_path)) {
        std::cerr << "Error: Local file does not exist: " << local_path << std::endl;
        std::cerr << "Hint: Use --create-test <size> to create a test file" << std::endl;
        return 1;
    }

    auto file_size = std::filesystem::file_size(local_path);

    std::cout << "========================================" << std::endl;
    std::cout << "       File Upload Example" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Server: " << host << ":" << port << std::endl;
    std::cout << "  Local file: " << local_path << std::endl;
    std::cout << "  Remote name: " << remote_name << std::endl;
    std::cout << "  File size: " << format_bytes(file_size) << std::endl;
    std::cout << "  Compression: " << (comp_mode == compression_mode::none ? "none" :
                                       comp_mode == compression_mode::always ? "always" : "adaptive") << std::endl;
    std::cout << "  Compression level: " << (comp_level == compression_level::fast ? "fast" :
                                             comp_level == compression_level::balanced ? "balanced" : "best") << std::endl;
    std::cout << "  Overwrite: " << (overwrite ? "yes" : "no") << std::endl;
    std::cout << std::endl;

    // Build the client with configured compression settings
    std::cout << "[1/4] Creating client..." << std::endl;
    auto client_result = file_transfer_client::builder()
        .with_compression(comp_mode)
        .with_compression_level(comp_level)
        .with_auto_reconnect(true)
        .with_connect_timeout(std::chrono::milliseconds{10000})
        .build();

    if (!client_result.has_value()) {
        std::cerr << "Failed to create client: " << client_result.error().message << std::endl;
        return 1;
    }

    auto& client = client_result.value();

    // Set up progress tracker for rate calculation
    progress_tracker tracker;

    // Register progress callback with detailed information
    client.on_progress([&tracker](const transfer_progress& progress) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - tracker.last_update).count();

        // Calculate rate (update every 100ms for smoothness)
        if (elapsed >= 100) {
            auto bytes_delta = progress.bytes_transferred - tracker.last_bytes;
            tracker.current_rate = static_cast<double>(bytes_delta) * 1000.0 / static_cast<double>(elapsed);
            tracker.last_bytes = progress.bytes_transferred;
            tracker.last_update = now;
        }

        // Display progress bar
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
            std::cout << "[Complete] Upload successful!" << std::endl;
            std::cout << "  Bytes transferred: " << format_bytes(result.bytes_transferred) << std::endl;
        } else {
            std::cout << "[Failed] Upload failed: " << result.error_message << std::endl;
        }
    });

    // Register connection state callback
    client.on_connection_state_changed([](connection_state state) {
        std::cout << "[Connection] " << to_string(state) << std::endl;
    });

    // Connect to server
    std::cout << "[2/4] Connecting to server..." << std::endl;
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
    std::cout << "[Connection] Connected successfully!" << std::endl;
    std::cout << std::endl;

    // Start upload
    std::cout << "[3/4] Starting upload..." << std::endl;
    upload_options options;
    options.compression = comp_mode;
    options.overwrite = overwrite;

    auto upload_result = client.upload_file(local_path, remote_name, options);
    if (!upload_result.has_value()) {
        std::cerr << "Failed to initiate upload: " << upload_result.error().message << std::endl;

        // Detailed error handling
        auto& error = upload_result.error();
        if (error.message.find("exists") != std::string::npos) {
            std::cerr << "Hint: Use --overwrite option to replace existing file" << std::endl;
        } else if (error.message.find("space") != std::string::npos) {
            std::cerr << "Hint: Server may be running low on storage space" << std::endl;
        } else if (error.message.find("size") != std::string::npos) {
            std::cerr << "Hint: File may exceed server's maximum file size limit" << std::endl;
        }

        (void)client.disconnect();
        return 1;
    }

    auto& handle = upload_result.value();
    std::cout << "Upload started with handle ID: " << handle.get_id() << std::endl;
    std::cout << std::endl;

    // Wait for upload completion
    std::cout << "[4/4] Waiting for upload to complete..." << std::endl;
    std::cout << std::endl;

    auto wait_result = handle.wait();
    if (!wait_result.has_value()) {
        std::cerr << "Error while waiting for upload: " << wait_result.error().message << std::endl;
        (void)client.disconnect();
        return 1;
    }

    auto& transfer_info = wait_result.value();
    auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - tracker.start_time);

    // Print final statistics
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "       Upload Summary" << std::endl;
    std::cout << "========================================" << std::endl;

    if (transfer_info.success) {
        std::cout << "Status: SUCCESS" << std::endl;
        std::cout << "Bytes transferred: " << format_bytes(transfer_info.bytes_transferred) << std::endl;
        std::cout << "Time elapsed: " << total_elapsed.count() << " ms" << std::endl;

        double avg_rate = static_cast<double>(transfer_info.bytes_transferred) * 1000.0 /
                          static_cast<double>(total_elapsed.count());
        std::cout << "Average rate: " << format_rate(avg_rate) << std::endl;

        // Get compression statistics
        auto comp_stats = client.get_compression_stats();
        if (comp_stats.total_uncompressed_bytes > 0) {
            std::cout << "Compression ratio: " << std::fixed << std::setprecision(2)
                      << comp_stats.compression_ratio() << std::endl;
            std::cout << "Data saved: " << format_bytes(
                comp_stats.total_uncompressed_bytes - comp_stats.total_compressed_bytes) << std::endl;
        }
    } else {
        std::cout << "Status: FAILED" << std::endl;
        if (transfer_info.error_message) {
            std::cout << "Error: " << *transfer_info.error_message << std::endl;
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
