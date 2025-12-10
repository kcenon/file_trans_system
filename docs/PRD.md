# File Transfer System - Product Requirements Document

## Document Information

| Item | Description |
|------|-------------|
| **Project Name** | file_trans_system |
| **Version** | 1.0.0 |
| **Status** | Draft |
| **Created** | 2025-12-11 |
| **Author** | kcenon@naver.com |

---

## 1. Executive Summary

### 1.1 Purpose

The **file_trans_system** is a high-performance, production-ready C++20 file transfer library designed to enable reliable transmission and reception of multiple files with chunk-based streaming capabilities. It integrates seamlessly with the existing ecosystem (common_system, thread_system, logger_system, monitoring_system, container_system, network_system) to provide enterprise-grade file transfer functionality.

### 1.2 Goals

1. **Multi-file Transfer**: Support concurrent transfer of multiple files in a single session
2. **Chunk-based Streaming**: Enable large file transfers through configurable chunk splitting
3. **Real-time LZ4 Compression**: Per-chunk compression/decompression for increased effective throughput
4. **Reliability**: Ensure data integrity with checksums, resume capability, and error recovery
5. **Performance**: Achieve high throughput leveraging async I/O and thread pooling
6. **Observability**: Full integration with monitoring and logging systems
7. **Security**: Support encrypted transfers with TLS/SSL

### 1.3 Success Metrics

| Metric | Target |
|--------|--------|
| Throughput (1GB file, LAN) | ≥ 500 MB/s |
| Throughput (1GB file, WAN) | ≥ 100 MB/s (network limited) |
| Effective throughput with compression | 2-4x improvement for compressible data |
| LZ4 compression speed | ≥ 400 MB/s per core |
| LZ4 decompression speed | ≥ 1.5 GB/s per core |
| Compression ratio (text/logs) | 2:1 to 4:1 typical |
| Memory footprint | < 50 MB baseline |
| Resume accuracy | 100% (verified checksum) |
| Concurrent transfers | ≥ 100 simultaneous files |

---

## 2. Problem Statement

### 2.1 Current Challenges

1. **Large File Handling**: Transferring files larger than available memory requires streaming
2. **Network Instability**: Interrupted transfers should resume without re-sending entire files
3. **Bandwidth Limitations**: Network bandwidth is often the bottleneck; compression can increase effective throughput
4. **Multi-file Coordination**: Batch transfers need progress tracking and error handling per file
5. **Resource Management**: Efficient use of memory, disk I/O, and network bandwidth
6. **Cross-platform Support**: Consistent behavior across Linux, macOS, and Windows

### 2.2 Use Cases

| Use Case | Description |
|----------|-------------|
| **UC-01** | Transfer a single large file (>10GB) between two endpoints |
| **UC-02** | Transfer multiple small files as a batch operation |
| **UC-03** | Resume an interrupted transfer from the last successful chunk |
| **UC-04** | Monitor transfer progress with detailed metrics |
| **UC-05** | Secure file transfer over untrusted networks |
| **UC-06** | Prioritized transfer queue with bandwidth throttling |
| **UC-07** | Transfer compressible files (logs, text, JSON) with real-time compression |
| **UC-08** | Transfer pre-compressed files (ZIP, media) without double-compression overhead |

---

## 3. System Architecture

### 3.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           file_trans_system                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌────────────────────┐ │
│  │   Sender    │  │  Receiver   │  │  Transfer   │  │     Progress       │ │
│  │   Engine    │  │   Engine    │  │   Manager   │  │     Tracker        │ │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └─────────┬──────────┘ │
│         │                │                │                    │            │
│  ┌──────▼────────────────▼────────────────▼────────────────────▼──────────┐ │
│  │                         Chunk Manager                                   │ │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐ │ │
│  │  │ Splitter │  │Assembler │  │ Checksum │  │  Resume  │  │   LZ4    │ │ │
│  │  │          │  │          │  │          │  │ Handler  │  │Compressor│ │ │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘ │ │
│  └─────────────────────────────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────────────────────┤
│                            Integration Layer                                 │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────┐│
│  │ common   │ │ thread   │ │ logger   │ │monitoring│ │ network  │ │ LZ4  ││
│  │ _system  │ │ _system  │ │ _system  │ │ _system  │ │ _system  │ │ lib  ││
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────┘│
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Data Pipeline Architecture

The file transfer system uses a **typed_thread_pool-based pipeline architecture** from thread_system to maximize throughput through parallel processing of different pipeline stages.

