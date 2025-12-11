# Reference Documentation

Detailed technical reference documentation index for **file_trans_system**.

**Version**: 2.0.0
**Last Updated**: 2025-12-11

## Document Index

### Core References

| Document | Description |
|----------|-------------|
| [API Reference](api-reference.md) | Complete API documentation - classes, methods, types |
| [Quick Reference](quick-reference.md) | Quick reference card for common operations |
| [Dependency Requirements](dependencies.md) | **Mandatory** dependency systems and integration guide |

### Architecture References

| Document | Description |
|----------|-------------|
| [Pipeline Architecture](pipeline-architecture.md) | Multi-stage pipeline design details |
| [Protocol Specification](protocol-spec.md) | Wire protocol and message formats |

### Configuration & Tuning

| Document | Description |
|----------|-------------|
| [Configuration Guide](configuration.md) | Complete reference for all configuration options |
| [LZ4 Compression Guide](lz4-compression.md) | Compression modes, levels, and tuning |

### Error Handling

| Document | Description |
|----------|-------------|
| [Error Codes](error-codes.md) | Complete error code reference with resolutions |

---

## Quick Links

### Server Quick Start

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

// Create server
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_max_connections(100)
    .with_max_file_size(10ULL * 1024 * 1024 * 1024)  // 10GB
    .build();

// Validate uploads
server->on_upload_request([](const upload_request& req) {
    return req.file_size < 5ULL * 1024 * 1024 * 1024;  // Accept < 5GB
});

// Start server
server->start(endpoint{"0.0.0.0", 19000});
```

→ Learn more in [Getting Started](../getting-started.md)

### Client Quick Start

```cpp
// Create client
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::adaptive)
    .with_auto_reconnect(true)
    .build();

// Connect to server
client->connect(endpoint{"192.168.1.100", 19000});

// Upload
auto upload = client->upload_file("/local/data.zip", "data.zip");

// Download
auto download = client->download_file("report.pdf", "/local/report.pdf");

// List files
auto files = client->list_files();
```

### Mandatory Dependencies

| System | Required |
|--------|----------|
| common_system | **Yes** |
| thread_system | **Yes** |
| logger_system | **Yes** |
| container_system | **Yes** |
| network_system | **Yes** |

→ See [Dependency Requirements](dependencies.md) for integration guide

### API Overview

| Class | Purpose |
|-------|---------|
| `file_transfer_server` | File storage and client management |
| `file_transfer_client` | Server connection and file upload/download |

→ See [API Reference](api-reference.md) for detailed API

### Key Concepts

| Concept | Description |
|---------|-------------|
| **Chunk** | Fixed-size segment of a file (default: 256KB) |
| **Pipeline** | Multi-stage parallel processing architecture |
| **Adaptive Compression** | Automatic detection of compressible data |
| **Resume** | Continue interrupted transfers from checkpoint |
| **Auto-Reconnect** | Automatic recovery on connection loss |

### Error Ranges

| Range | Category |
|-------|----------|
| -700 ~ -709 | Connection errors |
| -710 ~ -719 | Transfer errors |
| -720 ~ -739 | Chunk errors |
| -740 ~ -749 | Storage errors |
| -750 ~ -759 | File I/O errors |
| -760 ~ -779 | Resume errors |
| -780 ~ -789 | Compression errors |
| -790 ~ -799 | Configuration errors |

→ See [Error Codes](error-codes.md) for details

### Performance Targets

| Metric | Target |
|--------|--------|
| LAN Throughput | >= 500 MB/s |
| WAN Throughput | >= 100 MB/s |
| LZ4 Compression | >= 400 MB/s |
| LZ4 Decompression | >= 1.5 GB/s |
| Memory Baseline | < 50 MB |
| Concurrent Clients | >= 100 |

---

## Document Versions

| Document | Version | Last Updated |
|----------|---------|--------------|
| API Reference | 2.0.0 | 2025-12-11 |
| Pipeline Architecture | 2.0.0 | 2025-12-11 |
| Protocol Specification | 2.0.0 | 2025-12-11 |
| Configuration Guide | 2.0.0 | 2025-12-11 |
| LZ4 Compression Guide | 2.0.0 | 2025-12-11 |
| Error Codes | 2.0.0 | 2025-12-11 |
| Quick Reference | 2.0.0 | 2025-12-11 |
| Dependency Requirements | 2.0.0 | 2025-12-11 |

---

*Last Updated: 2025-12-11*
*Version: 2.0.0*
