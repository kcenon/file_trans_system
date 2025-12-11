# File Transfer System - Product Requirements Document

## Document Information

| Item | Description |
|------|-------------|
| **Project Name** | file_trans_system |
| **Version** | 0.2.0 |
| **Status** | Draft |
| **Created** | 2025-12-11 |
| **Author** | kcenon@naver.com |

---

## 1. Executive Summary

### 1.1 Purpose

The **file_trans_system** is a high-performance, production-ready C++20 file transfer library that implements a **client-server architecture** for centralized file management. The server maintains a file storage repository while clients connect to upload, download, or query files. It integrates seamlessly with the existing ecosystem (common_system, thread_system, logger_system, monitoring_system, container_system, network_system) to provide enterprise-grade file transfer functionality.

### 1.2 Goals

1. **Client-Server Architecture**: Central server with file storage, multiple client connections
2. **Bidirectional Transfer**: Support both upload (client→server) and download (server→client)
3. **File Management**: Server-side file listing, storage management, and access control
4. **Real-time LZ4 Compression**: Per-chunk compression/decompression for increased effective throughput
5. **Reliability**: Ensure data integrity with checksums, resume capability, and error recovery
6. **Performance**: Achieve high throughput leveraging async I/O and thread pooling
7. **Observability**: Full integration with monitoring and logging systems
8. **Security**: Support encrypted transfers with TLS/SSL

### 1.3 Success Metrics

| Metric | Target |
|--------|--------|
| Upload throughput (1GB file, LAN) | ≥ 500 MB/s |
| Download throughput (1GB file, LAN) | ≥ 500 MB/s |
| Throughput (1GB file, WAN) | ≥ 100 MB/s (network limited) |
| Effective throughput with compression | 2-4x improvement for compressible data |
| LZ4 compression speed | ≥ 400 MB/s per core |
| LZ4 decompression speed | ≥ 1.5 GB/s per core |
| Compression ratio (text/logs) | 2:1 to 4:1 typical |
| Memory footprint | < 50 MB baseline |
| Resume accuracy | 100% (verified checksum) |
| Concurrent connections | ≥ 100 simultaneous clients |
| File listing response | < 100ms for 10,000 files |

---

## 2. Problem Statement

### 2.1 Current Challenges

1. **Centralized File Management**: Need a central repository for file storage and distribution
2. **Bidirectional Transfers**: Both upload and download capabilities from a single server
3. **Large File Handling**: Transferring files larger than available memory requires streaming
4. **Network Instability**: Interrupted transfers should resume without re-sending entire files
5. **Bandwidth Limitations**: Network bandwidth is often the bottleneck; compression can increase effective throughput
6. **Multi-client Coordination**: Multiple clients need concurrent access to server resources
7. **Resource Management**: Efficient use of memory, disk I/O, and network bandwidth
8. **Cross-platform Support**: Consistent behavior across Linux, macOS, and Windows

### 2.2 Use Cases

| Use Case | Description |
|----------|-------------|
| **UC-01** | Client uploads a single file to the server |
| **UC-02** | Client downloads a single file from the server |
| **UC-03** | Client uploads multiple files as a batch operation |
| **UC-04** | Client downloads multiple files from the server |
| **UC-05** | Client queries the server for available files (list) |
| **UC-06** | Resume an interrupted upload/download from the last successful chunk |
| **UC-07** | Monitor transfer progress with detailed metrics |
| **UC-08** | Secure file transfer over untrusted networks |
| **UC-09** | Server manages file storage (quota, retention, cleanup) |
| **UC-10** | Transfer compressible files (logs, text, JSON) with real-time compression |
| **UC-11** | Transfer pre-compressed files (ZIP, media) without double-compression overhead |

---

## 3. System Architecture

