/**
 * @file custom_pipeline.cpp
 * @brief Pipeline customization example
 *
 * This example demonstrates:
 * - Customizing chunk size for different file types
 * - Configuring compression modes and levels
 * - Optimizing settings for various scenarios (LAN vs WAN)
 * - Comparing performance with different configurations
 */

#include <kcenon/file_transfer/client/file_transfer_client.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
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
 * @brief Preset configurations for different scenarios
 */
struct pipeline_preset {
    std::string name;
    std::string description;
    std::size_t chunk_size;
    compression_mode comp_mode;
    compression_level comp_level;
};

const std::vector<pipeline_preset> presets = {
    {
        "default",
        "Balanced settings for general use",
        256 * 1024,  // 256KB
        compression_mode::adaptive,
        compression_level::fast
    },
    {
        "lan-optimized",
        "High throughput for local networks",
        1024 * 1024,  // 1MB
        compression_mode::none,
        compression_level::fast
    },
    {
        "wan-optimized",
        "Bandwidth efficient for slow connections",
        64 * 1024,  // 64KB
        compression_mode::always,
        compression_level::best
    },
    {
        "small-files",
        "Optimized for many small files",
        32 * 1024,  // 32KB
        compression_mode::adaptive,
        compression_level::fast
    },
    {
        "large-files",
        "Optimized for large file transfers",
        2 * 1024 * 1024,  // 2MB
        compression_mode::adaptive,
        compression_level::balanced
    },
    {
        "high-latency",
        "For networks with high latency",
        512 * 1024,  // 512KB
        compression_mode::always,
        compression_level::balanced
    }
};

/**
 * @brief Create a test file with specified characteristics
 */
void create_test_file(
    const std::filesystem::path& path,
    size_t size,
    bool compressible
) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to create file: " + path.string());
    }

    std::vector<char> buffer(std::min(size, size_t{65536}));

    if (compressible) {
        // Create highly compressible data (repetitive pattern)
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = static_cast<char>('A' + (i % 26));
        }
    } else {
        // Create incompressible random data
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(0, 255);
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = static_cast<char>(dist(gen));
        }
    }

    size_t remaining = size;
    while (remaining > 0) {
        size_t to_write = std::min(remaining, buffer.size());
        file.write(buffer.data(), static_cast<std::streamsize>(to_write));
        remaining -= to_write;
    }
}

/**
 * @brief Get string representation of compression mode
 */
auto compression_mode_string(compression_mode mode) -> std::string {
    switch (mode) {
        case compression_mode::none: return "none";
        case compression_mode::always: return "always";
        case compression_mode::adaptive: return "adaptive";
        default: return "unknown";
    }
}

/**
 * @brief Get string representation of compression level
 */
auto compression_level_string(compression_level level) -> std::string {
    switch (level) {
        case compression_level::fast: return "fast";
        case compression_level::balanced: return "balanced";
        case compression_level::best: return "best";
        default: return "unknown";
    }
}

}  // namespace

void print_usage(const char* program) {
    std::cout << "Custom Pipeline Example - File Transfer System" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << program << " [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --host <host>           Server hostname (default: localhost)" << std::endl;
    std::cout << "  -p, --port <port>           Server port (default: 8080)" << std::endl;
    std::cout << "  --preset <name>             Use a preset configuration" << std::endl;
    std::cout << "  --chunk-size <bytes>        Custom chunk size (e.g., 256K, 1M)" << std::endl;
    std::cout << "  --compression <mode>        Compression: none, always, adaptive" << std::endl;
    std::cout << "  --level <level>             Compression level: fast, balanced, best" << std::endl;
    std::cout << "  --file <path>               File to upload (will create test if not exists)" << std::endl;
    std::cout << "  --file-size <size>          Size for test file (default: 10M)" << std::endl;
    std::cout << "  --compressible              Create compressible test file (default)" << std::endl;
    std::cout << "  --incompressible            Create incompressible (random) test file" << std::endl;
    std::cout << "  --list-presets              Show available presets" << std::endl;
    std::cout << "  --compare                   Compare presets performance" << std::endl;
    std::cout << "  --help                      Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << " --preset lan-optimized --file data.bin" << std::endl;
    std::cout << "  " << program << " --chunk-size 512K --compression always" << std::endl;
    std::cout << "  " << program << " --list-presets" << std::endl;
    std::cout << "  " << program << " --compare --file-size 50M" << std::endl;
}

