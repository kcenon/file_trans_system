# Architecture Overview

This document provides a comprehensive overview of the **file_trans_system** architecture, including system layers, component relationships, and design patterns.

**Version**: 0.2.0
**Last Updated**: 2025-12-11

## Table of Contents

1. [System Overview](#system-overview)
2. [Layered Architecture](#layered-architecture)
3. [Server Components](#server-components)
4. [Client Components](#client-components)
5. [Pipeline Architecture](#pipeline-architecture)
6. [Data Flow](#data-flow)
7. [Design Patterns](#design-patterns)
8. [Threading Model](#threading-model)
9. [Memory Management](#memory-management)
10. [Security Considerations](#security-considerations)

---

## System Overview

file_trans_system is a high-performance file transfer library built on a **client-server architecture** with **multi-stage pipeline processing**. The design prioritizes:

- **Bidirectional Transfer**: Upload (client→server) and download (server→client)
- **Throughput**: Maximize data transfer rate through parallelism
- **Reliability**: Ensure data integrity with checksums and verification
- **Resumability**: Support interrupted transfer recovery
- **Scalability**: Handle 100+ concurrent client connections
- **Memory Efficiency**: Bounded memory usage regardless of file size
- **Auto-Recovery**: Automatic reconnection with exponential backoff

### High-Level Architecture

```
┌───────────────────────────────────────────────────────────────────────────────────┐
│                                 SERVER LAYER                                       │
│                                                                                    │
│  ┌─────────────────────────────────────────────────────────────────────────────┐  │
│  │                          file_transfer_server                                │  │
│  │                                                                              │  │
│  │  ┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────────┐  │  │
│  │  │   storage_manager   │  │ connection_manager  │  │  server_pipeline    │  │  │
│  │  │                     │  │                     │  │                     │  │  │
│  │  │  ├ file_index      │  │  ├ client_sessions │  │  ├ upload_pipeline  │  │  │
│  │  │  ├ quota_manager   │  │  ├ auth_handler    │  │  └ download_pipeline│  │  │
│  │  │  └ path_validator  │  │  └ request_router  │  │                     │  │  │
│  │  └─────────────────────┘  └─────────────────────┘  └─────────────────────┘  │  │
│  └─────────────────────────────────────────────────────────────────────────────┘  │
│                                        ▲                                          │
├────────────────────────────────────────┼──────────────────────────────────────────┤
│                               NETWORK LAYER                                        │
│                                        │                                          │
│                    ┌───────────────────┼───────────────────┐                      │
│                    │                   │                   │                      │
│                    ▼                   ▼                   ▼                      │
├────────────────────────────────────────────────────────────────────────────────────┤
│                                 CLIENT LAYER                                       │
│                                                                                    │
│  ┌────────────────────┐  ┌────────────────────┐  ┌────────────────────┐          │
│  │   Client A         │  │   Client B         │  │   Client C         │          │
│  │   (upload)         │  │   (download)       │  │   (list_files)     │          │
│  └────────────────────┘  └────────────────────┘  └────────────────────┘          │
│                                                                                    │
│  ┌─────────────────────────────────────────────────────────────────────────────┐  │
│  │                          file_transfer_client                                │  │
│  │                                                                              │  │
│  │  ┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────────┐  │  │
│  │  │  connection_handler │  │  client_pipeline    │  │auto_reconnect_handler│ │  │
│  │  │                     │  │                     │  │                     │  │  │
│  │  │  ├ server_endpoint │  │  ├ upload_pipeline  │  │  ├ retry_policy    │  │  │
│  │  │  └ session_state   │  │  └ download_pipeline│  │  └ backoff_calc    │  │  │
│  │  └─────────────────────┘  └─────────────────────┘  └─────────────────────┘  │  │
│  └─────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                    │
├────────────────────────────────────────────────────────────────────────────────────┤
│                              INFRASTRUCTURE LAYER                                  │
│                                                                                    │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐                  │
│  │common_system│  │thread_system│ │network_system│ │container_  │                  │
│  │            │  │            │  │            │  │   system   │                  │
│  │ Result<T>  │  │typed_thread │  │   socket   │  │  bounded   │                  │
│  │ error      │  │   _pool    │  │   buffer   │  │   queue    │                  │
│  │ time       │  │ job_queue  │  │ TLS 1.3    │  │            │                  │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘                  │
│                                                                                    │
└────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Layered Architecture

### Layer 1: API Layer

The **API Layer** provides the public interface for applications.

| Class | Role | Key Methods |
|-------|------|-------------|
| `file_transfer_server` | Central storage server | `start()`, `stop()`, `on_upload_request()`, `on_download_request()` |
| `file_transfer_client` | Connect and transfer files | `connect()`, `upload_file()`, `download_file()`, `list_files()` |
| `transfer_manager` | Manage concurrent transfers | `get_status()`, `list_transfers()`, `get_statistics()` |

**Design Principle**: Builder pattern for fluent configuration, callback-based event handling.

### Layer 2: Server Core Layer

The **Server Core Layer** manages storage, connections, and transfer orchestration.

| Component | Purpose |
|-----------|---------|
| `storage_manager` | Manages files in storage directory, enforces quotas |
| `connection_manager` | Handles client connections, routing, and session state |
| `server_pipeline` | Orchestrates upload and download processing |

**Design Principle**: Centralized resource management with concurrent access control.

### Layer 3: Client Core Layer

The **Client Core Layer** handles connection management and bidirectional transfers.

| Component | Purpose |
|-----------|---------|
| `connection_handler` | Manages server connection and session state |
| `client_pipeline` | Processes uploads and downloads |
| `auto_reconnect_handler` | Automatic reconnection with exponential backoff |

**Design Principle**: Resilient connections with automatic recovery.

### Layer 4: Service Layer

The **Service Layer** provides reusable processing components.

| Component | Purpose |
|-----------|---------|
| `chunk_splitter` | Splits files into chunks |
| `chunk_assembler` | Reassembles chunks into files |
| `chunk_compressor` | Compresses/decompresses chunks |
| `lz4_engine` | Low-level LZ4 operations |
| `adaptive_compression` | Compressibility detection |
| `checksum` | CRC32 and SHA-256 operations |

**Design Principle**: Single responsibility, stateless where possible.

### Layer 5: Transport Layer

The **Transport Layer** abstracts network communication using **network_system**.

| Component | Purpose |
|-----------|---------|
| `transport_interface` | Abstract transport API |
| `tcp_transport` | TCP + TLS 1.3 implementation |
| `quic_transport` | QUIC implementation (Phase 2) |
| `transport_factory` | Factory for transport creation |

**Design Principle**: Strategy pattern for pluggable transports.

### Layer 6: Infrastructure Layer

The **Infrastructure Layer** provides foundational utilities from the kcenon ecosystem.

| System | Components Used |
|--------|----------------|
| common_system | `Result<T>`, error codes, time utilities |
| thread_system | `typed_thread_pool<pipeline_stage>` for parallel processing |
| network_system | TCP/TLS transport, socket management, buffers |
| container_system | `bounded_queue<T>` for backpressure |

---

## Server Components

### Server Component Diagram

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                              file_transfer_server                                     │
│                                                                                       │
│  Configuration:                       Callbacks:                                      │
│  ├ storage_directory                 ├ on_upload_request()                           │
│  ├ max_connections                   ├ on_download_request()                         │
│  ├ max_file_size                     ├ on_transfer_complete()                        │
│  ├ storage_quota                     └ on_client_connected()                         │
│  └ transport_type                                                                     │
│                                                                                       │
│  Methods:                                                                             │
│  ├ start(endpoint)                                                                   │
│  ├ stop()                                                                            │
│  ├ get_statistics()                                                                  │
│  └ list_stored_files()                                                               │
└─────────────────────────────────────────────────────────────────────────────────────┘
         │
         │ contains
         ▼
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                              Core Server Components                                   │
│                                                                                       │
│  ┌─────────────────────────────────────────────────────────────────────────────┐    │
│  │                           storage_manager                                    │    │
│  │                                                                              │    │
│  │  ├ create_file(filename, size) → Result<path>                               │    │
│  │  ├ delete_file(filename) → Result<void>                                     │    │
│  │  ├ get_file_path(filename) → Result<path>                                   │    │
│  │  ├ list_files(pattern, offset, limit) → Result<vector<file_info>>           │    │
│  │  ├ validate_filename(filename) → Result<void>                               │    │
│  │  ├ can_store_file(size) → Result<void>                                      │    │
│  │  └ get_storage_stats() → storage_statistics                                 │    │
│  └─────────────────────────────────────────────────────────────────────────────┘    │
│                                                                                       │
│  ┌─────────────────────────────────────────────────────────────────────────────┐    │
│  │                          connection_manager                                  │    │
│  │                                                                              │    │
│  │  ├ accept_connection() → Result<client_session>                             │    │
│  │  ├ close_connection(session_id) → Result<void>                              │    │
│  │  ├ get_active_connections() → vector<session_info>                          │    │
│  │  ├ route_request(request) → Result<void>                                    │    │
│  │  └ broadcast(message) → Result<void>                                        │    │
│  └─────────────────────────────────────────────────────────────────────────────┘    │
│                                                                                       │
│  ┌─────────────────────────────────────────────────────────────────────────────┐    │
│  │                            server_pipeline                                   │    │
│  │                                                                              │    │
│  │  Upload Pipeline (receive from client):                                     │    │
│  │  ┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────┐                     │    │
│  │  │ Network │──▶│Decompress│──▶│ Assemble│──▶│  Write  │                     │    │
│  │  │ Receive │   │         │   │         │   │ Storage │                     │    │
│  │  └─────────┘   └─────────┘   └─────────┘   └─────────┘                     │    │
│  │                                                                              │    │
│  │  Download Pipeline (send to client):                                        │    │
│  │  ┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────┐                     │    │
│  │  │  Read   │──▶│  Chunk  │──▶│ Compress│──▶│ Network │                     │    │
│  │  │ Storage │   │         │   │         │   │  Send   │                     │    │
│  │  └─────────┘   └─────────┘   └─────────┘   └─────────┘                     │    │
│  └─────────────────────────────────────────────────────────────────────────────┘    │
│                                                                                       │
└─────────────────────────────────────────────────────────────────────────────────────┘
         │
         │ uses
         ▼
┌───────────────────┬───────────────────┬───────────────────┬───────────────────┐
│  chunk_splitter   │ chunk_compressor  │  chunk_assembler  │  tcp_transport    │
│                   │                   │                   │                   │
│  split()          │  compress()       │  process_chunk()  │  listen()         │
│  calculate_       │  decompress()     │  is_complete()    │  accept()         │
│    metadata()     │  get_statistics() │  finalize()       │  send/receive     │
└───────────────────┴───────────────────┴───────────────────┴───────────────────┘
```

### Storage Manager Details

```cpp
class storage_manager {
public:
    // File operations
    auto create_file(const std::string& filename, uint64_t size) -> Result<std::filesystem::path>;
    auto delete_file(const std::string& filename) -> Result<void>;
    auto get_file_path(const std::string& filename) -> Result<std::filesystem::path>;
    auto file_exists(const std::string& filename) -> bool;

    // Listing
    auto list_files(
        const std::string& pattern = "*",
        std::size_t offset = 0,
        std::size_t limit = 1000
    ) -> Result<std::vector<file_info>>;

    // Validation
    auto validate_filename(const std::string& filename) -> Result<void>;
    auto can_store_file(uint64_t size) -> Result<void>;

    // Statistics
    auto get_storage_stats() -> storage_statistics;

private:
    std::filesystem::path storage_directory_;
    uint64_t storage_quota_;
    uint64_t max_file_size_;
    std::unordered_map<std::string, file_metadata> file_index_;
    mutable std::shared_mutex index_mutex_;
};
```

### Connection Manager Details

```cpp
class connection_manager {
public:
    auto start_accepting(const endpoint& listen_addr) -> Result<void>;
    auto stop_accepting() -> void;

    auto accept_connection() -> Result<client_session>;
    auto close_connection(session_id id) -> Result<void>;
    auto close_all_connections() -> void;

    auto get_active_connections() -> std::vector<session_info>;
    auto get_connection_count() -> std::size_t;

    auto route_request(const client_request& request) -> Result<void>;

private:
    std::size_t max_connections_;
    std::unordered_map<session_id, client_session> sessions_;
    mutable std::shared_mutex sessions_mutex_;
    transport_factory transport_factory_;
};
```

---

## Client Components

### Client Component Diagram

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                              file_transfer_client                                     │
│                                                                                       │
│  Configuration:                       Callbacks:                                      │
│  ├ compression_mode                  ├ on_progress()                                 │
│  ├ compression_level                 ├ on_complete()                                 │
│  ├ chunk_size                        ├ on_error()                                    │
│  ├ auto_reconnect                    └ on_disconnected()                             │
│  └ reconnect_policy                                                                  │
│                                                                                       │
│  Methods:                                                                             │
│  ├ connect(endpoint)                                                                 │
│  ├ disconnect()                                                                      │
│  ├ upload_file(local_path, remote_name, options)                                    │
│  ├ download_file(remote_name, local_path, options)                                  │
│  ├ list_files(options)                                                              │
│  ├ cancel(transfer_id)                                                              │
│  ├ pause(transfer_id)                                                               │
│  └ resume(transfer_id)                                                              │
└─────────────────────────────────────────────────────────────────────────────────────┘
         │
         │ contains
         ▼
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                              Core Client Components                                   │
│                                                                                       │
│  ┌─────────────────────────────────────────────────────────────────────────────┐    │
│  │                          connection_handler                                  │    │
│  │                                                                              │    │
│  │  ├ connect(endpoint) → Result<void>                                         │    │
│  │  ├ disconnect() → void                                                      │    │
│  │  ├ is_connected() → bool                                                    │    │
│  │  ├ send_request(request) → Result<response>                                 │    │
│  │  └ get_session_state() → session_state                                      │    │
│  └─────────────────────────────────────────────────────────────────────────────┘    │
│                                                                                       │
│  ┌─────────────────────────────────────────────────────────────────────────────┐    │
│  │                       auto_reconnect_handler                                 │    │
│  │                                                                              │    │
│  │  ├ enable(policy) → void                                                    │    │
│  │  ├ disable() → void                                                         │    │
│  │  ├ on_disconnection() → void   [triggers reconnection]                      │    │
│  │  ├ get_retry_count() → uint32_t                                             │    │
│  │  └ reset_retry_count() → void                                               │    │
│  │                                                                              │    │
│  │  Reconnect Policy:                                                          │    │
│  │  ├ initial_delay: 1s                                                        │    │
│  │  ├ max_delay: 60s                                                           │    │
│  │  ├ multiplier: 2.0                                                          │    │
│  │  └ max_attempts: 10                                                         │    │
│  └─────────────────────────────────────────────────────────────────────────────┘    │
│                                                                                       │
│  ┌─────────────────────────────────────────────────────────────────────────────┐    │
│  │                            client_pipeline                                   │    │
│  │                                                                              │    │
│  │  Upload Pipeline (send to server):                                          │    │
│  │  ┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────┐                     │    │
│  │  │  Read   │──▶│  Chunk  │──▶│ Compress│──▶│ Network │                     │    │
│  │  │  Local  │   │         │   │         │   │  Send   │                     │    │
│  │  └─────────┘   └─────────┘   └─────────┘   └─────────┘                     │    │
│  │                                                                              │    │
│  │  Download Pipeline (receive from server):                                   │    │
│  │  ┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────┐                     │    │
│  │  │ Network │──▶│Decompress│──▶│ Assemble│──▶│  Write  │                     │    │
│  │  │ Receive │   │         │   │         │   │  Local  │                     │    │
│  │  └─────────┘   └─────────┘   └─────────┘   └─────────┘                     │    │
│  └─────────────────────────────────────────────────────────────────────────────┘    │
│                                                                                       │
└─────────────────────────────────────────────────────────────────────────────────────┘
         │
         │ uses
         ▼
┌───────────────────┬───────────────────┬───────────────────┬───────────────────┐
│  chunk_splitter   │ chunk_compressor  │  chunk_assembler  │  tcp_transport    │
│                   │                   │                   │                   │
│  split()          │  compress()       │  process_chunk()  │  connect()        │
│  calculate_       │  decompress()     │  is_complete()    │  send()           │
│    metadata()     │  get_statistics() │  finalize()       │  receive()        │
└───────────────────┴───────────────────┴───────────────────┴───────────────────┘
```

### Auto-Reconnect Algorithm

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        Auto-Reconnect State Machine                          │
│                                                                              │
│  ┌───────────────┐         disconnect          ┌───────────────┐            │
│  │   CONNECTED   │ ───────────────────────────▶│ DISCONNECTED  │            │
│  └───────────────┘                             └───────┬───────┘            │
│         ▲                                              │                     │
│         │                                              │ auto_reconnect      │
│         │                                              │ enabled?            │
│         │                                              ▼                     │
│         │                                      ┌───────────────┐            │
│         │                                      │  RECONNECTING │            │
│         │                                      │               │◀──┐        │
│         │                                      │ attempt: N    │   │        │
│         │                                      │ delay: Xs     │   │ failed │
│         │                                      └───────┬───────┘   │        │
│         │                                              │           │        │
│         │ success                                      │ try       │        │
│         │                                              ▼           │        │
│         │                                      ┌───────────────┐   │        │
│         └──────────────────────────────────────│   CONNECTING  │───┘        │
│                                                │               │            │
│                                                │ (actual conn) │            │
│                                                └───────────────┘            │
│                                                        │                     │
│                                                        │ max_attempts        │
│                                                        │ exceeded?           │
│                                                        ▼                     │
│                                                ┌───────────────┐            │
│                                                │    FAILED     │            │
│                                                │ (emit error)  │            │
│                                                └───────────────┘            │
│                                                                              │
│  Backoff Formula:                                                           │
│  delay = min(initial_delay × multiplier^attempt, max_delay)                 │
│                                                                              │
│  Example (default policy):                                                  │
│  Attempt 1: 1s, Attempt 2: 2s, Attempt 3: 4s, ..., Attempt 7+: 60s         │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Pipeline Architecture

### Stage Definition

```cpp
enum class pipeline_stage : uint8_t {
    io_read,        // File read operations
    chunk_process,  // Chunk assembly/disassembly
    compression,    // LZ4 compress/decompress
    network,        // Network send/receive
    io_write        // File write operations
};
```

### Upload Pipeline Flow (Client → Server)

```
CLIENT SIDE:
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  io_read    │     │chunk_process│     │ compression │     │   network   │
│   stage     │────▶│    stage    │────▶│    stage    │────▶│    stage    │
│             │     │             │     │             │     │             │
│ Read local  │     │ Create      │     │ LZ4 compress│     │ Send to     │
│ file chunks │     │ chunk header│     │ (adaptive)  │     │ server      │
│             │     │ Add CRC32   │     │             │     │             │
└──────┬──────┘     └──────┬──────┘     └──────┬──────┘     └──────┬──────┘
       │                   │                   │                   │
       ▼                   ▼                   ▼                   ▼
  ┌─────────┐         ┌─────────┐         ┌─────────┐         ┌─────────┐
  │read_queue│         │chunk_queue│       │comp_queue│        │send_queue│
  │   (16)  │         │   (16)  │         │   (32)  │         │   (64)  │
  └─────────┘         └─────────┘         └─────────┘         └─────────┘
                                                                   │
                                                                   │ Network
                                                                   ▼
SERVER SIDE:
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   network   │     │ compression │     │chunk_process│     │  io_write   │
│   receive   │────▶│  decompress │────▶│   assemble  │────▶│   stage     │
│             │     │             │     │             │     │             │
│ Receive     │     │ LZ4         │     │ Verify CRC32│     │ Write to    │
│ from client │     │ decompress  │     │ Handle order│     │ storage     │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
```

### Download Pipeline Flow (Server → Client)

```
SERVER SIDE:
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  io_read    │     │chunk_process│     │ compression │     │   network   │
│   stage     │────▶│    stage    │────▶│    stage    │────▶│    stage    │
│             │     │             │     │             │     │             │
│ Read from   │     │ Create      │     │ LZ4 compress│     │ Send to     │
│ storage     │     │ chunk header│     │ (adaptive)  │     │ client      │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
                                                                   │
                                                                   │ Network
                                                                   ▼
CLIENT SIDE:
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   network   │     │ compression │     │chunk_process│     │  io_write   │
│   receive   │────▶│  decompress │────▶│   assemble  │────▶│   stage     │
│             │     │             │     │             │     │             │
│ Receive     │     │ LZ4         │     │ Verify CRC32│     │ Write to    │
│ from server │     │ decompress  │     │ Handle order│     │ local file  │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
```

---

## Data Flow

### Protocol Message Types

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Protocol Message Types                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Control Messages:                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │ 0x01 CONNECT           │ Client connection request               │    │
│  │ 0x02 CONNECT_ACK       │ Server acknowledges connection          │    │
│  │ 0x03 DISCONNECT        │ Graceful disconnect                     │    │
│  │ 0x04 HEARTBEAT         │ Keep-alive ping/pong                    │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  Upload Messages:                                                           │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │ 0x10 UPLOAD_REQUEST    │ C→S: Request to upload a file           │    │
│  │ 0x11 UPLOAD_ACCEPT     │ S→C: Upload approved                    │    │
│  │ 0x12 UPLOAD_REJECT     │ S→C: Upload denied + reason             │    │
│  │ 0x13 UPLOAD_COMPLETE   │ C→S: All chunks sent                    │    │
│  │ 0x14 UPLOAD_ACK        │ S→C: File fully received                │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  Download Messages:                                                         │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │ 0x50 DOWNLOAD_REQUEST  │ C→S: Request to download a file         │    │
│  │ 0x51 DOWNLOAD_ACCEPT   │ S→C: Download approved + metadata       │    │
│  │ 0x52 DOWNLOAD_REJECT   │ S→C: Download denied + reason           │    │
│  │ 0x53 DOWNLOAD_COMPLETE │ S→C: All chunks sent                    │    │
│  │ 0x54 DOWNLOAD_ACK      │ C→S: File fully received                │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  Listing Messages:                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │ 0x60 LIST_REQUEST      │ C→S: Request file listing               │    │
│  │ 0x61 LIST_RESPONSE     │ S→C: File list + metadata               │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  Data Messages:                                                             │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │ 0x20 CHUNK_DATA        │ Chunk payload (both directions)         │    │
│  │ 0x21 CHUNK_ACK         │ Chunk acknowledgment                    │    │
│  │ 0x22 CHUNK_NACK        │ Request retransmission                  │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Chunk Structure

```
┌─────────────────────────────────────────────────────────────────┐
│                         CHUNK HEADER                             │
├──────────────────┬──────────────────────────────────────────────┤
│ transfer_id      │ 16 bytes - UUID identifying the transfer     │
├──────────────────┼──────────────────────────────────────────────┤
│ chunk_index      │  8 bytes - Sequential chunk number           │
├──────────────────┼──────────────────────────────────────────────┤
│ chunk_offset     │  8 bytes - Byte offset in file               │
├──────────────────┼──────────────────────────────────────────────┤
│ original_size    │  4 bytes - Uncompressed data size            │
├──────────────────┼──────────────────────────────────────────────┤
│ compressed_size  │  4 bytes - Compressed data size              │
├──────────────────┼──────────────────────────────────────────────┤
│ checksum         │  4 bytes - CRC32 of data                     │
├──────────────────┼──────────────────────────────────────────────┤
│ flags            │  1 byte  - Chunk flags (first/last/compressed)│
├──────────────────┼──────────────────────────────────────────────┤
│ reserved         │  3 bytes - Future use                        │
├──────────────────┴──────────────────────────────────────────────┤
│                          DATA                                    │
│                  (compressed or raw bytes)                       │
└─────────────────────────────────────────────────────────────────┘
```

### Transfer State Machine

```
                                    ┌──────────────┐
                                    │   pending    │
                                    └──────┬───────┘
                                           │
                                           │ start()
                                           ▼
                                    ┌──────────────┐
                           ┌───────│ initializing │───────┐
                           │       └──────┬───────┘       │
                           │              │               │
                     error │              │ connected     │ error
                           │              ▼               │
                           │       ┌──────────────┐       │
                           │   ┌──▶│ transferring │──┐    │
                           │   │   └──────┬───────┘  │    │
                           │   │          │          │    │
                           │   │ resume   │ complete │    │
                           │   │          ▼          │    │
                           │   │   ┌──────────────┐  │    │
                           │   └───│  verifying   │  │    │
                           │       └──────┬───────┘  │    │
                           │              │          │    │
                           │              │ verified │    │
                           │              ▼          │    │
                           │       ┌──────────────┐  │    │
                           │       │  completed   │  │    │
                           │       └──────────────┘  │    │
                           │                         │    │
                           │    cancel()             │    │
                           ▼                         ▼    ▼
                    ┌──────────────┐          ┌──────────────┐
                    │   failed     │          │  cancelled   │
                    └──────────────┘          └──────────────┘
```

---

## Design Patterns

### 1. Builder Pattern

Used for configuring complex objects with many options.

```cpp
// Server configuration
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_max_connections(100)
    .with_max_file_size(10ULL * 1024 * 1024 * 1024)  // 10GB
    .with_storage_quota(1ULL * 1024 * 1024 * 1024 * 1024)  // 1TB
    .with_transport(transport_type::tcp)
    .build();

// Client configuration
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::adaptive)
    .with_chunk_size(256 * 1024)
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy{
        .initial_delay = std::chrono::seconds(1),
        .max_delay = std::chrono::seconds(60),
        .multiplier = 2.0,
        .max_attempts = 10
    })
    .build();
```

### 2. Pipeline Pattern

Stages process data independently with queues for decoupling.

```cpp
// Each stage is a typed job in the thread pool
class compress_job : public typed_job_t<pipeline_stage::compression> {
    void execute() override {
        auto compressed = compressor_.compress(chunk_);
        output_queue_.push(std::move(compressed));
    }
};
```

### 3. Strategy Pattern

Pluggable transport implementations.

```cpp
class transport_interface {
    virtual auto connect(const endpoint& addr) -> Result<void> = 0;
    virtual auto listen(const endpoint& addr) -> Result<void> = 0;
    virtual auto send(span<const byte> data) -> Result<void> = 0;
    virtual auto receive(span<byte> buffer) -> Result<size_t> = 0;
};

class tcp_transport : public transport_interface { ... };
class quic_transport : public transport_interface { ... };
```

### 4. Observer Pattern

Callback-based event notification.

```cpp
// Server callbacks
server->on_upload_request([](const upload_request& req) {
    return req.file_size < 1e9;  // Accept files < 1GB
});

server->on_transfer_complete([](const transfer_result& result) {
    log_info("Transfer completed: {}", result.filename);
});

// Client callbacks
client->on_progress([](const transfer_progress& p) {
    double percent = 100.0 * p.bytes_transferred / p.total_bytes;
    std::cout << percent << "% complete\n";
});

client->on_disconnected([](const disconnect_reason& reason) {
    log_warn("Disconnected: {}", reason.message);
});
```

### 5. Factory Pattern

Creating transport instances.

```cpp
auto transport = transport_factory::create(transport_type::tcp);
```

### 6. Singleton Pattern (with modification)

Server's storage manager uses a single instance per server.

```cpp
// Internal to file_transfer_server
std::unique_ptr<storage_manager> storage_;  // One per server instance
```

---

## Threading Model

### Thread Pool Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                   typed_thread_pool<pipeline_stage>                          │
│                                                                              │
│  Stage: io_read                Stage: compression               Stage: network│
│  ┌─────────────┐               ┌─────────────┐                 ┌─────────────┐│
│  │  Worker 0   │               │  Worker 0   │                 │  Worker 0   ││
│  ├─────────────┤               ├─────────────┤                 ├─────────────┤│
│  │  Worker 1   │               │  Worker 1   │                 │  Worker 1   ││
│  └─────────────┘               ├─────────────┤                 └─────────────┘│
│                                │  Worker 2   │                                │
│                                ├─────────────┤                                │
│                                │  Worker 3   │                                │
│                                └─────────────┘                                │
│                                                                              │
│  Job Routing: Jobs are routed to workers based on their stage type          │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Server Threading Model

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Server Threads                                      │
│                                                                              │
│  ┌─────────────────┐                                                        │
│  │  Accept Thread  │  Single thread for accepting new connections           │
│  └────────┬────────┘                                                        │
│           │                                                                  │
│           │ dispatch                                                         │
│           ▼                                                                  │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                     Connection Thread Pool                           │   │
│  │  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐               │   │
│  │  │ Session │  │ Session │  │ Session │  │ Session │  ...          │   │
│  │  │    1    │  │    2    │  │    3    │  │    4    │               │   │
│  │  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘               │   │
│  └───────┼────────────┼────────────┼────────────┼────────────────────┘   │
│          │            │            │            │                         │
│          └────────────┴────────────┴────────────┘                         │
│                              │                                             │
│                              ▼                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                    Pipeline Thread Pool                              │   │
│  │                  (shared across all sessions)                        │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Client Threading Model

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Client Threads                                      │
│                                                                              │
│  ┌─────────────────┐                                                        │
│  │  Main Thread    │  API calls, callbacks                                  │
│  └────────┬────────┘                                                        │
│           │                                                                  │
│           │                                                                  │
│           ▼                                                                  │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                    Pipeline Thread Pool                              │   │
│  │                                                                      │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                 │   │
│  │  │  IO Workers │  │ Compression │  │   Network   │                 │   │
│  │  │             │  │   Workers   │  │   Workers   │                 │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘                 │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  ┌─────────────────┐                                                        │
│  │ Reconnect Thread│  (if auto_reconnect enabled)                          │
│  │                 │  Handles reconnection attempts                        │
│  └─────────────────┘                                                        │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Concurrency Model

| Aspect | Implementation |
|--------|----------------|
| **Inter-stage Communication** | Bounded queues with backpressure |
| **Intra-stage Parallelism** | Multiple workers per stage |
| **Thread Safety** | Lock-free queues where possible |
| **Graceful Shutdown** | Drain queues before stopping |
| **Connection Isolation** | Each client session has independent state |

---

## Memory Management

### Memory Bounds

Memory usage is deterministic and bounded:

```
max_memory = Σ (queue_size × chunk_size) for all queues

Default (256KB chunks):
  Per-Client Pipeline:  (16 + 16 + 32 + 64) × 256KB = 32MB
  Server (100 clients): 32MB × 100 = 3.2GB max

  With lower settings (64KB chunks, smaller queues):
  Per-Client Pipeline:  (4 + 8 + 16 + 32) × 64KB = 3.75MB
  Server (100 clients): 3.75MB × 100 = 375MB max
```

### Buffer Pooling

Chunk buffers are pooled to reduce allocation overhead:

```cpp
class chunk_buffer_pool {
    // Pre-allocated buffer pool
    // Reuse buffers across chunks
    // Zero-copy where possible
};
```

### Server Memory Management

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        Server Memory Layout                                  │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                       Fixed Allocations                              │   │
│  │  ├ storage_manager file index:  ~10KB per 1000 files                │   │
│  │  ├ connection_manager sessions: ~1KB per connection                 │   │
│  │  └ thread pool overhead:        ~1MB                                │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                    Per-Transfer Allocations                          │   │
│  │  ├ Pipeline queues: queue_size × chunk_size                         │   │
│  │  ├ Checksum buffers: ~4KB per transfer                              │   │
│  │  └ Metadata: ~500 bytes per transfer                                │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Backpressure

When downstream is slow, upstream blocks:

```
Fast reader, slow network (Upload scenario):
  read_queue:     [████████████████] FULL ← Reader BLOCKS
  compress_queue: [████████████████████████████████] FULL
  send_queue:     [████████████████████████████████████████████████████████████████] FULL
                                                            ↑ Slow network
```

---

## Security Considerations

### Transport Security

- **TLS 1.3** encryption by default
- Certificate validation
- Perfect forward secrecy

### Data Integrity

- **CRC32** per-chunk integrity
- **SHA-256** per-file verification
- Automatic retransmission on corruption

### Path Safety (Server-Side)

```cpp
// Path traversal prevention algorithm
auto storage_manager::validate_filename(const std::string& filename) -> Result<void> {
    // 1. Check for empty filename
    if (filename.empty()) {
        return Error{error::invalid_filename, "Filename cannot be empty"};
    }

    // 2. Check for path separators (no subdirectories allowed)
    if (filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos) {
        return Error{error::invalid_filename, "Path separators not allowed"};
    }

    // 3. Check for parent directory references
    if (filename == ".." || filename.find("..") != std::string::npos) {
        return Error{error::invalid_filename, "Parent directory reference not allowed"};
    }

    // 4. Check for hidden files (optional policy)
    if (filename[0] == '.') {
        return Error{error::invalid_filename, "Hidden files not allowed"};
    }

    // 5. Check filename length
    if (filename.length() > 255) {
        return Error{error::invalid_filename, "Filename too long"};
    }

    // 6. Check for invalid characters
    static const std::string invalid_chars = "<>:\"|?*";
    for (char c : filename) {
        if (invalid_chars.find(c) != std::string::npos || c < 32) {
            return Error{error::invalid_filename, "Invalid character in filename"};
        }
    }

    return {};  // Success
}
```

### Access Control

- Server-side callbacks for upload/download approval
- Per-file size limits
- Storage quota enforcement
- Connection limits

---

*Last updated: 2025-12-11*
*Version: 0.2.0*
