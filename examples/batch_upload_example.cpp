/**
 * @file batch_upload_example.cpp
 * @brief Batch file upload example with concurrent transfers
 *
 * This example demonstrates:
 * - Uploading multiple files in parallel
 * - Tracking batch progress across all files
 * - Handling individual file failures within a batch
 * - Configuring concurrency and error handling options
 * - Using batch transfer handles for control
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
#include <vector>

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
 * @brief Create a test file with specified size
 */
void create_test_file(const std::filesystem::path& path, size_t size) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to create file: " + path.string());
    }

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
}

/**
 * @brief Create multiple test files for demonstration
 */
void create_test_files(const std::string& directory, size_t count, size_t base_size) {
    std::filesystem::create_directories(directory);

    std::cout << "Creating " << count << " test files in " << directory << "..." << std::endl;

    for (size_t i = 0; i < count; ++i) {
        std::string filename = "test_file_" + std::to_string(i + 1) + ".dat";
        auto path = std::filesystem::path(directory) / filename;

        // Vary file sizes for realistic testing
        size_t size = base_size + (i * base_size / 4);
        create_test_file(path, size);

        std::cout << "  Created: " << filename << " (" << format_bytes(size) << ")" << std::endl;
    }

    std::cout << std::endl;
}

/**
 * @brief Print batch progress in a formatted way
 */
void print_batch_progress(const batch_progress& progress) {
    // Progress bar
    constexpr int bar_width = 40;
    int filled = static_cast<int>(progress.completion_percentage() / 100.0 * bar_width);

    std::cout << "\r[";
    for (int i = 0; i < bar_width; ++i) {
        if (i < filled) std::cout << "=";
        else if (i == filled) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << std::fixed << std::setprecision(1) << progress.completion_percentage() << "%";

    // File counts
    std::cout << " | Files: " << progress.completed_files << "/" << progress.total_files;
    if (progress.failed_files > 0) {
        std::cout << " (failed: " << progress.failed_files << ")";
    }

    // Transfer rate
    std::cout << " | " << format_rate(progress.overall_rate);

    // Data progress
    std::cout << " | " << format_bytes(progress.transferred_bytes) << "/" << format_bytes(progress.total_bytes);

    std::cout << "     " << std::flush;
}

}  // namespace

void print_usage(const char* program) {
    std::cout << "Batch Upload Example - File Transfer System" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << program << " [options] <file1> [file2] [file3] ..." << std::endl;
    std::cout << "   or: " << program << " --directory <dir>" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --host <host>       Server hostname (default: localhost)" << std::endl;
    std::cout << "  -p, --port <port>       Server port (default: 8080)" << std::endl;
    std::cout << "  -j, --jobs <n>          Max concurrent transfers (default: 4)" << std::endl;
    std::cout << "  -d, --directory <dir>   Upload all files from directory" << std::endl;
    std::cout << "  --continue-on-error     Continue batch even if some files fail (default)" << std::endl;
    std::cout << "  --stop-on-error         Stop batch on first failure" << std::endl;
    std::cout << "  --overwrite             Overwrite existing files on server" << std::endl;
    std::cout << "  --create-test <count>   Create test files for demo (5 files default)" << std::endl;
    std::cout << "  --help                  Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << " file1.txt file2.txt file3.txt" << std::endl;
    std::cout << "  " << program << " -j 8 --directory ./uploads" << std::endl;
    std::cout << "  " << program << " --create-test 10 --directory ./test_files" << std::endl;
}