#### 3.2.1 Sender Pipeline

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                           SENDER PIPELINE                                        │
│                                                                                  │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐  │
│  │  File Read   │───▶│    Chunk     │───▶│     LZ4      │───▶│   Network    │  │
│  │    Stage     │    │   Assembly   │    │  Compress    │    │    Send      │  │
│  └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘  │
│        │                   │                   │                   │            │
│        ▼                   ▼                   ▼                   ▼            │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │                    typed_thread_pool<pipeline_stage>                      │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │  │
│  │  │  IO      │  │  IO      │  │  Compute │  │  Compute │  │ Network  │   │  │
│  │  │ Worker 1 │  │ Worker 2 │  │ Worker 1 │  │ Worker 2 │  │ Worker 1 │   │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                  │
│  Stage Queues:                                                                   │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐               │
│  │ read_queue │─▶│chunk_queue │─▶│ comp_queue │─▶│ send_queue │               │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘               │
└─────────────────────────────────────────────────────────────────────────────────┘
```

#### 3.2.2 Receiver Pipeline

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                           RECEIVER PIPELINE                                      │
│                                                                                  │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐  │
│  │   Network    │───▶│     LZ4      │───▶│    Chunk     │───▶│  File Write  │  │
│  │   Receive    │    │  Decompress  │    │   Assembly   │    │    Stage     │  │
│  └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘  │
│        │                   │                   │                   │            │
│        ▼                   ▼                   ▼                   ▼            │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │                    typed_thread_pool<pipeline_stage>                      │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │  │
│  │  │ Network  │  │  Compute │  │  Compute │  │  IO      │  │  IO      │   │  │
│  │  │ Worker 1 │  │ Worker 1 │  │ Worker 2 │  │ Worker 1 │  │ Worker 2 │   │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                  │
│  Stage Queues:                                                                   │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐               │
│  │ recv_queue │─▶│decomp_queue│─▶│assem_queue │─▶│write_queue │               │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘               │
└─────────────────────────────────────────────────────────────────────────────────┘
```

#### 3.2.3 Pipeline Stage Types

Using thread_system's `typed_thread_pool`, pipeline stages are categorized by job type:

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

#### 3.2.4 Pipeline Configuration

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

    // Auto-tune based on hardware
    static auto auto_detect() -> pipeline_config;
};
```

#### 3.2.5 Pipeline Benefits

| Benefit | Description |
|---------|-------------|
| **Parallel Processing** | Each stage runs concurrently, maximizing CPU and I/O utilization |
| **Backpressure Control** | Queue sizes prevent memory exhaustion under load |
| **Stage Isolation** | I/O-bound and CPU-bound stages don't block each other |
| **Scalability** | Worker counts can be tuned per stage based on bottleneck |
| **Type-Based Scheduling** | Priority handling for real-time vs batch transfers |

### 3.3 Component Descriptions

#### 3.3.1 Sender Engine
- Reads files from disk and prepares chunks for transmission
- Applies LZ4 compression to each chunk before sending (if enabled)
- Manages send queue with priority support
- Handles flow control and backpressure
- **Uses pipeline stages**: io_read → chunk_process → compression → network

#### 3.3.2 Receiver Engine
- Accepts incoming chunks and validates integrity
- Decompresses LZ4-compressed chunks (if compression flag is set)
- Writes chunks to disk in correct order
- Handles out-of-order chunk reassembly
- **Uses pipeline stages**: network → compression → chunk_process → io_write

#### 3.3.3 Transfer Manager
- Coordinates multiple concurrent transfers
- Manages transfer lifecycle (init → transfer → verify → complete)
- Provides unified API for send/receive operations
- Controls compression settings per transfer

#### 3.3.4 Chunk Manager
- **Splitter**: Divides files into configurable chunks (default: 64KB - 1MB)
- **Assembler**: Reconstructs files from received chunks
- **Checksum**: Calculates and verifies chunk/file integrity (CRC32, SHA-256)
- **Resume Handler**: Tracks transfer state for resume capability
- **LZ4 Compressor**: Real-time per-chunk compression/decompression

#### 3.3.5 Progress Tracker
- Real-time transfer progress monitoring
- Tracks both raw and compressed bytes for accurate progress
- Reports compression ratio and effective throughput
- Integration with monitoring_system for metrics export
- Event callbacks for UI/CLI progress display

---

## 4. Functional Requirements

### 4.1 Core Features

#### FR-01: Single File Transfer
| ID | FR-01 |
|----|-------|
| **Description** | Transfer a single file from sender to receiver |
| **Priority** | P0 (Critical) |
| **Acceptance Criteria** | File transferred with 100% integrity verified by checksum |

#### FR-02: Multi-file Batch Transfer
| ID | FR-02 |
|----|-------|
| **Description** | Transfer multiple files in a single operation |
| **Priority** | P0 (Critical) |
| **Acceptance Criteria** | All files transferred, individual file status tracked |

#### FR-03: Chunk-based Transfer
| ID | FR-03 |
|----|-------|
| **Description** | Split files into chunks for streaming transfer |
| **Priority** | P0 (Critical) |
| **Acceptance Criteria** | Configurable chunk size, correct reassembly |

#### FR-04: Transfer Resume
| ID | FR-04 |
|----|-------|
| **Description** | Resume interrupted transfers from last successful chunk |
| **Priority** | P1 (High) |
| **Acceptance Criteria** | Resume within 1 second, no data loss |

#### FR-05: Progress Monitoring
| ID | FR-05 |
|----|-------|
| **Description** | Real-time progress tracking with callbacks |
| **Priority** | P1 (High) |
| **Acceptance Criteria** | Progress updates at configurable intervals |

#### FR-06: Integrity Verification
| ID | FR-06 |
|----|-------|
| **Description** | Verify data integrity using checksums |
| **Priority** | P0 (Critical) |
| **Acceptance Criteria** | CRC32 per chunk, SHA-256 per file |

#### FR-07: Concurrent Transfers
| ID | FR-07 |
|----|-------|
| **Description** | Support multiple simultaneous file transfers |
| **Priority** | P1 (High) |
| **Acceptance Criteria** | ≥100 concurrent transfers without degradation |

#### FR-08: Bandwidth Throttling
| ID | FR-08 |
|----|-------|
| **Description** | Limit transfer bandwidth per connection/total |
| **Priority** | P2 (Medium) |
| **Acceptance Criteria** | Bandwidth within 5% of configured limit |

#### FR-09: Real-time LZ4 Compression
| ID | FR-09 |
|----|-------|
| **Description** | Per-chunk LZ4 compression/decompression for increased effective throughput |
| **Priority** | P0 (Critical) |
| **Acceptance Criteria** | Compression speed ≥400 MB/s, decompression ≥1.5 GB/s, transparent to user |

#### FR-10: Adaptive Compression
| ID | FR-10 |
|----|-------|
| **Description** | Automatically skip compression for incompressible data |
| **Priority** | P1 (High) |
| **Acceptance Criteria** | Detect incompressible chunks within 1KB sample, avoid CPU waste |

#### FR-11: Compression Statistics
| ID | FR-11 |
|----|-------|
| **Description** | Report compression ratio and effective throughput |
| **Priority** | P2 (Medium) |
| **Acceptance Criteria** | Per-transfer and aggregate compression metrics available |

#### FR-12: Pipeline-based Processing
| ID | FR-12 |
|----|-------|
| **Description** | Multi-stage pipeline using typed_thread_pool for parallel processing |
| **Priority** | P0 (Critical) |
| **Acceptance Criteria** | Sender: read→chunk→compress→send, Receiver: recv→decompress→assemble→write |

#### FR-13: Pipeline Backpressure
| ID | FR-13 |
|----|-------|
| **Description** | Prevent memory exhaustion through bounded queues between pipeline stages |
| **Priority** | P1 (High) |
| **Acceptance Criteria** | Configurable queue sizes, automatic slowdown when queues are full |

### 4.2 API Requirements

#### 4.2.1 Sender API

```cpp
namespace kcenon::file_transfer {

class file_sender {
public:
    // Builder pattern for configuration
    class builder;

