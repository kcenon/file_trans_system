# Getting Started Guide

A step-by-step guide to start using **file_trans_system** in your C++20 projects.

## Table of Contents

1. [Installation](#installation)
2. [Quick Start](#quick-start)
3. [Basic Examples](#basic-examples)
4. [Configuration Options](#configuration-options)
5. [Error Handling](#error-handling)
6. [Progress Monitoring](#progress-monitoring)
7. [Advanced Usage](#advanced-usage)
8. [Troubleshooting](#troubleshooting)

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
    GIT_TAG v1.0.0
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

**Sender (send_file.cpp):**

```cpp
#include <kcenon/file_transfer/file_transfer.h>
#include <iostream>

using namespace kcenon::file_transfer;

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: send_file <file> <host> <port>\n";
        return 1;
    }

    // Create sender
    auto sender = file_sender::builder().build();
    if (!sender) {
        std::cerr << "Failed to create sender: " << sender.error().message() << "\n";
        return 1;
    }

    // Show progress
    sender->on_progress([](const transfer_progress& p) {
        double pct = 100.0 * p.bytes_transferred / p.total_bytes;
        std::cout << "\rProgress: " << std::fixed << std::setprecision(1)
                  << pct << "% (" << p.transfer_rate / 1e6 << " MB/s)" << std::flush;
    });

    // Send file
    auto result = sender->send_file(
        argv[1],
        endpoint{argv[2], static_cast<uint16_t>(std::stoi(argv[3]))}
    );

    if (result) {
        std::cout << "\nTransfer complete!\n";
        return 0;
    } else {
        std::cerr << "\nTransfer failed: " << result.error().message() << "\n";
        return 1;
    }
}
```

**Receiver (receive_file.cpp):**

```cpp
#include <kcenon/file_transfer/file_transfer.h>
#include <iostream>
#include <csignal>

using namespace kcenon::file_transfer;

std::atomic<bool> running{true};

void signal_handler(int) { running = false; }

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: receive_file <port> <output_dir>\n";
        return 1;
    }

    std::signal(SIGINT, signal_handler);

    // Create receiver
    auto receiver = file_receiver::builder()
        .with_output_directory(argv[2])
        .build();

    if (!receiver) {
        std::cerr << "Failed to create receiver: " << receiver.error().message() << "\n";
        return 1;
    }

    // Accept all transfers
    receiver->on_transfer_request([](const transfer_request&) {
        return true;
    });

    // Handle completion
    receiver->on_complete([](const transfer_result& r) {
        if (r.verified) {
            std::cout << "Received: " << r.output_path << "\n";
        } else {
            std::cerr << "Failed: " << r.error->message << "\n";
        }
    });

    // Start listening
    auto port = static_cast<uint16_t>(std::stoi(argv[1]));
    receiver->start(endpoint{"0.0.0.0", port});

    std::cout << "Listening on port " << port << "...\n";

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    receiver->stop();
    return 0;
}
```

**Usage:**

```bash
# Terminal 1 - Start receiver
./receive_file 19000 /downloads

# Terminal 2 - Send file
./send_file /path/to/file.zip 127.0.0.1 19000
```

---

## Basic Examples

### Example 1: Send with Compression

```cpp
auto sender = file_sender::builder()
    .with_compression(compression_mode::enabled)  // Always compress
    .with_compression_level(compression_level::fast)  // Fast compression
    .build();

auto result = sender->send_file(
    "/path/to/logs.txt",
    endpoint{"192.168.1.100", 19000}
);
```

### Example 2: Send Large Files with Bandwidth Limit

```cpp
auto sender = file_sender::builder()
    .with_chunk_size(512 * 1024)  // 512KB chunks
    .with_bandwidth_limit(10 * 1024 * 1024)  // 10 MB/s limit
    .build();

auto result = sender->send_file(
    "/path/to/large_video.mp4",
    endpoint{"192.168.1.100", 19000}
);
```

### Example 3: Send Multiple Files

```cpp
std::vector<std::filesystem::path> files = {
    "/path/to/file1.txt",
    "/path/to/file2.txt",
    "/path/to/file3.txt"
};

auto sender = file_sender::builder()
    .with_compression(compression_mode::adaptive)
    .build();

auto result = sender->send_files(
    files,
    endpoint{"192.168.1.100", 19000}
);

if (result) {
    std::cout << "Batch transfer ID: " << result->id.to_string() << "\n";
    std::cout << "Files transferred: " << result->file_count << "\n";
}
```

### Example 4: Receive with Size Limit

```cpp
auto receiver = file_receiver::builder()
    .with_output_directory("/downloads")
    .build();

// Only accept files under 1GB
receiver->on_transfer_request([](const transfer_request& req) {
    for (const auto& file : req.files) {
        if (file.file_size > 1ULL * 1024 * 1024 * 1024) {
            return false;  // Reject
        }
    }
    return true;  // Accept
});

receiver->start(endpoint{"0.0.0.0", 19000});
```

### Example 5: Receive with File Type Filter

```cpp
receiver->on_transfer_request([](const transfer_request& req) {
    static const std::set<std::string> allowed_extensions = {
        ".txt", ".pdf", ".doc", ".docx", ".xls", ".xlsx"
    };

    for (const auto& file : req.files) {
        std::filesystem::path p(file.filename);
        if (allowed_extensions.find(p.extension()) == allowed_extensions.end()) {
            return false;  // Reject unknown file types
        }
    }
    return true;
});
```

---

## Configuration Options

### Sender Configuration

| Option | Method | Default | Description |
|--------|--------|---------|-------------|
| Compression Mode | `with_compression()` | `adaptive` | disabled, enabled, adaptive |
| Compression Level | `with_compression_level()` | `fast` | fast, high_compression |
| Chunk Size | `with_chunk_size()` | 256KB | 64KB - 1MB |
| Bandwidth Limit | `with_bandwidth_limit()` | 0 (unlimited) | bytes/second |
| Transport | `with_transport()` | `tcp` | tcp, quic (Phase 2) |

```cpp
auto sender = file_sender::builder()
    .with_compression(compression_mode::adaptive)
    .with_compression_level(compression_level::fast)
    .with_chunk_size(256 * 1024)
    .with_bandwidth_limit(0)
    .with_transport(transport_type::tcp)
    .build();
```

### Receiver Configuration

| Option | Method | Default | Description |
|--------|--------|---------|-------------|
| Output Directory | `with_output_directory()` | Current dir | Where to save files |
| Bandwidth Limit | `with_bandwidth_limit()` | 0 (unlimited) | bytes/second |
| Transport | `with_transport()` | `tcp` | tcp, quic (Phase 2) |

```cpp
auto receiver = file_receiver::builder()
    .with_output_directory("/downloads")
    .with_bandwidth_limit(50 * 1024 * 1024)  // 50 MB/s
    .build();
```

### Pipeline Configuration

For advanced tuning:

```cpp
pipeline_config config{
    .io_read_workers = 2,
    .chunk_workers = 2,
    .compression_workers = 4,
    .network_workers = 2,
    .io_write_workers = 2,

    .read_queue_size = 16,
    .compress_queue_size = 32,
    .send_queue_size = 64
};

auto sender = file_sender::builder()
    .with_pipeline_config(config)
    .build();
```

Or use auto-detection:

```cpp
auto sender = file_sender::builder()
    .with_pipeline_config(pipeline_config::auto_detect())
    .build();
```

---

## Error Handling

### Result Pattern

All operations return `Result<T>`:

```cpp
auto result = sender->send_file(path, endpoint);

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

### Retryable Errors

```cpp
auto result = sender->send_file(path, endpoint);

if (!result) {
    if (error::is_retryable(result.error().code())) {
        // Network errors - can retry
        std::cout << "Retrying...\n";
        result = sender->send_file(path, endpoint);
    } else {
        // Permanent error - don't retry
        std::cerr << "Fatal error: " << result.error().message() << "\n";
    }
}
```

### Common Error Codes

| Code | Name | Description | Action |
|------|------|-------------|--------|
| -700 | `transfer_init_failed` | Connection failed | Check network, retry |
| -702 | `transfer_timeout` | Operation timed out | Retry or resume |
| -703 | `transfer_rejected` | Receiver rejected | Check request |
| -720 | `chunk_checksum_error` | Data corruption | Automatic retry |
| -723 | `file_hash_mismatch` | File verification failed | Re-transfer |
| -743 | `file_not_found` | Source file missing | Check path |
| -744 | `disk_full` | No disk space | Free space |

### Error Categories

```cpp
auto code = result.error().code();

if (error::is_transfer_error(code)) {
    // Network or connection issues
} else if (error::is_chunk_error(code)) {
    // Data integrity issues
} else if (error::is_file_error(code)) {
    // File system issues
} else if (error::is_resume_error(code)) {
    // Resume operation issues
} else if (error::is_compression_error(code)) {
    // Compression/decompression issues
} else if (error::is_config_error(code)) {
    // Configuration issues
}
```

---

## Progress Monitoring

### Progress Callback

```cpp
sender->on_progress([](const transfer_progress& p) {
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
| `bytes_transferred` | `uint64_t` | Raw bytes transferred |
| `bytes_on_wire` | `uint64_t` | Actual bytes sent (compressed) |
| `total_bytes` | `uint64_t` | Total file size |
| `transfer_rate` | `double` | Current speed (bytes/sec) |
| `effective_rate` | `double` | Effective rate with compression |
| `compression_ratio` | `double` | Compression ratio |
| `elapsed_time` | `duration` | Time since start |
| `estimated_remaining` | `duration` | Estimated time to completion |
| `state` | `transfer_state_enum` | Current state |

### Transfer Statistics

```cpp
// Get aggregate statistics
auto stats = manager->get_statistics();
std::cout << "Total transferred: " << stats.total_bytes_transferred << "\n";
std::cout << "Active transfers: " << stats.active_transfer_count << "\n";
std::cout << "Average rate: " << stats.average_transfer_rate / 1e6 << " MB/s\n";

// Get compression statistics
auto comp_stats = manager->get_compression_stats();
std::cout << "Compression ratio: " << comp_stats.compression_ratio() << ":1\n";
std::cout << "Compression speed: " << comp_stats.compression_speed_mbps() << " MB/s\n";
std::cout << "Chunks compressed: " << comp_stats.chunks_compressed << "\n";
std::cout << "Chunks skipped: " << comp_stats.chunks_skipped << "\n";
```

---

## Advanced Usage

### Transfer Control

```cpp
// Start transfer
auto handle = sender->send_file(path, endpoint);
auto transfer_id = handle->id;

// Pause transfer
sender->pause(transfer_id);

// Resume transfer
sender->resume(transfer_id);

// Cancel transfer
sender->cancel(transfer_id);
```

### Transfer Resume After Disconnect

```cpp
// Original transfer
auto handle = sender->send_file(path, endpoint);
auto transfer_id = handle->id;

// ... connection lost ...

// Resume transfer (automatically uses checkpoint)
auto resume_result = sender->resume(transfer_id);
if (resume_result) {
    std::cout << "Resumed from " << resume_result->bytes_already_transferred
              << " bytes\n";
}
```

### Custom Transfer Options

```cpp
transfer_options opts{
    .compression = compression_mode::enabled,
    .level = compression_level::high_compression,
    .chunk_size = 512 * 1024,
    .verify_checksum = true,
    .bandwidth_limit = 10 * 1024 * 1024,
    .priority = 1  // Higher priority
};

auto result = sender->send_file(path, endpoint, opts);
```

### Pipeline Statistics

```cpp
auto pipeline_stats = sender->get_pipeline_stats();

// Find bottleneck
auto bottleneck = pipeline_stats.bottleneck_stage();
std::cout << "Bottleneck: " << stage_name(bottleneck) << "\n";

// Stage throughput
std::cout << "IO Read: " << pipeline_stats.io_read_stats.throughput_mbps() << " MB/s\n";
std::cout << "Compression: " << pipeline_stats.compression_stats.throughput_mbps() << " MB/s\n";
std::cout << "Network: " << pipeline_stats.network_stats.throughput_mbps() << " MB/s\n";

// Queue depths
auto depths = sender->get_queue_depths();
std::cout << "Read queue: " << depths.read_queue << "/" << depths.read_queue_max << "\n";
std::cout << "Send queue: " << depths.send_queue << "/" << depths.send_queue_max << "\n";
```

---

## Troubleshooting

### Common Issues

#### "Connection refused" Error

**Cause**: Receiver not running or wrong port.

**Solution**:
```bash
# Check if receiver is listening
netstat -an | grep 19000

# Ensure receiver starts before sender
./receive_file 19000 /downloads &
./send_file file.txt localhost 19000
```

#### Slow Transfer Speed

**Cause**: Bottleneck in pipeline or network.

**Solution**:
```cpp
// Check where the bottleneck is
auto stats = sender->get_pipeline_stats();
std::cout << "Bottleneck: " << stage_name(stats.bottleneck_stage()) << "\n";

// If compression bottleneck, add workers
pipeline_config config = pipeline_config::auto_detect();
config.compression_workers = 8;

// If network bottleneck, check bandwidth
// If IO bottleneck, consider faster storage
```

#### "File hash mismatch" Error

**Cause**: File changed during transfer or data corruption.

**Solution**:
```cpp
// Don't modify source file during transfer
// Or disable verification for streaming use cases
transfer_options opts{
    .verify_checksum = false  // Disable if source may change
};
```

#### High Memory Usage

**Cause**: Queue sizes too large.

**Solution**:
```cpp
pipeline_config config{
    .read_queue_size = 4,    // Reduce from default 16
    .compress_queue_size = 8, // Reduce from default 32
    .send_queue_size = 16    // Reduce from default 64
};
// Memory: ~7MB vs default ~32MB
```

### Debug Logging

Enable verbose logging:

```cpp
#include <kcenon/logger/logger.h>

// Set log level to debug
logger::set_level(log_level::debug);

// Now file_trans_system will output detailed logs
```

### Performance Tuning

| Scenario | Recommended Settings |
|----------|---------------------|
| **Text files** | `compression_mode::enabled`, `compression_level::fast` |
| **Video/Images** | `compression_mode::disabled` |
| **Mixed workload** | `compression_mode::adaptive` |
| **Low bandwidth** | Larger chunk size, `high_compression` level |
| **High bandwidth** | Smaller chunk size, more workers |
| **Low memory** | Smaller queue sizes |

---

## Next Steps

- Read the [API Reference](reference/api-reference.md) for complete API documentation
- Explore [Pipeline Architecture](reference/pipeline-architecture.md) for advanced tuning
- Check [Error Codes](reference/error-codes.md) for comprehensive error handling
- Review [Protocol Specification](reference/protocol-spec.md) for wire protocol details

---

*Last updated: 2025-12-11*