int main(int argc, char* argv[]) {
    // Default configuration
    std::string host = "localhost";
    uint16_t port = 8080;
    std::size_t max_concurrent = 4;
    bool continue_on_error = true;
    bool overwrite = false;
    std::vector<std::string> files;
    std::string directory;
    std::optional<size_t> create_test_count;

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
        } else if (arg == "-j" || arg == "--jobs") {
            if (++i >= argc) {
                std::cerr << "Error: --jobs requires an argument" << std::endl;
                return 1;
            }
            max_concurrent = static_cast<std::size_t>(std::stoi(argv[i]));
        } else if (arg == "-d" || arg == "--directory") {
            if (++i >= argc) {
                std::cerr << "Error: --directory requires an argument" << std::endl;
                return 1;
            }
            directory = argv[i];
        } else if (arg == "--continue-on-error") {
            continue_on_error = true;
        } else if (arg == "--stop-on-error") {
            continue_on_error = false;
        } else if (arg == "--overwrite") {
            overwrite = true;
        } else if (arg == "--create-test") {
            create_test_count = 5;  // Default count
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                ++i;
                create_test_count = static_cast<size_t>(std::stoi(argv[i]));
            }
        } else if (arg[0] != '-') {
            files.push_back(arg);
        }
    }

    // Create test files if requested
    if (create_test_count) {
        if (directory.empty()) {
            directory = "./batch_test_files";
        }
        create_test_files(directory, *create_test_count, 512 * 1024);  // 512KB base size
    }

    // Collect files from directory if specified
    if (!directory.empty()) {
        if (!std::filesystem::exists(directory)) {
            std::cerr << "Error: Directory does not exist: " << directory << std::endl;
            return 1;
        }

        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                files.push_back(entry.path().string());
            }
        }
    }

    // Validate files
    if (files.empty()) {
        std::cerr << "Error: No files specified for upload" << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    // Calculate total size and create upload entries
    std::vector<upload_entry> upload_entries;
    uint64_t total_size = 0;

    std::cout << "========================================" << std::endl;
    std::cout << "     Batch Upload Example" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Files to upload:" << std::endl;

    for (const auto& file : files) {
        if (!std::filesystem::exists(file)) {
            std::cerr << "Warning: File not found, skipping: " << file << std::endl;
            continue;
        }

        auto size = std::filesystem::file_size(file);
        total_size += size;

        std::filesystem::path path(file);
        std::string remote_name = path.filename().string();

        std::cout << "  " << std::setw(30) << std::left << remote_name
                  << " " << std::setw(12) << std::right << format_bytes(size) << std::endl;

        upload_entries.emplace_back(path, remote_name);
    }

    if (upload_entries.empty()) {
        std::cerr << "Error: No valid files to upload" << std::endl;
        return 1;
    }

    std::cout << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Server: " << host << ":" << port << std::endl;
    std::cout << "  Total files: " << upload_entries.size() << std::endl;
    std::cout << "  Total size: " << format_bytes(total_size) << std::endl;
    std::cout << "  Max concurrent: " << max_concurrent << std::endl;
    std::cout << "  Continue on error: " << (continue_on_error ? "yes" : "no") << std::endl;
    std::cout << "  Overwrite: " << (overwrite ? "yes" : "no") << std::endl;
    std::cout << std::endl;

    // Build the client
    std::cout << "[1/4] Creating client..." << std::endl;
    auto client_result = file_transfer_client::builder()
        .with_compression(compression_mode::adaptive)
        .with_compression_level(compression_level::fast)
        .with_auto_reconnect(true)
        .with_connect_timeout(std::chrono::milliseconds{10000})
        .build();

    if (!client_result.has_value()) {
        std::cerr << "Failed to create client: " << client_result.error().message << std::endl;
        return 1;
    }

    auto& client = client_result.value();

    // Register callbacks for individual file progress
    client.on_progress([](const transfer_progress& progress) {
        // Individual progress is tracked internally; batch progress is shown in main loop
    });

    client.on_complete([](const transfer_result& result) {
        if (result.success) {
            std::cout << std::endl << "[File Complete] " << result.filename << " - "
                      << format_bytes(result.bytes_transferred) << std::endl;
        } else {
            std::cout << std::endl << "[File Failed] " << result.filename << " - "
                      << result.error_message << std::endl;
        }
    });

    // Connect to server
    std::cout << "[2/4] Connecting to server..." << std::endl;
    auto connect_result = client.connect(endpoint{host, port});
    if (!connect_result.has_value()) {
        std::cerr << "Failed to connect: " << connect_result.error().message << std::endl;
        return 1;
    }
    std::cout << "[Connection] Connected!" << std::endl;
    std::cout << std::endl;

    // Configure batch options
    batch_options options;
    options.max_concurrent = max_concurrent;
    options.continue_on_error = continue_on_error;
    options.overwrite = overwrite;

    // Start batch upload
    std::cout << "[3/4] Starting batch upload..." << std::endl;
    auto start_time = std::chrono::steady_clock::now();

    auto batch_result = client.upload_files(upload_entries, options);
    if (!batch_result.has_value()) {
        std::cerr << "Failed to start batch upload: " << batch_result.error().message << std::endl;
        (void)client.disconnect();
        return 1;
    }

    auto& batch_handle = batch_result.value();
    std::cout << "Batch started with ID: " << batch_handle.get_id() << std::endl;
    std::cout << std::endl;

    // Monitor progress until completion
    std::cout << "[4/4] Uploading files..." << std::endl;
    std::cout << std::endl;

    // Poll for progress updates
    while (true) {
        auto progress = batch_handle.get_batch_progress();

        // Check if batch is complete
        if (progress.completed_files + progress.failed_files >= progress.total_files) {
            print_batch_progress(progress);
            std::cout << std::endl;
            break;
        }

        print_batch_progress(progress);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Wait for final result
    auto wait_result = batch_handle.wait();
    if (!wait_result.has_value()) {
        std::cerr << "Error waiting for batch completion: " << wait_result.error().message << std::endl;
        (void)client.disconnect();
        return 1;
    }

    auto& result = wait_result.value();
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // Print summary
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "       Batch Upload Summary" << std::endl;
    std::cout << "========================================" << std::endl;

    if (result.all_succeeded()) {
        std::cout << "Status: ALL FILES UPLOADED SUCCESSFULLY" << std::endl;
    } else if (result.succeeded > 0) {
        std::cout << "Status: COMPLETED WITH ERRORS" << std::endl;
    } else {
        std::cout << "Status: ALL FILES FAILED" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Results:" << std::endl;
    std::cout << "  Total files: " << result.total_files << std::endl;
    std::cout << "  Succeeded: " << result.succeeded << std::endl;
    std::cout << "  Failed: " << result.failed << std::endl;
    std::cout << "  Total bytes: " << format_bytes(result.total_bytes) << std::endl;
    std::cout << "  Time elapsed: " << elapsed.count() << " ms" << std::endl;

    if (elapsed.count() > 0) {
        double avg_rate = static_cast<double>(result.total_bytes) * 1000.0 /
                          static_cast<double>(elapsed.count());
        std::cout << "  Average rate: " << format_rate(avg_rate) << std::endl;
    }

    // Show per-file results
    std::cout << std::endl;
    std::cout << "Per-file results:" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    for (const auto& file_result : result.file_results) {
        std::cout << "  " << std::setw(30) << std::left << file_result.filename;

        if (file_result.success) {
            std::cout << " [OK] " << format_bytes(file_result.bytes_transferred)
                      << " in " << file_result.elapsed.count() << "ms" << std::endl;
        } else {
            std::cout << " [FAILED]";
            if (file_result.error_message) {
                std::cout << " " << *file_result.error_message;
            }
            std::cout << std::endl;
        }
    }

    std::cout << std::string(70, '-') << std::endl;

    // Get compression statistics
    auto comp_stats = client.get_compression_stats();
    if (comp_stats.total_uncompressed_bytes > 0) {
        std::cout << std::endl;
        std::cout << "Compression:" << std::endl;
        std::cout << "  Ratio: " << std::fixed << std::setprecision(2)
                  << comp_stats.compression_ratio() << std::endl;
        std::cout << "  Data saved: " << format_bytes(
            comp_stats.total_uncompressed_bytes - comp_stats.total_compressed_bytes) << std::endl;
    }

    std::cout << std::endl;

    // Disconnect
    auto disconnect_result = client.disconnect();
    if (!disconnect_result.has_value()) {
        std::cerr << "Disconnect error: " << disconnect_result.error().message << std::endl;
    }

    return result.all_succeeded() ? 0 : (result.succeeded > 0 ? 2 : 1);
}
