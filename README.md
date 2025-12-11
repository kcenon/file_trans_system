# file_trans_system

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/std/the-standard)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-1.0.0-green.svg)](https://github.com/kcenon/file_trans_system)

A high-performance, production-ready C++20 library for reliable file transfer with compression, resume capability, and multi-stage pipeline architecture.

## Key Features

- **High Performance**: ≥500 MB/s LAN throughput with multi-stage pipeline processing
- **LZ4 Compression**: Adaptive compression with ~400 MB/s speed and ~2.1:1 ratio
- **Resume Support**: Automatic checkpoint-based transfer resume on interruption
- **Multi-file Batch**: Transfer multiple files in a single session
- **Progress Tracking**: Real-time progress callbacks with statistics
- **Integrity Verification**: CRC32 per-chunk + SHA-256 per-file verification
- **Concurrent Transfers**: Support for ≥100 simultaneous transfers
- **Low Memory Footprint**: Bounded memory usage (~32MB per direction)

## Quick Start

### Basic Sender

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

int main() {
    // Create sender with adaptive compression
    auto sender = file_sender::builder()
        .with_compression(compression_mode::adaptive)
        .with_chunk_size(256 * 1024)  // 256KB chunks
        .build();

    if (!sender) {
        std::cerr << "Failed to create sender\n";
        return 1;
    }

    // Register progress callback
    sender->on_progress([](const transfer_progress& p) {
        double percent = 100.0 * p.bytes_transferred / p.total_bytes;
        std::cout << percent << "% - " << p.transfer_rate / 1e6 << " MB/s\n";
    });

    // Send file
    auto result = sender->send_file(
        "/path/to/file.dat",
        endpoint{"192.168.1.100", 19000}
    );

    if (result) {
        std::cout << "Transfer complete: " << result->id.to_string() << "\n";
    } else {
        std::cerr << "Transfer failed: " << result.error().message() << "\n";
    }
}
```

### Basic Receiver

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

int main() {
    // Create receiver
    auto receiver = file_receiver::builder()
        .with_output_directory("/downloads")
        .build();

    if (!receiver) {
        std::cerr << "Failed to create receiver\n";
        return 1;
    }

    // Accept transfers under 10GB
    receiver->on_transfer_request([](const transfer_request& req) {
        uint64_t total = 0;
        for (const auto& file : req.files) total += file.file_size;
        return total < 10ULL * 1024 * 1024 * 1024;
    });

    // Handle completion
    receiver->on_complete([](const transfer_result& result) {
        if (result.verified) {
            std::cout << "Received: " << result.output_path << "\n";
        }
    });

    // Start listening
    receiver->start(endpoint{"0.0.0.0", 19000});

    // Wait for signal...
    std::this_thread::sleep_for(std::chrono::hours(24));

    receiver->stop();
}
```

## Architecture

file_trans_system uses a **multi-stage pipeline architecture** for maximum throughput:

```
┌────────────────────────────────────────────────────────────────────────┐
│                         SENDER PIPELINE                                 │
│                                                                         │
│  File Read  ──▶  Chunk     ──▶   LZ4      ──▶  Network                 │
│   Stage         Assembly       Compress        Send                     │
│  (io_read)   (chunk_process) (compression)   (network)                 │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │              typed_thread_pool<pipeline_stage>                  │   │
│  │   [IO Workers] [Compute Workers] [Network Workers]              │   │
│  └─────────────────────────────────────────────────────────────────┘   │
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
| Concurrent Transfers | ≥ 100 |

## Configuration

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
| [LZ4 Compression](docs/reference/lz4-compression.md) | Compression details |

### Design Documents

| Document | Description |
|----------|-------------|
| [PRD](docs/PRD.md) | Product Requirements Document |
| [SRS](docs/SRS.md) | Software Requirements Specification |
| [SDS](docs/SDS.md) | Software Design Specification |
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

### CMake Integration

```cmake
find_package(file_trans_system REQUIRED)
target_link_libraries(your_target PRIVATE kcenon::file_transfer)
```

## Examples

See the [examples/](examples/) directory for:

- `simple_sender.cpp` - Basic file sending
- `simple_receiver.cpp` - Basic file receiving
- `batch_transfer.cpp` - Multi-file batch transfer
- `resume_transfer.cpp` - Transfer resume handling
- `custom_pipeline.cpp` - Pipeline configuration tuning

## Error Handling

All operations return `Result<T>` for explicit error handling:

```cpp
auto result = sender->send_file(path, endpoint);
if (!result) {
    auto code = result.error().code();
    if (error::is_retryable(code)) {
        // Retry with exponential backoff
        sender->resume(transfer_id);
    } else {
        std::cerr << "Permanent error: " << result.error().message() << "\n";
    }
}
```

Common error codes:

| Code | Name | Description |
|------|------|-------------|
| -700 | `transfer_init_failed` | Connection failed |
| -702 | `transfer_timeout` | Transfer timed out |
| -720 | `chunk_checksum_error` | Data corruption detected |
| -743 | `file_not_found` | Source file not found |

## License

This project is licensed under the BSD 3-Clause License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please read our contributing guidelines before submitting PRs.

## Roadmap

- [ ] **Phase 1**: Core TCP transfer with LZ4 compression
- [ ] **Phase 2**: QUIC transport support
- [ ] **Phase 3**: Encryption layer (AES-256-GCM)
- [ ] **Phase 4**: Cloud storage integration

---

*file_trans_system v1.0.0 | High-Performance File Transfer Library*
