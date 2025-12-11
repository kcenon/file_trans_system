# Getting Started Guide

A step-by-step guide to start using **file_trans_system** in your C++20 projects.

**Version:** 0.2.0
**Architecture:** Client-Server Model

---

## Table of Contents

1. [Installation](#installation)
2. [Quick Start](#quick-start)
3. [Server Examples](#server-examples)
4. [Client Examples](#client-examples)
5. [Upload & Download](#upload--download)
6. [Error Handling](#error-handling)
7. [Progress Monitoring](#progress-monitoring)
8. [Advanced Usage](#advanced-usage)
9. [Troubleshooting](#troubleshooting)

---

## Installation

### Requirements

- **Compiler**: C++20 compatible
  - GCC 11+
  - Clang 14+
  - MSVC 19.29+
- **Build System**: CMake 3.20+
- **External Dependencies**: LZ4 (v1.9.0+)

### Ecosystem Dependencies

file_trans_system is built on the **kcenon ecosystem** libraries:

| Library | Purpose |
|---------|---------|
| common_system | Result<T>, error handling, time utilities |
| thread_system | `typed_thread_pool` for pipeline parallelism |
| **network_system** | **TCP/TLS 1.3 and QUIC transport layer** |
| container_system | Bounded queues for backpressure |

> **Important**: The transport layer is implemented using **network_system**, which provides production-ready TCP and QUIC implementations. No external network library (like Boost.Asio or libuv) is required.

### CMake Integration

#### Option 1: FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
    file_trans_system
    GIT_REPOSITORY https://github.com/kcenon/file_trans_system.git
    GIT_TAG v0.2.0
)

FetchContent_MakeAvailable(file_trans_system)

target_link_libraries(your_target PRIVATE kcenon::file_transfer)
```

#### Option 2: find_package

```cmake
find_package(file_trans_system REQUIRED)
target_link_libraries(your_target PRIVATE kcenon::file_transfer)
```

#### Option 3: Subdirectory

```cmake
add_subdirectory(external/file_trans_system)
target_link_libraries(your_target PRIVATE kcenon::file_transfer)
```

### Include Header

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;
```

---

## Quick Start

### 5-Minute Example

**Server (file_server.cpp):**

```cpp
#include <kcenon/file_transfer/file_transfer.h>
#include <iostream>
#include <csignal>

using namespace kcenon::file_transfer;

std::atomic<bool> running{true};

void signal_handler(int) { running = false; }

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: file_server <port> <storage_dir>\n";
        return 1;
    }

    std::signal(SIGINT, signal_handler);

    // Create server
    auto server = file_transfer_server::builder()
        .with_storage_directory(argv[2])
        .with_max_connections(100)
        .build();

    if (!server) {
        std::cerr << "Failed to create server: " << server.error().message() << "\n";
        return 1;
    }

    // Accept all uploads and downloads
    server->on_upload_request([](const upload_request& req) {
        std::cout << "Upload request: " << req.filename
                  << " (" << req.file_size << " bytes)\n";
        return true;  // Accept
    });

    server->on_download_request([](const download_request& req) {
        std::cout << "Download request: " << req.filename << "\n";
        return true;  // Accept
    });

    // Handle completion
    server->on_upload_complete([](const transfer_result& r) {
        std::cout << "Upload complete: " << r.filename << "\n";
    });

    server->on_download_complete([](const transfer_result& r) {
        std::cout << "Download complete: " << r.filename << "\n";
    });

    // Start listening
    auto port = static_cast<uint16_t>(std::stoi(argv[1]));
    auto result = server->start(endpoint{"0.0.0.0", port});

    if (!result) {
        std::cerr << "Failed to start server: " << result.error().message() << "\n";
        return 1;
    }

    std::cout << "Server listening on port " << port << "...\n";
    std::cout << "Storage directory: " << argv[2] << "\n";

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\nShutting down...\n";
    server->stop();
    return 0;
}
```

**Client (file_client.cpp):**

```cpp
#include <kcenon/file_transfer/file_transfer.h>
#include <iostream>

using namespace kcenon::file_transfer;

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: file_client <host> <port> <command> [args...]\n";
        std::cerr << "Commands:\n";
        std::cerr << "  upload <local_file> <remote_name>\n";
        std::cerr << "  download <remote_name> <local_file>\n";
        std::cerr << "  list [pattern]\n";
        return 1;
    }

    // Create client with auto-reconnect
    auto client = file_transfer_client::builder()
        .with_auto_reconnect(true)
        .with_compression(compression_mode::adaptive)
        .build();

    if (!client) {
        std::cerr << "Failed to create client: " << client.error().message() << "\n";
        return 1;
    }

    // Progress callback
    client->on_progress([](const transfer_progress& p) {
        double pct = 100.0 * p.bytes_transferred / p.total_bytes;
        std::cout << "\rProgress: " << std::fixed << std::setprecision(1)
                  << pct << "% (" << p.transfer_rate / 1e6 << " MB/s)" << std::flush;
    });

    // Connect to server
    auto port = static_cast<uint16_t>(std::stoi(argv[2]));
    auto connect_result = client->connect(endpoint{argv[1], port});

    if (!connect_result) {
        std::cerr << "Connection failed: " << connect_result.error().message() << "\n";
        return 1;
    }

    std::cout << "Connected to " << argv[1] << ":" << port << "\n";

    std::string command = argv[3];

    if (command == "upload" && argc >= 6) {
        auto result = client->upload_file(argv[4], argv[5]);
        if (result) {
            std::cout << "\nUpload complete!\n";
        } else {
            std::cerr << "\nUpload failed: " << result.error().message() << "\n";
            return 1;
        }
    }
    else if (command == "download" && argc >= 6) {
        auto result = client->download_file(argv[4], argv[5]);
        if (result) {
            std::cout << "\nDownload complete: " << argv[5] << "\n";
        } else {
            std::cerr << "\nDownload failed: " << result.error().message() << "\n";
            return 1;
        }
    }
    else if (command == "list") {
        std::string pattern = argc >= 5 ? argv[4] : "*";
        auto result = client->list_files(pattern);
        if (result) {
            std::cout << "Files on server:\n";
            for (const auto& file : *result) {
                std::cout << "  " << file.name << " (" << file.size << " bytes)\n";
            }
        } else {
            std::cerr << "List failed: " << result.error().message() << "\n";
            return 1;
        }
    }
    else {
        std::cerr << "Unknown command: " << command << "\n";
        return 1;
    }

    client->disconnect();
    return 0;
}
```

**Usage:**

```bash
# Terminal 1 - Start server
./file_server 19000 /data/files

# Terminal 2 - Upload file
./file_client localhost 19000 upload /local/report.pdf report.pdf

# Terminal 3 - List files
./file_client localhost 19000 list

# Terminal 4 - Download file
./file_client localhost 19000 download report.pdf /local/downloaded.pdf
```

---

## Server Examples

### Example 1: Basic Server

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .build();

server->start(endpoint{"0.0.0.0", 19000});
```

### Example 2: Production Server with Limits

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_max_connections(500)
    .with_max_file_size(10ULL * 1024 * 1024 * 1024)  // 10GB
    .with_storage_quota(1ULL * 1024 * 1024 * 1024 * 1024)  // 1TB
    .with_connection_timeout(5min)
    .build();

// Custom upload validation
server->on_upload_request([](const upload_request& req) {
    // Only allow specific extensions
    auto ext = std::filesystem::path(req.filename).extension();
    if (ext != ".pdf" && ext != ".doc" && ext != ".txt") {
        return false;  // Reject
    }

    // Check file size
    if (req.file_size > 1e9) {
        return false;  // Reject > 1GB
    }

    return true;
});

// Connection events
server->on_client_connected([](const client_info& info) {
    std::cout << "Client connected: " << info.address << "\n";
});

server->on_client_disconnected([](const client_info& info, disconnect_reason reason) {
    std::cout << "Client disconnected: " << info.address
              << " (" << to_string(reason) << ")\n";
});

server->start(endpoint{"0.0.0.0", 19000});
```

### Example 3: Server with Storage Monitoring

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_storage_quota(100ULL * 1024 * 1024 * 1024)  // 100GB
    .build();

// Monitor storage usage
server->on_storage_warning([](const storage_warning& warning) {
    if (warning.percent_used > 80) {
        std::cout << "Warning: Storage " << warning.percent_used << "% full\n";
    }
});

server->on_storage_full([]() {
    std::cerr << "Error: Storage quota exceeded!\n";
});

// Periodically check storage status
std::thread monitor([&server]() {
    while (server->is_running()) {
        auto status = server->get_storage_status();
        std::cout << "Storage: " << (status.used_bytes / 1e9) << "GB / "
                  << (status.quota_bytes / 1e9) << "GB\n";
        std::this_thread::sleep_for(std::chrono::minutes(5));
    }
});
```

---

## Client Examples

### Example 1: Simple Upload

```cpp
auto client = file_transfer_client::builder().build();

client->connect(endpoint{"192.168.1.100", 19000});

auto result = client->upload_file("/local/data.zip", "data.zip");
if (result) {
    std::cout << "Upload successful!\n";
}

client->disconnect();
```

### Example 2: Upload with Compression Options

```cpp
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::enabled)
    .with_compression_level(compression_level::high_compression)
    .build();

client->connect(endpoint{"192.168.1.100", 19000});

// Override compression for specific upload
upload_options opts{
    .compression = compression_mode::disabled  // Don't compress this file
};

auto result = client->upload_file("/local/video.mp4", "video.mp4", opts);
```

### Example 3: Client with Auto-Reconnect

```cpp
auto client = file_transfer_client::builder()
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy{
        .initial_delay = std::chrono::milliseconds(500),
        .max_delay = std::chrono::seconds(30),
        .multiplier = 1.5,
        .max_attempts = 20
    })
    .build();

// Connection event handlers
client->on_connected([](const server_info& info) {
    std::cout << "Connected to: " << info.address << "\n";
});

client->on_disconnected([](disconnect_reason reason) {
    std::cout << "Disconnected: " << to_string(reason) << "\n";
});

client->on_reconnecting([](int attempt, auto delay) {
    std::cout << "Reconnecting (attempt " << attempt << ")...\n";
});

client->on_reconnected([]() {
    std::cout << "Reconnected successfully!\n";
});

client->connect(endpoint{"192.168.1.100", 19000});
```

### Example 4: Bandwidth-Limited Client

```cpp
auto client = file_transfer_client::builder()
    .with_bandwidth_limit(10 * 1024 * 1024)  // 10 MB/s
    .with_compression(compression_mode::enabled)  // Maximize effective throughput
    .build();

client->connect(endpoint{"192.168.1.100", 19000});
client->upload_file("/local/large_file.dat", "large_file.dat");
```

---

## Upload & Download

### Upload File

```cpp
// Simple upload
auto result = client->upload_file("/local/file.txt", "file.txt");

// Upload with options
upload_options opts{
    .compression = compression_mode::enabled,
    .level = compression_level::fast,
    .verify_checksum = true
};
auto result = client->upload_file("/local/file.txt", "file.txt", opts);
```

### Upload Multiple Files

```cpp
std::vector<upload_entry> files = {
    {"/local/file1.txt", "file1.txt"},
    {"/local/file2.txt", "file2.txt"},
    {"/local/file3.txt", "dir/file3.txt"}  // Server subdirectory
};

auto result = client->upload_files(files);
if (result) {
    std::cout << "Uploaded " << result->file_count << " files\n";
}
```

### Download File

```cpp
// Simple download
auto result = client->download_file("report.pdf", "/local/report.pdf");

// Download with options
download_options opts{
    .verify_checksum = true,
    .overwrite = false  // Don't overwrite if exists
};
auto result = client->download_file("report.pdf", "/local/report.pdf", opts);

if (!result && result.error().code() == error::file_already_exists) {
    std::cout << "File already exists locally\n";
}
```

### Download Multiple Files

```cpp
std::vector<download_entry> files = {
    {"file1.txt", "/local/file1.txt"},
    {"file2.txt", "/local/file2.txt"}
};

auto result = client->download_files(files);
```

### List Files

```cpp
// List all files
auto result = client->list_files();

// List with pattern
auto result = client->list_files("*.pdf");

// List with pagination
auto result = client->list_files("*", 0, 100);  // First 100 files

if (result) {
    for (const auto& file : *result) {
        std::cout << file.name << "\t"
                  << file.size << " bytes\t"
                  << file.modified_time << "\n";
    }
}
```

---

## Error Handling

### Result Pattern

All operations return `Result<T>`:

```cpp
auto result = client->upload_file(local, remote);

if (result) {
    // Success - access the value
    std::cout << "Transfer ID: " << result->id.to_string() << "\n";
} else {
    // Failure - access the error
    auto code = result.error().code();
    auto message = result.error().message();
    std::cerr << "Error " << code << ": " << message << "\n";
}
```

### Connection Errors

```cpp
auto result = client->connect(endpoint);
if (!result) {
    switch (result.error().code()) {
        case error::connection_failed:
            std::cerr << "Cannot connect to server\n";
            break;
        case error::connection_timeout:
            std::cerr << "Connection timed out\n";
            break;
        case error::server_unavailable:
            std::cerr << "Server is not available\n";
            break;
    }
}
```

### Transfer Errors

```cpp
auto result = client->upload_file(local, remote);
if (!result) {
    switch (result.error().code()) {
        case error::upload_rejected:
            std::cerr << "Server rejected the upload\n";
            break;
        case error::storage_full:
            std::cerr << "Server storage is full\n";
            break;
        case error::file_already_exists:
            std::cerr << "File already exists on server\n";
            break;
        case error::file_not_found:
            std::cerr << "Local file not found\n";
            break;
    }
}

auto result = client->download_file(remote, local);
if (!result) {
    switch (result.error().code()) {
        case error::download_rejected:
            std::cerr << "Server rejected the download\n";
            break;
        case error::file_not_found_on_server:
            std::cerr << "File not found on server\n";
            break;
    }
}
```

### Common Error Codes

| Code | Name | Description | Action |
|------|------|-------------|--------|
| -700 | `connection_failed` | Cannot connect | Check server address |
| -703 | `connection_lost` | Connection dropped | Auto-reconnect or retry |
| -704 | `reconnect_failed` | All reconnect attempts failed | Manual intervention |
| -713 | `upload_rejected` | Server rejected upload | Check server rules |
| -714 | `download_rejected` | Server rejected download | Check permissions |
| -720 | `chunk_checksum_error` | Data corruption | Automatic retry |
| -744 | `file_already_exists` | File exists on server | Use overwrite option |
| -745 | `storage_full` | Server storage full | Contact admin |
| -746 | `file_not_found_on_server` | Remote file not found | Check filename |
| -750 | `file_not_found` | Local file not found | Check path |

---

## Progress Monitoring

### Progress Callback

```cpp
client->on_progress([](const transfer_progress& p) {
    // Calculate percentage
    double percent = 100.0 * p.bytes_transferred / p.total_bytes;

    // Transfer rate in MB/s
    double rate_mbps = p.transfer_rate / (1024 * 1024);

    // Estimated time remaining
    auto remaining = p.estimated_remaining;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(remaining);

    // Compression ratio
    double ratio = p.compression_ratio;

    std::cout << std::fixed << std::setprecision(1)
              << percent << "% complete, "
              << rate_mbps << " MB/s, "
              << seconds.count() << "s remaining, "
              << ratio << ":1 compression\n";
});
```

### Progress Information

| Field | Type | Description |
|-------|------|-------------|
| `id` | `transfer_id` | Unique transfer identifier |
| `direction` | `transfer_direction` | upload or download |
| `bytes_transferred` | `uint64_t` | Raw bytes transferred |
| `bytes_on_wire` | `uint64_t` | Actual bytes sent (compressed) |
| `total_bytes` | `uint64_t` | Total file size |
| `transfer_rate` | `double` | Current speed (bytes/sec) |
| `compression_ratio` | `double` | Compression ratio |
| `elapsed_time` | `duration` | Time since start |
| `estimated_remaining` | `duration` | Estimated time to completion |

### Client Statistics

```cpp
auto stats = client->get_statistics();
std::cout << "Total uploaded: " << stats.total_uploaded_bytes << "\n";
std::cout << "Total downloaded: " << stats.total_downloaded_bytes << "\n";
std::cout << "Current upload rate: " << stats.current_upload_rate_mbps << " MB/s\n";
std::cout << "Current download rate: " << stats.current_download_rate_mbps << " MB/s\n";
```

### Server Statistics

```cpp
auto stats = server->get_statistics();
std::cout << "Active connections: " << stats.active_connections << "\n";
std::cout << "Total uploaded: " << stats.total_uploaded_bytes << "\n";
std::cout << "Total downloaded: " << stats.total_downloaded_bytes << "\n";
std::cout << "Upload throughput: " << stats.upload_throughput_mbps << " MB/s\n";
std::cout << "Download throughput: " << stats.download_throughput_mbps << " MB/s\n";
```

---

## Advanced Usage

### Transfer Control

```cpp
// Start upload
auto handle = client->upload_file(local, remote);
auto transfer_id = handle->id;

// Pause transfer
client->pause(transfer_id);

// Resume transfer
client->resume(transfer_id);

// Cancel transfer
client->cancel(transfer_id);
```

### Transfer Resume After Disconnect

```cpp
// Transfers automatically resume after reconnection
client->on_reconnected([&client]() {
    // Active transfers continue from where they left off
    auto active = client->get_active_transfers();
    for (const auto& transfer : active) {
        std::cout << "Resuming: " << transfer.filename
                  << " from " << transfer.bytes_transferred << " bytes\n";
    }
});
```

### Pipeline Statistics

```cpp
auto pipeline_stats = client->get_pipeline_stats();

// Find bottleneck
auto bottleneck = pipeline_stats.bottleneck_stage();
std::cout << "Bottleneck: " << stage_name(bottleneck) << "\n";

// Stage throughput
std::cout << "IO Read: " << pipeline_stats.io_read_stats.throughput_mbps() << " MB/s\n";
std::cout << "Compression: " << pipeline_stats.compression_stats.throughput_mbps() << " MB/s\n";
std::cout << "Network: " << pipeline_stats.network_stats.throughput_mbps() << " MB/s\n";
```

---

## Troubleshooting

### Common Issues

#### "Connection refused" Error

**Cause**: Server not running or wrong address/port.

**Solution**:
```bash
# Check if server is listening
netstat -an | grep 19000

# Ensure server starts before client
./file_server 19000 /data/files
./file_client localhost 19000 list
```

#### "Upload rejected" Error

**Cause**: Server rejected the upload request.

**Solution**: Check server validation callbacks:
```cpp
// Server side - check what's being rejected
server->on_upload_request([](const upload_request& req) {
    std::cout << "Upload request: " << req.filename
              << " (" << req.file_size << " bytes)\n";

    // Temporarily accept all to debug
    return true;
});
```

#### Slow Transfer Speed

**Cause**: Bottleneck in pipeline or network.

**Solution**:
```cpp
// Check where the bottleneck is
auto stats = client->get_pipeline_stats();
std::cout << "Bottleneck: " << stage_name(stats.bottleneck_stage()) << "\n";

// If compression bottleneck, add workers
pipeline_config config = pipeline_config::auto_detect();
config.compression_workers = 8;

// If network bottleneck, check bandwidth/latency
// If IO bottleneck, consider faster storage
```

#### Frequent Disconnections

**Cause**: Network instability or server timeout.

**Solution**:
```cpp
// Enable aggressive reconnection
auto client = file_transfer_client::builder()
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy::aggressive())
    .build();

// Or adjust server timeout
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_connection_timeout(10min)  // Longer timeout
    .build();
```

#### High Memory Usage

**Cause**: Queue sizes too large.

**Solution**:
```cpp
pipeline_config config{
    .read_queue_size = 4,
    .compress_queue_size = 8,
    .send_queue_size = 16,
    .recv_queue_size = 16
};
// Memory: ~14MB vs default ~64MB
```

### Debug Logging

Enable verbose logging:

```cpp
#include <kcenon/logger/logger.h>

// Set log level to debug
logger::set_level(log_level::debug);

// Now file_trans_system will output detailed logs
```

### Performance Tuning Guide

| Scenario | Recommended Settings |
|----------|---------------------|
| **Text files** | `compression_mode::enabled`, `compression_level::fast` |
| **Video/Images** | `compression_mode::disabled` |
| **Mixed workload** | `compression_mode::adaptive` |
| **Unreliable network** | Smaller chunks (64KB), aggressive reconnect |
| **High bandwidth** | Larger chunks (512KB), more workers |
| **Low memory** | Smaller queue sizes |
| **Many clients** | Tune server thread pool |

---

## Next Steps

- Read the [API Reference](reference/api-reference.md) for complete API documentation
- Explore [Pipeline Architecture](reference/pipeline-architecture.md) for advanced tuning
- Check [Error Codes](reference/error-codes.md) for comprehensive error handling
- Review [Protocol Specification](reference/protocol-spec.md) for wire protocol details
- See [Configuration Guide](reference/configuration.md) for all configuration options

---

*Version: 0.2.0*
*Last updated: 2025-12-11*