### 3.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           file_trans_system                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                        file_transfer_server                            │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌───────────┐  │  │
│  │  │   Server     │  │   Storage    │  │   Client     │  │  Transfer │  │  │
│  │  │   Handler    │  │   Manager    │  │   Manager    │  │  Manager  │  │  │
│  │  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘  └─────┬─────┘  │  │
│  │         │                 │                 │                │        │  │
│  │  ┌──────▼─────────────────▼─────────────────▼────────────────▼──────┐ │  │
│  │  │                         File Storage                             │ │  │
│  │  │                       /data/files/                               │ │  │
│  │  └──────────────────────────────────────────────────────────────────┘ │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│                                    ▲                                         │
│                                    │ TCP/TLS or QUIC                        │
│                                    ▼                                         │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                       file_transfer_client                             │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌───────────┐  │  │
│  │  │   Upload     │  │  Download    │  │    List      │  │  Progress │  │  │
│  │  │   Engine     │  │   Engine     │  │   Handler    │  │  Tracker  │  │  │
│  │  └──────────────┘  └──────────────┘  └──────────────┘  └───────────┘  │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│                                                                              │
├─────────────────────────────────────────────────────────────────────────────┤
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                         Chunk Manager                                 │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────┐ │   │
│  │  │ Splitter │  │Assembler │  │ Checksum │  │  Resume  │  │  LZ4   │ │   │
│  │  │          │  │          │  │          │  │ Handler  │  │Compress│ │   │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └────────┘ │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────────────────┤
│                            Integration Layer                                 │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────┐ │
│  │ common   │ │ thread   │ │ logger   │ │monitoring│ │ network  │ │ LZ4  │ │
│  │ _system  │ │ _system  │ │ _system  │ │ _system  │ │ _system  │ │ lib  │ │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────┘ │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Data Flow

#### 3.2.1 Upload Flow (Client → Server)

```
┌────────────────────────────────────────────────────────────────────────────┐
│                           UPLOAD FLOW                                       │
│                                                                             │
│  Client                                              Server                 │
│  ┌─────────────────┐                    ┌─────────────────────────────────┐│
│  │ Local File      │                    │        File Storage             ││
│  │ /local/data.zip │                    │      /data/files/data.zip       ││
│  └────────┬────────┘                    └──────────────▲──────────────────┘│
│           │                                            │                    │
│           ▼                                            │                    │
│  ┌─────────────────┐    UPLOAD_REQUEST    ┌───────────┴───────────┐       │
│  │  Upload Engine  │─────────────────────▶│    Server Handler     │       │
│  │                 │◀─────────────────────│                       │       │
│  │  io_read        │    UPLOAD_ACCEPT     │  validate request     │       │
│  │  chunk_process  │                      │  check storage quota  │       │
│  │  compression    │                      └───────────────────────┘       │
│  │  network_send   │                                                       │
│  └────────┬────────┘                                                       │
│           │                                                                 │
│           │  CHUNK_DATA [0..N]           ┌───────────────────────┐        │
│           │─────────────────────────────▶│  Server Receive       │        │
│           │◀─────────────────────────────│  Pipeline             │        │
│           │  CHUNK_ACK                   │                       │        │
│           │                              │  network_recv         │        │
│           │  TRANSFER_COMPLETE           │  decompression        │        │
│           │─────────────────────────────▶│  chunk_assemble       │        │
│           │◀─────────────────────────────│  io_write             │        │
│           │  TRANSFER_VERIFY             └───────────────────────┘        │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

#### 3.2.2 Download Flow (Server → Client)

```
┌────────────────────────────────────────────────────────────────────────────┐
│                          DOWNLOAD FLOW                                      │
│                                                                             │
│  Client                                              Server                 │
│  ┌─────────────────┐                    ┌─────────────────────────────────┐│
│  │ Local File      │                    │        File Storage             ││
│  │/local/report.pdf│                    │    /data/files/report.pdf       ││
│  └────────▲────────┘                    └──────────────┬──────────────────┘│
│           │                                            │                    │
│           │                                            ▼                    │
│  ┌────────┴────────┐   DOWNLOAD_REQUEST   ┌───────────────────────┐       │
│  │ Download Engine │─────────────────────▶│    Server Handler     │       │
│  │                 │◀─────────────────────│                       │       │
│  │  network_recv   │   DOWNLOAD_ACCEPT    │  validate request     │       │
│  │  decompression  │   (+ file metadata)  │  check file exists    │       │
│  │  chunk_assemble │                      └───────────┬───────────┘       │
│  │  io_write       │                                  │                    │
│  └────────▲────────┘                                  ▼                    │
│           │                              ┌───────────────────────┐        │
│           │  CHUNK_DATA [0..N]           │  Server Send          │        │
│           │◀─────────────────────────────│  Pipeline             │        │
│           │─────────────────────────────▶│                       │        │
│           │  CHUNK_ACK                   │  io_read              │        │
│           │                              │  chunk_process        │        │
│           │  TRANSFER_COMPLETE           │  compression          │        │
│           │◀─────────────────────────────│  network_send         │        │
│           │─────────────────────────────▶└───────────────────────┘        │
│           │  TRANSFER_VERIFY                                               │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

#### 3.2.3 List Files Flow

