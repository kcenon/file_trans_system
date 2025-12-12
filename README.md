# file_trans_system

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/std/the-standard)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-0.2.0-green.svg)](https://github.com/kcenon/file_trans_system)

A high-performance, production-ready C++20 library for reliable file transfer with Client-Server architecture, LZ4 compression, resume capability, and multi-stage pipeline processing.

## Key Features

- **Client-Server Architecture**: Central server with multiple client connections
- **Bidirectional Transfer**: Upload files to server, download files from server
- **High Performance**: ≥500 MB/s LAN throughput with multi-stage pipeline processing
- **LZ4 Compression**: Adaptive compression with ~400 MB/s speed and ~2.1:1 ratio
- **Resume Support**: Automatic checkpoint-based transfer resume on interruption
- **Auto-Reconnect**: Automatic reconnection with exponential backoff policy
- **File Management**: List, upload, and download files from server storage
- **Progress Tracking**: Real-time progress callbacks with statistics
- **Integrity Verification**: CRC32 per-chunk + SHA-256 per-file verification
- **Concurrent Transfers**: Support for ≥100 simultaneous client connections
- **Low Memory Footprint**: Bounded memory usage (~32MB per direction)

## Quick Start

### File Transfer Server

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

int main() {
    // Create server with storage directory
    auto server = file_transfer_server::builder()
        .with_storage_directory("/data/files")
        .with_max_connections(100)
        .with_max_file_size(10ULL * 1024 * 1024 * 1024)  // 10GB
        .build();

    if (!server) {
        std::cerr << "Failed to create server: " << server.error().message() << "\n";
        return 1;
    }

    // Accept uploads under 5GB
    server->on_upload_request([](const upload_request& req) {
        return req.file_size < 5ULL * 1024 * 1024 * 1024;
    });

    // Allow all downloads
    server->on_download_request([](const download_request& req) {
        return true;
    });

    // Handle upload completion
    server->on_upload_complete([](const transfer_result& result) {
        if (result.verified) {
            std::cout << "Received: " << result.filename << "\n";
        }
    });

    // Start server
    server->start(endpoint{"0.0.0.0", 19000});

    std::cout << "Server listening on port 19000...\n";
    std::this_thread::sleep_for(std::chrono::hours(24));

    server->stop();
}
```

### File Transfer Client

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

int main() {
    // Create client with auto-reconnect
    auto client = file_transfer_client::builder()
        .with_compression(compression_mode::adaptive)
        .with_auto_reconnect(true)
        .with_reconnect_policy(reconnect_policy{
            .initial_delay = std::chrono::seconds(1),
            .max_delay = std::chrono::seconds(30),
            .multiplier = 2.0,
            .max_attempts = 10
        })
        .build();

    if (!client) {
        std::cerr << "Failed to create client: " << client.error().message() << "\n";
        return 1;
    }

    // Connect to server
    auto connect_result = client->connect(endpoint{"192.168.1.100", 19000});
    if (!connect_result) {
        std::cerr << "Connection failed: " << connect_result.error().message() << "\n";
        return 1;
    }

    // Register progress callback
    client->on_progress([](const transfer_progress& p) {
        double percent = 100.0 * p.bytes_transferred / p.total_bytes;
        std::cout << p.direction << ": " << percent << "% - "
                  << p.transfer_rate / 1e6 << " MB/s\n";
    });

    // Upload file
    auto upload_result = client->upload_file("/local/data.zip", "data.zip");
    if (upload_result) {
        std::cout << "Upload complete: " << upload_result->id.to_string() << "\n";
    }

    // Download file
    auto download_result = client->download_file("report.pdf", "/local/report.pdf");
    if (download_result) {
        std::cout << "Download complete: " << download_result->output_path << "\n";
    }

    // List server files
    auto files = client->list_files();
    if (files) {
        for (const auto& file : *files) {
            std::cout << file.filename << " (" << file.file_size << " bytes)\n";
        }
    }

    client->disconnect();
}
```

## Architecture

file_trans_system uses a **Client-Server architecture** with **multi-stage pipeline processing**:

```
                    ┌─────────────────────────────────┐
                    │    file_transfer_server         │
                    │                                 │
                    │  ┌───────────────────────────┐  │
                    │  │    Storage: /data/files   │  │
                    │  └───────────────────────────┘  │
                    │                                 │
                    │  on_upload_request()            │
                    │  on_download_request()          │
                    │  on_upload_complete()           │
                    │  on_download_complete()         │
                    └────────────┬────────────────────┘
                                 │
         ┌───────────────────────┼───────────────────────┐
         │                       │                       │
         ▼                       ▼                       ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│    Client A     │    │    Client B     │    │    Client C     │
│  upload_file()  │    │ download_file() │    │  list_files()   │
│                 │    │                 │    │                 │
│  Auto-Reconnect │    │  Auto-Reconnect │    │  Auto-Reconnect │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

### Pipeline Architecture

Each transfer uses a multi-stage pipeline for maximum throughput:

```
┌────────────────────────────────────────────────────────────────────────┐
│                       UPLOAD PIPELINE (Client)                         │
│                                                                        │
│  File Read  ──▶  Chunk     ──▶   LZ4      ──▶  Network                │
│   Stage         Assembly       Compress        Send                    │
│  (io_read)   (chunk_process) (compression)   (network)                │
│                                                                        │
│  ┌──────────────────────────────────────────────────────────────────┐ │
│  │              typed_thread_pool<pipeline_stage>                   │ │
│  │   [IO Workers] [Compute Workers] [Network Workers]               │ │
│  └──────────────────────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────────────┐
│                      DOWNLOAD PIPELINE (Client)                        │
│                                                                        │
│  Network   ──▶   LZ4       ──▶  Chunk     ──▶  File Write             │
│  Receive       Decompress      Reassembly      Stage                  │
│ (network)    (compression)  (chunk_process)  (io_write)               │
└────────────────────────────────────────────────────────────────────────┘
```

Each stage runs independently with bounded queues providing backpressure, ensuring:
- **Memory Bounds**: Fixed maximum memory usage regardless of file size
- **Parallelism**: I/O and CPU operations execute concurrently
- **Throughput**: Each stage runs at its maximum speed

## Performance Targets

| Metric | Target |
|--------|--------|
| LAN Throughput (1GB single file) | ≥ 500 MB/s |
| WAN Throughput | ≥ 100 MB/s (network limited) |
| LZ4 Compression Speed | ≥ 400 MB/s |
| LZ4 Decompression Speed | ≥ 1.5 GB/s |
| Memory Baseline | < 50 MB |
| Concurrent Client Connections | ≥ 100 |

## Configuration

### Server Configuration

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")         // Required
    .with_max_connections(100)                     // Default: 100
    .with_max_file_size(10ULL * 1024 * 1024 * 1024) // Default: 10GB
    .with_storage_quota(1ULL * 1024 * 1024 * 1024 * 1024) // 1TB
    .with_pipeline_config(pipeline_config::auto_detect())
    .build();
```

### Client Configuration

```cpp
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::adaptive)   // Default
    .with_chunk_size(256 * 1024)                   // 256KB (default)
    .with_auto_reconnect(true)                     // Enable auto-reconnect
    .with_reconnect_policy(reconnect_policy{
        .initial_delay = 1s,
        .max_delay = 30s,
        .multiplier = 2.0,
        .max_attempts = 10
    })
    .with_pipeline_config(pipeline_config::auto_detect())
    .build();
```

### Compression Modes

| Mode | Description | Use Case |
|------|-------------|----------|
| `disabled` | No compression | Pre-compressed files (video, images) |
| `enabled` | Always compress | Text files, logs, documents |
| `adaptive` | Auto-detect (default) | Mixed workloads |

### Pipeline Configuration

```cpp
pipeline_config config{
    .io_read_workers = 2,       // File read parallelism
    .compression_workers = 4,    // LZ4 compression threads
    .network_workers = 2,        // Network send/receive
    .send_queue_size = 64        // Backpressure buffer
};

// Or use auto-detection
auto config = pipeline_config::auto_detect();
```

## Protocol

The library uses a custom lightweight binary protocol optimized for file transfer:

- **54 bytes** per-chunk overhead (vs HTTP ~800 bytes)
- **TLS 1.3** encryption by default
- **Efficient resume**: Bitmap-based missing chunk tracking

### Message Types

| Code | Message | Direction | Description |
|------|---------|-----------|-------------|
| 0x10 | UPLOAD_REQUEST | C→S | Upload request with metadata |
| 0x11 | UPLOAD_ACCEPT | S→C | Upload approved |
| 0x12 | UPLOAD_REJECT | S→C | Upload rejected with reason |
| 0x50 | DOWNLOAD_REQUEST | C→S | Download request for file |
| 0x51 | DOWNLOAD_ACCEPT | S→C | Download approved with metadata |
| 0x52 | DOWNLOAD_REJECT | S→C | Download rejected |
| 0x60 | LIST_REQUEST | C→S | File listing request |
| 0x61 | LIST_RESPONSE | S→C | File listing response |

## Dependencies

file_trans_system is built on top of the kcenon ecosystem libraries:

| System | Required | Purpose |
|--------|----------|---------|
| common_system | Yes | Result<T>, error handling, time utilities |
| thread_system | Yes | `typed_thread_pool<pipeline_stage>` for parallel pipeline processing |
| **network_system** | Yes | **TCP/TLS 1.3 transport layer (Phase 1), QUIC transport (Phase 2)** |
| container_system | Yes | Bounded queues for backpressure |
| LZ4 | Yes | Compression library (v1.9.0+) |
| logger_system | Optional | Structured logging |
| monitoring_system | Optional | Metrics export |

> **Note**: The transport layer is implemented using **network_system**, which provides both TCP and QUIC transports. No external transport library is required.

## Documentation

| Document | Description |
|----------|-------------|
| [Quick Reference](docs/reference/quick-reference.md) | Common operations cheat sheet |
| [API Reference](docs/reference/api-reference.md) | Complete API documentation |
| [Protocol Specification](docs/reference/protocol-spec.md) | Wire protocol details |
| [Pipeline Architecture](docs/reference/pipeline-architecture.md) | Pipeline design guide |
| [Configuration Guide](docs/reference/configuration.md) | Tuning options |
| [Error Codes](docs/reference/error-codes.md) | Error code reference |
| [Getting Started](docs/reference/getting-started.md) | Step-by-step tutorial |
| [Sequence Diagrams](docs/reference/sequence-diagrams.md) | Interaction flows |

### Design Documents

| Document | Description |
|----------|-------------|
| [PRD](docs/PRD.md) | Product Requirements Document |
| [SRS](docs/SRS.md) | Software Requirements Specification |
| [SDS](docs/SDS.md) | Software Design Specification |
| [Architecture](docs/architecture.md) | Architecture overview |
| [Verification](docs/Verification.md) | Verification plan |
| [Validation](docs/Validation.md) | Validation plan |

## Building

### Requirements

- C++20 compatible compiler:
  - GCC 11+
  - Clang 14+
  - MSVC 19.29+
- CMake 3.20+
- LZ4 library (v1.9.0+)

### Build Commands

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

### Testing

Build and run tests:

```bash
mkdir build && cd build
cmake .. -DBUILD_TESTING=ON
cmake --build . --parallel

# Run all tests
ctest --output-on-failure

# Or run specific test executables
./bin/file_trans_unit_tests          # Unit tests
./bin/file_trans_integration_tests   # Integration tests
```

#### Test Categories

| Category | Description | File |
|----------|-------------|------|
| **Unit Tests - Core** | Chunk splitter, assembler, checksum (CRC32, SHA-256) | `tests/unit/core/` |
| **Unit Tests - Compression** | LZ4 compression engine tests | `tests/unit/compression/` |
| **Unit Tests - Protocol** | Protocol types, error codes, result handling, chunk flags | `tests/unit/protocol/` |
| **Unit Tests - State** | Connection/server state, config validation, statistics | `tests/unit/state/` |
| **Integration Tests - Basic** | Server/client lifecycle, connection, basic transfers | `test_basic_scenarios.cpp` |
| **Integration Tests - Advanced** | Error handling, large files, compression, stress tests | `test_error_advanced_scenarios.cpp` |
| **Integration Tests - Concurrency** | Multi-client connections, 100-connection load test, concurrent transfers | `test_concurrency.cpp` |

### CMake Integration

```cmake
find_package(file_trans_system REQUIRED)
target_link_libraries(your_target PRIVATE kcenon::file_transfer)
```

## Examples

See the [examples/](examples/) directory for:

- `simple_server.cpp` - Basic file transfer server
- `simple_client.cpp` - Basic file transfer client
- `upload_example.cpp` - File upload with progress
- `download_example.cpp` - File download with verification
- `batch_transfer.cpp` - Multi-file batch transfer
- `resume_transfer.cpp` - Transfer resume handling
- `custom_pipeline.cpp` - Pipeline configuration tuning
- `auto_reconnect.cpp` - Auto-reconnect demonstration

## Error Handling

All operations return `Result<T>` for explicit error handling:

```cpp
// Upload with error handling
auto result = client->upload_file(path, filename);
if (!result) {
    auto code = result.error().code();
    switch (code) {
        case error::upload_rejected:
            std::cerr << "Server rejected upload\n";
            break;
        case error::storage_full:
            std::cerr << "Server storage is full\n";
            break;
        case error::transfer_timeout:
            // Retryable - can resume
            client->resume_upload(transfer_id);
            break;
        default:
            std::cerr << "Error: " << result.error().message() << "\n";
    }
}

// Download with error handling
auto download = client->download_file(filename, local_path);
if (!download) {
    if (download.error().code() == error::file_not_found_on_server) {
        std::cerr << "File not found on server\n";
    }
}
```

### Common Error Codes

| Code | Name | Description |
|------|------|-------------|
| -700 | `transfer_init_failed` | Connection failed |
| -702 | `transfer_timeout` | Transfer timed out |
| -711 | `connection_closed` | Connection closed unexpectedly |
| -712 | `upload_rejected` | Server rejected upload |
| -720 | `chunk_checksum_error` | Data corruption detected |
| -743 | `file_not_found` | Local file not found |
| -745 | `storage_full` | Server storage quota exceeded |
| -746 | `file_not_found_on_server` | Requested file not on server |
| -747 | `access_denied` | Permission denied |
| -748 | `invalid_filename` | Invalid filename |

## Auto-Reconnect

The client supports automatic reconnection with exponential backoff:

```cpp
auto client = file_transfer_client::builder()
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy{
        .initial_delay = std::chrono::seconds(1),
        .max_delay = std::chrono::seconds(30),
        .multiplier = 2.0,
        .max_attempts = 10
    })
    .build();

// Set reconnection callback
client->on_reconnect([](int attempt, const reconnect_info& info) {
    std::cout << "Reconnecting (attempt " << attempt << ")...\n";
});

// Set connection restored callback
client->on_connection_restored([]() {
    std::cout << "Connection restored!\n";
});
```

## License

This project is licensed under the BSD 3-Clause License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please read our contributing guidelines before submitting PRs.

## Roadmap

- [x] **Phase 1**: Client-Server architecture with TCP transfer and LZ4 compression
- [ ] **Phase 2**: QUIC transport support
- [ ] **Phase 3**: Encryption layer (AES-256-GCM)
- [ ] **Phase 4**: Cloud storage integration

---

*file_trans_system v0.2.0 | High-Performance File Transfer Library with Client-Server Architecture*
