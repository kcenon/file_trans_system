/**
 * @file quota_management.cpp
 * @brief Server storage quota management example
 *
 * This example demonstrates:
 * - Configuring storage quotas
 * - Monitoring storage usage
 * - Implementing quota-based access control
 * - Displaying storage statistics
 * - Warning and rejection thresholds
 */

#include <kcenon/file_transfer/server/file_transfer_server.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>

using namespace kcenon::file_transfer;

namespace {

std::atomic<bool> running{true};
std::mutex cout_mutex;

/**
 * @brief Format bytes into human-readable string
 */
auto format_bytes(uint64_t bytes) -> std::string {
    constexpr uint64_t KB = 1024;
    constexpr uint64_t MB = KB * 1024;
    constexpr uint64_t GB = MB * 1024;
    constexpr uint64_t TB = GB * 1024;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    if (bytes >= TB) {
        oss << static_cast<double>(bytes) / static_cast<double>(TB) << " TB";
    } else if (bytes >= GB) {
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
 * @brief Get current timestamp string
 */
auto timestamp() -> std::string {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%H:%M:%S");
    return oss.str();
}

/**
 * @brief Quota configuration
 */
struct quota_config {
    uint64_t total_quota;           // Total storage quota
    double warning_threshold;       // Percentage at which to warn (e.g., 80%)
    double reject_threshold;        // Percentage at which to reject uploads (e.g., 95%)
    uint64_t max_file_size;         // Maximum individual file size
};

/**
 * @brief Storage monitor state
 */
struct storage_state {
    uint64_t used = 0;
    uint64_t available = 0;
    uint64_t file_count = 0;
    double usage_percent = 0.0;
    bool warning_active = false;
    bool reject_active = false;
};

/**
 * @brief Print a visual usage bar
 */
void print_usage_bar(double percentage, int width = 40) {
    int filled = static_cast<int>(percentage / 100.0 * width);

    std::cout << "[";
    for (int i = 0; i < width; ++i) {
        if (i < filled) {
            if (percentage >= 95) {
                std::cout << "!";  // Critical
            } else if (percentage >= 80) {
                std::cout << "#";  // Warning
            } else {
                std::cout << "=";  // Normal
            }
        } else {
            std::cout << " ";
        }
    }
    std::cout << "] " << std::fixed << std::setprecision(1) << percentage << "%";
}

}  // namespace

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nShutdown signal received..." << std::endl;
        running = false;
    }
}

void print_usage(const char* program) {
    std::cout << "Quota Management Example - File Transfer System" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << program << " [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p, --port <port>           Listen port (default: 8080)" << std::endl;
    std::cout << "  -d, --dir <directory>       Storage directory (default: ./quota_storage)" << std::endl;
    std::cout << "  --quota <size>              Total storage quota (e.g., 100M, 1G, 10G)" << std::endl;
    std::cout << "  --warn-at <percent>         Warning threshold percentage (default: 80)" << std::endl;
    std::cout << "  --reject-at <percent>       Rejection threshold percentage (default: 95)" << std::endl;
    std::cout << "  --max-file <size>           Maximum file size (e.g., 10M, 100M)" << std::endl;
    std::cout << "  --monitor-interval <sec>    Storage monitoring interval (default: 5)" << std::endl;
    std::cout << "  --help                      Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << " --quota 1G --warn-at 75 --reject-at 90" << std::endl;
    std::cout << "  " << program << " --quota 500M --max-file 50M" << std::endl;
    std::cout << "  " << program << " --dir /data/storage --quota 10G" << std::endl;
}