    // Single file transfer
    [[nodiscard]] auto send_file(
        const std::filesystem::path& file_path,
        const endpoint& destination,
        const transfer_options& options = {}
    ) -> Result<transfer_handle>;

    // Multi-file transfer
    [[nodiscard]] auto send_files(
        std::span<const std::filesystem::path> files,
        const endpoint& destination,
        const transfer_options& options = {}
    ) -> Result<batch_transfer_handle>;

    // Cancel transfer
    [[nodiscard]] auto cancel(const transfer_id& id) -> Result<void>;

    // Pause/Resume
    [[nodiscard]] auto pause(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto resume(const transfer_id& id) -> Result<void>;

    // Progress callback
    void on_progress(std::function<void(const transfer_progress&)> callback);
};

} // namespace kcenon::file_transfer
```

#### 4.2.2 Receiver API

```cpp
namespace kcenon::file_transfer {

class file_receiver {
public:
    class builder;

    // Start listening for incoming transfers
    [[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;

    // Stop receiving
    [[nodiscard]] auto stop() -> Result<void>;

    // Set output directory
    void set_output_directory(const std::filesystem::path& dir);

    // Accept/Reject callback
    void on_transfer_request(
        std::function<bool(const transfer_request&)> callback
    );

    // Progress callback
    void on_progress(std::function<void(const transfer_progress&)> callback);

    // Completion callback
    void on_complete(std::function<void(const transfer_result&)> callback);
};

} // namespace kcenon::file_transfer
```

#### 4.2.3 Transfer Manager API

```cpp
namespace kcenon::file_transfer {

class transfer_manager {
public:
    class builder;

    // Get transfer status
    [[nodiscard]] auto get_status(const transfer_id& id)
        -> Result<transfer_status>;

    // List active transfers
    [[nodiscard]] auto list_transfers()
        -> Result<std::vector<transfer_info>>;

    // Get statistics
    [[nodiscard]] auto get_statistics() -> transfer_statistics;

    // Get compression statistics
    [[nodiscard]] auto get_compression_stats() -> compression_statistics;

    // Set global bandwidth limit
    void set_bandwidth_limit(std::size_t bytes_per_second);

    // Set concurrent transfer limit
    void set_max_concurrent_transfers(std::size_t max_count);

