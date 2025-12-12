/**
 * @file batch_download_example.cpp
 * @brief Batch file download example with concurrent transfers
 *
 * This example demonstrates:
 * - Downloading multiple files in parallel
 * - Selecting files from server file list
 * - Tracking batch progress across all downloads
 * - Handling individual file failures within a batch
 * - Verifying downloaded files
 */

#include <kcenon/file_transfer/client/file_transfer_client.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
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
 * @brief Convert glob pattern to regex
 */
auto glob_to_regex(const std::string& pattern) -> std::regex {
    std::string regex_pattern;
    for (char c : pattern) {
        switch (c) {
            case '*': regex_pattern += ".*"; break;
            case '?': regex_pattern += "."; break;
            case '.': regex_pattern += "\\."; break;
            case '[': case ']': case '(': case ')':
            case '+': case '^': case '$': case '|':
            case '\\': regex_pattern += "\\"; regex_pattern += c; break;
            default: regex_pattern += c; break;
        }
    }
    return std::regex(regex_pattern, std::regex::icase);
}

/**
 * @brief Print batch progress
 */
void print_batch_progress(const batch_progress& progress) {
    constexpr int bar_width = 40;
    int filled = static_cast<int>(progress.completion_percentage() / 100.0 * bar_width);

    std::cout << "\r[";
    for (int i = 0; i < bar_width; ++i) {
        if (i < filled) std::cout << "=";
        else if (i == filled) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << std::fixed << std::setprecision(1) << progress.completion_percentage() << "%";
    std::cout << " | Files: " << progress.completed_files << "/" << progress.total_files;
    if (progress.failed_files > 0) {
        std::cout << " (failed: " << progress.failed_files << ")";
    }
    std::cout << " | " << format_rate(progress.overall_rate);
    std::cout << " | " << format_bytes(progress.transferred_bytes) << "/" << format_bytes(progress.total_bytes);
    std::cout << "     " << std::flush;
}

}  // namespace

void print_usage(const char* program) {
    std::cout << "Batch Download Example - File Transfer System" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << program << " [options] <file1> [file2] [file3] ..." << std::endl;
    std::cout << "   or: " << program << " --pattern <glob_pattern>" << std::endl;
    std::cout << "   or: " << program << " --all" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --host <host>       Server hostname (default: localhost)" << std::endl;
    std::cout << "  -p, --port <port>       Server port (default: 8080)" << std::endl;
    std::cout << "  -d, --directory <dir>   Download directory (default: ./downloads)" << std::endl;
    std::cout << "  -j, --jobs <n>          Max concurrent downloads (default: 4)" << std::endl;
    std::cout << "  --pattern <glob>        Download files matching pattern (e.g., \"*.txt\")" << std::endl;
    std::cout << "  --all                   Download all files from server" << std::endl;
    std::cout << "  --continue-on-error     Continue batch even if some files fail (default)" << std::endl;
    std::cout << "  --stop-on-error         Stop batch on first failure" << std::endl;
    std::cout << "  --overwrite             Overwrite existing local files" << std::endl;
    std::cout << "  --list                  List available files and exit" << std::endl;
    std::cout << "  --help                  Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << " file1.txt file2.txt file3.txt" << std::endl;
    std::cout << "  " << program << " --pattern \"*.dat\" -d ./data" << std::endl;
    std::cout << "  " << program << " --all -j 8 --overwrite" << std::endl;
    std::cout << "  " << program << " --list" << std::endl;
}