```
┌────────────────────────────────────────────────────────────────────────────┐
│                          LIST FILES FLOW                                    │
│                                                                             │
│  Client                                              Server                 │
│                                                                             │
│  ┌─────────────────┐    LIST_REQUEST      ┌───────────────────────┐       │
│  │  List Handler   │─────────────────────▶│    Server Handler     │       │
│  │                 │                      │                       │       │
│  │  filter: *.pdf  │                      │  scan storage dir     │       │
│  │  sort: by_size  │                      │  apply filter         │       │
│  │  limit: 100     │                      │  apply sort           │       │
│  │                 │◀─────────────────────│  paginate results     │       │
│  │  Display files  │    LIST_RESPONSE     │                       │       │
│  └─────────────────┘                      └───────────────────────┘       │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

### 3.3 Pipeline Architecture

The file transfer system uses a **typed_thread_pool-based pipeline architecture** from thread_system to maximize throughput through parallel processing of different pipeline stages.

#### 3.3.1 Client Upload Pipeline

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      CLIENT UPLOAD PIPELINE                                  │
│                                                                              │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌────────────┐│
│  │  File Read   │───▶│    Chunk     │───▶│     LZ4      │───▶│  Network   ││
│  │    Stage     │    │   Assembly   │    │  Compress    │    │    Send    ││
│  └──────────────┘    └──────────────┘    └──────────────┘    └────────────┘│
│        │                   │                   │                   │        │
│        ▼                   ▼                   ▼                   ▼        │
│  ┌──────────────────────────────────────────────────────────────────────┐  │
│  │                    typed_thread_pool<pipeline_stage>                  │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐              │  │
│  │  │  IO (2)  │  │ Chunk(2) │  │Compress(4)│ │Network(2)│              │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘              │  │
│  └──────────────────────────────────────────────────────────────────────┘  │
│                                                                              │
│  Stage Queues (Backpressure Control):                                       │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐           │
│  │ read_queue │─▶│chunk_queue │─▶│ comp_queue │─▶│ send_queue │           │
│  │    (16)    │  │    (16)    │  │    (32)    │  │    (64)    │           │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘           │
└─────────────────────────────────────────────────────────────────────────────┘
```

#### 3.3.2 Client Download Pipeline

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     CLIENT DOWNLOAD PIPELINE                                 │
│                                                                              │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌────────────┐│
│  │   Network    │───▶│     LZ4      │───▶│    Chunk     │───▶│ File Write ││
│  │   Receive    │    │  Decompress  │    │   Assembly   │    │    Stage   ││
│  └──────────────┘    └──────────────┘    └──────────────┘    └────────────┘│
│        │                   │                   │                   │        │
│        ▼                   ▼                   ▼                   ▼        │
│  ┌──────────────────────────────────────────────────────────────────────┐  │
│  │                    typed_thread_pool<pipeline_stage>                  │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐              │  │
│  │  │Network(2)│  │Decomp(4) │  │ Chunk(2) │  │  IO (2)  │              │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘              │  │
│  └──────────────────────────────────────────────────────────────────────┘  │
│                                                                              │
│  Stage Queues:                                                              │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐           │
│  │ recv_queue │─▶│decomp_queue│─▶│assem_queue │─▶│write_queue │           │
│  │    (64)    │  │    (32)    │  │    (16)    │  │    (16)    │           │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘           │
└─────────────────────────────────────────────────────────────────────────────┘
```

#### 3.3.3 Pipeline Stage Types

```cpp
namespace kcenon::file_transfer {

// Pipeline stage types for typed_thread_pool
enum class pipeline_stage : uint8_t {
    io_read,        // File read operations (I/O bound)
    chunk_process,  // Chunk assembly/disassembly (CPU light)
    compression,    // LZ4 compress/decompress (CPU bound)
    network,        // Network send/receive (I/O bound)
    io_write        // File write operations (I/O bound)
};

} // namespace kcenon::file_transfer
```

#### 3.3.4 Pipeline Configuration

```cpp
// Pipeline worker configuration
struct pipeline_config {
    // Worker counts per stage (auto-tuned by default)
    std::size_t io_read_workers      = 2;   // Disk read parallelism
    std::size_t chunk_workers        = 2;   // Chunk processing
    std::size_t compression_workers  = 4;   // LZ4 compression (CPU-bound)
    std::size_t network_workers      = 2;   // Network operations
    std::size_t io_write_workers     = 2;   // Disk write parallelism