    // Set default compression mode
    void set_default_compression(compression_mode mode);
};

} // namespace kcenon::file_transfer
```

#### 4.2.4 Compression API

```cpp
namespace kcenon::file_transfer {

// Compression modes
enum class compression_mode {
    disabled,       // No compression
    enabled,        // Always compress
    adaptive        // Auto-detect compressibility (default)
};

// Compression level (speed vs ratio trade-off)
enum class compression_level {
    fast,           // LZ4 default - fastest
    high_compression // LZ4-HC - better ratio, slower
};

// Transfer options with compression settings
struct transfer_options {
    compression_mode    compression     = compression_mode::adaptive;
    compression_level   level           = compression_level::fast;
    std::size_t         chunk_size      = 256 * 1024;  // 256KB default
    bool                verify_checksum = true;
    std::optional<std::size_t> bandwidth_limit;
};

// Compression statistics
struct compression_statistics {
    uint64_t    total_raw_bytes;
    uint64_t    total_compressed_bytes;
    double      compression_ratio;          // raw / compressed
    double      compression_speed_mbps;     // MB/s
    double      decompression_speed_mbps;   // MB/s
    uint64_t    chunks_compressed;
    uint64_t    chunks_skipped;             // Incompressible chunks
};

// Per-chunk compression interface
class chunk_compressor {
public:
    // Compress a chunk, returns compressed data or original if incompressible
    [[nodiscard]] auto compress(
        std::span<const std::byte> input
    ) -> Result<compressed_chunk>;

    // Decompress a chunk
    [[nodiscard]] auto decompress(
        std::span<const std::byte> compressed,
        std::size_t original_size
    ) -> Result<std::vector<std::byte>>;

    // Check if data is worth compressing (quick sample test)
    [[nodiscard]] auto is_compressible(
        std::span<const std::byte> sample
    ) -> bool;
};

} // namespace kcenon::file_transfer
```

#### 4.2.5 Pipeline API

```cpp
namespace kcenon::file_transfer {

// Pipeline stage types for typed_thread_pool routing
enum class pipeline_stage : uint8_t {
    io_read,        // File read operations (I/O bound)
    chunk_process,  // Chunk assembly/disassembly (CPU light)
    compression,    // LZ4 compress/decompress (CPU bound)
    network,        // Network send/receive (I/O bound)
    io_write        // File write operations (I/O bound)
};

// Pipeline worker configuration
struct pipeline_config {
    // Worker counts per stage
    std::size_t io_read_workers      = 2;
    std::size_t chunk_workers        = 2;
    std::size_t compression_workers  = 4;   // More workers for CPU-bound
    std::size_t network_workers      = 2;
    std::size_t io_write_workers     = 2;

    // Queue sizes (backpressure control)
    std::size_t read_queue_size      = 16;
    std::size_t compress_queue_size  = 32;
    std::size_t send_queue_size      = 64;
    std::size_t decompress_queue_size = 32;
    std::size_t write_queue_size     = 16;

    // Auto-tune based on hardware capabilities
    [[nodiscard]] static auto auto_detect() -> pipeline_config;
};

// Pipeline job base class
template<pipeline_stage Stage>
class pipeline_job : public thread::typed_job_t<pipeline_stage> {
public:
    explicit pipeline_job(const std::string& name = "pipeline_job")
        : typed_job_t<pipeline_stage>(Stage, name) {}
};

// Sender pipeline controller
class sender_pipeline {
public:
    class builder;

    // Start the pipeline
    [[nodiscard]] auto start(const pipeline_config& config = {}) -> Result<void>;

    // Stop the pipeline gracefully
    [[nodiscard]] auto stop(bool wait_for_completion = true) -> Result<void>;

    // Submit file for sending (enters pipeline)
    [[nodiscard]] auto submit(
        const std::filesystem::path& file,
        const endpoint& destination,
        const transfer_options& options = {}
    ) -> Result<transfer_handle>;

    // Get pipeline statistics
    [[nodiscard]] auto get_stats() const -> pipeline_statistics;

    // Get current queue depths
    [[nodiscard]] auto get_queue_depths() const -> queue_depth_info;
};

// Receiver pipeline controller
class receiver_pipeline {
public:
    class builder;

    // Start the pipeline
    [[nodiscard]] auto start(
        const endpoint& listen_addr,
        const pipeline_config& config = {}
    ) -> Result<void>;

    // Stop the pipeline gracefully
    [[nodiscard]] auto stop(bool wait_for_completion = true) -> Result<void>;

    // Set output directory
    void set_output_directory(const std::filesystem::path& dir);

    // Get pipeline statistics
    [[nodiscard]] auto get_stats() const -> pipeline_statistics;

    // Get current queue depths
    [[nodiscard]] auto get_queue_depths() const -> queue_depth_info;
};

// Pipeline statistics
struct pipeline_statistics {
    // Per-stage metrics
    struct stage_stats {
        uint64_t    jobs_processed;
        uint64_t    bytes_processed;
        double      avg_latency_us;         // Average job latency in microseconds
        double      throughput_mbps;        // MB/s
        std::size_t current_queue_depth;
        std::size_t max_queue_depth;
    };

    stage_stats io_read_stats;
    stage_stats chunk_stats;
    stage_stats compression_stats;
    stage_stats network_stats;
    stage_stats io_write_stats;