void list_presets() {
    std::cout << "Available Pipeline Presets:" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    for (const auto& preset : presets) {
        std::cout << std::endl;
        std::cout << "  " << preset.name << std::endl;
        std::cout << "    Description: " << preset.description << std::endl;
        std::cout << "    Chunk size: " << format_bytes(preset.chunk_size) << std::endl;
        std::cout << "    Compression: " << compression_mode_string(preset.comp_mode) << std::endl;
        std::cout << "    Level: " << compression_level_string(preset.comp_level) << std::endl;
    }

    std::cout << std::endl;
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

/**
 * @brief Run transfer with specific configuration
 */
auto run_transfer(
    const std::string& host,
    uint16_t port,
    const std::string& file_path,
    const pipeline_preset& config
) -> std::pair<bool, double> {
    // Build client with config
    auto client_result = file_transfer_client::builder()
        .with_chunk_size(config.chunk_size)
        .with_compression(config.comp_mode)
        .with_compression_level(config.comp_level)
        .with_auto_reconnect(false)
        .with_connect_timeout(std::chrono::milliseconds{5000})
        .build();

    if (!client_result.has_value()) {
        std::cerr << "Failed to create client: " << client_result.error().message << std::endl;
        return {false, 0.0};
    }

    auto& client = client_result.value();

    // Connect
    auto connect_result = client.connect(endpoint{host, port});
    if (!connect_result.has_value()) {
        std::cerr << "Failed to connect: " << connect_result.error().message << std::endl;
        return {false, 0.0};
    }

    // Upload
    auto start_time = std::chrono::steady_clock::now();

    upload_options options;
    options.overwrite = true;

    std::string remote_name = std::filesystem::path(file_path).filename().string() +
                              "_" + config.name;

    auto upload_result = client.upload_file(file_path, remote_name, options);
    if (!upload_result.has_value()) {
        std::cerr << "Failed to start upload: " << upload_result.error().message << std::endl;
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
    double rate = static_cast<double>(wait_result.value().bytes_transferred) * 1000.0 /
                  static_cast<double>(elapsed);

    return {true, rate};
}

int main(int argc, char* argv[]) {
    // Default configuration
    std::string host = "localhost";
    uint16_t port = 8080;
    std::optional<std::string> preset_name;
    std::size_t chunk_size = 256 * 1024;
    compression_mode comp_mode = compression_mode::adaptive;
    compression_level comp_level = compression_level::fast;
    std::string file_path = "pipeline_test.bin";
    std::size_t file_size = 10 * 1024 * 1024;  // 10MB
    bool compressible = true;
    bool compare_mode = false;
    bool custom_settings = false;

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
        } else if (arg == "--preset") {
            if (++i >= argc) {
                std::cerr << "Error: --preset requires a name argument" << std::endl;
                return 1;
            }
            preset_name = argv[i];
        } else if (arg == "--chunk-size") {
            if (++i >= argc) {
                std::cerr << "Error: --chunk-size requires an argument" << std::endl;
                return 1;
            }
            chunk_size = parse_size(argv[i]);
            custom_settings = true;
        } else if (arg == "--compression") {
            if (++i >= argc) {
                std::cerr << "Error: --compression requires an argument" << std::endl;
                return 1;
            }
            try {
                comp_mode = parse_compression_mode(argv[i]);
                custom_settings = true;
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
                return 1;
            }
        } else if (arg == "--level") {
            if (++i >= argc) {
                std::cerr << "Error: --level requires an argument" << std::endl;
                return 1;
            }
            try {
                comp_level = parse_compression_level(argv[i]);
                custom_settings = true;
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
                return 1;
            }
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
        } else if (arg == "--compressible") {
            compressible = true;
        } else if (arg == "--incompressible") {
            compressible = false;
        } else if (arg == "--compare") {
            compare_mode = true;
        }
    }

    // Apply preset if specified
    pipeline_preset current_config{
        "custom",
        "Custom configuration",
        chunk_size,
        comp_mode,
        comp_level
    };

    if (preset_name && !custom_settings) {
        auto it = std::find_if(presets.begin(), presets.end(),
            [&](const pipeline_preset& p) { return p.name == *preset_name; });
        if (it == presets.end()) {
            std::cerr << "Error: Unknown preset: " << *preset_name << std::endl;
            std::cerr << "Use --list-presets to see available presets" << std::endl;
            return 1;
        }
        current_config = *it;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "    Custom Pipeline Example" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    // Create test file if it doesn't exist
    if (!std::filesystem::exists(file_path)) {
        std::cout << "Creating test file..." << std::endl;
        std::cout << "  Size: " << format_bytes(file_size) << std::endl;
        std::cout << "  Type: " << (compressible ? "compressible" : "incompressible") << std::endl;
        try {
            create_test_file(file_path, file_size, compressible);
        } catch (const std::exception& e) {
            std::cerr << "Error creating test file: " << e.what() << std::endl;
            return 1;
        }
        std::cout << std::endl;
    }

    file_size = std::filesystem::file_size(file_path);

    if (compare_mode) {
        // Compare all presets
        std::cout << "Comparing Pipeline Configurations" << std::endl;
        std::cout << std::string(70, '=') << std::endl;
        std::cout << "File: " << file_path << " (" << format_bytes(file_size) << ")" << std::endl;
        std::cout << "Server: " << host << ":" << port << std::endl;
        std::cout << std::endl;

        std::vector<std::pair<std::string, double>> results;

        for (const auto& preset : presets) {
            std::cout << "Testing preset: " << preset.name << "..." << std::flush;

            auto [success, rate] = run_transfer(host, port, file_path, preset);

            if (success) {
                results.emplace_back(preset.name, rate);
                std::cout << " " << format_rate(rate) << std::endl;
            } else {
                std::cout << " FAILED" << std::endl;
            }

            // Small delay between tests
            std::this_thread::sleep_for(std::chrono::milliseconds{500});
        }

        // Print comparison results
        std::cout << std::endl;
        std::cout << "Results Summary" << std::endl;
        std::cout << std::string(50, '-') << std::endl;

        // Sort by rate
        std::sort(results.begin(), results.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        int rank = 1;
        for (const auto& [name, rate] : results) {
            std::cout << rank++ << ". " << std::left << std::setw(20) << name
                      << format_rate(rate) << std::endl;
        }

        if (!results.empty()) {
            std::cout << std::endl;
            std::cout << "Best configuration for this file: " << results[0].first << std::endl;
        }

    } else {
        // Single transfer with current configuration
        std::cout << "Configuration:" << std::endl;
        std::cout << "  Server: " << host << ":" << port << std::endl;
        std::cout << "  File: " << file_path << " (" << format_bytes(file_size) << ")" << std::endl;
        std::cout << "  Preset: " << current_config.name << std::endl;
        std::cout << "  Chunk size: " << format_bytes(current_config.chunk_size) << std::endl;
        std::cout << "  Compression: " << compression_mode_string(current_config.comp_mode) << std::endl;
        std::cout << "  Level: " << compression_level_string(current_config.comp_level) << std::endl;
        std::cout << std::endl;

        // Build client
        std::cout << "[1/3] Creating client with custom pipeline settings..." << std::endl;
        auto client_result = file_transfer_client::builder()
            .with_chunk_size(current_config.chunk_size)
            .with_compression(current_config.comp_mode)
            .with_compression_level(current_config.comp_level)
            .with_auto_reconnect(true)
            .with_connect_timeout(std::chrono::milliseconds{10000})
            .build();

        if (!client_result.has_value()) {
            std::cerr << "Failed to create client: " << client_result.error().message << std::endl;
            return 1;
        }

        auto& client = client_result.value();

        // Track progress
        auto start_time = std::chrono::steady_clock::now();
        uint64_t last_bytes = 0;
        auto last_update = start_time;
        double current_rate = 0.0;

        client.on_progress([&](const transfer_progress& progress) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_update).count();

            if (elapsed >= 100) {
                auto bytes_delta = progress.bytes_transferred - last_bytes;
                current_rate = static_cast<double>(bytes_delta) * 1000.0 /
                               static_cast<double>(elapsed);
                last_bytes = progress.bytes_transferred;
                last_update = now;
            }

            constexpr int bar_width = 30;
            int filled = static_cast<int>(progress.percentage / 100.0 * bar_width);

            std::cout << "\r[";
            for (int i = 0; i < bar_width; ++i) {
                if (i < filled) std::cout << "=";
                else if (i == filled) std::cout << ">";
                else std::cout << " ";
            }
            std::cout << "] " << std::fixed << std::setprecision(1) << progress.percentage << "%"
                      << " | " << format_bytes(progress.bytes_transferred)
                      << " | " << format_rate(current_rate)
                      << "     " << std::flush;

            if (progress.percentage >= 100.0) {
                std::cout << std::endl;
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
        std::cout << "[3/3] Starting upload..." << std::endl;
        upload_options options;
        options.overwrite = true;

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

        // Print summary
        std::cout << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "       Transfer Summary" << std::endl;
        std::cout << "========================================" << std::endl;

        if (wait_result.has_value() && wait_result.value().success) {
            auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time);
            double avg_rate = static_cast<double>(wait_result.value().bytes_transferred) *
                              1000.0 / static_cast<double>(total_elapsed.count());

            std::cout << "Status: SUCCESS" << std::endl;
            std::cout << "Bytes transferred: " << format_bytes(wait_result.value().bytes_transferred) << std::endl;
            std::cout << "Time elapsed: " << total_elapsed.count() << " ms" << std::endl;
            std::cout << "Average rate: " << format_rate(avg_rate) << std::endl;

            auto comp_stats = client.get_compression_stats();
            if (comp_stats.total_uncompressed_bytes > 0) {
                std::cout << "Compression ratio: " << std::fixed << std::setprecision(2)
                          << comp_stats.compression_ratio() << std::endl;
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