    // Queue sizes (backpressure control)
    std::size_t read_queue_size      = 16;  // Pending read chunks
    std::size_t compress_queue_size  = 32;  // Pending compression
    std::size_t send_queue_size      = 64;  // Pending network sends
    std::size_t decompress_queue_size = 32; // Pending decompression
    std::size_t write_queue_size     = 16;  // Pending writes

    // Auto-tune based on hardware
    static auto auto_detect() -> pipeline_config;
};
```

### 3.4 Component Descriptions

#### 3.4.1 file_transfer_server

The central server component that manages file storage and handles client requests.

**Responsibilities:**
- Listen for incoming client connections
- Authenticate and authorize client requests
- Handle upload requests (receive files from clients)
- Handle download requests (send files to clients)
- Handle list requests (return available files)
- Manage file storage (quota, retention, cleanup)
- Track active transfers and connected clients

**Key Features:**
- Multi-client support with connection pooling
- Configurable storage directory and quotas
- File access control (optional)
- Automatic file metadata indexing

#### 3.4.2 file_transfer_client

The client component that connects to a server to perform file operations.

**Responsibilities:**
- Connect to and disconnect from the server
- Upload files to the server
- Download files from the server
- Query server for available files
- Track transfer progress
- Handle transfer resume on interruption

**Key Features:**
- Auto-reconnect with exponential backoff
- Concurrent upload/download support
- Progress callbacks with compression metrics
- Transfer pause/resume/cancel

#### 3.4.3 Storage Manager (Server-side)

Manages the server's file storage repository.

**Responsibilities:**
- File storage organization
- Storage quota enforcement
- File retention policies
- File metadata caching for fast queries
- Concurrent access management

#### 3.4.4 Chunk Manager

Shared component for chunk-level operations.

**Responsibilities:**
- **Splitter**: Divides files into configurable chunks (default: 64KB - 1MB)
- **Assembler**: Reconstructs files from received chunks
- **Checksum**: Calculates and verifies chunk/file integrity (CRC32, SHA-256)
- **Resume Handler**: Tracks transfer state for resume capability
- **LZ4 Compressor**: Real-time per-chunk compression/decompression

---

## 4. Functional Requirements

### 4.1 Core Features

#### FR-01: Server Startup and Shutdown
| ID | FR-01 |
|----|-------|
| **Description** | Server starts listening on configured endpoint and gracefully shuts down |
| **Priority** | P0 (Critical) |
| **Acceptance Criteria** | Server accepts connections, handles graceful shutdown with active transfers |

#### FR-02: Client Connection
| ID | FR-02 |
|----|-------|
| **Description** | Client connects to server with optional authentication |
| **Priority** | P0 (Critical) |
| **Acceptance Criteria** | Successful handshake, connection state management |

#### FR-03: File Upload (Single)
| ID | FR-03 |
|----|-------|
| **Description** | Client uploads a single file to the server |
| **Priority** | P0 (Critical) |
| **Acceptance Criteria** | File transferred with 100% integrity verified by checksum |

#### FR-04: File Upload (Batch)
| ID | FR-04 |
|----|-------|
| **Description** | Client uploads multiple files in a single operation |
| **Priority** | P1 (High) |
| **Acceptance Criteria** | All files transferred, individual file status tracked |

#### FR-05: File Download (Single)
| ID | FR-05 |
|----|-------|
| **Description** | Client downloads a single file from the server |
| **Priority** | P0 (Critical) |
| **Acceptance Criteria** | File transferred with 100% integrity verified by checksum |

#### FR-06: File Download (Batch)
| ID | FR-06 |
|----|-------|
| **Description** | Client downloads multiple files in a single operation |
| **Priority** | P1 (High) |
| **Acceptance Criteria** | All files transferred, individual file status tracked |

#### FR-07: File Listing
| ID | FR-07 |
|----|-------|
| **Description** | Client queries server for available files with filtering and sorting |
| **Priority** | P0 (Critical) |
| **Acceptance Criteria** | Returns file list with metadata, supports filter/sort/pagination |

#### FR-08: Chunk-based Transfer
| ID | FR-08 |
|----|-------|
| **Description** | Split files into chunks for streaming transfer |
| **Priority** | P0 (Critical) |
| **Acceptance Criteria** | Configurable chunk size, correct reassembly |

#### FR-09: Transfer Resume
| ID | FR-09 |
|----|-------|
| **Description** | Resume interrupted uploads/downloads from last successful chunk |
| **Priority** | P1 (High) |
| **Acceptance Criteria** | Resume within 1 second, no data loss |

#### FR-10: Progress Monitoring
| ID | FR-10 |
|----|-------|
| **Description** | Real-time progress tracking with callbacks |
| **Priority** | P1 (High) |
| **Acceptance Criteria** | Progress updates at configurable intervals |

#### FR-11: Integrity Verification
| ID | FR-11 |
|----|-------|
| **Description** | Verify data integrity using checksums |
| **Priority** | P0 (Critical) |
| **Acceptance Criteria** | CRC32 per chunk, SHA-256 per file |

#### FR-12: Concurrent Transfers
| ID | FR-12 |
|----|-------|
| **Description** | Support multiple simultaneous file transfers |
| **Priority** | P1 (High) |
| **Acceptance Criteria** | ≥100 concurrent transfers without degradation |

#### FR-13: Bandwidth Throttling
| ID | FR-13 |
|----|-------|
| **Description** | Limit transfer bandwidth per connection/total |
| **Priority** | P2 (Medium) |
| **Acceptance Criteria** | Bandwidth within 5% of configured limit |

#### FR-14: Real-time LZ4 Compression
| ID | FR-14 |
|----|-------|
| **Description** | Per-chunk LZ4 compression/decompression |
| **Priority** | P0 (Critical) |
| **Acceptance Criteria** | Compression speed ≥400 MB/s, decompression ≥1.5 GB/s |

#### FR-15: Adaptive Compression
| ID | FR-15 |
|----|-------|
| **Description** | Automatically skip compression for incompressible data |
| **Priority** | P1 (High) |
| **Acceptance Criteria** | Detect incompressible chunks within 1KB sample |

#### FR-16: Storage Management
| ID | FR-16 |
|----|-------|
| **Description** | Server manages file storage with quotas and retention |
| **Priority** | P1 (High) |
| **Acceptance Criteria** | Enforce storage limits, optional auto-cleanup |

#### FR-17: Pipeline-based Processing
| ID | FR-17 |
|----|-------|
| **Description** | Multi-stage pipeline for parallel processing |
| **Priority** | P0 (Critical) |
| **Acceptance Criteria** | Upload/Download pipelines with configurable stages |

#### FR-18: Pipeline Backpressure
| ID | FR-18 |
|----|-------|
| **Description** | Prevent memory exhaustion through bounded queues |
| **Priority** | P1 (High) |
| **Acceptance Criteria** | Configurable queue sizes, automatic slowdown |

### 4.2 API Requirements

#### 4.2.1 Server API

```cpp
namespace kcenon::file_transfer {

class file_transfer_server {
public:
    // Builder pattern for configuration
    class builder {
    public:
        builder& with_storage_directory(const std::filesystem::path& dir);
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_max_connections(std::size_t max_conn);
        builder& with_max_file_size(uint64_t max_size);
        builder& with_storage_quota(uint64_t quota_bytes);
        builder& with_allowed_extensions(std::vector<std::string> exts);
        builder& with_transport(transport_type type);
        [[nodiscard]] auto build() -> Result<file_transfer_server>;
    };

