/**
 * @file auto_reconnect.cpp
 * @brief Auto-reconnection configuration and callback handling example
 *
 * This example demonstrates:
 * - Configuring auto-reconnection policy
 * - Setting up reconnect delay and backoff
 * - Handling connection state changes
 * - Monitoring reconnection attempts
 * - Graceful handling of network interruptions
 */

#include <kcenon/file_transfer/client/file_transfer_client.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

using namespace kcenon::file_transfer;

namespace {

std::atomic<bool> running{true};

/**
 * @brief Format bytes into human-readable string
 */
auto format_bytes(uint64_t bytes) -> std::string {
    constexpr uint64_t KB = 1024;
    constexpr uint64_t MB = KB * 1024;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    if (bytes >= MB) {
        oss << static_cast<double>(bytes) / static_cast<double>(MB) << " MB";
    } else if (bytes >= KB) {
        oss << static_cast<double>(bytes) / static_cast<double>(KB) << " KB";
    } else {
        oss << bytes << " bytes";
    }
    return oss.str();
}

/**
 * @brief Format duration to human-readable string
 */
auto format_duration(std::chrono::milliseconds ms) -> std::string {
    if (ms.count() >= 1000) {
        return std::to_string(ms.count() / 1000) + "." +
               std::to_string((ms.count() % 1000) / 100) + "s";
    }
    return std::to_string(ms.count()) + "ms";
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

}  // namespace

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nShutdown signal received..." << std::endl;
        running = false;
    }
}

void print_usage(const char* program) {
    std::cout << "Auto-Reconnect Example - File Transfer System" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << program << " [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --host <host>           Server hostname (default: localhost)" << std::endl;
    std::cout << "  -p, --port <port>           Server port (default: 8080)" << std::endl;
    std::cout << "  --max-attempts <n>          Maximum reconnection attempts (default: 5)" << std::endl;
    std::cout << "  --initial-delay <ms>        Initial delay before reconnect (default: 1000ms)" << std::endl;
    std::cout << "  --max-delay <ms>            Maximum delay between attempts (default: 30000ms)" << std::endl;
    std::cout << "  --backoff <multiplier>      Backoff multiplier (default: 2.0)" << std::endl;
    std::cout << "  --no-reconnect              Disable auto-reconnection" << std::endl;
    std::cout << "  --upload <file>             Upload a file after connecting" << std::endl;
    std::cout << "  --create-test               Create a test file for upload" << std::endl;
    std::cout << "  --help                      Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << " --max-attempts 10 --initial-delay 2000" << std::endl;
    std::cout << "  " << program << " --backoff 1.5 --max-delay 60000" << std::endl;
    std::cout << "  " << program << " --create-test --upload test_file.bin" << std::endl;
}