    // Overall metrics
    uint64_t    total_files_processed;
    uint64_t    total_bytes_transferred;
    double      overall_throughput_mbps;
    duration    total_elapsed_time;
};

// Queue depth information for monitoring
struct queue_depth_info {
    std::size_t read_queue;
    std::size_t chunk_queue;
    std::size_t compress_queue;
    std::size_t send_queue;
    std::size_t recv_queue;
    std::size_t decompress_queue;
    std::size_t assemble_queue;
    std::size_t write_queue;
};

} // namespace kcenon::file_transfer
```

### 4.3 Data Structures

#### 4.3.1 Chunk Structure

```cpp
// Chunk flags
enum class chunk_flags : uint8_t {
    none            = 0x00,
    first_chunk     = 0x01,     // First chunk of file
    last_chunk      = 0x02,     // Last chunk of file
    compressed      = 0x04,     // LZ4 compressed
    encrypted       = 0x08      // TLS encrypted (reserved)
};

struct chunk_header {
    transfer_id     transfer_id;        // Unique transfer identifier
    uint64_t        file_index;         // Index in batch transfer
    uint64_t        chunk_index;        // Chunk sequence number
    uint64_t        chunk_offset;       // Byte offset in original file
    uint32_t        original_size;      // Original (uncompressed) data size
    uint32_t        compressed_size;    // Compressed size (= original if not compressed)
    uint32_t        checksum;           // CRC32 of original (uncompressed) data
    chunk_flags     flags;              // Chunk flags including compression flag
};

struct chunk {
    chunk_header            header;
    std::vector<std::byte>  data;       // Possibly compressed
};

struct compressed_chunk {
    std::vector<std::byte>  data;
    uint32_t                original_size;
    bool                    is_compressed;  // false if compression was skipped
};
```

#### 4.3.2 Transfer Metadata

```cpp
struct file_metadata {
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    std::filesystem::perms  permissions;
    std::chrono::system_clock::time_point modified_time;
    bool                    compressible_hint;  // Hint from file extension
};

struct transfer_request {
    transfer_id                     id;
    std::vector<file_metadata>      files;
    transfer_options                options;
};

struct transfer_progress {
    transfer_id     id;
    uint64_t        bytes_transferred;      // Raw bytes (uncompressed)
    uint64_t        bytes_on_wire;          // Actual bytes sent (compressed)
    uint64_t        total_bytes;            // Total raw bytes
    double          transfer_rate;          // Raw bytes/second
    double          effective_rate;         // Accounting for compression
    double          compression_ratio;      // Current compression ratio
    duration        elapsed_time;
    duration        estimated_remaining;
    transfer_state  state;
};
```

---

## 5. Non-Functional Requirements

### 5.1 Performance

| Requirement | Target | Measurement |
|-------------|--------|-------------|
| **NFR-01** Throughput | ≥500 MB/s (LAN) | 1GB file transfer time |
| **NFR-02** Latency | <10ms chunk processing | End-to-end chunk latency |
| **NFR-03** Memory | <50MB baseline | RSS during idle |
| **NFR-04** Memory (transfer) | <100MB per 1GB transfer | RSS during transfer |
| **NFR-05** CPU utilization | <30% per core | During sustained transfer |
| **NFR-06** LZ4 compression | ≥400 MB/s | Compression throughput |
| **NFR-07** LZ4 decompression | ≥1.5 GB/s | Decompression throughput |
| **NFR-08** Compression ratio | 2:1 to 4:1 for text | Typical compressible data |
| **NFR-09** Adaptive detection | <100μs | Compressibility check time |

### 5.2 Reliability

| Requirement | Target |
|-------------|--------|
| **NFR-10** Data integrity | 100% (SHA-256 verified) |
| **NFR-11** Resume accuracy | 100% successful resume |
| **NFR-12** Error recovery | Auto-retry with exponential backoff |
| **NFR-13** Graceful degradation | Reduced throughput under load |
| **NFR-14** Compression fallback | Seamless fallback on decompression error |

### 5.3 Security

| Requirement | Description |
|-------------|-------------|
| **NFR-15** Encryption | TLS 1.3 for network transfer |
| **NFR-16** Authentication | Optional certificate-based auth |
| **NFR-17** Path traversal | Prevent directory escape attacks |
| **NFR-18** Resource limits | Max file size, transfer count limits |

### 5.4 Compatibility

| Requirement | Description |
|-------------|-------------|
| **NFR-19** C++ Standard | C++20 or later |
| **NFR-20** Platforms | Linux, macOS, Windows |
| **NFR-21** Compilers | GCC 11+, Clang 14+, MSVC 19.29+ |
| **NFR-22** LZ4 library | LZ4 1.9.0+ (BSD license) |

---

## 6. Integration Requirements

### 6.1 System Dependencies

| System | Usage | Required |
|--------|-------|----------|
| **common_system** | Result<T>, interfaces, error codes | Yes |
| **thread_system** | Async task execution, thread pool | Yes |
| **network_system** | TCP/TLS (Phase 1) and QUIC (Phase 2) transport | Yes |
| **container_system** | Chunk serialization | Yes |
| **LZ4** | Real-time compression/decompression | Yes |
| **logger_system** | Diagnostic logging | Optional |
| **monitoring_system** | Metrics and tracing | Optional |

> **Note**: network_system provides both TCP and QUIC transport, so no separate external QUIC library is required.

### 6.2 Transport Protocol Design

#### 6.2.1 Protocol Selection Rationale

**HTTP is explicitly excluded** from this system for the following reasons:
- Unnecessary abstraction layer for streaming file transfer
- High header overhead (~800 bytes per request) unsuitable for high-frequency chunk transmission
- Stateless design conflicts with connection-based resume capability
- HTTP chunked encoding semantics differ from our chunk-based transfer model

**Supported Transport Protocols:**

| Protocol | Phase | Priority | Use Case |
|----------|-------|----------|----------|
| **TCP + TLS 1.3** | Phase 1 | Primary | All environments, default |
| **QUIC** | Phase 2 | Optional | High-loss networks, mobile |

#### 6.2.2 Custom Application Protocol

A lightweight custom protocol is used on top of TCP/QUIC for minimal overhead:

```cpp
// Message types (1 byte)
enum class message_type : uint8_t {
    // Session management
    handshake_request   = 0x01,
    handshake_response  = 0x02,

