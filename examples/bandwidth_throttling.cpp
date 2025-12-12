/**
 * @file bandwidth_throttling.cpp
 * @brief Bandwidth throttling example
 *
 * This example demonstrates:
 * - Setting upload and download bandwidth limits
 * - Monitoring actual transfer rates
 * - Comparing throttled vs unlimited transfers
 * - Dynamic bandwidth adjustment scenarios
 */

#include <kcenon/file_transfer/client/file_transfer_client.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
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
 * @brief Format transfer rate into human-readable string
 */
auto format_rate(double bytes_per_second) -> std::string {
    return format_bytes(static_cast<uint64_t>(bytes_per_second)) + "/s";
}

/**
 * @brief Create a test file for demonstration
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

    std::cout << "Created test file: " << path << " (" << format_bytes(size) << ")" << std::endl;
}

/**
 * @brief Parse size string (e.g., "10M", "1G", "100K")
 */
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

/**
 * @brief Common bandwidth presets
 */
struct bandwidth_preset {
    std::string name;
    std::string description;
    std::size_t upload_limit;
    std::size_t download_limit;
};

const std::vector<bandwidth_preset> presets = {
    {"unlimited", "No bandwidth limits", 0, 0},
    {"dialup", "56 Kbps dial-up simulation", 7 * 1024, 7 * 1024},
    {"dsl", "1 Mbps DSL connection", 128 * 1024, 128 * 1024},
    {"cable", "10 Mbps cable connection", 1 * 1024 * 1024, 1 * 1024 * 1024},
    {"fast", "50 Mbps connection", 6 * 1024 * 1024, 6 * 1024 * 1024},
    {"asymmetric", "Common home connection (10 down / 1 up)", 128 * 1024, 1 * 1024 * 1024},
};

}  // namespace

void print_usage(const char* program) {
    std::cout << "Bandwidth Throttling Example - File Transfer System" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << program << " [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --host <host>           Server hostname (default: localhost)" << std::endl;
    std::cout << "  -p, --port <port>           Server port (default: 8080)" << std::endl;
    std::cout << "  --upload-limit <rate>       Upload limit (e.g., 100K, 1M, 10M)" << std::endl;
    std::cout << "  --download-limit <rate>     Download limit (e.g., 100K, 1M, 10M)" << std::endl;
    std::cout << "  --preset <name>             Use a bandwidth preset" << std::endl;
    std::cout << "  --file <path>               File to upload" << std::endl;
    std::cout << "  --file-size <size>          Size for test file (default: 5M)" << std::endl;
    std::cout << "  --compare                   Compare different bandwidth settings" << std::endl;
    std::cout << "  --list-presets              Show available presets" << std::endl;
    std::cout << "  --help                      Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << " --upload-limit 1M --download-limit 2M" << std::endl;
    std::cout << "  " << program << " --preset cable --file data.bin" << std::endl;
    std::cout << "  " << program << " --compare --file-size 10M" << std::endl;
}

void list_presets() {
    std::cout << "Available Bandwidth Presets:" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    for (const auto& preset : presets) {
        std::cout << std::endl;
        std::cout << "  " << preset.name << std::endl;
        std::cout << "    Description: " << preset.description << std::endl;
        if (preset.upload_limit == 0) {
            std::cout << "    Upload: unlimited" << std::endl;
        } else {
            std::cout << "    Upload: " << format_rate(static_cast<double>(preset.upload_limit)) << std::endl;
        }
        if (preset.download_limit == 0) {
            std::cout << "    Download: unlimited" << std::endl;
        } else {
            std::cout << "    Download: " << format_rate(static_cast<double>(preset.download_limit)) << std::endl;
        }
    }

    std::cout << std::endl;
}

/**
 * @brief Run upload with specific bandwidth settings
 */
