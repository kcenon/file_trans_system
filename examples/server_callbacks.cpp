/**
 * @file server_callbacks.cpp
 * @brief Server callback handling example
 *
 * This example demonstrates:
 * - Validating upload and download requests
 * - Monitoring client connections
 * - Implementing access control patterns
 * - Logging server events
 * - Transfer progress monitoring
 */

#include <kcenon/file_transfer/server/file_transfer_server.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

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
 * @brief Get current timestamp string
 */
auto timestamp() -> std::string {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

/**
 * @brief Thread-safe logging
 */
void log(const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << "[" << timestamp() << "] [" << level << "] " << message << std::endl;
}

/**
 * @brief Access control configuration
 */
struct access_config {
    std::set<std::string> allowed_extensions{".txt", ".bin", ".dat", ".log", ".csv", ".json"};
    std::set<std::string> blocked_extensions{".exe", ".sh", ".bat", ".dll", ".so"};
    uint64_t max_file_size = 100ULL * 1024 * 1024;  // 100MB default
    std::set<std::string> blocked_clients;
    bool allow_uploads = true;
    bool allow_downloads = true;
};

/**
 * @brief Client session information
 */
struct client_session {
    client_id id;
    std::string address;
    uint16_t port;
    std::chrono::steady_clock::time_point connected_at;
    uint64_t bytes_uploaded = 0;
    uint64_t bytes_downloaded = 0;
    uint32_t files_uploaded = 0;
    uint32_t files_downloaded = 0;
};

/**
 * @brief Server event statistics
 */
struct event_stats {
    std::atomic<uint32_t> connections{0};
    std::atomic<uint32_t> disconnections{0};
    std::atomic<uint32_t> upload_requests{0};
    std::atomic<uint32_t> upload_rejections{0};
    std::atomic<uint32_t> download_requests{0};
    std::atomic<uint32_t> download_rejections{0};
    std::atomic<uint32_t> completed_uploads{0};
    std::atomic<uint32_t> completed_downloads{0};
    std::atomic<uint32_t> failed_transfers{0};
};

/**
 * @brief Get file extension in lowercase
 */
auto get_extension(const std::string& filename) -> std::string {
    auto pos = filename.rfind('.');
    if (pos == std::string::npos) {
        return "";
    }
    std::string ext = filename.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

}  // namespace

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nShutdown signal received..." << std::endl;
        running = false;
    }
}

void print_usage(const char* program) {
    std::cout << "Server Callbacks Example - File Transfer System" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << program << " [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p, --port <port>           Listen port (default: 8080)" << std::endl;
    std::cout << "  -d, --dir <directory>       Storage directory (default: ./server_storage)" << std::endl;
    std::cout << "  --max-size <bytes>          Maximum file size (e.g., 100M, 1G)" << std::endl;
    std::cout << "  --allow-ext <list>          Comma-separated allowed extensions" << std::endl;
    std::cout << "  --block-ext <list>          Comma-separated blocked extensions" << std::endl;
    std::cout << "  --no-uploads                Disable upload acceptance" << std::endl;
    std::cout << "  --no-downloads              Disable download acceptance" << std::endl;
    std::cout << "  --verbose                   Enable verbose logging" << std::endl;
    std::cout << "  --help                      Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << " --port 9000 --dir /data/files" << std::endl;
    std::cout << "  " << program << " --max-size 50M --allow-ext .txt,.csv,.json" << std::endl;
    std::cout << "  " << program << " --block-ext .exe,.sh --verbose" << std::endl;
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
            default: break;
        }
    }
    return static_cast<uint64_t>(value);
}

auto parse_extensions(const std::string& list) -> std::set<std::string> {
    std::set<std::string> result;
    std::istringstream iss(list);
    std::string ext;

    while (std::getline(iss, ext, ',')) {
        // Trim whitespace
        ext.erase(0, ext.find_first_not_of(" \t"));
        ext.erase(ext.find_last_not_of(" \t") + 1);

        // Add dot if missing
        if (!ext.empty() && ext[0] != '.') {
            ext = "." + ext;
        }

        // Convert to lowercase
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (!ext.empty()) {
            result.insert(ext);
        }
    }

    return result;
}