    // Transfer control
    transfer_request    = 0x10,
    transfer_accept     = 0x11,
    transfer_reject     = 0x12,
    transfer_cancel     = 0x13,

    // Data transfer
    chunk_data          = 0x20,
    chunk_ack           = 0x21,
    chunk_nack          = 0x22,  // Retransmission request

    // Resume
    resume_request      = 0x30,
    resume_response     = 0x31,

    // Completion
    transfer_complete   = 0x40,
    transfer_verify     = 0x41,

    // Control
    keepalive           = 0xF0,
    error               = 0xFF
};

// Message frame (5 bytes overhead)
struct message_frame {
    message_type    type;           // 1 byte
    uint32_t        payload_length; // 4 bytes (big-endian)
    // payload follows...
};
```

**Overhead Comparison (1GB file, 256KB chunks = 4,096 chunks):**

| Protocol | Per-Chunk Overhead | Total Overhead | Percentage |
|----------|-------------------|----------------|------------|
| HTTP/1.1 | ~800 bytes | ~3.2 MB | 0.31% |
| Custom/TCP | 54 bytes | ~221 KB | 0.02% |
| Custom/QUIC | ~74 bytes | ~303 KB | 0.03% |

#### 6.2.3 TCP Transport (Phase 1 - Required)

**Advantages:**
- 40+ years of proven reliability
- Kernel-level optimization on all platforms
- Already supported by network_system
- Firewall-friendly (port 443 with TLS)

**Configuration:**
```cpp
struct tcp_transport_config {
    bool        enable_tls      = true;     // TLS 1.3
    bool        tcp_nodelay     = true;     // Disable Nagle's algorithm
    std::size_t send_buffer     = 256 * 1024;
    std::size_t recv_buffer     = 256 * 1024;
    duration    connect_timeout = 10s;
    duration    read_timeout    = 30s;
};
```

#### 6.2.4 QUIC Transport (Phase 2 - Optional)

**Advantages:**
- 0-RTT connection resumption
- No head-of-line blocking (stream multiplexing)
- Connection migration (survives IP changes)
- Built-in encryption (TLS 1.3)

**When to use QUIC:**
- High packet loss environments (>0.5%)
- Mobile networks with frequent handoffs
- Multi-file transfers benefiting from multiplexing

**Implementation**: Uses network_system's QUIC library (no external dependency required)

```cpp
struct quic_transport_config {
    bool        enable_0rtt         = true;
    std::size_t max_streams         = 100;      // Per connection
    std::size_t initial_window      = 10 * 1024 * 1024;
    duration    idle_timeout        = 30s;
    bool        enable_migration    = true;
};
```

#### 6.2.5 Transport Abstraction Layer

```cpp
// Abstract interface for transport implementations
class transport_interface {
public:
    virtual ~transport_interface() = default;

    [[nodiscard]] virtual auto connect(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto disconnect() -> Result<void> = 0;
    [[nodiscard]] virtual auto send(std::span<const std::byte> data) -> Result<void> = 0;
    [[nodiscard]] virtual auto receive(std::span<std::byte> buffer) -> Result<std::size_t> = 0;
    [[nodiscard]] virtual auto is_connected() const -> bool = 0;

    // QUIC-specific (no-op for TCP)
    [[nodiscard]] virtual auto create_stream() -> Result<stream_id> { return stream_id{0}; }
    [[nodiscard]] virtual auto close_stream(stream_id) -> Result<void> { return {}; }
};

// Factory for creating transport instances
[[nodiscard]] auto create_transport(transport_type type) -> std::unique_ptr<transport_interface>;
```

### 6.3 LZ4 Library Integration

```cpp
// LZ4 integration wrapper
namespace kcenon::file_transfer::compression {

class lz4_engine {
public:
    // Standard LZ4 compression (fast)
    [[nodiscard]] static auto compress(
        std::span<const std::byte> input,
        std::span<std::byte> output
    ) -> Result<std::size_t>;

