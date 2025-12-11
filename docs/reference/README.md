# Reference Documentation

This directory contains reference documentation for the **file_trans_system** library.

## Document Index

| Document | Description |
|----------|-------------|
| [API Reference](api-reference.md) | Complete API documentation for all public classes and methods |
| [Error Codes](error-codes.md) | Comprehensive list of error codes with descriptions |
| [Protocol Specification](protocol-spec.md) | Wire protocol format and message types |
| [Configuration Guide](configuration.md) | Configuration options and tuning parameters |
| [Pipeline Architecture](pipeline-architecture.md) | Multi-stage pipeline design and optimization |
| [LZ4 Compression](lz4-compression.md) | Compression features, modes, and performance |
| [Quick Reference](quick-reference.md) | Quick reference card for common operations |

## Quick Links

### Getting Started
```cpp
#include <kcenon/file_transfer/file_transfer.h>

// Send a file
auto sender = file_sender::builder()
    .with_compression(compression_mode::adaptive)
    .build();

auto result = sender->send_file("large_file.dat", endpoint{"192.168.1.100", 19000});
```

### Key Concepts

| Concept | Description |
|---------|-------------|
| **Chunk** | Fixed-size segment of a file (default: 256KB) |
| **Pipeline** | Multi-stage parallel processing architecture |
| **Adaptive Compression** | Automatic detection of compressible data |
| **Resume** | Continue interrupted transfers from checkpoint |

### Performance Targets

| Metric | Target |
|--------|--------|
| LAN Throughput | >= 500 MB/s |
| LZ4 Compression | >= 400 MB/s |
| LZ4 Decompression | >= 1.5 GB/s |
| Memory Baseline | < 50 MB |

---

*Last updated: 2025-12-11*
