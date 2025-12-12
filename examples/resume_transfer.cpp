/**
 * @file resume_transfer.cpp
 * @brief Transfer pause/resume example with interruption handling
 *
 * This example demonstrates:
 * - Pausing and resuming file transfers
 * - Simulating transfer interruptions
 * - Using transfer handles for control
 * - Progress monitoring during pause/resume cycles
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

std::atomic<bool> pause_requested{false};
std::atomic<bool> resume_requested{false};
std::atomic<bool> cancel_requested{false};

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
 * @brief Parse size string (e.g., "10M", "1G")
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

}  // namespace

void print_usage(const char* program) {
    std::cout << "Resume Transfer Example - File Transfer System" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << program << " [options] <local_file> <remote_name>" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --host <host>       Server hostname (default: localhost)" << std::endl;
    std::cout << "  -p, --port <port>       Server port (default: 8080)" << std::endl;
    std::cout << "  --create-test <size>    Create test file of specified size (e.g., 50M, 100M)" << std::endl;
    std::cout << "  --auto-pause <percent>  Auto-pause at specified percentage (for demo)" << std::endl;
    std::cout << "  --pause-duration <ms>   Duration to stay paused (default: 3000ms)" << std::endl;
    std::cout << "  --help                  Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Interactive controls during transfer:" << std::endl;
    std::cout << "  Press Ctrl+C once to pause" << std::endl;
    std::cout << "  Press Ctrl+C again to resume (when paused)" << std::endl;
    std::cout << "  Press Ctrl+C three times to cancel" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << " --create-test 50M test.bin upload.bin" << std::endl;
    std::cout << "  " << program << " --auto-pause 30 large_file.bin remote.bin" << std::endl;
}

void signal_handler(int signal) {
    if (signal == SIGINT) {
        if (pause_requested && !resume_requested) {
            std::cout << "\n[Signal] Resume requested..." << std::endl;
            resume_requested = true;
        } else if (!pause_requested) {
            std::cout << "\n[Signal] Pause requested..." << std::endl;
            pause_requested = true;
        } else {
            std::cout << "\n[Signal] Cancel requested..." << std::endl;
            cancel_requested = true;
        }
    }
}

int main(int argc, char* argv[]) {
    // Default configuration
    std::string host = "localhost";
    uint16_t port = 8080;
    std::string local_path;
    std::string remote_name;
    std::optional<size_t> create_test_size;
    std::optional<double> auto_pause_percent;
    std::chrono::milliseconds pause_duration{3000};

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
        } else if (arg == "--create-test") {
            if (++i >= argc) {
                std::cerr << "Error: --create-test requires a size argument" << std::endl;
                return 1;
            }
            create_test_size = parse_size(argv[i]);
        } else if (arg == "--auto-pause") {
            if (++i >= argc) {
                std::cerr << "Error: --auto-pause requires a percentage argument" << std::endl;
                return 1;
            }
            auto_pause_percent = std::stod(argv[i]);
        } else if (arg == "--pause-duration") {
            if (++i >= argc) {
                std::cerr << "Error: --pause-duration requires a milliseconds argument" << std::endl;
                return 1;
            }
            pause_duration = std::chrono::milliseconds{std::stoi(argv[i])};
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
        return 1;
    }

    auto file_size = std::filesystem::file_size(local_path);

    std::cout << "========================================" << std::endl;
    std::cout << "    Resume Transfer Example" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Server: " << host << ":" << port << std::endl;
    std::cout << "  Local file: " << local_path << std::endl;
    std::cout << "  Remote name: " << remote_name << std::endl;
    std::cout << "  File size: " << format_bytes(file_size) << std::endl;
    if (auto_pause_percent) {
        std::cout << "  Auto-pause at: " << *auto_pause_percent << "%" << std::endl;
        std::cout << "  Pause duration: " << pause_duration.count() << "ms" << std::endl;
    }
    std::cout << std::endl;

    // Set up signal handler for interactive pause/resume
    std::signal(SIGINT, signal_handler);

    // Build client
    std::cout << "[1/4] Creating client..." << std::endl;
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

    // Track transfer state
    std::atomic<bool> transfer_complete{false};
    std::atomic<bool> is_paused{false};
    std::atomic<double> current_percentage{0.0};

    // Register progress callback
    client.on_progress([&](const transfer_progress& progress) {
        current_percentage = progress.percentage;

        // Display progress
        constexpr int bar_width = 30;
        int filled = static_cast<int>(progress.percentage / 100.0 * bar_width);

        std::string status = is_paused ? "PAUSED" : "ACTIVE";

        std::cout << "\r[";
        for (int i = 0; i < bar_width; ++i) {
            if (i < filled) std::cout << "=";
            else if (i == filled) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << std::fixed << std::setprecision(1) << progress.percentage << "%"
                  << " | " << format_bytes(progress.bytes_transferred)
                  << " | " << status << "     " << std::flush;

        if (progress.percentage >= 100.0) {
            std::cout << std::endl;
        }
    });

    // Register completion callback
    client.on_complete([&](const transfer_result& result) {
        transfer_complete = true;
        if (result.success) {
            std::cout << "[Complete] Transfer finished successfully!" << std::endl;
        } else {
            std::cout << "[Failed] Transfer failed: " << result.error_message << std::endl;
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
        return 1;
    }
    std::cout << "[Connection] Connected!" << std::endl;
    std::cout << std::endl;

    // Start upload
    std::cout << "[3/4] Starting upload..." << std::endl;
    upload_options options;
    options.overwrite = true;

    auto upload_result = client.upload_file(local_path, remote_name, options);
    if (!upload_result.has_value()) {
        std::cerr << "Failed to initiate upload: " << upload_result.error().message << std::endl;
        (void)client.disconnect();
        return 1;
    }

    auto& handle = upload_result.value();
    std::cout << "Upload started (handle ID: " << handle.get_id() << ")" << std::endl;
    std::cout << std::endl;

    std::cout << "[4/4] Monitoring transfer with pause/resume capability..." << std::endl;
    if (!auto_pause_percent) {
        std::cout << "Press Ctrl+C to pause/resume" << std::endl;
    }
    std::cout << std::endl;

    // Monitor transfer with pause/resume handling
    bool auto_paused = false;
    while (!transfer_complete && !cancel_requested) {
        // Check for auto-pause trigger
        if (auto_pause_percent && !auto_paused && current_percentage >= *auto_pause_percent) {
            std::cout << std::endl;
            std::cout << "[Auto-pause] Triggered at " << current_percentage << "%" << std::endl;

            auto pause_result = handle.pause();
            if (pause_result.has_value()) {
                is_paused = true;
                std::cout << "[Paused] Transfer paused. Waiting " << pause_duration.count() << "ms..." << std::endl;

                // Wait for pause duration
                std::this_thread::sleep_for(pause_duration);

                std::cout << "[Resuming] Resuming transfer..." << std::endl;
                auto resume_result = handle.resume();
                if (resume_result.has_value()) {
                    is_paused = false;
                    std::cout << "[Resumed] Transfer resumed!" << std::endl;
                } else {
                    std::cerr << "[Error] Failed to resume: " << resume_result.error().message << std::endl;
                }
            } else {
                std::cerr << "[Error] Failed to pause: " << pause_result.error().message << std::endl;
            }
            auto_paused = true;
        }

        // Check for manual pause request
        if (pause_requested && !is_paused) {
            auto pause_result = handle.pause();
            if (pause_result.has_value()) {
                is_paused = true;
                std::cout << std::endl;
                std::cout << "[Paused] Transfer paused. Press Ctrl+C to resume." << std::endl;
            } else {
                std::cerr << std::endl;
                std::cerr << "[Error] Failed to pause: " << pause_result.error().message << std::endl;
            }
            pause_requested = false;
        }

        // Check for manual resume request
        if (resume_requested && is_paused) {
            auto resume_result = handle.resume();
            if (resume_result.has_value()) {
                is_paused = false;
                std::cout << "[Resumed] Transfer resumed!" << std::endl;
            } else {
                std::cerr << "[Error] Failed to resume: " << resume_result.error().message << std::endl;
            }
            resume_requested = false;
            pause_requested = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    // Handle cancellation
    if (cancel_requested) {
        std::cout << std::endl;
        std::cout << "[Cancelling] Cancelling transfer..." << std::endl;
        auto cancel_result = handle.cancel();
        if (cancel_result.has_value()) {
            std::cout << "[Cancelled] Transfer cancelled." << std::endl;
        } else {
            std::cerr << "[Error] Failed to cancel: " << cancel_result.error().message << std::endl;
        }
    }

    // Get final transfer result
    auto status = handle.get_status();
    auto progress = handle.get_progress();

    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "       Transfer Summary" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Final status: " << to_string(status) << std::endl;
    std::cout << "Bytes transferred: " << format_bytes(progress.bytes_transferred) << std::endl;
    std::cout << "Completion: " << std::fixed << std::setprecision(1)
              << progress.completion_percentage() << "%" << std::endl;
    std::cout << std::endl;

    // Disconnect
    auto disconnect_result = client.disconnect();
    if (!disconnect_result.has_value()) {
        std::cerr << "Disconnect error: " << disconnect_result.error().message << std::endl;
    }

    return (status == transfer_status::completed) ? 0 : 1;
}