    // LZ4-HC compression (high compression ratio)
    [[nodiscard]] static auto compress_hc(
        std::span<const std::byte> input,
        std::span<std::byte> output,
        int compression_level = 9
    ) -> Result<std::size_t>;

    // Decompression (same for both modes)
    [[nodiscard]] static auto decompress(
        std::span<const std::byte> compressed,
        std::span<std::byte> output,
        std::size_t original_size
    ) -> Result<std::size_t>;

    // Calculate maximum compressed size for buffer allocation
    [[nodiscard]] static auto max_compressed_size(
        std::size_t input_size
    ) -> std::size_t;
};

} // namespace kcenon::file_transfer::compression
```

### 6.4 Error Code Range

Following the ecosystem convention, file_trans_system reserves error codes in range **-700 to -799**:

| Range | Category |
|-------|----------|
| -700 to -719 | Transfer errors (init, cancel, timeout) |
| -720 to -739 | Chunk errors (checksum, sequence, size) |
| -740 to -759 | File I/O errors (read, write, permission) |
| -760 to -779 | Resume errors (state, corruption) |
| -780 to -789 | Compression errors (compress, decompress, invalid) |
| -790 to -799 | Configuration errors |

### 6.5 Interface Implementation

```cpp
// Implements IExecutor for transfer task scheduling
class transfer_executor : public common::IExecutor {
    // Uses thread_system internally
};

// Optional IMonitor integration
class transfer_monitor : public common::IMonitor {
    // Exports metrics including compression stats to monitoring_system
};
```

---

## 7. Directory Structure

```
file_trans_system/
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── PRD.md
│   ├── PRD_KR.md
│   ├── API.md
│   └── ARCHITECTURE.md
├── include/
│   └── kcenon/
│       └── file_transfer/
│           ├── file_transfer.h           # Main header
│           ├── core/
│           │   ├── file_sender.h
│           │   ├── file_receiver.h
│           │   ├── transfer_manager.h
│           │   ├── chunk_manager.h
│           │   └── error_codes.h
│           ├── chunk/
│           │   ├── chunk.h
│           │   ├── chunk_splitter.h
│           │   ├── chunk_assembler.h
│           │   └── checksum.h
│           ├── compression/
│           │   ├── lz4_engine.h
│           │   ├── chunk_compressor.h
│           │   ├── adaptive_compression.h
│           │   └── compression_stats.h
│           ├── transport/
│           │   ├── transport_interface.h
│           │   ├── transport_factory.h
│           │   ├── tcp_transport.h
│           │   ├── quic_transport.h      # Phase 2
│           │   └── protocol_messages.h
│           ├── resume/
│           │   ├── resume_handler.h
│           │   └── transfer_state.h
│           ├── adapters/
│           │   └── common_adapter.h
│           ├── di/
│           │   └── file_transfer_module.h
│           └── metrics/
│               └── transfer_metrics.h
├── src/
│   ├── core/
│   ├── chunk/
│   ├── compression/
│   ├── transport/
│   ├── resume/
│   └── adapters/
├── tests/
│   ├── unit/
│   │   ├── chunk_test.cpp
│   │   ├── compression_test.cpp
│   │   └── ...
│   ├── integration/
│   └── benchmark/
│       ├── compression_benchmark.cpp
│       └── transfer_benchmark.cpp
└── examples/
    ├── simple_send/
    ├── simple_receive/
    ├── compressed_transfer/
    └── batch_transfer/
```

---

## 8. Development Phases

### Phase 1: Core Infrastructure (2-3 weeks)
- [ ] Project setup with CMake
- [ ] LZ4 library integration
- [ ] Chunk data structures and serialization
- [ ] Basic chunk splitter/assembler
- [ ] CRC32 checksum implementation
- [ ] Unit tests for core components

### Phase 2: LZ4 Compression Engine (1-2 weeks)
- [ ] LZ4 compression wrapper
- [ ] LZ4-HC support for high compression mode
- [ ] Adaptive compression detection
- [ ] Compression statistics tracking
- [ ] Compression unit tests and benchmarks

### Phase 3: Transfer Engine (2-3 weeks)
- [ ] Transport abstraction layer
- [ ] TCP transport implementation (via network_system)
- [ ] Custom protocol message handling
- [ ] File sender with compression support
- [ ] File receiver with decompression support
- [ ] Basic transfer manager
- [ ] Integration tests

### Phase 4: Reliability Features (2 weeks)
- [ ] Resume handler implementation
- [ ] Transfer state persistence
- [ ] SHA-256 file verification
- [ ] Error recovery and retry logic
- [ ] Compression error handling

### Phase 5: Advanced Features (2 weeks)
- [ ] Multi-file batch transfer
- [ ] Concurrent transfer support
- [ ] Bandwidth throttling
- [ ] Progress tracking with compression metrics

### Phase 6: Integration & Polish (1-2 weeks)
- [ ] logger_system integration
- [ ] monitoring_system integration
- [ ] Performance benchmarks
- [ ] Documentation and examples

### Phase 7: QUIC Transport (Optional, 2-3 weeks)
- [ ] network_system QUIC library integration
- [ ] QUIC transport implementation
- [ ] 0-RTT connection resumption
- [ ] Connection migration support
- [ ] Multi-stream file transfer
- [ ] QUIC-specific benchmarks
- [ ] Fallback mechanism (QUIC → TCP)

---

## 9. Risks and Mitigations

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Large file memory exhaustion | High | Medium | Streaming with fixed buffer pools |
| Network instability | Medium | High | Robust retry and resume logic |
| Chunk ordering complexity | Medium | Medium | Sequence numbers and validation |
| Cross-platform file permissions | Low | Medium | Abstract permission model |
| Performance bottlenecks | Medium | Medium | Early benchmarking, profiling |
| LZ4 compression overhead | Low | Low | Adaptive compression, skip incompressible |
| Compression ratio variability | Low | Medium | Report actual ratios, don't guarantee |

---

## 10. Success Criteria

### 10.1 Functional Completeness
- [ ] All P0 requirements implemented and tested
- [ ] All P1 requirements implemented and tested
- [ ] LZ4 compression fully integrated
- [ ] API documentation complete

### 10.2 Quality Gates
- [ ] ≥80% code coverage
- [ ] Zero ThreadSanitizer warnings
- [ ] Zero AddressSanitizer leaks
- [ ] All integration tests passing

### 10.3 Performance Validation
- [ ] Throughput targets met (verified by benchmark)
- [ ] Compression speed targets met (≥400 MB/s)
- [ ] Decompression speed targets met (≥1.5 GB/s)
- [ ] Memory targets met (verified by profiling)
- [ ] Resume functionality validated

---

## Appendix A: Glossary

| Term | Definition |
|------|------------|
| **Chunk** | A fixed-size segment of a file for streaming transfer |
| **Transfer** | A single file or batch of files being sent/received |
| **Resume** | Continuing an interrupted transfer from the last successful point |
| **Checksum** | A hash value used to verify data integrity |
| **Backpressure** | Flow control mechanism to prevent buffer overflow |
| **LZ4** | A fast lossless compression algorithm optimized for speed |
| **Adaptive Compression** | Automatic detection and skipping of incompressible data |
| **Compression Ratio** | Original size divided by compressed size (higher is better) |

---

## Appendix B: LZ4 Compression Details

### B.1 Why LZ4?

| Algorithm | Compress Speed | Decompress Speed | Ratio |
|-----------|----------------|------------------|-------|
| **LZ4** | ~500 MB/s | ~2 GB/s | 2.1:1 |
| LZ4-HC | ~50 MB/s | ~2 GB/s | 2.7:1 |
| zstd | ~400 MB/s | ~1 GB/s | 2.9:1 |
| gzip | ~30 MB/s | ~300 MB/s | 2.7:1 |
| snappy | ~400 MB/s | ~800 MB/s | 1.8:1 |

LZ4 offers the best balance of:
- **Speed**: Near memory bandwidth compression/decompression
- **Simplicity**: Single-header library, minimal dependencies
- **License**: BSD license (commercially friendly)
- **Maturity**: Battle-tested in production (Linux kernel, ZFS, etc.)

### B.2 Adaptive Compression Strategy

```cpp
// Pseudo-code for adaptive compression decision
bool should_compress(span<byte> chunk) {
    // Sample first 1KB of chunk
    auto sample = chunk.first(min(1024, chunk.size()));

    // Try compressing sample
    auto compressed = lz4_compress(sample);

    // Only compress if we get at least 10% reduction
    return compressed.size() < sample.size() * 0.9;
}
```

### B.3 File Type Heuristics

| File Type | Compression | Reason |
|-----------|-------------|--------|
| .txt, .log, .json, .xml | Yes | Highly compressible text |
| .csv, .html, .css, .js | Yes | Text-based formats |
| .cpp, .h, .py, .java | Yes | Source code |
| .zip, .gz, .tar.gz | No | Already compressed |
| .jpg, .png, .mp4 | No | Already compressed media |
| .exe, .dll, .so | Maybe | Binary, may have some gain |

---

## Appendix C: References

### Internal Documentation
- [common_system Documentation](../../../common_system/README.md)
- [thread_system Documentation](../../../thread_system/README.md)
- [network_system Documentation](../../../network_system/README.md) - Provides TCP and QUIC transport
- [container_system Documentation](../../../container_system/README.md)

### Compression
- [LZ4 Official Repository](https://github.com/lz4/lz4)
- [LZ4 Frame Format Specification](https://github.com/lz4/lz4/blob/dev/doc/lz4_Frame_format.md)

### Transport Protocols
- [RFC 9000 - QUIC: A UDP-Based Multiplexed and Secure Transport](https://tools.ietf.org/html/rfc9000)
- [RFC 9001 - Using TLS to Secure QUIC](https://tools.ietf.org/html/rfc9001)
- [RFC 9002 - QUIC Loss Detection and Congestion Control](https://tools.ietf.org/html/rfc9002)

### Other References
- [RFC 7233 - HTTP Range Requests](https://tools.ietf.org/html/rfc7233) (for reference only, HTTP not used)