    // Lifecycle management
    [[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;
    [[nodiscard]] auto stop() -> Result<void>;
    [[nodiscard]] auto is_running() const -> bool;

    // File management
    [[nodiscard]] auto list_files(const list_options& opts = {})
        -> std::vector<file_info>;
    [[nodiscard]] auto delete_file(const std::string& filename) -> Result<void>;
    [[nodiscard]] auto get_file_info(const std::string& filename)
        -> Result<file_info>;
    [[nodiscard]] auto get_storage_usage() -> storage_stats;

    // Request callbacks
    void on_upload_request(
        std::function<bool(const upload_request&)> callback
    );
    void on_download_request(
        std::function<bool(const download_request&)> callback
    );

    // Event callbacks
    void on_transfer_progress(
        std::function<void(const transfer_progress&)> callback
    );
    void on_transfer_complete(
        std::function<void(const transfer_result&)> callback
    );
    void on_client_connected(
        std::function<void(const client_info&)> callback
    );
    void on_client_disconnected(
        std::function<void(const client_info&)> callback
    );

    // Statistics
    [[nodiscard]] auto get_statistics() -> server_statistics;
    [[nodiscard]] auto get_connected_clients() -> std::vector<client_info>;
};

} // namespace kcenon::file_transfer
```

#### 4.2.2 Client API

```cpp
namespace kcenon::file_transfer {

class file_transfer_client {
public:
    // Builder pattern for configuration
    class builder {
    public:
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_compression(compression_mode mode);
        builder& with_compression_level(compression_level level);
        builder& with_chunk_size(std::size_t size);
        builder& with_bandwidth_limit(std::size_t bytes_per_second);
        builder& with_transport(transport_type type);
        builder& with_auto_reconnect(bool enabled, duration interval = 5s);
        [[nodiscard]] auto build() -> Result<file_transfer_client>;
    };