auto run_throttled_upload(
    const std::string& host,
    uint16_t port,
    const std::string& file_path,
    std::size_t upload_limit,
    bool verbose
) -> std::pair<bool, double> {
    // Build client with bandwidth limit
    auto builder = file_transfer_client::builder()
        .with_compression(compression_mode::none)  // Disable compression for accurate rate measurement
        .with_auto_reconnect(false)
        .with_connect_timeout(std::chrono::milliseconds{5000});

    if (upload_limit > 0) {
        builder.with_upload_bandwidth_limit(upload_limit);
    }

    auto client_result = builder.build();

    if (!client_result.has_value()) {
        if (verbose) {
            std::cerr << "Failed to create client: " << client_result.error().message << std::endl;
        }
        return {false, 0.0};
    }

    auto& client = client_result.value();

    // Track rate samples
    std::vector<double> rate_samples;
    uint64_t last_bytes = 0;
    auto last_update = std::chrono::steady_clock::now();

    if (verbose) {
        client.on_progress([&](const transfer_progress& progress) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_update).count();

            if (elapsed >= 200) {
                auto bytes_delta = progress.bytes_transferred - last_bytes;
                double rate = static_cast<double>(bytes_delta) * 1000.0 /
                              static_cast<double>(elapsed);
                rate_samples.push_back(rate);
                last_bytes = progress.bytes_transferred;
                last_update = now;

                constexpr int bar_width = 25;
                int filled = static_cast<int>(progress.percentage / 100.0 * bar_width);

                std::cout << "\r[";
                for (int i = 0; i < bar_width; ++i) {
                    if (i < filled) std::cout << "=";
                    else if (i == filled) std::cout << ">";
                    else std::cout << " ";
                }
                std::cout << "] " << std::fixed << std::setprecision(1) << progress.percentage << "%"
                          << " | " << format_rate(rate);
                if (upload_limit > 0) {
                    std::cout << " (limit: " << format_rate(static_cast<double>(upload_limit)) << ")";
                }
                std::cout << "     " << std::flush;

                if (progress.percentage >= 100.0) {
                    std::cout << std::endl;
                }
            }
        });
    }

    // Connect
    auto connect_result = client.connect(endpoint{host, port});
    if (!connect_result.has_value()) {
        if (verbose) {
            std::cerr << "Failed to connect: " << connect_result.error().message << std::endl;
        }
        return {false, 0.0};
    }

    // Upload
    auto start_time = std::chrono::steady_clock::now();

    upload_options options;
    options.overwrite = true;
    options.compression = compression_mode::none;

    std::string remote_name = "throttle_test_" + std::to_string(upload_limit);

    auto upload_result = client.upload_file(file_path, remote_name, options);
    if (!upload_result.has_value()) {
        if (verbose) {
            std::cerr << "Failed to start upload: " << upload_result.error().message << std::endl;
        }
        (void)client.disconnect();
        return {false, 0.0};
    }

    auto wait_result = upload_result.value().wait();
    auto end_time = std::chrono::steady_clock::now();

    (void)client.disconnect();

    if (!wait_result.has_value() || !wait_result.value().success) {
        return {false, 0.0};
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    double avg_rate = static_cast<double>(wait_result.value().bytes_transferred) * 1000.0 /
                      static_cast<double>(elapsed);

    return {true, avg_rate};
}

