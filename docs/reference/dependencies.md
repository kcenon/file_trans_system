# Dependency Requirements Guide

Mandatory dependency requirements for the **file_trans_system** library.

**Version:** 2.0.0
**Architecture:** Client-Server Model

---

## Table of Contents

1. [Overview](#overview)
2. [Mandatory Dependencies](#mandatory-dependencies)
3. [common_system](#common_system)
4. [thread_system](#thread_system)
5. [logger_system](#logger_system)
6. [container_system](#container_system)
7. [network_system](#network_system)
8. [Integration Guide](#integration-guide)
9. [Build Configuration](#build-configuration)

---

## Overview

### Why These Dependencies Are Required

The **file_trans_system** is built on top of a modular infrastructure that provides essential services for high-performance file transfer. **All five core systems are mandatory** and must be used together.

```
┌─────────────────────────────────────────────────────────────────┐
│                      file_trans_system                          │
│                 (file_transfer_server/client)                   │
└───────────────────────────┬─────────────────────────────────────┘
                            │
        ┌───────────────────┼───────────────────┐
        │                   │                   │
        ▼                   ▼                   ▼
┌───────────────┐   ┌───────────────┐   ┌───────────────┐
│ network_system│   │ thread_system │   │ logger_system │
└───────┬───────┘   └───────┬───────┘   └───────┬───────┘
        │                   │                   │
        └───────────────────┼───────────────────┘
                            │
                            ▼
                   ┌───────────────┐
                   │container_system│
                   └───────┬───────┘
                            │
                            ▼
                   ┌───────────────┐
                   │ common_system │
                   └───────────────┘
```

### Dependency Hierarchy

| Level | System | Depends On |
|-------|--------|------------|
| 0 | common_system | (none - foundation) |
| 1 | container_system | common_system |
| 2 | thread_system | common_system, container_system |
| 2 | logger_system | common_system, container_system |
| 3 | network_system | common_system, container_system, thread_system |
| 4 | **file_trans_system** | **ALL of the above** |

---

## Mandatory Dependencies

### Summary Table

| System | Purpose | Required | Substitutable |
|--------|---------|----------|---------------|
| **common_system** | Base utilities, error handling, types | **Yes** | No |
| **thread_system** | Thread pool, concurrency, async operations | **Yes** | No |
| **logger_system** | Structured logging, diagnostics | **Yes** | No |
| **container_system** | High-performance containers, buffers | **Yes** | No |
| **network_system** | TCP/QUIC transport, connection management | **Yes** | No |

> **IMPORTANT**: These dependencies are **NOT optional**. The file_trans_system will not compile or function correctly without all five systems properly integrated.

### Why No Substitution?

1. **Tight Integration**: Internal APIs depend on specific container types and threading primitives
2. **Performance Guarantees**: The 5-stage pipeline requires specific thread pool behavior
3. **Error Propagation**: Error codes and result types are defined in common_system
4. **Memory Management**: Buffer pooling is handled by container_system
5. **Protocol Implementation**: Network framing is built on network_system primitives

---

## common_system

### Purpose

Provides foundational utilities used throughout all other systems.

### Required Components

| Component | Usage in file_trans_system |
|-----------|---------------------------|
| `result<T, E>` | All API return types |
| `error_code` | Error handling and propagation |
| `expected<T>` | Optional value handling |
| `span<T>` | Zero-copy data views |
| `byte_buffer` | Raw byte manipulation |
| `uuid` | Transfer IDs, client IDs |
| `endpoint` | Server/client addresses |
| `duration`, `time_point` | Timeouts, scheduling |

### Code Example

```cpp
#include <kcenon/common/result.h>
#include <kcenon/common/error_code.h>
#include <kcenon/common/endpoint.h>

using namespace kcenon::common;

// All file_transfer APIs return result<T, error>
result<transfer_id, error> upload_result = client->upload_file(path, name);

if (!upload_result) {
    error_code code = upload_result.error().code();
    std::string msg = upload_result.error().message();
    // Handle error
}

// Endpoints for connection
endpoint server_addr{"192.168.1.100", 19000};
client->connect(server_addr);
```

### Why Mandatory

- **result<T, E>**: Every public API uses this for error handling
- **error_code**: All error codes (-700 to -799) are defined here
- **endpoint**: Required for server binding and client connection
- **uuid**: Used for transfer tracking and client identification

---

## thread_system

### Purpose

Provides high-performance thread pools and async primitives for the 5-stage pipeline.

### Required Components

| Component | Usage in file_trans_system |
|-----------|---------------------------|
| `thread_pool` | Worker threads for each pipeline stage |
| `priority_thread_pool` | Priority-based task scheduling |
| `async_task<T>` | Async operation results |
| `semaphore` | Backpressure control |
| `mutex`, `condition_variable` | Synchronization |
| `atomic_queue` | Lock-free inter-stage queues |

### Code Example

```cpp
#include <kcenon/thread/thread_pool.h>
#include <kcenon/thread/async_task.h>

using namespace kcenon::thread;

// Pipeline configuration uses thread_pool internally
pipeline_config config{
    .io_read_workers = 2,       // Threads for file I/O
    .compression_workers = 4,   // Threads for LZ4 compression
    .network_workers = 2,       // Threads for network send/recv
    .send_queue_size = 64,      // Bounded queue size
    .recv_queue_size = 64
};

// Async operations return async_task<T>
async_task<transfer_result> task = client->upload_file_async(path, name);
transfer_result result = task.get();  // Block until complete
```

### Why Mandatory

- **5-Stage Pipeline**: Each stage runs on dedicated thread pool workers
- **Backpressure**: Bounded queues prevent memory exhaustion
- **Concurrent Transfers**: Multiple simultaneous uploads/downloads
- **Async API**: Non-blocking operations use async_task

---

## logger_system

### Purpose

Provides structured logging for debugging, monitoring, and diagnostics.

### Required Components

| Component | Usage in file_trans_system |
|-----------|---------------------------|
| `logger` | Main logging interface |
| `log_level` | DEBUG, INFO, WARN, ERROR, FATAL |
| `log_context` | Structured context (transfer_id, client_id) |
| `log_sink` | Output destinations (file, console, network) |
| `performance_logger` | Throughput and latency metrics |

### Code Example

```cpp
#include <kcenon/logger/logger.h>

using namespace kcenon::logger;

// Logger is automatically used by file_trans_system
// Configure log level for debugging
logger::set_level(log_level::debug);

// Add file sink for persistent logs
logger::add_sink(file_sink{"/var/log/file_transfer.log"});

// Structured logging with context
logger::info("Transfer started", {
    {"transfer_id", transfer.id.to_string()},
    {"file_name", transfer.filename},
    {"file_size", transfer.size}
});
```

### Log Format

```
2025-12-11T10:30:45.123Z [INFO] [file_trans] Transfer started
  transfer_id=550e8400-e29b-41d4-a716-446655440000
  file_name=data.zip
  file_size=1073741824
  client_addr=192.168.1.50:45678
```

### Why Mandatory

- **Debugging**: Internal operations log extensively
- **Error Diagnosis**: Error context includes log correlation IDs
- **Performance Monitoring**: Throughput metrics are logged
- **Audit Trail**: Upload/download operations are logged

---

## container_system

### Purpose

Provides high-performance containers optimized for file transfer operations.

### Required Components

| Component | Usage in file_trans_system |
|-----------|---------------------------|
| `chunk_buffer` | Fixed-size chunk storage |
| `buffer_pool` | Pre-allocated buffer management |
| `ring_buffer` | Lock-free producer-consumer queues |
| `flat_map` | Fast key-value lookups |
| `small_vector` | Stack-allocated small vectors |
| `object_pool` | Reusable object allocation |

### Code Example

```cpp
#include <kcenon/container/chunk_buffer.h>
#include <kcenon/container/buffer_pool.h>

using namespace kcenon::container;

// Chunk buffers are used internally for file data
// Buffer pool reduces allocation overhead
buffer_pool<chunk_buffer> pool{
    .chunk_size = 256 * 1024,  // 256KB chunks
    .initial_count = 128,       // Pre-allocate 128 buffers
    .max_count = 1024          // Maximum 1024 buffers
};

// Get buffer from pool (zero-allocation in hot path)
auto buffer = pool.acquire();
// ... use buffer ...
pool.release(std::move(buffer));
```

### Why Mandatory

- **Zero-Copy**: chunk_buffer enables zero-copy data flow
- **Memory Efficiency**: buffer_pool eliminates allocation overhead
- **Lock-Free Queues**: ring_buffer for inter-stage communication
- **Fast Lookups**: flat_map for client and transfer management

---

## network_system

### Purpose

Provides transport layer abstraction for TCP and QUIC protocols.

### Required Components

| Component | Usage in file_trans_system |
|-----------|---------------------------|
| `tcp_server` | Server-side TCP listener |
| `tcp_client` | Client-side TCP connection |
| `connection` | Abstract connection interface |
| `frame_codec` | Message framing and parsing |
| `connection_pool` | Connection reuse |
| `ssl_context` | TLS encryption (optional) |

### Code Example

```cpp
#include <kcenon/network/tcp_server.h>
#include <kcenon/network/tcp_client.h>

using namespace kcenon::network;

// file_transfer_server uses tcp_server internally
// file_transfer_client uses tcp_client internally

// Transport selection
auto client = file_transfer_client::builder()
    .with_transport(transport_type::tcp)   // Default
    // .with_transport(transport_type::quic)  // Phase 2
    .build();

// Connection events are propagated from network_system
client->on_disconnected([](disconnect_reason reason) {
    // reason is from network_system
});
```

### Protocol Stack

```
┌─────────────────────────────────────┐
│        file_trans_system            │
│     (application protocol)          │
├─────────────────────────────────────┤
│          frame_codec                │
│     (length-prefixed framing)       │
├─────────────────────────────────────┤
│     tcp_client / tcp_server         │
│         (transport)                 │
├─────────────────────────────────────┤
│          TCP / QUIC                 │
│     (operating system)              │
└─────────────────────────────────────┘
```

### Why Mandatory

- **Transport Abstraction**: Unified interface for TCP/QUIC
- **Connection Management**: Reconnection, pooling, keepalive
- **Frame Codec**: Wire protocol implementation
- **Event System**: Connection state change notifications

---

## Integration Guide

### Include Order

Include headers in dependency order to avoid compilation issues:

```cpp
// 1. common_system (foundation)
#include <kcenon/common/result.h>
#include <kcenon/common/error_code.h>
#include <kcenon/common/endpoint.h>

// 2. container_system
#include <kcenon/container/buffer_pool.h>

// 3. thread_system
#include <kcenon/thread/thread_pool.h>

// 4. logger_system
#include <kcenon/logger/logger.h>

// 5. network_system
#include <kcenon/network/tcp_client.h>

// 6. file_trans_system (top level)
#include <kcenon/file_transfer/file_transfer.h>
```

### Initialization Order

Systems must be initialized in dependency order:

```cpp
int main() {
    // 1. Initialize logger first (for diagnostics)
    kcenon::logger::initialize({
        .level = log_level::info,
        .sinks = {console_sink{}, file_sink{"/var/log/app.log"}}
    });

    // 2. Initialize thread pools
    kcenon::thread::initialize({
        .default_pool_size = std::thread::hardware_concurrency()
    });

    // 3. Initialize network (if using custom config)
    kcenon::network::initialize({
        .tcp_nodelay = true,
        .keep_alive = true
    });

    // 4. Now safe to use file_trans_system
    auto server = file_transfer_server::builder()
        .with_storage_directory("/data")
        .build();

    server->start(endpoint{"0.0.0.0", 19000});

    // ... application code ...

    // Shutdown in reverse order
    server->stop();
    kcenon::network::shutdown();
    kcenon::thread::shutdown();
    kcenon::logger::shutdown();
}
```

### Using the Unified Header

For convenience, use the unified header that includes all dependencies:

```cpp
#include <kcenon/file_transfer/file_transfer.h>

// This header automatically includes:
// - common_system essentials
// - thread_system (for pipeline_config)
// - logger_system (for log configuration)
// - container_system (for buffer types)
// - network_system (for transport types)

using namespace kcenon::file_transfer;
```

---

## Build Configuration

### CMake Integration

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_app)

# Find all required packages
find_package(kcenon_common REQUIRED)
find_package(kcenon_container REQUIRED)
find_package(kcenon_thread REQUIRED)
find_package(kcenon_logger REQUIRED)
find_package(kcenon_network REQUIRED)
find_package(kcenon_file_transfer REQUIRED)

add_executable(my_app main.cpp)

# Link all dependencies (order matters!)
target_link_libraries(my_app PRIVATE
    kcenon::common
    kcenon::container
    kcenon::thread
    kcenon::logger
    kcenon::network
    kcenon::file_transfer
)
```

### vcpkg Integration

```json
{
  "name": "my-app",
  "dependencies": [
    "kcenon-common",
    "kcenon-container",
    "kcenon-thread",
    "kcenon-logger",
    "kcenon-network",
    "kcenon-file-transfer"
  ]
}
```

### Conan Integration

```txt
[requires]
kcenon-common/2.0.0
kcenon-container/2.0.0
kcenon-thread/2.0.0
kcenon-logger/2.0.0
kcenon-network/2.0.0
kcenon-file-transfer/2.0.0

[generators]
cmake
```

---

## Version Compatibility

### Minimum Versions

| System | Minimum Version | Recommended |
|--------|-----------------|-------------|
| common_system | 2.0.0 | 2.0.0 |
| container_system | 2.0.0 | 2.0.0 |
| thread_system | 2.0.0 | 2.0.0 |
| logger_system | 2.0.0 | 2.0.0 |
| network_system | 2.0.0 | 2.0.0 |
| file_trans_system | 2.0.0 | 2.0.0 |

### ABI Compatibility

All systems follow semantic versioning. Within a major version:
- **Minor version bumps**: ABI compatible, new features
- **Patch version bumps**: ABI compatible, bug fixes only

> **WARNING**: Mixing different major versions of these systems is **NOT SUPPORTED** and will result in undefined behavior.

---

## Troubleshooting

### Common Build Errors

| Error | Cause | Solution |
|-------|-------|----------|
| `undefined reference to kcenon::common::*` | common_system not linked | Add `kcenon::common` to target_link_libraries |
| `cannot find <kcenon/thread/thread_pool.h>` | thread_system not found | Run `find_package(kcenon_thread REQUIRED)` |
| `ABI mismatch detected` | Version mismatch | Ensure all systems use same major version |
| `logger not initialized` | Missing initialization | Call `kcenon::logger::initialize()` first |

### Runtime Errors

| Error | Cause | Solution |
|-------|-------|----------|
| `thread_pool exhausted` | Too few workers | Increase `pipeline_config` worker counts |
| `buffer_pool exhausted` | Too few buffers | Increase `buffer_pool::max_count` |
| `connection refused` | Network not initialized | Call `kcenon::network::initialize()` |

---

## See Also

- [API Reference](api-reference.md) - Complete API documentation
- [Pipeline Architecture](pipeline-architecture.md) - 5-stage pipeline details
- [Configuration Guide](configuration.md) - All configuration options
- [Quick Reference](quick-reference.md) - Common usage patterns

---

*Last Updated: 2025-12-11*
*Version: 2.0.0*