    // Connection management
    [[nodiscard]] auto connect(const endpoint& server_addr) -> Result<void>;
    [[nodiscard]] auto disconnect() -> Result<void>;
    [[nodiscard]] auto is_connected() const -> bool;

    // Upload operations
    [[nodiscard]] auto upload_file(
        const std::filesystem::path& local_path,
        const std::string& remote_name = {},
        const transfer_options& options = {}
    ) -> Result<transfer_handle>;

    [[nodiscard]] auto upload_files(
        std::span<const std::filesystem::path> files,
        const transfer_options& options = {}
    ) -> Result<batch_transfer_handle>;

    // Download operations
    [[nodiscard]] auto download_file(
        const std::string& remote_name,
        const std::filesystem::path& local_path,
        const transfer_options& options = {}
    ) -> Result<transfer_handle>;

    [[nodiscard]] auto download_files(
        std::span<const std::string> remote_names,
        const std::filesystem::path& output_dir,
        const transfer_options& options = {}
    ) -> Result<batch_transfer_handle>;

    // File listing
    [[nodiscard]] auto list_files(
        const list_options& options = {}
    ) -> Result<std::vector<file_info>>;

    // Transfer control
    [[nodiscard]] auto cancel(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto pause(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto resume(const transfer_id& id) -> Result<void>;

    // Callbacks
    void on_progress(std::function<void(const transfer_progress&)> callback);
    void on_connection_state(std::function<void(connection_state)> callback);

    // Statistics
    [[nodiscard]] auto get_statistics() -> client_statistics;
};

} // namespace kcenon::file_transfer
```

#### 4.2.3 Data Structures

```cpp
namespace kcenon::file_transfer {

// Connection state
enum class connection_state {
    disconnected,
    connecting,
    connected,
    reconnecting
};

// File information
struct file_info {
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    std::chrono::system_clock::time_point created_time;
    std::chrono::system_clock::time_point modified_time;
};

// Upload request (server callback)
struct upload_request {
    client_info             client;
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    compression_mode        compression;
};

// Download request (server callback)
struct download_request {
    client_info             client;
    std::string             filename;
    uint64_t                offset;         // For resume
    uint64_t                length;         // 0 = entire file
};

// List options
struct list_options {
    std::string             filter_pattern;     // Glob pattern (e.g., "*.pdf")
    sort_field              sort_by = sort_field::name;
    sort_order              order = sort_order::ascending;
    uint32_t                offset = 0;         // Pagination
    uint32_t                limit = 0;          // 0 = no limit
};

// Transfer options
struct transfer_options {
    compression_mode            compression     = compression_mode::adaptive;
    compression_level           level           = compression_level::fast;
    std::size_t                 chunk_size      = 256 * 1024;  // 256KB
    bool                        verify_checksum = true;
    std::optional<std::size_t>  bandwidth_limit;
    std::optional<int>          priority;
    bool                        overwrite_existing = false;
};

// Transfer progress
struct transfer_progress {
    transfer_id         id;
    transfer_direction  direction;          // upload or download
    std::string         filename;
    uint64_t            bytes_transferred;  // Raw bytes
    uint64_t            bytes_on_wire;      // Compressed bytes
    uint64_t            total_bytes;
    double              transfer_rate;      // bytes/second
    double              effective_rate;     // With compression
    double              compression_ratio;
    duration            elapsed_time;
    duration            estimated_remaining;
    transfer_state      state;
};

// Storage statistics
struct storage_stats {
    uint64_t            total_capacity;
    uint64_t            used_bytes;
    uint64_t            available_bytes;
    uint64_t            file_count;
};

// Client information
struct client_info {
    std::string         client_id;
    endpoint            address;
    std::chrono::system_clock::time_point connected_at;
    uint64_t            bytes_uploaded;
    uint64_t            bytes_downloaded;
};

} // namespace kcenon::file_transfer
```

---

## 5. Non-Functional Requirements

### 5.1 Performance

| Requirement | Target | Measurement |
|-------------|--------|-------------|
| **NFR-01** Upload throughput | ≥500 MB/s (LAN) | 1GB file upload time |
| **NFR-02** Download throughput | ≥500 MB/s (LAN) | 1GB file download time |
| **NFR-03** Latency | <10ms chunk processing | End-to-end chunk latency |
| **NFR-04** Memory (server) | <100MB + 1MB/connection | RSS during operation |
| **NFR-05** Memory (client) | <50MB baseline | RSS during idle |
| **NFR-06** CPU utilization | <30% per core | During sustained transfer |
| **NFR-07** LZ4 compression | ≥400 MB/s | Compression throughput |
| **NFR-08** LZ4 decompression | ≥1.5 GB/s | Decompression throughput |
| **NFR-09** Compression ratio | 2:1 to 4:1 for text | Typical compressible data |
| **NFR-10** List response | <100ms for 10K files | File listing query |

### 5.2 Reliability

| Requirement | Target |
|-------------|--------|
| **NFR-11** Data integrity | 100% (SHA-256 verified) |
| **NFR-12** Resume accuracy | 100% successful resume |
| **NFR-13** Error recovery | Auto-retry with exponential backoff |
| **NFR-14** Graceful degradation | Reduced throughput under load |
| **NFR-15** Server uptime | 99.9% availability |

### 5.3 Security

| Requirement | Description |
|-------------|-------------|
| **NFR-16** Encryption | TLS 1.3 for network transfer |
| **NFR-17** Authentication | Optional token/certificate-based auth |
| **NFR-18** Path traversal | Prevent directory escape attacks |
| **NFR-19** Resource limits | Max file size, connection limits |
| **NFR-20** File validation | Filename sanitization |

### 5.4 Compatibility

| Requirement | Description |
|-------------|-------------|
| **NFR-21** C++ Standard | C++20 or later |
| **NFR-22** Platforms | Linux, macOS, Windows |
| **NFR-23** Compilers | GCC 11+, Clang 14+, MSVC 19.29+ |
| **NFR-24** LZ4 library | LZ4 1.9.0+ (BSD license) |

---

## 6. Protocol Design

### 6.1 Message Types

```cpp
enum class message_type : uint8_t {
    // Session management (0x01-0x0F)
    handshake_request   = 0x01,
    handshake_response  = 0x02,