int main(int argc, char* argv[]) {
    // Default configuration
    std::string host = "localhost";
    uint16_t port = 8080;
    std::size_t upload_limit = 0;
    std::size_t download_limit = 0;
    std::optional<std::string> preset_name;
    std::string file_path = "throttle_test.bin";
    std::size_t file_size = 5 * 1024 * 1024;  // 5MB
    bool compare_mode = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--list-presets") {
            list_presets();
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
        } else if (arg == "--upload-limit") {
            if (++i >= argc) {
                std::cerr << "Error: --upload-limit requires an argument" << std::endl;
                return 1;
            }
            upload_limit = parse_size(argv[i]);
        } else if (arg == "--download-limit") {
            if (++i >= argc) {
                std::cerr << "Error: --download-limit requires an argument" << std::endl;
                return 1;
            }
            download_limit = parse_size(argv[i]);
        } else if (arg == "--preset") {
            if (++i >= argc) {
                std::cerr << "Error: --preset requires a name argument" << std::endl;
                return 1;
            }
            preset_name = argv[i];
        } else if (arg == "--file") {
            if (++i >= argc) {
                std::cerr << "Error: --file requires a path argument" << std::endl;
                return 1;
            }
            file_path = argv[i];
        } else if (arg == "--file-size") {
            if (++i >= argc) {
                std::cerr << "Error: --file-size requires an argument" << std::endl;
                return 1;
            }
            file_size = parse_size(argv[i]);
        } else if (arg == "--compare") {
            compare_mode = true;
        }
    }

    // Apply preset if specified
    if (preset_name) {
        auto it = std::find_if(presets.begin(), presets.end(),
            [&](const bandwidth_preset& p) { return p.name == *preset_name; });
        if (it == presets.end()) {
            std::cerr << "Error: Unknown preset: " << *preset_name << std::endl;
            std::cerr << "Use --list-presets to see available presets" << std::endl;
            return 1;
        }
        upload_limit = it->upload_limit;
        download_limit = it->download_limit;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "   Bandwidth Throttling Example" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    // Create test file if it doesn't exist
    if (!std::filesystem::exists(file_path)) {
        std::cout << "Creating test file..." << std::endl;
        try {
            create_test_file(file_path, file_size);
        } catch (const std::exception& e) {
            std::cerr << "Error creating test file: " << e.what() << std::endl;
            return 1;
        }
        std::cout << std::endl;
    }

    file_size = std::filesystem::file_size(file_path);

    if (compare_mode) {
        // Compare different bandwidth settings
        std::cout << "Comparing Bandwidth Settings" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        std::cout << "File: " << file_path << " (" << format_bytes(file_size) << ")" << std::endl;
        std::cout << "Server: " << host << ":" << port << std::endl;
        std::cout << std::endl;

        // Test presets (excluding very slow ones for practicality)
        std::vector<std::pair<std::string, std::size_t>> test_configs = {
            {"Unlimited", 0},
            {"10 MB/s", 10 * 1024 * 1024},
            {"5 MB/s", 5 * 1024 * 1024},
            {"1 MB/s", 1 * 1024 * 1024},
            {"500 KB/s", 500 * 1024},
        };

        std::vector<std::tuple<std::string, std::size_t, double, double>> results;

        for (const auto& [name, limit] : test_configs) {
            std::cout << "Testing: " << name << "..." << std::endl;

            auto [success, rate] = run_throttled_upload(host, port, file_path, limit, true);

            if (success) {
                double elapsed = static_cast<double>(file_size) / rate;
                results.emplace_back(name, limit, rate, elapsed);
                std::cout << "  Result: " << format_rate(rate) << std::endl;
            } else {
                std::cout << "  Result: FAILED" << std::endl;
            }

            std::cout << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds{500});
        }

        // Print comparison table
        std::cout << "Results Summary" << std::endl;
        std::cout << std::string(70, '-') << std::endl;
        std::cout << std::left << std::setw(15) << "Config"
                  << std::setw(15) << "Limit"
                  << std::setw(15) << "Actual Rate"
                  << std::setw(15) << "Time"
                  << "Efficiency" << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        for (const auto& [name, limit, rate, elapsed] : results) {
            std::cout << std::left << std::setw(15) << name;

            if (limit == 0) {
                std::cout << std::setw(15) << "unlimited";
            } else {
                std::cout << std::setw(15) << format_rate(static_cast<double>(limit));
            }

            std::cout << std::setw(15) << format_rate(rate);
            std::cout << std::setw(15) << (std::to_string(static_cast<int>(elapsed)) + "s");

            if (limit > 0) {
                double efficiency = (rate / static_cast<double>(limit)) * 100.0;
                std::cout << std::fixed << std::setprecision(1) << efficiency << "%";
            } else {
                std::cout << "-";
            }
            std::cout << std::endl;
        }

        std::cout << std::string(70, '-') << std::endl;
        std::cout << std::endl;
        std::cout << "Note: Efficiency shows how close actual rate is to the limit." << std::endl;
        std::cout << "Values close to 100% indicate accurate throttling." << std::endl;

    } else {
        // Single transfer with specified limits
        std::cout << "Configuration:" << std::endl;
        std::cout << "  Server: " << host << ":" << port << std::endl;
        std::cout << "  File: " << file_path << " (" << format_bytes(file_size) << ")" << std::endl;
        if (upload_limit > 0) {
            std::cout << "  Upload limit: " << format_rate(static_cast<double>(upload_limit)) << std::endl;
        } else {
            std::cout << "  Upload limit: unlimited" << std::endl;
        }
        if (download_limit > 0) {
            std::cout << "  Download limit: " << format_rate(static_cast<double>(download_limit)) << std::endl;
        } else {
            std::cout << "  Download limit: unlimited" << std::endl;
        }
        std::cout << std::endl;

        // Build client
        std::cout << "[1/3] Creating client with bandwidth limits..." << std::endl;
        auto builder = file_transfer_client::builder()
            .with_compression(compression_mode::none)
            .with_auto_reconnect(true)
            .with_connect_timeout(std::chrono::milliseconds{10000});

        if (upload_limit > 0) {
            builder.with_upload_bandwidth_limit(upload_limit);
        }
        if (download_limit > 0) {
            builder.with_download_bandwidth_limit(download_limit);
        }

        auto client_result = builder.build();

        if (!client_result.has_value()) {
            std::cerr << "Failed to create client: " << client_result.error().message << std::endl;
            return 1;
        }

        auto& client = client_result.value();

        // Track progress
        auto start_time = std::chrono::steady_clock::now();
        std::vector<double> rate_samples;
        uint64_t last_bytes = 0;
        auto last_update = start_time;

        client.on_progress([&](const transfer_progress& progress) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_update).count();

            if (elapsed >= 200) {
                auto bytes_delta = progress.bytes_transferred - last_bytes;
                double rate = static_cast<double>(bytes_delta) * 1000.0 /
                              static_cast<double>(elapsed);
                rate_samples.push_back(rate);
                last_bytes = progress.bytes_transferred;
                last_update = now;

                constexpr int bar_width = 25;
                int filled = static_cast<int>(progress.percentage / 100.0 * bar_width);

                std::cout << "\r[";
                for (int i = 0; i < bar_width; ++i) {
                    if (i < filled) std::cout << "=";
                    else if (i == filled) std::cout << ">";
                    else std::cout << " ";
                }
                std::cout << "] " << std::fixed << std::setprecision(1) << progress.percentage << "%"
                          << " | " << format_rate(rate);
                if (upload_limit > 0) {
                    std::cout << " (limit: " << format_rate(static_cast<double>(upload_limit)) << ")";
                }
                std::cout << "     " << std::flush;

                if (progress.percentage >= 100.0) {
                    std::cout << std::endl;
                }
            }
        });

        client.on_complete([](const transfer_result& result) {
            if (result.success) {
                std::cout << "[Complete] Transfer successful!" << std::endl;
            } else {
                std::cout << "[Failed] " << result.error_message << std::endl;
            }
        });

        // Connect
        std::cout << "[2/3] Connecting to server..." << std::endl;
        auto connect_result = client.connect(endpoint{host, port});
        if (!connect_result.has_value()) {
            std::cerr << "Failed to connect: " << connect_result.error().message << std::endl;
            return 1;
        }
        std::cout << "[Connection] Connected!" << std::endl;
        std::cout << std::endl;

        // Upload
        std::cout << "[3/3] Starting throttled upload..." << std::endl;
        upload_options options;
        options.overwrite = true;
        options.compression = compression_mode::none;

        auto upload_result = client.upload_file(file_path,
            std::filesystem::path(file_path).filename().string(), options);
        if (!upload_result.has_value()) {
            std::cerr << "Failed to start upload: " << upload_result.error().message << std::endl;
            (void)client.disconnect();
            return 1;
        }

        // Wait for completion
        auto wait_result = upload_result.value().wait();
        auto end_time = std::chrono::steady_clock::now();

        // Calculate statistics
        double avg_rate = 0.0;
        double min_rate = 0.0;
        double max_rate = 0.0;

        if (!rate_samples.empty()) {
            avg_rate = std::accumulate(rate_samples.begin(), rate_samples.end(), 0.0) /
                       static_cast<double>(rate_samples.size());
            min_rate = *std::min_element(rate_samples.begin(), rate_samples.end());
            max_rate = *std::max_element(rate_samples.begin(), rate_samples.end());
        }

        // Print summary
        std::cout << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "       Transfer Summary" << std::endl;
        std::cout << "========================================" << std::endl;

        if (wait_result.has_value() && wait_result.value().success) {
            auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time);
            double overall_rate = static_cast<double>(wait_result.value().bytes_transferred) *
                                  1000.0 / static_cast<double>(total_elapsed.count());

            std::cout << "Status: SUCCESS" << std::endl;
            std::cout << "Bytes transferred: " << format_bytes(wait_result.value().bytes_transferred) << std::endl;
            std::cout << "Time elapsed: " << total_elapsed.count() << " ms" << std::endl;
            std::cout << std::endl;
            std::cout << "Rate Statistics:" << std::endl;
            std::cout << "  Overall average: " << format_rate(overall_rate) << std::endl;
            std::cout << "  Sample average: " << format_rate(avg_rate) << std::endl;
            std::cout << "  Minimum: " << format_rate(min_rate) << std::endl;
            std::cout << "  Maximum: " << format_rate(max_rate) << std::endl;

            if (upload_limit > 0) {
                double efficiency = (overall_rate / static_cast<double>(upload_limit)) * 100.0;
                std::cout << std::endl;
                std::cout << "Throttling Analysis:" << std::endl;
                std::cout << "  Target limit: " << format_rate(static_cast<double>(upload_limit)) << std::endl;
                std::cout << "  Actual rate: " << format_rate(overall_rate) << std::endl;
                std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << efficiency << "%" << std::endl;

                if (efficiency > 95.0) {
                    std::cout << "  Status: Excellent throttling accuracy" << std::endl;
                } else if (efficiency > 80.0) {
                    std::cout << "  Status: Good throttling accuracy" << std::endl;
                } else {
                    std::cout << "  Status: Rate below limit (possible network constraint)" << std::endl;
                }
            }
        } else {
            std::cout << "Status: FAILED" << std::endl;
        }

        std::cout << std::endl;

        // Disconnect
        (void)client.disconnect();
    }

    return 0;
}