auto parse_size(const std::string& size_str) -> uint64_t {
    size_t pos = 0;
    double value = std::stod(size_str, &pos);

    if (pos < size_str.size()) {
        char suffix = static_cast<char>(std::toupper(size_str[pos]));
        switch (suffix) {
            case 'K': return static_cast<uint64_t>(value * 1024);
            case 'M': return static_cast<uint64_t>(value * 1024 * 1024);
            case 'G': return static_cast<uint64_t>(value * 1024 * 1024 * 1024);
            case 'T': return static_cast<uint64_t>(value * 1024 * 1024 * 1024 * 1024);
            default: break;
        }
    }
    return static_cast<uint64_t>(value);
}

int main(int argc, char* argv[]) {
    // Default configuration
    uint16_t port = 8080;
    std::string storage_dir = "./quota_storage";
    quota_config quota;
    quota.total_quota = 1ULL * 1024 * 1024 * 1024;  // 1GB default
    quota.warning_threshold = 80.0;
    quota.reject_threshold = 95.0;
    quota.max_file_size = 100ULL * 1024 * 1024;  // 100MB default
    int monitor_interval = 5;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-p" || arg == "--port") {
            if (++i >= argc) {
                std::cerr << "Error: --port requires an argument" << std::endl;
                return 1;
            }
            port = static_cast<uint16_t>(std::stoi(argv[i]));
        } else if (arg == "-d" || arg == "--dir") {
            if (++i >= argc) {
                std::cerr << "Error: --dir requires an argument" << std::endl;
                return 1;
            }
            storage_dir = argv[i];
        } else if (arg == "--quota") {
            if (++i >= argc) {
                std::cerr << "Error: --quota requires an argument" << std::endl;
                return 1;
            }
            quota.total_quota = parse_size(argv[i]);
        } else if (arg == "--warn-at") {
            if (++i >= argc) {
                std::cerr << "Error: --warn-at requires an argument" << std::endl;
                return 1;
            }
            quota.warning_threshold = std::stod(argv[i]);
        } else if (arg == "--reject-at") {
            if (++i >= argc) {
                std::cerr << "Error: --reject-at requires an argument" << std::endl;
                return 1;
            }
            quota.reject_threshold = std::stod(argv[i]);
        } else if (arg == "--max-file") {
            if (++i >= argc) {
                std::cerr << "Error: --max-file requires an argument" << std::endl;
                return 1;
            }
            quota.max_file_size = parse_size(argv[i]);
        } else if (arg == "--monitor-interval") {
            if (++i >= argc) {
                std::cerr << "Error: --monitor-interval requires an argument" << std::endl;
                return 1;
            }
            monitor_interval = std::stoi(argv[i]);
        }
    }

    std::cout << "========================================" << std::endl;
    std::cout << "    Quota Management Example" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Port: " << port << std::endl;
    std::cout << "  Storage directory: " << storage_dir << std::endl;
    std::cout << std::endl;
    std::cout << "Quota Settings:" << std::endl;
    std::cout << "  Total quota: " << format_bytes(quota.total_quota) << std::endl;
    std::cout << "  Warning threshold: " << std::fixed << std::setprecision(1)
              << quota.warning_threshold << "%" << std::endl;
    std::cout << "  Rejection threshold: " << std::fixed << std::setprecision(1)
              << quota.reject_threshold << "%" << std::endl;
    std::cout << "  Max file size: " << format_bytes(quota.max_file_size) << std::endl;
    std::cout << "  Monitor interval: " << monitor_interval << "s" << std::endl;
    std::cout << std::endl;

    // Create storage directory
    std::filesystem::create_directories(storage_dir);

    // Set up signal handler
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Build server with quota settings
    std::cout << "[Setup] Creating server with quota configuration..." << std::endl;
    auto server_result = file_transfer_server::builder()
        .with_storage_directory(storage_dir)
        .with_max_connections(50)
        .with_max_file_size(quota.max_file_size)
        .with_storage_quota(quota.total_quota)
        .with_chunk_size(256 * 1024)
        .build();

    if (!server_result.has_value()) {
        std::cerr << "[Error] Failed to create server: "
                  << server_result.error().message << std::endl;
        return 1;
    }

    auto& server = server_result.value();

    // Storage state tracking
    storage_state state;
    std::atomic<uint32_t> rejected_by_quota{0};
    std::atomic<uint32_t> rejected_by_size{0};
    std::atomic<uint32_t> total_uploads{0};
    std::atomic<uint32_t> successful_uploads{0};

    // Upload request callback with quota checking
    server.on_upload_request([&](const upload_request& req) -> bool {
        total_uploads++;

        // Get current storage state
        auto storage = server.get_storage_stats();
        double current_usage = storage.usage_percent();

        // Check if file size exceeds limit
        if (req.file_size > quota.max_file_size) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "\n[" << timestamp() << "] [REJECT-SIZE] "
                      << req.filename << " (" << format_bytes(req.file_size)
                      << " > " << format_bytes(quota.max_file_size) << ")" << std::endl;
            rejected_by_size++;
            return false;
        }

        // Check if upload would exceed quota
        if (storage.used_size + req.file_size > quota.total_quota) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "\n[" << timestamp() << "] [REJECT-QUOTA] "
                      << req.filename << " - Would exceed quota ("
                      << format_bytes(storage.used_size + req.file_size) << " > "
                      << format_bytes(quota.total_quota) << ")" << std::endl;
            rejected_by_quota++;
            return false;
        }

        // Check rejection threshold
        if (current_usage >= quota.reject_threshold) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "\n[" << timestamp() << "] [REJECT-THRESHOLD] "
                      << req.filename << " - Storage at "
                      << std::fixed << std::setprecision(1) << current_usage
                      << "% (threshold: " << quota.reject_threshold << "%)" << std::endl;
            rejected_by_quota++;
            return false;
        }

        // Warning if near threshold
        if (current_usage >= quota.warning_threshold && !state.warning_active) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "\n[" << timestamp() << "] [WARNING] Storage usage at "
                      << std::fixed << std::setprecision(1) << current_usage
                      << "% - approaching limit" << std::endl;
        }

        // Accept upload
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "\n[" << timestamp() << "] [ACCEPT] "
                      << req.filename << " (" << format_bytes(req.file_size) << ")" << std::endl;
        }

        return true;
    });

    // Transfer complete callback
    server.on_transfer_complete([&](const transfer_result& result) {
        if (result.success) {
            successful_uploads++;
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "\n[" << timestamp() << "] [COMPLETE] "
                      << result.filename << " (" << format_bytes(result.bytes_transferred)
                      << ")" << std::endl;
        }
    });

    // Client connection callbacks
    server.on_client_connected([](const client_info& info) {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "\n[" << timestamp() << "] [CONNECT] Client "
                  << info.id.value << " from " << info.address << std::endl;
    });

    server.on_client_disconnected([](const client_info& info) {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "\n[" << timestamp() << "] [DISCONNECT] Client "
                  << info.id.value << std::endl;
    });

    // Start server
    std::cout << "[Setup] Starting server on port " << port << "..." << std::endl;
    auto start_result = server.start(endpoint{port});
    if (!start_result.has_value()) {
        std::cerr << "[Error] Failed to start server: "
                  << start_result.error().message << std::endl;
        return 1;
    }

    std::cout << "[Setup] Server started successfully!" << std::endl;
    std::cout << std::endl;
    std::cout << "Press Ctrl+C to stop the server." << std::endl;
    std::cout << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Storage Monitor (updating every " << monitor_interval << "s)" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << std::endl;

    // Main monitoring loop
    while (running && server.is_running()) {
        // Get current storage statistics
        auto storage = server.get_storage_stats();
        auto server_stats = server.get_statistics();

        state.used = storage.used_size;
        state.available = storage.available_size;
        state.file_count = storage.file_count;
        state.usage_percent = storage.usage_percent();
        state.warning_active = state.usage_percent >= quota.warning_threshold;
        state.reject_active = state.usage_percent >= quota.reject_threshold;

        // Clear and print storage dashboard
        {
            std::lock_guard<std::mutex> lock(cout_mutex);

            // Move cursor up and clear (simple approach - print new section)
            std::cout << "\r";

            // Storage bar
            std::cout << "Storage: ";
            print_usage_bar(state.usage_percent);
            std::cout << std::endl;

            // Detailed stats
            std::cout << "  Used:      " << std::setw(12) << format_bytes(state.used)
                      << " / " << format_bytes(quota.total_quota) << std::endl;
            std::cout << "  Available: " << std::setw(12) << format_bytes(state.available) << std::endl;
            std::cout << "  Files:     " << std::setw(12) << state.file_count << std::endl;
            std::cout << std::endl;

            // Status indicators
            std::cout << "Status: ";
            if (state.reject_active) {
                std::cout << "[CRITICAL - Rejecting uploads]";
            } else if (state.warning_active) {
                std::cout << "[WARNING - Near capacity]";
            } else {
                std::cout << "[OK - Accepting uploads]";
            }
            std::cout << std::endl;

            // Upload statistics
            std::cout << std::endl;
            std::cout << "Upload Statistics:" << std::endl;
            std::cout << "  Total requests:    " << std::setw(6) << total_uploads << std::endl;
            std::cout << "  Successful:        " << std::setw(6) << successful_uploads << std::endl;
            std::cout << "  Rejected (quota):  " << std::setw(6) << rejected_by_quota << std::endl;
            std::cout << "  Rejected (size):   " << std::setw(6) << rejected_by_size << std::endl;

            if (total_uploads > 0) {
                double acceptance_rate = 100.0 * static_cast<double>(successful_uploads) /
                                         static_cast<double>(total_uploads.load());
                std::cout << "  Acceptance rate:   " << std::setw(5)
                          << std::fixed << std::setprecision(1) << acceptance_rate << "%" << std::endl;
            }

            // Connection info
            std::cout << std::endl;
            std::cout << "Server Status:" << std::endl;
            std::cout << "  Active connections: " << server_stats.active_connections << std::endl;
            std::cout << "  Active transfers:   " << server_stats.active_transfers << std::endl;

            std::cout << std::endl;
            std::cout << std::string(60, '-') << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds{monitor_interval});
    }

    std::cout << std::endl;

    // Stop server
    std::cout << "[Shutdown] Stopping server..." << std::endl;
    auto stop_result = server.stop();
    if (!stop_result.has_value()) {
        std::cerr << "[Error] Error during shutdown: "
                  << stop_result.error().message << std::endl;
    }

    // Print final summary
    auto final_storage = server.get_storage_stats();
    auto final_stats = server.get_statistics();

    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "       Final Summary" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Storage:" << std::endl;
    std::cout << "  Final usage: " << format_bytes(final_storage.used_size)
              << " / " << format_bytes(quota.total_quota)
              << " (" << std::fixed << std::setprecision(1)
              << final_storage.usage_percent() << "%)" << std::endl;
    std::cout << "  Files stored: " << final_storage.file_count << std::endl;
    std::cout << std::endl;
    std::cout << "Quota Management:" << std::endl;
    std::cout << "  Total upload requests: " << total_uploads << std::endl;
    std::cout << "  Successful uploads: " << successful_uploads << std::endl;
    std::cout << "  Rejected by quota: " << rejected_by_quota << std::endl;
    std::cout << "  Rejected by file size: " << rejected_by_size << std::endl;
    std::cout << std::endl;
    std::cout << "Data Transfer:" << std::endl;
    std::cout << "  Total received: " << format_bytes(final_stats.total_bytes_received) << std::endl;
    std::cout << "  Total sent: " << format_bytes(final_stats.total_bytes_sent) << std::endl;
    std::cout << std::endl;

    std::cout << "[Shutdown] Server stopped." << std::endl;

    return 0;
}