    // Upload operations (0x10-0x1F)
    upload_request      = 0x10,
    upload_accept       = 0x11,
    upload_reject       = 0x12,
    upload_cancel       = 0x13,

    // Download operations (0x50-0x5F)
    download_request    = 0x50,
    download_accept     = 0x51,
    download_reject     = 0x52,
    download_cancel     = 0x53,

    // File listing (0x60-0x6F)
    list_request        = 0x60,
    list_response       = 0x61,

    // Data transfer (0x20-0x2F)
    chunk_data          = 0x20,
    chunk_ack           = 0x21,
    chunk_nack          = 0x22,

    // Resume (0x30-0x3F)
    resume_request      = 0x30,
    resume_response     = 0x31,

    // Completion (0x40-0x4F)
    transfer_complete   = 0x40,
    transfer_verify     = 0x41,

    // Control (0xF0-0xFF)
    keepalive           = 0xF0,
    error               = 0xFF
};
```

### 6.2 Message Frame

```
┌─────────────────────────────────┐
│ Message Type    │ 1 byte        │
├─────────────────────────────────┤
│ Payload Length  │ 4 bytes (BE)  │
├─────────────────────────────────┤
│ Payload         │ Variable      │
└─────────────────────────────────┘

Total overhead: 5 bytes per message
```

---

## 7. Error Codes

Following the ecosystem convention, file_trans_system reserves error codes in range **-700 to -799**:

| Range | Category |
|-------|----------|
| -700 to -719 | Transfer errors (init, cancel, timeout) |
| -720 to -739 | Chunk errors (checksum, sequence, size) |
| -740 to -759 | File I/O errors (read, write, permission, not_found) |
| -760 to -779 | Resume errors (state, corruption) |
| -780 to -789 | Compression errors (compress, decompress, invalid) |
| -790 to -799 | Configuration errors |

### 7.1 New Error Codes for Client-Server

| Code | Name | Description |
|------|------|-------------|
| -744 | file_already_exists | Upload: file already exists on server |
| -745 | storage_full | Server storage quota exceeded |
| -746 | file_not_found_on_server | Download: requested file not found |
| -747 | access_denied | Permission denied for operation |
| -748 | invalid_filename | Filename contains invalid characters |
| -749 | connection_refused | Server refused connection |
| -750 | server_busy | Server at max capacity |

---

## 8. Directory Structure

```
file_trans_system/
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── PRD.md
│   ├── PRD_KR.md
│   ├── SRS.md
│   ├── SDS.md
│   └── reference/
├── include/
│   └── kcenon/
│       └── file_transfer/
│           ├── file_transfer.h           # Main header
│           ├── server/
│           │   ├── file_transfer_server.h
│           │   ├── server_handler.h
│           │   └── storage_manager.h
│           ├── client/
│           │   ├── file_transfer_client.h
│           │   ├── upload_engine.h
│           │   └── download_engine.h
│           ├── core/
│           │   ├── transfer_manager.h
│           │   └── error_codes.h
│           ├── chunk/
│           │   ├── chunk.h
│           │   ├── chunk_splitter.h
│           │   ├── chunk_assembler.h
│           │   └── checksum.h
│           ├── compression/
│           │   ├── lz4_engine.h
│           │   ├── chunk_compressor.h
│           │   └── compression_stats.h
│           ├── transport/
│           │   ├── transport_interface.h
│           │   ├── tcp_transport.h
│           │   ├── quic_transport.h
│           │   └── protocol_messages.h
│           └── resume/
│               ├── resume_handler.h
│               └── transfer_state.h
├── src/
│   ├── server/
│   ├── client/
│   ├── core/
│   ├── chunk/
│   ├── compression/
│   ├── transport/
│   └── resume/
├── tests/
│   ├── unit/
│   ├── integration/
│   └── benchmark/
└── examples/
    ├── simple_server/
    ├── simple_client/
    ├── upload_example/
    ├── download_example/
    └── batch_transfer/
```

---

## 9. Development Phases

### Phase 1: Core Infrastructure (2-3 weeks)
- [ ] Project setup with CMake
- [ ] LZ4 library integration
- [ ] Chunk data structures and serialization
- [ ] Basic chunk splitter/assembler
- [ ] CRC32 checksum implementation

### Phase 2: Server Foundation (2-3 weeks)
- [ ] file_transfer_server basic implementation
- [ ] Storage manager with file indexing
- [ ] Server handler for client connections
- [ ] Upload request handling
- [ ] Download request handling

### Phase 3: Client Foundation (2-3 weeks)
- [ ] file_transfer_client implementation
- [ ] Connection management with auto-reconnect
- [ ] Upload engine with pipeline
- [ ] Download engine with pipeline
- [ ] List files functionality

### Phase 4: Compression & Resume (2 weeks)
- [ ] LZ4 compression integration
- [ ] Adaptive compression detection
- [ ] Resume handler implementation
- [ ] Transfer state persistence

### Phase 5: Advanced Features (2 weeks)
- [ ] Batch upload/download
- [ ] Bandwidth throttling
- [ ] Storage quota management
- [ ] Progress tracking with compression metrics

### Phase 6: Integration & Polish (1-2 weeks)
- [ ] logger_system integration
- [ ] monitoring_system integration
- [ ] Performance benchmarks
- [ ] Documentation and examples

### Phase 7: QUIC Transport (Optional, 2-3 weeks)
- [ ] QUIC transport implementation
- [ ] 0-RTT connection resumption
- [ ] Connection migration support

---

## 10. Success Criteria

### 10.1 Functional Completeness
- [ ] All P0 requirements implemented and tested
- [ ] All P1 requirements implemented and tested
- [ ] Server and client APIs complete
- [ ] Upload/Download/List operations working

### 10.2 Quality Gates
- [ ] ≥80% code coverage
- [ ] Zero ThreadSanitizer warnings
- [ ] Zero AddressSanitizer leaks
- [ ] All integration tests passing

### 10.3 Performance Validation
- [ ] Upload throughput targets met
- [ ] Download throughput targets met
- [ ] Compression speed targets met
- [ ] Memory targets met
- [ ] Resume functionality validated

---

## Appendix A: Glossary

| Term | Definition |
|------|------------|
| **Server** | Central component that stores files and handles client requests |
| **Client** | Component that connects to server for file operations |
| **Upload** | Transfer of file from client to server |
| **Download** | Transfer of file from server to client |
| **Chunk** | A fixed-size segment of a file for streaming transfer |
| **Resume** | Continuing an interrupted transfer from the last successful point |
| **Storage** | Server-side file repository |

---

## Appendix B: References

### Internal Documentation
- [common_system Documentation](../../../common_system/README.md)
- [thread_system Documentation](../../../thread_system/README.md)
- [network_system Documentation](../../../network_system/README.md)
- [container_system Documentation](../../../container_system/README.md)

### External References
- [LZ4 Official Repository](https://github.com/lz4/lz4)
- [RFC 9000 - QUIC](https://tools.ietf.org/html/rfc9000)