int main(int argc, char* argv[]) {
    // Default configuration
    uint16_t port = 8080;
    std::string storage_dir = "./server_storage";
    access_config access;
    bool verbose = false;

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
        } else if (arg == "--max-size") {
            if (++i >= argc) {
                std::cerr << "Error: --max-size requires an argument" << std::endl;
                return 1;
            }
            access.max_file_size = parse_size(argv[i]);
        } else if (arg == "--allow-ext") {
            if (++i >= argc) {
                std::cerr << "Error: --allow-ext requires an argument" << std::endl;
                return 1;
            }
            access.allowed_extensions = parse_extensions(argv[i]);
        } else if (arg == "--block-ext") {
            if (++i >= argc) {
                std::cerr << "Error: --block-ext requires an argument" << std::endl;
                return 1;
            }
            access.blocked_extensions = parse_extensions(argv[i]);
        } else if (arg == "--no-uploads") {
            access.allow_uploads = false;
        } else if (arg == "--no-downloads") {
            access.allow_downloads = false;
        } else if (arg == "--verbose") {
            verbose = true;
        }
    }

    std::cout << "========================================" << std::endl;
    std::cout << "    Server Callbacks Example" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Port: " << port << std::endl;
    std::cout << "  Storage: " << storage_dir << std::endl;
    std::cout << "  Max file size: " << format_bytes(access.max_file_size) << std::endl;
    std::cout << "  Uploads: " << (access.allow_uploads ? "enabled" : "disabled") << std::endl;
    std::cout << "  Downloads: " << (access.allow_downloads ? "enabled" : "disabled") << std::endl;
    std::cout << "  Allowed extensions: ";
    for (const auto& ext : access.allowed_extensions) {
        std::cout << ext << " ";
    }
    std::cout << std::endl;
    std::cout << "  Blocked extensions: ";
    for (const auto& ext : access.blocked_extensions) {
        std::cout << ext << " ";
    }
    std::cout << std::endl;
    std::cout << std::endl;

    // Create storage directory
    std::filesystem::create_directories(storage_dir);

    // Set up signal handler
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Build server
    log("INFO", "Creating server...");
    auto server_result = file_transfer_server::builder()
        .with_storage_directory(storage_dir)
        .with_max_connections(100)
        .with_max_file_size(access.max_file_size)
        .with_storage_quota(10ULL * 1024 * 1024 * 1024)  // 10GB
        .with_chunk_size(256 * 1024)
        .build();

    if (!server_result.has_value()) {
        log("ERROR", "Failed to create server: " + server_result.error().message);
        return 1;
    }

    auto& server = server_result.value();

    // Track sessions and statistics
    std::unordered_map<uint64_t, client_session> sessions;
    std::mutex sessions_mutex;
    event_stats stats;

    // =========================================================================
    // Upload Request Validation Callback
    // =========================================================================
    server.on_upload_request([&](const upload_request& req) -> bool {
        stats.upload_requests++;

        std::ostringstream oss;
        oss << "Upload request: file=" << req.filename
            << ", size=" << format_bytes(req.file_size)
            << ", client=" << req.client.value;

        if (verbose) {
            log("REQUEST", oss.str());
        }

        // Check if uploads are enabled
        if (!access.allow_uploads) {
            log("REJECT", "Uploads disabled: " + req.filename);
            stats.upload_rejections++;
            return false;
        }

        // Check file size
        if (req.file_size > access.max_file_size) {
            log("REJECT", "File too large: " + req.filename +
                " (" + format_bytes(req.file_size) + " > " +
                format_bytes(access.max_file_size) + ")");
            stats.upload_rejections++;
            return false;
        }

        // Check file extension
        std::string ext = get_extension(req.filename);

        // Check blocked extensions first
        if (access.blocked_extensions.count(ext)) {
            log("REJECT", "Blocked extension: " + req.filename);
            stats.upload_rejections++;
            return false;
        }

        // Check allowed extensions (if list is not empty)
        if (!access.allowed_extensions.empty() &&
            !access.allowed_extensions.count(ext)) {
            log("REJECT", "Extension not allowed: " + req.filename);
            stats.upload_rejections++;
            return false;
        }

        // Check for suspicious filenames
        if (req.filename.find("..") != std::string::npos) {
            log("REJECT", "Suspicious filename (path traversal): " + req.filename);
            stats.upload_rejections++;
            return false;
        }

        log("ACCEPT", "Upload: " + req.filename + " (" + format_bytes(req.file_size) + ")");
        return true;
    });

    // =========================================================================
    // Download Request Validation Callback
    // =========================================================================
    server.on_download_request([&](const download_request& req) -> bool {
        stats.download_requests++;

        if (verbose) {
            std::ostringstream oss;
            oss << "Download request: file=" << req.filename
                << ", client=" << req.client.value;
            log("REQUEST", oss.str());
        }

        // Check if downloads are enabled
        if (!access.allow_downloads) {
            log("REJECT", "Downloads disabled: " + req.filename);
            stats.download_rejections++;
            return false;
        }

        // Check for suspicious filenames
        if (req.filename.find("..") != std::string::npos) {
            log("REJECT", "Suspicious filename (path traversal): " + req.filename);
            stats.download_rejections++;
            return false;
        }

        log("ACCEPT", "Download: " + req.filename);
        return true;
    });

    // =========================================================================
    // Client Connected Callback
    // =========================================================================
    server.on_client_connected([&](const client_info& info) {
        stats.connections++;

        {
            std::lock_guard<std::mutex> lock(sessions_mutex);
            client_session session;
            session.id = info.id;
            session.address = info.address;
            session.port = info.port;
            session.connected_at = std::chrono::steady_clock::now();
            sessions[info.id.value] = session;
        }

        std::ostringstream oss;
        oss << "Client connected: id=" << info.id.value
            << ", address=" << info.address << ":" << info.port;
        log("CONNECT", oss.str());
    });

    // =========================================================================
    // Client Disconnected Callback
    // =========================================================================
    server.on_client_disconnected([&](const client_info& info) {
        stats.disconnections++;

        client_session session;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex);
            if (auto it = sessions.find(info.id.value); it != sessions.end()) {
                session = it->second;
                sessions.erase(it);
            }
        }

        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - session.connected_at);

        std::ostringstream oss;
        oss << "Client disconnected: id=" << info.id.value
            << ", duration=" << duration.count() << "s"
            << ", up=" << session.files_uploaded << " files/" << format_bytes(session.bytes_uploaded)
            << ", down=" << session.files_downloaded << " files/" << format_bytes(session.bytes_downloaded);
        log("DISCONNECT", oss.str());
    });

    // =========================================================================
    // Transfer Complete Callback
    // =========================================================================
    server.on_transfer_complete([&](const transfer_result& result) {
        if (result.success) {
            // Note: We can't easily distinguish upload vs download here without more context
            stats.completed_uploads++;  // Simplified for example

            std::ostringstream oss;
            oss << "Transfer complete: file=" << result.filename
                << ", bytes=" << format_bytes(result.bytes_transferred);
            log("COMPLETE", oss.str());
        } else {
            stats.failed_transfers++;

            std::ostringstream oss;
            oss << "Transfer failed: file=" << result.filename
                << ", error=" << result.error_message;
            log("FAILED", oss.str());
        }
    });

    // =========================================================================
    // Transfer Progress Callback
    // =========================================================================
    if (verbose) {
        server.on_progress([](const transfer_progress& progress) {
            if (static_cast<int>(progress.percentage) % 25 == 0) {  // Log at 0%, 25%, 50%, 75%, 100%
                std::ostringstream oss;
                oss << "Progress: file=" << progress.filename
                    << ", " << std::fixed << std::setprecision(1) << progress.percentage << "%"
                    << " (" << format_bytes(progress.bytes_transferred) << "/"
                    << format_bytes(progress.total_bytes) << ")";
                log("PROGRESS", oss.str());
            }
        });
    }

    // Start server
    log("INFO", "Starting server on port " + std::to_string(port) + "...");
    auto start_result = server.start(endpoint{port});
    if (!start_result.has_value()) {
        log("ERROR", "Failed to start server: " + start_result.error().message);
        return 1;
    }

    log("INFO", "Server started successfully!");
    std::cout << std::endl;
    std::cout << "Press Ctrl+C to stop the server." << std::endl;
    std::cout << std::endl;

    // Main monitoring loop
    while (running && server.is_running()) {
        // Print periodic status
        auto server_stats = server.get_statistics();
        auto storage = server.get_storage_stats();

        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "\r[" << timestamp() << "] "
                      << "Clients: " << server_stats.active_connections
                      << " | Transfers: " << server_stats.active_transfers
                      << " | Requests: " << stats.upload_requests << "/" << stats.download_requests
                      << " | Rejections: " << stats.upload_rejections << "/" << stats.download_rejections
                      << " | Storage: " << format_bytes(storage.used_size)
                      << "     " << std::flush;
        }

        std::this_thread::sleep_for(std::chrono::seconds{2});
    }

    std::cout << std::endl;
    std::cout << std::endl;

    // Stop server
    log("INFO", "Stopping server...");
    auto stop_result = server.stop();
    if (!stop_result.has_value()) {
        log("ERROR", "Error during shutdown: " + stop_result.error().message);
    }

    // Print final statistics
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "       Server Statistics" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Connections:" << std::endl;
    std::cout << "  Total connections: " << stats.connections << std::endl;
    std::cout << "  Total disconnections: " << stats.disconnections << std::endl;
    std::cout << std::endl;
    std::cout << "Upload Requests:" << std::endl;
    std::cout << "  Total: " << stats.upload_requests << std::endl;
    std::cout << "  Rejected: " << stats.upload_rejections << std::endl;
    std::cout << "  Acceptance rate: ";
    if (stats.upload_requests > 0) {
        double rate = 100.0 * (1.0 - static_cast<double>(stats.upload_rejections) /
                       static_cast<double>(stats.upload_requests.load()));
        std::cout << std::fixed << std::setprecision(1) << rate << "%" << std::endl;
    } else {
        std::cout << "N/A" << std::endl;
    }
    std::cout << std::endl;
    std::cout << "Download Requests:" << std::endl;
    std::cout << "  Total: " << stats.download_requests << std::endl;
    std::cout << "  Rejected: " << stats.download_rejections << std::endl;
    std::cout << std::endl;
    std::cout << "Transfers:" << std::endl;
    std::cout << "  Completed: " << stats.completed_uploads << std::endl;
    std::cout << "  Failed: " << stats.failed_transfers << std::endl;
    std::cout << std::endl;

    auto final_stats = server.get_statistics();
    std::cout << "Data Transfer:" << std::endl;
    std::cout << "  Total received: " << format_bytes(final_stats.total_bytes_received) << std::endl;
    std::cout << "  Total sent: " << format_bytes(final_stats.total_bytes_sent) << std::endl;
    std::cout << std::endl;

    log("INFO", "Server stopped.");

    return 0;
}