int main(int argc, char* argv[]) {
    // Default configuration
    std::string host = "localhost";
    uint16_t port = 8080;
    std::string download_dir = "./downloads";
    std::size_t max_concurrent = 4;
    bool continue_on_error = true;
    bool overwrite = false;
    bool download_all = false;
    bool list_only = false;
    std::string pattern;
    std::vector<std::string> files;

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
        } else if (arg == "-d" || arg == "--directory") {
            if (++i >= argc) {
                std::cerr << "Error: --directory requires an argument" << std::endl;
                return 1;
            }
            download_dir = argv[i];
        } else if (arg == "-j" || arg == "--jobs") {
            if (++i >= argc) {
                std::cerr << "Error: --jobs requires an argument" << std::endl;
                return 1;
            }
            max_concurrent = static_cast<std::size_t>(std::stoi(argv[i]));
        } else if (arg == "--pattern") {
            if (++i >= argc) {
                std::cerr << "Error: --pattern requires an argument" << std::endl;
                return 1;
            }
            pattern = argv[i];
        } else if (arg == "--all") {
            download_all = true;
        } else if (arg == "--continue-on-error") {
            continue_on_error = true;
        } else if (arg == "--stop-on-error") {
            continue_on_error = false;
        } else if (arg == "--overwrite") {
            overwrite = true;
        } else if (arg == "--list") {
            list_only = true;
        } else if (arg[0] != '-') {
            files.push_back(arg);
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
        return 1;
    }
    std::cout << "Connected!" << std::endl;
    std::cout << std::endl;

    // Get file list from server
    list_options list_opts;
    list_opts.pattern = "*";
    list_opts.limit = 10000;

    auto list_result = client.list_files(list_opts);
    if (!list_result.has_value()) {
        std::cerr << "Failed to get file list: " << list_result.error().message << std::endl;
        (void)client.disconnect();
        return 1;
    }

    auto& server_files = list_result.value();

    // Handle list-only mode
    if (list_only) {
        std::cout << "========================================" << std::endl;
        std::cout << "       Files on Server" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << std::endl;

        if (server_files.empty()) {
            std::cout << "(No files on server)" << std::endl;
        } else {
            std::cout << std::setw(40) << std::left << "Filename"
                      << std::setw(15) << std::right << "Size" << std::endl;
            std::cout << std::string(55, '-') << std::endl;

            uint64_t total_size = 0;
            for (const auto& file : server_files) {
                std::cout << std::setw(40) << std::left << file.filename
                          << std::setw(15) << std::right << format_bytes(file.size) << std::endl;
                total_size += file.size;
            }

            std::cout << std::string(55, '-') << std::endl;
            std::cout << "Total: " << server_files.size() << " file(s), " << format_bytes(total_size) << std::endl;
        }

        (void)client.disconnect();
        return 0;
    }

    // Build download list based on options
    std::vector<download_entry> download_entries;

    if (download_all) {
        // Download all files
        for (const auto& file : server_files) {
            auto local_path = std::filesystem::path(download_dir) / file.filename;
            download_entries.emplace_back(file.filename, local_path);
        }
    } else if (!pattern.empty()) {
        // Filter by pattern
        std::regex pattern_regex = glob_to_regex(pattern);
        for (const auto& file : server_files) {
            if (std::regex_match(file.filename, pattern_regex)) {
                auto local_path = std::filesystem::path(download_dir) / file.filename;
                download_entries.emplace_back(file.filename, local_path);
            }
        }
    } else if (!files.empty()) {
        // Download specific files
        for (const auto& filename : files) {
            auto local_path = std::filesystem::path(download_dir) / filename;
            download_entries.emplace_back(filename, local_path);
        }
    }

    if (download_entries.empty()) {
        std::cerr << "Error: No files specified for download" << std::endl;
        std::cerr << "Hint: Use --all, --pattern, or specify file names" << std::endl;
        std::cerr << "Hint: Use --list to see available files on server" << std::endl;
        (void)client.disconnect();
        print_usage(argv[0]);
        return 1;
    }

    // Create download directory
    std::filesystem::create_directories(download_dir);

    // Calculate total size
    uint64_t total_size = 0;
    for (const auto& entry : download_entries) {
        for (const auto& file : server_files) {
            if (file.filename == entry.remote_name) {
                total_size += file.size;
                break;
            }
        }
    }

    // Print configuration
    std::cout << "========================================" << std::endl;
    std::cout << "     Batch Download Example" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Files to download:" << std::endl;

    for (const auto& entry : download_entries) {
        uint64_t size = 0;
        for (const auto& file : server_files) {
            if (file.filename == entry.remote_name) {
                size = file.size;
                break;
            }
        }
        std::cout << "  " << std::setw(30) << std::left << entry.remote_name
                  << " " << std::setw(12) << std::right << format_bytes(size) << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Server: " << host << ":" << port << std::endl;
    std::cout << "  Download directory: " << download_dir << std::endl;
    std::cout << "  Total files: " << download_entries.size() << std::endl;
    std::cout << "  Total size: " << format_bytes(total_size) << std::endl;
    std::cout << "  Max concurrent: " << max_concurrent << std::endl;
    std::cout << "  Continue on error: " << (continue_on_error ? "yes" : "no") << std::endl;
    std::cout << "  Overwrite: " << (overwrite ? "yes" : "no") << std::endl;
    std::cout << std::endl;

    // Register callbacks
    client.on_complete([](const transfer_result& result) {
        if (result.success) {
            std::cout << std::endl << "[File Complete] " << result.filename << " - "
                      << format_bytes(result.bytes_transferred) << std::endl;
        } else {
            std::cout << std::endl << "[File Failed] " << result.filename << " - "
                      << result.error_message << std::endl;
        }
    });

    // Configure batch options
    batch_options options;
    options.max_concurrent = max_concurrent;
    options.continue_on_error = continue_on_error;
    options.overwrite = overwrite;

    // Start batch download
    std::cout << "[1/3] Starting batch download..." << std::endl;
    auto start_time = std::chrono::steady_clock::now();

    auto batch_result = client.download_files(download_entries, options);
    if (!batch_result.has_value()) {
        std::cerr << "Failed to start batch download: " << batch_result.error().message << std::endl;
        (void)client.disconnect();
        return 1;
    }

    auto& batch_handle = batch_result.value();
    std::cout << "Batch started with ID: " << batch_handle.get_id() << std::endl;
    std::cout << std::endl;

    // Monitor progress
    std::cout << "[2/3] Downloading files..." << std::endl;
    std::cout << std::endl;

    while (true) {
        auto progress = batch_handle.get_batch_progress();

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

    // Verify downloaded files
    std::cout << std::endl;
    std::cout << "[3/3] Verifying downloaded files..." << std::endl;

    size_t verified_count = 0;
    for (const auto& file_result : result.file_results) {
        if (file_result.success) {
            // Find local path for this file
            for (const auto& entry : download_entries) {
                if (entry.remote_name == file_result.filename) {
                    if (std::filesystem::exists(entry.local_path)) {
                        auto local_size = std::filesystem::file_size(entry.local_path);
                        if (local_size == file_result.bytes_transferred) {
                            ++verified_count;
                        }
                    }
                    break;
                }
            }
        }
    }

    std::cout << "Verified: " << verified_count << "/" << result.succeeded << " files" << std::endl;

    // Print summary
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "       Batch Download Summary" << std::endl;
    std::cout << "========================================" << std::endl;

    if (result.all_succeeded()) {
        std::cout << "Status: ALL FILES DOWNLOADED SUCCESSFULLY" << std::endl;
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
    std::cout << "  Verified: " << verified_count << std::endl;
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
    }

    std::cout << std::endl;
    std::cout << "Downloaded files are in: " << std::filesystem::absolute(download_dir) << std::endl;
    std::cout << std::endl;

    // Disconnect
    auto disconnect_result = client.disconnect();
    if (!disconnect_result.has_value()) {
        std::cerr << "Disconnect error: " << disconnect_result.error().message << std::endl;
    }

    return result.all_succeeded() ? 0 : (result.succeeded > 0 ? 2 : 1);
}