int main(int argc, char* argv[]) {
    // Default configuration
    std::string host = "localhost";
    uint16_t port = 8080;
    reconnect_policy policy;
    policy.max_attempts = 5;
    policy.initial_delay = std::chrono::milliseconds{1000};
    policy.max_delay = std::chrono::milliseconds{30000};
    policy.backoff_multiplier = 2.0;
    bool auto_reconnect_enabled = true;
    std::optional<std::string> upload_file;
    bool create_test = false;

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
        } else if (arg == "--max-attempts") {
            if (++i >= argc) {
                std::cerr << "Error: --max-attempts requires an argument" << std::endl;
                return 1;
            }
            policy.max_attempts = static_cast<std::size_t>(std::stoi(argv[i]));
        } else if (arg == "--initial-delay") {
            if (++i >= argc) {
                std::cerr << "Error: --initial-delay requires an argument" << std::endl;
                return 1;
            }
            policy.initial_delay = std::chrono::milliseconds{std::stoi(argv[i])};
        } else if (arg == "--max-delay") {
            if (++i >= argc) {
                std::cerr << "Error: --max-delay requires an argument" << std::endl;
                return 1;
            }
            policy.max_delay = std::chrono::milliseconds{std::stoi(argv[i])};
        } else if (arg == "--backoff") {
            if (++i >= argc) {
                std::cerr << "Error: --backoff requires an argument" << std::endl;
                return 1;
            }
            policy.backoff_multiplier = std::stod(argv[i]);
        } else if (arg == "--no-reconnect") {
            auto_reconnect_enabled = false;
        } else if (arg == "--upload") {
            if (++i >= argc) {
                std::cerr << "Error: --upload requires a file argument" << std::endl;
                return 1;
            }
            upload_file = argv[i];
        } else if (arg == "--create-test") {
            create_test = true;
        }
    }

    // Create test file if requested
    std::string test_file_path = "auto_reconnect_test.bin";
    if (create_test) {
        try {
            create_test_file(test_file_path, 10 * 1024 * 1024);  // 10MB
            if (!upload_file) {
                upload_file = test_file_path;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error creating test file: " << e.what() << std::endl;
            return 1;
        }
    }

    std::cout << "========================================" << std::endl;
    std::cout << "    Auto-Reconnect Example" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Server: " << host << ":" << port << std::endl;
    std::cout << "  Auto-reconnect: " << (auto_reconnect_enabled ? "enabled" : "disabled") << std::endl;
    if (auto_reconnect_enabled) {
        std::cout << "  Reconnect policy:" << std::endl;
        std::cout << "    Max attempts: " << policy.max_attempts << std::endl;
        std::cout << "    Initial delay: " << format_duration(policy.initial_delay) << std::endl;
        std::cout << "    Max delay: " << format_duration(policy.max_delay) << std::endl;
        std::cout << "    Backoff multiplier: " << std::fixed << std::setprecision(1)
                  << policy.backoff_multiplier << "x" << std::endl;
    }
    if (upload_file) {
        std::cout << "  Upload file: " << *upload_file << std::endl;
    }
    std::cout << std::endl;

    // Set up signal handler
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Build client with reconnection policy
    std::cout << "[1/3] Creating client with auto-reconnect policy..." << std::endl;
    auto client_result = file_transfer_client::builder()
        .with_compression(compression_mode::adaptive)
        .with_auto_reconnect(auto_reconnect_enabled, policy)
        .with_connect_timeout(std::chrono::milliseconds{5000})
        .build();

    if (!client_result.has_value()) {
        std::cerr << "Failed to create client: " << client_result.error().message << std::endl;
        return 1;
    }

    auto& client = client_result.value();

    // Track connection state
    std::atomic<connection_state> current_state{connection_state::disconnected};
    std::atomic<int> reconnect_count{0};
    auto last_state_change = std::chrono::steady_clock::now();

    // Register connection state callback
    client.on_connection_state_changed([&](connection_state state) {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_state_change);
        last_state_change = now;

        current_state = state;

        std::cout << "[Connection] State: " << to_string(state);
        if (duration.count() > 0) {
            std::cout << " (after " << format_duration(duration) << ")";
        }
        std::cout << std::endl;

        if (state == connection_state::reconnecting) {
            reconnect_count++;
            std::cout << "[Reconnect] Attempt #" << reconnect_count << " of "
                      << policy.max_attempts << std::endl;

            // Calculate expected delay
            auto attempt = static_cast<unsigned>(reconnect_count.load() - 1);
            auto delay_multiplier = std::pow(policy.backoff_multiplier, attempt);
            auto expected_delay = std::chrono::milliseconds{
                static_cast<long long>(policy.initial_delay.count() * delay_multiplier)
            };
            if (expected_delay > policy.max_delay) {
                expected_delay = policy.max_delay;
            }
            std::cout << "[Reconnect] Expected delay: " << format_duration(expected_delay) << std::endl;
        } else if (state == connection_state::connected) {
            if (reconnect_count > 0) {
                std::cout << "[Reconnect] Successfully reconnected after "
                          << reconnect_count << " attempt(s)" << std::endl;
            }
        }
    });

    // Register progress callback
    client.on_progress([](const transfer_progress& progress) {
        constexpr int bar_width = 25;
        int filled = static_cast<int>(progress.percentage / 100.0 * bar_width);

        std::cout << "\r[";
        for (int i = 0; i < bar_width; ++i) {
            if (i < filled) std::cout << "=";
            else if (i == filled) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << std::fixed << std::setprecision(1) << progress.percentage << "%"
                  << " | " << format_bytes(progress.bytes_transferred)
                  << std::flush;

        if (progress.percentage >= 100.0) {
            std::cout << std::endl;
        }
    });

    // Register completion callback
    client.on_complete([](const transfer_result& result) {
        if (result.success) {
            std::cout << "[Complete] Transfer successful: " << result.filename << std::endl;
        } else {
            std::cout << "[Failed] Transfer failed: " << result.error_message << std::endl;
        }
    });

    // Attempt initial connection
    std::cout << std::endl;
    std::cout << "[2/3] Connecting to server..." << std::endl;
    auto connect_result = client.connect(endpoint{host, port});
    if (!connect_result.has_value()) {
        std::cerr << "Initial connection failed: " << connect_result.error().message << std::endl;
        if (auto_reconnect_enabled) {
            std::cout << "Waiting for auto-reconnection..." << std::endl;
            std::cout << "(Press Ctrl+C to exit)" << std::endl;
        } else {
            return 1;
        }
    }

    std::cout << std::endl;
    std::cout << "[3/3] Connection established. Monitoring..." << std::endl;
    std::cout << "(Press Ctrl+C to exit)" << std::endl;
    std::cout << std::endl;

    // If upload requested, do it
    if (upload_file && client.is_connected()) {
        std::cout << "[Upload] Starting file upload: " << *upload_file << std::endl;

        if (!std::filesystem::exists(*upload_file)) {
            std::cerr << "[Error] File not found: " << *upload_file << std::endl;
        } else {
            upload_options options;
            options.overwrite = true;

            auto upload_result = client.upload_file(
                *upload_file,
                std::filesystem::path(*upload_file).filename().string(),
                options
            );

            if (upload_result.has_value()) {
                auto& handle = upload_result.value();
                std::cout << "[Upload] Started (handle: " << handle.get_id() << ")" << std::endl;

                // Wait for completion
                auto wait_result = handle.wait();
                if (wait_result.has_value() && wait_result.value().success) {
                    std::cout << "[Upload] Completed successfully" << std::endl;
                } else {
                    std::cout << "[Upload] Failed" << std::endl;
                }
            } else {
                std::cerr << "[Upload] Failed to start: " << upload_result.error().message << std::endl;
            }
        }
        std::cout << std::endl;
    }

    // Monitor connection state
    std::cout << "Monitoring connection state..." << std::endl;
    std::cout << "Disconnect the server to test auto-reconnection." << std::endl;
    std::cout << std::endl;

    while (running) {
        // Print periodic status
        auto state = current_state.load();
        auto stats = client.get_statistics();

        std::cout << "\r[Status] " << to_string(state)
                  << " | Reconnects: " << reconnect_count
                  << " | Files uploaded: " << stats.total_files_uploaded
                  << " | Files downloaded: " << stats.total_files_downloaded
                  << "     " << std::flush;

        std::this_thread::sleep_for(std::chrono::seconds{2});
    }

    std::cout << std::endl;
    std::cout << std::endl;

    // Print final summary
    std::cout << "========================================" << std::endl;
    std::cout << "       Session Summary" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Total reconnection attempts: " << reconnect_count << std::endl;
    std::cout << "Final connection state: " << to_string(current_state.load()) << std::endl;

    auto stats = client.get_statistics();
    std::cout << "Bytes uploaded: " << format_bytes(stats.total_bytes_uploaded) << std::endl;
    std::cout << "Bytes downloaded: " << format_bytes(stats.total_bytes_downloaded) << std::endl;
    std::cout << std::endl;

    // Disconnect
    if (client.is_connected()) {
        auto disconnect_result = client.disconnect();
        if (!disconnect_result.has_value()) {
            std::cerr << "Disconnect error: " << disconnect_result.error().message << std::endl;
        }
    }

    return 0;
}
