# File Transfer System - Software Requirements Specification

## Document Information

| Item | Description |
|------|-------------|
| **Project Name** | file_trans_system |
| **Document Type** | Software Requirements Specification (SRS) |
| **Version** | 1.1.0 |
| **Status** | Draft |
| **Created** | 2025-12-11 |
| **Author** | kcenon@naver.com |
| **Related Documents** | PRD.md v1.0.0 |

---

## 1. Introduction

### 1.1 Purpose

This Software Requirements Specification (SRS) document provides a complete and detailed description of the software requirements for the **file_trans_system** library. It serves as the primary reference for technical implementation, testing, and validation activities.

This document is intended for:
- Software developers implementing the system
- QA engineers designing test cases
- System architects validating the design
- Project managers tracking implementation progress

### 1.2 Scope

The file_trans_system is a C++20 library providing:
- High-performance file transfer capabilities
- Chunk-based streaming for large files
- Real-time LZ4 compression/decompression
- Transfer resume functionality
- Multi-file batch operations
- Integration with the existing ecosystem (common_system, thread_system, network_system, etc.)

### 1.3 Definitions, Acronyms, and Abbreviations

| Term | Definition |
|------|------------|
| **Chunk** | A fixed-size segment of a file for streaming transfer |
| **LZ4** | A fast lossless compression algorithm |
| **CRC32** | 32-bit Cyclic Redundancy Check for integrity |
| **SHA-256** | Secure Hash Algorithm 256-bit for file verification |
| **Backpressure** | Flow control mechanism to prevent buffer overflow |
| **Pipeline** | Multi-stage processing architecture |
| **TLS** | Transport Layer Security for encryption |

### 1.4 References

| Document | Description |
|----------|-------------|
| PRD.md | Product Requirements Document for file_trans_system |
| common_system/README.md | Common system interfaces and Result<T> |
| thread_system/README.md | Thread pool and async execution |
| network_system/README.md | TCP/TLS transport layer |
| LZ4 Documentation | https://github.com/lz4/lz4 |

### 1.5 Document Overview

- **Section 2**: Overall system description and context
- **Section 3**: Specific software requirements with PRD traceability
- **Section 4**: Interface requirements
- **Section 5**: Performance requirements
- **Section 6**: Design constraints
- **Section 7**: Quality attributes

---

## 2. Overall Description

### 2.1 Product Perspective

The file_trans_system operates as a library component within a larger ecosystem:

```
┌─────────────────────────────────────────────────────────────────────┐
│                      Application Layer                               │
├─────────────────────────────────────────────────────────────────────┤
│                     file_trans_system                                │
│  ┌──────────────┐  ┌───────────────┐  ┌──────────────────────────┐ │
│  │ Sender       │  │ Receiver      │  │ Transfer Manager         │ │
│  │ Pipeline     │  │ Pipeline      │  │                          │ │
│  └──────┬───────┘  └───────┬───────┘  └────────────┬─────────────┘ │
│         │                  │                        │               │
│  ┌──────▼──────────────────▼────────────────────────▼─────────────┐ │
│  │                    Chunk Manager                                │ │
│  │  ┌─────────┐ ┌──────────┐ ┌─────────┐ ┌──────────┐ ┌────────┐ │ │
│  │  │Splitter │ │Assembler │ │Checksum │ │ Resume   │ │  LZ4   │ │ │
│  │  └─────────┘ └──────────┘ └─────────┘ └──────────┘ └────────┘ │ │
│  └─────────────────────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────────────┤
│  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────────────┐│
│  │ common     │ │ thread     │ │ network    │ │ container          ││
│  │ _system    │ │ _system    │ │ _system    │ │ _system            ││
│  └────────────┘ └────────────┘ └────────────┘ └────────────────────┘│
└─────────────────────────────────────────────────────────────────────┘
```

### 2.2 Product Functions Summary

| Function | Description | PRD Reference |
|----------|-------------|---------------|
| Single File Transfer | Transfer one file with integrity verification | FR-01 |
| Batch File Transfer | Transfer multiple files in one operation | FR-02 |
| Chunk-based Streaming | Split files into chunks for transfer | FR-03 |
| Transfer Resume | Continue interrupted transfers | FR-04 |
| Progress Monitoring | Real-time progress callbacks | FR-05 |
| Integrity Verification | CRC32/SHA-256 checksums | FR-06 |
| Concurrent Transfers | Multiple simultaneous transfers | FR-07 |
| LZ4 Compression | Per-chunk compression/decompression | FR-09, FR-10 |
| Pipeline Processing | Multi-stage parallel processing | FR-12, FR-13 |

### 2.3 User Characteristics

| User Type | Description | Technical Level |
|-----------|-------------|-----------------|
| Library Integrator | Developers integrating file_trans_system | Advanced C++ |
| System Administrator | Configures and monitors transfers | Intermediate |
| End User | Uses applications built on file_trans_system | Basic |

### 2.4 Constraints

| Constraint | Description |
|------------|-------------|
| **Language** | C++20 standard required |
| **Platform** | Linux, macOS, Windows |
| **Compiler** | GCC 11+, Clang 14+, MSVC 19.29+ |
| **Dependencies** | common_system, thread_system, network_system, container_system, LZ4 |
| **License** | Must be compatible with BSD (LZ4) |

### 2.5 Assumptions and Dependencies

| ID | Assumption/Dependency |
|----|----------------------|
| A-01 | Network connectivity is available between sender and receiver |
| A-02 | File system has sufficient space for transferred files |
| A-03 | LZ4 library version 1.9.0 or higher is available |
| A-04 | thread_system provides typed_thread_pool functionality |
| A-05 | network_system supports TCP and TLS 1.3 connections |

---

## 3. Specific Requirements

### 3.1 Functional Requirements

#### 3.1.1 File Transfer Core (SRS-CORE)

##### SRS-CORE-001: Single File Send
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CORE-001 |
| **PRD Trace** | FR-01 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL provide capability to send a single file from sender to receiver |

**Inputs:**
- File path (std::filesystem::path)
- Destination endpoint (IP address, port)
- Transfer options (optional)

**Processing:**
1. Validate file exists and is readable
2. Calculate SHA-256 hash of entire file
3. Split file into chunks of configurable size
4. Calculate CRC32 for each chunk
5. Apply LZ4 compression if enabled
6. Transmit chunks via network_system
7. Wait for receiver acknowledgment

**Outputs:**
- Result<transfer_handle> with unique transfer identifier
- Error result on failure with specific error code

**Acceptance Criteria:**
- AC-001-1: File transferred with 100% data integrity (SHA-256 verified)
- AC-001-2: Transfer completes within performance targets
- AC-001-3: Progress callbacks invoked during transfer

---

##### SRS-CORE-002: Single File Receive
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CORE-002 |
| **PRD Trace** | FR-01 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL provide capability to receive a single file from sender |

**Inputs:**
- Listen endpoint (IP address, port)
- Output directory path
- Accept/reject callback (optional)

**Processing:**
1. Listen for incoming connections
2. Receive transfer request with file metadata
3. Invoke accept/reject callback if registered
4. Receive chunks in sequence
5. Verify CRC32 for each chunk
6. Decompress LZ4 if compression flag set
7. Write chunks to output file
8. Verify SHA-256 hash of completed file

**Outputs:**
- transfer_result with file path and verification status
- Error on integrity failure or timeout

**Acceptance Criteria:**
- AC-002-1: Received file matches original SHA-256 hash
- AC-002-2: Corrupted chunks detected and reported
- AC-002-3: Completion callback invoked on success

---

##### SRS-CORE-003: Multi-file Batch Transfer
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CORE-003 |
| **PRD Trace** | FR-02 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL support transferring multiple files in a single batch operation |

**Inputs:**
- Vector of file paths (std::span<const std::filesystem::path>)
- Destination endpoint
- Transfer options

**Processing:**
1. Validate all files exist and are readable
2. Create batch transfer session
3. Transfer files sequentially or concurrently (configurable)
4. Track individual file progress
5. Handle partial failures (continue with remaining files)

**Outputs:**
- Result<batch_transfer_handle>
- Individual status per file

**Acceptance Criteria:**
- AC-003-1: All files transferred with individual status tracking
- AC-003-2: Batch progress includes per-file breakdown
- AC-003-3: Partial failures do not abort entire batch

---

#### 3.1.2 Chunk Management (SRS-CHUNK)

##### SRS-CHUNK-001: File Splitting
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CHUNK-001 |
| **PRD Trace** | FR-03 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL split files into fixed-size chunks for streaming transfer |

**Specification:**
```cpp
struct chunk_config {
    std::size_t chunk_size = 256 * 1024;  // Default: 256KB
    std::size_t min_chunk_size = 64 * 1024;   // Minimum: 64KB
    std::size_t max_chunk_size = 1024 * 1024; // Maximum: 1MB
};
```

**Processing:**
1. Read file in chunk_size blocks
2. Assign sequential chunk_index to each chunk
3. Calculate chunk_offset (byte position in file)
4. Set first_chunk flag on first chunk
5. Set last_chunk flag on final chunk
6. Calculate CRC32 of chunk data

**Acceptance Criteria:**
- AC-CHUNK-001-1: File correctly reconstructed from chunks
- AC-CHUNK-001-2: Chunk sizes within configured bounds
- AC-CHUNK-001-3: Last chunk may be smaller than chunk_size

---

##### SRS-CHUNK-002: File Assembly
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CHUNK-002 |
| **PRD Trace** | FR-03 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL reassemble files from received chunks in correct order |

**Processing:**
1. Receive chunks (may arrive out of order)
2. Buffer chunks until sequential write is possible
3. Write chunks to file at correct offset
4. Handle duplicate chunks (idempotent)
5. Detect missing chunks
6. Complete assembly when last_chunk received and all gaps filled

**Acceptance Criteria:**
- AC-CHUNK-002-1: Out-of-order chunks correctly assembled
- AC-CHUNK-002-2: Missing chunks detected within timeout
- AC-CHUNK-002-3: Duplicate chunks handled without error

---

##### SRS-CHUNK-003: Chunk Checksum Verification
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CHUNK-003 |
| **PRD Trace** | FR-06 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL verify integrity of each chunk using CRC32 |

**Specification:**
```cpp
// CRC32 calculated on ORIGINAL (uncompressed) data
uint32_t calculate_crc32(std::span<const std::byte> data);
bool verify_crc32(std::span<const std::byte> data, uint32_t expected);
```

**Acceptance Criteria:**
- AC-CHUNK-003-1: All chunks verified before processing
- AC-CHUNK-003-2: Corrupted chunks rejected with error code -721
- AC-CHUNK-003-3: CRC32 verification adds < 1% overhead

---

##### SRS-CHUNK-004: File Hash Verification
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CHUNK-004 |
| **PRD Trace** | FR-06 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL verify integrity of complete files using SHA-256 |

**Specification:**
```cpp
// SHA-256 of entire file (original, uncompressed)
std::string calculate_sha256(const std::filesystem::path& file);
bool verify_sha256(const std::filesystem::path& file, const std::string& expected);
```

**Acceptance Criteria:**
- AC-CHUNK-004-1: SHA-256 calculated before send, verified after receive
- AC-CHUNK-004-2: Hash mismatch returns error code -722
- AC-CHUNK-004-3: Hash included in transfer_result

---

#### 3.1.3 Compression (SRS-COMP)

##### SRS-COMP-001: LZ4 Compression
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-COMP-001 |
| **PRD Trace** | FR-09 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL compress chunks using LZ4 algorithm |

**Specification:**
```cpp
class lz4_engine {
public:
    // Standard LZ4 compression
    [[nodiscard]] static auto compress(
        std::span<const std::byte> input,
        std::span<std::byte> output
    ) -> Result<std::size_t>;

    // LZ4-HC for higher compression ratio
    [[nodiscard]] static auto compress_hc(
        std::span<const std::byte> input,
        std::span<std::byte> output,
        int level = 9  // 1-12, default 9
    ) -> Result<std::size_t>;
};
```

**Performance Targets:**
| Mode | Compression Speed | Decompression Speed | Ratio |
|------|-------------------|---------------------|-------|
| LZ4 (fast) | ≥ 400 MB/s | ≥ 1.5 GB/s | ~2.1:1 |
| LZ4-HC | ≥ 50 MB/s | ≥ 1.5 GB/s | ~2.7:1 |

**Acceptance Criteria:**
- AC-COMP-001-1: Compression speed ≥ 400 MB/s (fast mode)
- AC-COMP-001-2: Decompression speed ≥ 1.5 GB/s
- AC-COMP-001-3: Compressed data decompresses to original exactly

---

##### SRS-COMP-002: LZ4 Decompression
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-COMP-002 |
| **PRD Trace** | FR-09 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL decompress LZ4-compressed chunks |

**Specification:**
```cpp
[[nodiscard]] static auto decompress(
    std::span<const std::byte> compressed,
    std::span<std::byte> output,
    std::size_t original_size
) -> Result<std::size_t>;
```

**Error Handling:**
- Invalid compressed data: error code -781
- Output buffer too small: error code -782
- Corrupted stream: error code -783

**Acceptance Criteria:**
- AC-COMP-002-1: Valid compressed data decompresses correctly
- AC-COMP-002-2: Invalid data returns appropriate error
- AC-COMP-002-3: original_size matches decompressed output

---

##### SRS-COMP-003: Adaptive Compression Detection
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-COMP-003 |
| **PRD Trace** | FR-10 |
| **Priority** | P1 (High) |
| **Description** | The system SHALL automatically detect incompressible data and skip compression |

**Algorithm:**
```cpp
bool is_compressible(std::span<const std::byte> data) {
    // Sample first 1KB (or less for small chunks)
    const auto sample_size = std::min(data.size(), 1024uz);
    auto sample = data.first(sample_size);

    // Try compressing sample
    auto compressed = lz4_compress(sample);

    // Only compress if >= 10% reduction
    return compressed.size() < sample.size() * 0.9;
}
```

**Acceptance Criteria:**
- AC-COMP-003-1: Detection completes in < 100μs
- AC-COMP-003-2: Already-compressed files (zip, jpg) detected as incompressible
- AC-COMP-003-3: Text/log files detected as compressible

---

##### SRS-COMP-004: Compression Mode Configuration
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-COMP-004 |
| **PRD Trace** | FR-09, FR-10 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL support configurable compression modes |

**Specification:**
```cpp
enum class compression_mode {
    disabled,   // No compression applied
    enabled,    // Always compress
    adaptive    // Auto-detect compressibility (default)
};

enum class compression_level {
    fast,            // LZ4 standard (default)
    high_compression // LZ4-HC
};
```

**Acceptance Criteria:**
- AC-COMP-004-1: disabled mode transmits raw data
- AC-COMP-004-2: enabled mode always compresses
- AC-COMP-004-3: adaptive mode uses is_compressible() check

---

##### SRS-COMP-005: Compression Statistics
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-COMP-005 |
| **PRD Trace** | FR-11 |
| **Priority** | P2 (Medium) |
| **Description** | The system SHALL track and report compression statistics |

**Specification:**
```cpp
struct compression_statistics {
    uint64_t total_raw_bytes;        // Original data size
    uint64_t total_compressed_bytes; // Compressed data size
    double   compression_ratio;      // raw / compressed
    double   compression_speed_mbps;
    double   decompression_speed_mbps;
    uint64_t chunks_compressed;      // Chunks that were compressed
    uint64_t chunks_skipped;         // Chunks where compression skipped
};
```

**Acceptance Criteria:**
- AC-COMP-005-1: Statistics available via get_compression_stats()
- AC-COMP-005-2: Per-transfer and aggregate statistics provided
- AC-COMP-005-3: Statistics updated in real-time during transfer

---

#### 3.1.4 Pipeline Processing (SRS-PIPE)

##### SRS-PIPE-001: Sender Pipeline
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-PIPE-001 |
| **PRD Trace** | FR-12 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL implement a multi-stage sender pipeline |

**Pipeline Stages:**
```
File Read → Chunk Assembly → LZ4 Compress → Network Send
(io_read)   (chunk_process)  (compression)   (network)
```

**Stage Configuration:**
| Stage | Type | Default Workers |
|-------|------|-----------------|
| io_read | I/O bound | 2 |
| chunk_process | CPU light | 2 |
| compression | CPU bound | 4 |
| network | I/O bound | 2 |

**Acceptance Criteria:**
- AC-PIPE-001-1: All stages execute concurrently
- AC-PIPE-001-2: Data flows through stages in order
- AC-PIPE-001-3: Stage worker counts are configurable

---

##### SRS-PIPE-002: Receiver Pipeline
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-PIPE-002 |
| **PRD Trace** | FR-12 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL implement a multi-stage receiver pipeline |

**Pipeline Stages:**
```
Network Recv → LZ4 Decompress → Chunk Assembly → File Write
(network)      (compression)    (chunk_process)  (io_write)
```

**Stage Configuration:**
| Stage | Type | Default Workers |
|-------|------|-----------------|
| network | I/O bound | 2 |
| compression | CPU bound | 4 |
| chunk_process | CPU light | 2 |
| io_write | I/O bound | 2 |

**Acceptance Criteria:**
- AC-PIPE-002-1: All stages execute concurrently
- AC-PIPE-002-2: Out-of-order chunks handled correctly
- AC-PIPE-002-3: Stage worker counts are configurable

---

##### SRS-PIPE-003: Pipeline Backpressure
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-PIPE-003 |
| **PRD Trace** | FR-13 |
| **Priority** | P1 (High) |
| **Description** | The system SHALL implement backpressure between pipeline stages |

**Queue Configuration:**
```cpp
struct pipeline_config {
    // Queue sizes (items, not bytes)
    std::size_t read_queue_size      = 16;
    std::size_t compress_queue_size  = 32;
    std::size_t send_queue_size      = 64;
    std::size_t decompress_queue_size = 32;
    std::size_t write_queue_size     = 16;
};
```

**Behavior:**
- When queue is full, upstream stage blocks
- When queue is empty, downstream stage waits
- Memory bounded by queue_size × chunk_size

**Acceptance Criteria:**
- AC-PIPE-003-1: Memory usage bounded regardless of file size
- AC-PIPE-003-2: Slow stages cause upstream blocking
- AC-PIPE-003-3: Queue depths available for monitoring

---

##### SRS-PIPE-004: Pipeline Statistics
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-PIPE-004 |
| **PRD Trace** | FR-12 |
| **Priority** | P1 (High) |
| **Description** | The system SHALL provide pipeline performance statistics |

**Specification:**
```cpp
struct pipeline_statistics {
    struct stage_stats {
        uint64_t    jobs_processed;
        uint64_t    bytes_processed;
        double      avg_latency_us;
        double      throughput_mbps;
        std::size_t current_queue_depth;
        std::size_t max_queue_depth;
    };

    stage_stats io_read_stats;
    stage_stats chunk_stats;
    stage_stats compression_stats;
    stage_stats network_stats;
    stage_stats io_write_stats;

    pipeline_stage bottleneck_stage;  // Identified bottleneck
};
```

**Acceptance Criteria:**
- AC-PIPE-004-1: Per-stage statistics available
- AC-PIPE-004-2: Bottleneck stage automatically identified
- AC-PIPE-004-3: Statistics updated in real-time

---

#### 3.1.5 Transfer Resume (SRS-RESUME)

##### SRS-RESUME-001: Transfer State Persistence
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-RESUME-001 |
| **PRD Trace** | FR-04 |
| **Priority** | P1 (High) |
| **Description** | The system SHALL persist transfer state for resume capability |

**State Data:**
```cpp
struct transfer_state {
    transfer_id         id;
    std::string         file_path;
    uint64_t            file_size;
    std::string         sha256_hash;
    uint64_t            chunks_completed;
    uint64_t            chunks_total;
    std::vector<bool>   chunk_bitmap;      // Received chunks
    compression_mode    compression;
    std::chrono::system_clock::time_point last_update;
};
```

**Acceptance Criteria:**
- AC-RESUME-001-1: State persisted after each chunk
- AC-RESUME-001-2: State recoverable after process restart
- AC-RESUME-001-3: State file < 1MB for any transfer size

---

##### SRS-RESUME-002: Transfer Resume
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-RESUME-002 |
| **PRD Trace** | FR-04 |
| **Priority** | P1 (High) |
| **Description** | The system SHALL resume interrupted transfers from last checkpoint |

**Processing:**
1. Load transfer_state from persistence
2. Validate file still exists (sender) or partial file valid (receiver)
3. Calculate missing chunks from chunk_bitmap
4. Resume sending/receiving only missing chunks
5. Verify complete file SHA-256 after all chunks received

**Acceptance Criteria:**
- AC-RESUME-002-1: Resume starts within 1 second
- AC-RESUME-002-2: No data loss or corruption on resume
- AC-RESUME-002-3: Resume works after network disconnection

---

#### 3.1.6 Progress Monitoring (SRS-PROGRESS)

##### SRS-PROGRESS-001: Progress Callbacks
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-PROGRESS-001 |
| **PRD Trace** | FR-05 |
| **Priority** | P1 (High) |
| **Description** | The system SHALL provide real-time progress callbacks |

**Specification:**
```cpp
struct transfer_progress {
    transfer_id     id;
    uint64_t        bytes_transferred;      // Raw bytes
    uint64_t        bytes_on_wire;          // Compressed bytes
    uint64_t        total_bytes;
    double          transfer_rate;          // Bytes/second
    double          effective_rate;         // With compression
    double          compression_ratio;
    duration        elapsed_time;
    duration        estimated_remaining;
    transfer_state  state;
};

void on_progress(std::function<void(const transfer_progress&)> callback);
```

**Acceptance Criteria:**
- AC-PROGRESS-001-1: Callbacks invoked at configurable intervals
- AC-PROGRESS-001-2: Progress includes compression metrics
- AC-PROGRESS-001-3: Callback does not block transfer

---

##### SRS-PROGRESS-002: Transfer States
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-PROGRESS-002 |
| **PRD Trace** | FR-05 |
| **Priority** | P1 (High) |
| **Description** | The system SHALL track transfer lifecycle states |

**State Machine:**
```
pending → initializing → transferring → verifying → completed
                ↓              ↓
            failed ←──────────┘
                ↑
          cancelled
```

**Acceptance Criteria:**
- AC-PROGRESS-002-1: All state transitions reported via callback
- AC-PROGRESS-002-2: Error state includes error code and message
- AC-PROGRESS-002-3: Final state always reported (completed/failed/cancelled)

---

#### 3.1.7 Concurrent Transfers (SRS-CONCURRENT)

##### SRS-CONCURRENT-001: Multiple Simultaneous Transfers
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CONCURRENT-001 |
| **PRD Trace** | FR-07 |
| **Priority** | P1 (High) |
| **Description** | The system SHALL support multiple concurrent file transfers |

**Configuration:**
```cpp
void set_max_concurrent_transfers(std::size_t max_count);
// Default: 100
```

**Acceptance Criteria:**
- AC-CONCURRENT-001-1: ≥100 simultaneous transfers supported
- AC-CONCURRENT-001-2: Each transfer has independent progress tracking
- AC-CONCURRENT-001-3: Transfers share thread pool efficiently

---

##### SRS-CONCURRENT-002: Bandwidth Throttling
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CONCURRENT-002 |
| **PRD Trace** | FR-08 |
| **Priority** | P2 (Medium) |
| **Description** | The system SHALL support bandwidth limiting |

**Specification:**
```cpp
void set_bandwidth_limit(std::size_t bytes_per_second);
// 0 = unlimited (default)

// Per-transfer limit
struct transfer_options {
    std::optional<std::size_t> bandwidth_limit;
};
```

**Acceptance Criteria:**
- AC-CONCURRENT-002-1: Actual bandwidth within 5% of limit
- AC-CONCURRENT-002-2: Global and per-transfer limits supported
- AC-CONCURRENT-002-3: Limit changes take effect immediately

---

#### 3.1.8 Transport Layer (SRS-TRANS)

##### SRS-TRANS-001: Transport Abstraction
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-TRANS-001 |
| **PRD Trace** | Section 6.2 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL provide an abstract transport layer supporting multiple protocols |

**Specification:**
```cpp
enum class transport_type {
    tcp,    // TCP + TLS 1.3 (default, Phase 1)
    quic    // QUIC (optional, Phase 2)
};

class transport_interface {
public:
    [[nodiscard]] virtual auto connect(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto disconnect() -> Result<void> = 0;
    [[nodiscard]] virtual auto send(std::span<const std::byte> data) -> Result<void> = 0;
    [[nodiscard]] virtual auto receive(std::span<std::byte> buffer) -> Result<std::size_t> = 0;
    [[nodiscard]] virtual auto is_connected() const -> bool = 0;
};
```

**Acceptance Criteria:**
- AC-TRANS-001-1: Transport abstraction allows protocol switching without API changes
- AC-TRANS-001-2: TCP transport fully functional in Phase 1
- AC-TRANS-001-3: QUIC transport available as optional in Phase 2

---

##### SRS-TRANS-002: TCP Transport
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-TRANS-002 |
| **PRD Trace** | Section 6.2.3 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL implement TCP transport via network_system |

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

**Acceptance Criteria:**
- AC-TRANS-002-1: TCP transport supports TLS 1.3 encryption
- AC-TRANS-002-2: TCP_NODELAY enabled by default for low latency
- AC-TRANS-002-3: Configurable buffer sizes and timeouts

---

##### SRS-TRANS-003: QUIC Transport (Phase 2)
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-TRANS-003 |
| **PRD Trace** | Section 6.2.4 |
| **Priority** | P2 (Medium) |
| **Description** | The system SHALL implement QUIC transport via network_system |

**Configuration:**
```cpp
struct quic_transport_config {
    bool        enable_0rtt         = true;
    std::size_t max_streams         = 100;
    std::size_t initial_window      = 10 * 1024 * 1024;
    duration    idle_timeout        = 30s;
    bool        enable_migration    = true;
};
```

**QUIC Benefits:**
- 0-RTT connection resumption
- No head-of-line blocking
- Connection migration (survives IP changes)
- Built-in TLS 1.3

**Acceptance Criteria:**
- AC-TRANS-003-1: 0-RTT connection resumption functional
- AC-TRANS-003-2: Multi-stream support for concurrent chunk transfer
- AC-TRANS-003-3: Connection migration works across network changes

---

##### SRS-TRANS-004: Protocol Fallback
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-TRANS-004 |
| **PRD Trace** | Phase 7 |
| **Priority** | P2 (Medium) |
| **Description** | The system SHALL support automatic fallback from QUIC to TCP |

**Processing:**
1. Attempt QUIC connection if configured
2. If QUIC fails (UDP blocked, timeout), fallback to TCP
3. Log fallback event for diagnostics
4. Continue transfer without interruption

**Acceptance Criteria:**
- AC-TRANS-004-1: Automatic fallback within 5 seconds of QUIC failure
- AC-TRANS-004-2: Transfer continues without data loss
- AC-TRANS-004-3: Fallback event logged for diagnostics

---

### 3.2 Data Requirements

#### 3.2.1 Chunk Data Structure

```cpp
enum class chunk_flags : uint8_t {
    none            = 0x00,
    first_chunk     = 0x01,     // First chunk of file
    last_chunk      = 0x02,     // Last chunk of file
    compressed      = 0x04,     // LZ4 compressed
    encrypted       = 0x08      // Reserved for TLS
};

struct chunk_header {
    transfer_id     transfer_id;        // 16 bytes UUID
    uint64_t        file_index;         // File index in batch
    uint64_t        chunk_index;        // Chunk sequence number
    uint64_t        chunk_offset;       // Byte offset in file
    uint32_t        original_size;      // Uncompressed size
    uint32_t        compressed_size;    // Compressed size
    uint32_t        checksum;           // CRC32 of original data
    chunk_flags     flags;              // Chunk flags
    // Total header size: 49 bytes + padding
};

struct chunk {
    chunk_header            header;
    std::vector<std::byte>  data;   // Compressed or raw data
};
```

#### 3.2.2 Transfer Metadata

```cpp
struct file_metadata {
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;        // 64 hex chars
    std::filesystem::perms  permissions;
    std::chrono::system_clock::time_point modified_time;
    bool                    compressible_hint;  // From extension
};

struct transfer_request {
    transfer_id                     id;
    std::vector<file_metadata>      files;
    transfer_options                options;
};

struct transfer_result {
    transfer_id             id;
    std::filesystem::path   output_path;
    uint64_t                bytes_transferred;
    uint64_t                bytes_on_wire;
    bool                    verified;           // SHA-256 match
    std::optional<error>    error;
    duration                elapsed_time;
};
```

---

## 4. Interface Requirements

### 4.1 User Interfaces

Not applicable - this is a library component without GUI.

### 4.2 Software Interfaces

#### 4.2.1 Sender Interface

```cpp
namespace kcenon::file_transfer {

class file_sender {
public:
    class builder {
    public:
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_compression(compression_mode mode);
        builder& with_compression_level(compression_level level);
        builder& with_chunk_size(std::size_t size);
        builder& with_bandwidth_limit(std::size_t bytes_per_second);
        [[nodiscard]] auto build() -> Result<file_sender>;
    };

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

    // Control operations
    [[nodiscard]] auto cancel(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto pause(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto resume(const transfer_id& id) -> Result<void>;

    // Progress callback
    void on_progress(std::function<void(const transfer_progress&)> callback);
};

} // namespace kcenon::file_transfer
```

#### 4.2.2 Receiver Interface

```cpp
namespace kcenon::file_transfer {

class file_receiver {
public:
    class builder {
    public:
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_output_directory(const std::filesystem::path& dir);
        builder& with_bandwidth_limit(std::size_t bytes_per_second);
        [[nodiscard]] auto build() -> Result<file_receiver>;
    };

    // Lifecycle
    [[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;
    [[nodiscard]] auto stop() -> Result<void>;

    // Configuration
    void set_output_directory(const std::filesystem::path& dir);

    // Callbacks
    void on_transfer_request(
        std::function<bool(const transfer_request&)> callback
    );
    void on_progress(std::function<void(const transfer_progress&)> callback);
    void on_complete(std::function<void(const transfer_result&)> callback);
};

} // namespace kcenon::file_transfer
```

#### 4.2.3 Transfer Manager Interface

```cpp
namespace kcenon::file_transfer {

class transfer_manager {
public:
    class builder {
    public:
        builder& with_max_concurrent(std::size_t max_count);
        builder& with_default_compression(compression_mode mode);
        builder& with_global_bandwidth_limit(std::size_t bytes_per_second);
        [[nodiscard]] auto build() -> Result<transfer_manager>;
    };

    // Status queries
    [[nodiscard]] auto get_status(const transfer_id& id)
        -> Result<transfer_status>;
    [[nodiscard]] auto list_transfers()
        -> Result<std::vector<transfer_info>>;

    // Statistics
    [[nodiscard]] auto get_statistics() -> transfer_statistics;
    [[nodiscard]] auto get_compression_stats() -> compression_statistics;

    // Configuration
    void set_bandwidth_limit(std::size_t bytes_per_second);
    void set_max_concurrent_transfers(std::size_t max_count);
    void set_default_compression(compression_mode mode);
};

} // namespace kcenon::file_transfer
```

### 4.3 Hardware Interfaces

Not applicable - the library abstracts hardware through OS APIs.

### 4.4 Communication Interfaces

#### 4.4.1 Network Protocol

**HTTP is explicitly excluded** from this system for the following reasons:
- Unnecessary abstraction layer for streaming file transfer
- High header overhead (~800 bytes per request) unsuitable for high-frequency chunk transmission
- Stateless design conflicts with connection-based resume capability

**Supported Transport Protocols:**

| Layer | Protocol | Phase | Description |
|-------|----------|-------|-------------|
| Transport (Primary) | TCP + TLS 1.3 | Phase 1 | Default, all environments |
| Transport (Optional) | QUIC | Phase 2 | High-loss networks, mobile |
| Application | Custom chunk-based protocol | - | Minimal overhead (54 bytes/chunk) |

> **Note**: Both TCP and QUIC are provided by network_system. No external transport library is required.

#### 4.4.2 Transport Abstraction

```cpp
// Transport type selection
enum class transport_type {
    tcp,    // TCP + TLS 1.3 (default)
    quic    // QUIC (optional, Phase 2)
};

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

#### 4.4.3 Message Format

```
┌──────────────────────────────────────────────────────────────┐
│                    Transfer Protocol                          │
├──────────────────────────────────────────────────────────────┤
│ Message Type (1 byte)                                        │
│   0x01 = HANDSHAKE_REQUEST                                   │
│   0x02 = HANDSHAKE_RESPONSE                                  │
│   0x10 = TRANSFER_REQUEST                                    │
│   0x11 = TRANSFER_ACCEPT                                     │
│   0x12 = TRANSFER_REJECT                                     │
│   0x13 = TRANSFER_CANCEL                                     │
│   0x20 = CHUNK_DATA                                          │
│   0x21 = CHUNK_ACK                                           │
│   0x22 = CHUNK_NACK (retransmission request)                 │
│   0x30 = RESUME_REQUEST                                      │
│   0x31 = RESUME_RESPONSE                                     │
│   0x40 = TRANSFER_COMPLETE                                   │
│   0x41 = TRANSFER_VERIFY                                     │
│   0xF0 = KEEPALIVE                                           │
│   0xFF = ERROR                                               │
├──────────────────────────────────────────────────────────────┤
│ Payload Length (4 bytes, big-endian)                         │
├──────────────────────────────────────────────────────────────┤
│ Payload (variable length)                                    │
│   - TRANSFER_REQUEST: serialized transfer_request            │
│   - CHUNK_DATA: chunk_header + data                          │
│   - etc.                                                     │
└──────────────────────────────────────────────────────────────┘

Total frame overhead: 5 bytes (vs HTTP ~800 bytes)
```

#### 4.4.4 Protocol Overhead Comparison

| Protocol | Per-Chunk Overhead | Total (1GB, 256KB chunks) | Percentage |
|----------|-------------------|---------------------------|------------|
| HTTP/1.1 | ~800 bytes | ~3.2 MB | 0.31% |
| Custom/TCP | 54 bytes | ~221 KB | 0.02% |
| Custom/QUIC | ~74 bytes | ~303 KB | 0.03% |

---

## 5. Performance Requirements

### 5.1 Throughput Requirements

| ID | Requirement | Target | Measurement | PRD Trace |
|----|-------------|--------|-------------|-----------|
| PERF-001 | LAN throughput (1GB file) | ≥ 500 MB/s | Transfer time | NFR-01 |
| PERF-002 | WAN throughput | ≥ 100 MB/s | Network limited | NFR-01 |
| PERF-003 | LZ4 compression speed | ≥ 400 MB/s | Per core | NFR-06 |
| PERF-004 | LZ4 decompression speed | ≥ 1.5 GB/s | Per core | NFR-07 |
| PERF-005 | Effective throughput (compressible) | 2-4x baseline | With compression | NFR-08 |

### 5.2 Latency Requirements

| ID | Requirement | Target | PRD Trace |
|----|-------------|--------|-----------|
| PERF-010 | Chunk processing latency | < 10 ms | NFR-02 |
| PERF-011 | Compressibility detection | < 100 μs | NFR-09 |
| PERF-012 | Resume start time | < 1 second | FR-04 |

### 5.3 Resource Requirements

| ID | Requirement | Target | PRD Trace |
|----|-------------|--------|-----------|
| PERF-020 | Baseline memory | < 50 MB | NFR-03 |
| PERF-021 | Per-transfer memory | < 100 MB / 1GB | NFR-04 |
| PERF-022 | CPU utilization | < 30% per core | NFR-05 |
| PERF-023 | Concurrent transfers | ≥ 100 | FR-07 |

### 5.4 Capacity Requirements

| ID | Requirement | Target |
|----|-------------|--------|
| PERF-030 | Maximum file size | Limited by filesystem (tested to 100GB) |
| PERF-031 | Maximum batch size | 10,000 files |
| PERF-032 | Maximum chunk size | 1 MB |
| PERF-033 | Minimum chunk size | 64 KB |

---

## 6. Design Constraints

### 6.1 Language and Standards

| Constraint | Requirement |
|------------|-------------|
| Language | C++20 |
| Standard Library | Full C++20 support required |
| Features Used | std::span, std::filesystem, coroutines (optional) |

### 6.2 Platform Support

| Platform | Minimum Version | Compiler |
|----------|-----------------|----------|
| Linux | Kernel 4.x | GCC 11+, Clang 14+ |
| macOS | 11.0+ | Apple Clang 14+ |
| Windows | 10/Server 2019 | MSVC 19.29+ |

### 6.3 External Dependencies

| Dependency | Version | Usage | License |
|------------|---------|-------|---------|
| common_system | Latest | Result<T>, interfaces | Project |
| thread_system | Latest | typed_thread_pool | Project |
| network_system | Latest | TCP/TLS (Phase 1) and QUIC (Phase 2) transport | Project |
| container_system | Latest | Serialization | Project |
| LZ4 | 1.9.0+ | Compression | BSD |

> **Note**: network_system provides both TCP and QUIC transport implementations. No external transport library is required.

### 6.4 Error Code Allocation

Following ecosystem conventions, file_trans_system uses error codes **-700 to -799**:

| Range | Category |
|-------|----------|
| -700 to -719 | Transfer errors |
| -720 to -739 | Chunk errors |
| -740 to -759 | File I/O errors |
| -760 to -779 | Resume errors |
| -780 to -789 | Compression errors |
| -790 to -799 | Configuration errors |

**Detailed Error Codes:**

| Code | Name | Description |
|------|------|-------------|
| -700 | transfer_init_failed | Failed to initialize transfer |
| -701 | transfer_cancelled | Transfer cancelled by user |
| -702 | transfer_timeout | Transfer timed out |
| -703 | transfer_rejected | Transfer rejected by receiver |
| -720 | chunk_checksum_error | Chunk CRC32 verification failed |
| -721 | chunk_sequence_error | Chunk received out of sequence |
| -722 | chunk_size_error | Chunk size exceeds maximum |
| -723 | file_hash_mismatch | SHA-256 verification failed |
| -740 | file_read_error | Failed to read source file |
| -741 | file_write_error | Failed to write destination file |
| -742 | file_permission_error | Insufficient file permissions |
| -743 | file_not_found | Source file not found |
| -760 | resume_state_invalid | Resume state corrupted |
| -761 | resume_file_changed | File modified since last transfer |
| -780 | compression_failed | LZ4 compression failed |
| -781 | decompression_failed | LZ4 decompression failed |
| -782 | compression_buffer_error | Output buffer too small |
| -790 | config_invalid | Invalid configuration parameter |

---

## 7. Quality Attributes

### 7.1 Reliability

| ID | Requirement | Target | PRD Trace |
|----|-------------|--------|-----------|
| REL-001 | Data integrity | 100% (SHA-256 verified) | NFR-10 |
| REL-002 | Resume success rate | 100% | NFR-11 |
| REL-003 | Error recovery | Auto-retry with exponential backoff | NFR-12 |
| REL-004 | Graceful degradation | Reduced throughput under load | NFR-13 |
| REL-005 | Compression fallback | Seamless on decompression error | NFR-14 |

### 7.2 Security

| ID | Requirement | Description | PRD Trace |
|----|-------------|-------------|-----------|
| SEC-001 | Encryption | TLS 1.3 for network transfer | NFR-15 |
| SEC-002 | Authentication | Optional certificate-based | NFR-16 |
| SEC-003 | Path traversal prevention | Validate output paths | NFR-17 |
| SEC-004 | Resource limits | Max file size, transfer count | NFR-18 |

### 7.3 Maintainability

| ID | Requirement | Target |
|----|-------------|--------|
| MAINT-001 | Code coverage | ≥ 80% |
| MAINT-002 | Documentation | API docs for all public interfaces |
| MAINT-003 | Coding standard | Follow C++ Core Guidelines |

### 7.4 Testability

| ID | Requirement | Description |
|----|-------------|-------------|
| TEST-001 | Unit tests | All components independently testable |
| TEST-002 | Integration tests | End-to-end transfer scenarios |
| TEST-003 | Benchmark tests | Performance regression detection |
| TEST-004 | Sanitizer clean | No TSan/ASan warnings |

---

## 8. Traceability Matrix

### 8.1 PRD to SRS Traceability

| PRD ID | PRD Description | SRS Requirements |
|--------|-----------------|------------------|
| FR-01 | Single File Transfer | SRS-CORE-001, SRS-CORE-002 |
| FR-02 | Multi-file Batch Transfer | SRS-CORE-003 |
| FR-03 | Chunk-based Transfer | SRS-CHUNK-001, SRS-CHUNK-002 |
| FR-04 | Transfer Resume | SRS-RESUME-001, SRS-RESUME-002 |
| FR-05 | Progress Monitoring | SRS-PROGRESS-001, SRS-PROGRESS-002 |
| FR-06 | Integrity Verification | SRS-CHUNK-003, SRS-CHUNK-004 |
| FR-07 | Concurrent Transfers | SRS-CONCURRENT-001 |
| FR-08 | Bandwidth Throttling | SRS-CONCURRENT-002 |
| FR-09 | Real-time LZ4 Compression | SRS-COMP-001, SRS-COMP-002, SRS-COMP-004 |
| FR-10 | Adaptive Compression | SRS-COMP-003 |
| FR-11 | Compression Statistics | SRS-COMP-005 |
| FR-12 | Pipeline-based Processing | SRS-PIPE-001, SRS-PIPE-002, SRS-PIPE-004 |
| FR-13 | Pipeline Backpressure | SRS-PIPE-003 |
| NFR-01 | Throughput | PERF-001, PERF-002 |
| NFR-02 | Latency | PERF-010 |
| NFR-03 | Memory (baseline) | PERF-020 |
| NFR-04 | Memory (transfer) | PERF-021 |
| NFR-05 | CPU utilization | PERF-022 |
| NFR-06 | LZ4 compression speed | PERF-003 |
| NFR-07 | LZ4 decompression speed | PERF-004 |
| NFR-08 | Compression ratio | PERF-005 |
| NFR-09 | Adaptive detection speed | PERF-011 |
| NFR-10 | Data integrity | REL-001 |
| NFR-11 | Resume accuracy | REL-002 |
| NFR-12 | Error recovery | REL-003 |
| NFR-13 | Graceful degradation | REL-004 |
| NFR-14 | Compression fallback | REL-005 |
| NFR-15 | Encryption | SEC-001 |
| NFR-16 | Authentication | SEC-002 |
| NFR-17 | Path traversal | SEC-003 |
| NFR-18 | Resource limits | SEC-004 |

### 8.2 Use Case to SRS Traceability

| Use Case | Description | SRS Requirements |
|----------|-------------|------------------|
| UC-01 | Large file transfer (>10GB) | SRS-CORE-001, SRS-CHUNK-001, SRS-PIPE-001 |
| UC-02 | Batch small files | SRS-CORE-003 |
| UC-03 | Resume interrupted transfer | SRS-RESUME-001, SRS-RESUME-002 |
| UC-04 | Monitor progress | SRS-PROGRESS-001, SRS-PROGRESS-002 |
| UC-05 | Secure transfer | SEC-001, SEC-002 |
| UC-06 | Prioritized queue | SRS-CONCURRENT-001, SRS-CONCURRENT-002 |
| UC-07 | Compress compressible files | SRS-COMP-001, SRS-COMP-003 |
| UC-08 | Skip compression for compressed files | SRS-COMP-003, SRS-COMP-004 |

---

## Appendix A: Acceptance Test Cases

### A.1 Core Transfer Tests

| Test ID | SRS Trace | Description | Expected Result |
|---------|-----------|-------------|-----------------|
| TC-001 | SRS-CORE-001 | Send 1GB file over LAN | Transfer completes, SHA-256 matches |
| TC-002 | SRS-CORE-002 | Receive 1GB file | File received, SHA-256 verified |
| TC-003 | SRS-CORE-003 | Send 100 files batch | All files transferred with status |
| TC-004 | SRS-CHUNK-003 | Corrupt chunk in transit | Corruption detected, transfer retried |
| TC-005 | SRS-CHUNK-004 | Modify file during transfer | SHA-256 mismatch detected |

### A.2 Compression Tests

| Test ID | SRS Trace | Description | Expected Result |
|---------|-----------|-------------|-----------------|
| TC-010 | SRS-COMP-001 | Compress text file | Compression ratio ≥ 2:1 |
| TC-011 | SRS-COMP-002 | Decompress chunk | Original data restored exactly |
| TC-012 | SRS-COMP-003 | Transfer ZIP file (adaptive) | Compression skipped |
| TC-013 | SRS-COMP-003 | Transfer text file (adaptive) | Compression applied |
| TC-014 | SRS-COMP-005 | Check compression stats | Accurate ratio and speed reported |

### A.3 Pipeline Tests

| Test ID | SRS Trace | Description | Expected Result |
|---------|-----------|-------------|-----------------|
| TC-020 | SRS-PIPE-001 | Verify parallel stages | All stages executing concurrently |
| TC-021 | SRS-PIPE-003 | Slow receiver backpressure | Sender slows down, memory bounded |
| TC-022 | SRS-PIPE-004 | Get pipeline statistics | Per-stage metrics available |

### A.4 Resume Tests

| Test ID | SRS Trace | Description | Expected Result |
|---------|-----------|-------------|-----------------|
| TC-030 | SRS-RESUME-001 | Kill transfer at 50% | State persisted |
| TC-031 | SRS-RESUME-002 | Resume killed transfer | Completes from 50%, SHA-256 OK |
| TC-032 | SRS-RESUME-002 | Resume after network failure | Completes without data loss |

### A.5 Performance Tests

| Test ID | SRS Trace | Description | Expected Result |
|---------|-----------|-------------|-----------------|
| TC-040 | PERF-001 | 1GB LAN transfer | ≥ 500 MB/s throughput |
| TC-041 | PERF-003 | LZ4 compression benchmark | ≥ 400 MB/s |
| TC-042 | PERF-004 | LZ4 decompression benchmark | ≥ 1.5 GB/s |
| TC-043 | PERF-020 | Memory baseline | < 50 MB RSS |
| TC-044 | PERF-023 | 100 concurrent transfers | All complete without errors |

---

## Appendix B: Glossary

| Term | Definition |
|------|------------|
| **Chunk** | A fixed-size segment of a file for streaming transfer |
| **Pipeline** | Multi-stage processing architecture with concurrent stages |
| **Backpressure** | Flow control mechanism preventing buffer overflow |
| **LZ4** | Fast lossless compression algorithm |
| **CRC32** | 32-bit checksum for chunk integrity |
| **SHA-256** | 256-bit hash for file integrity |
| **Transfer Handle** | Opaque identifier for managing a transfer |
| **Adaptive Compression** | Automatic detection and skipping of incompressible data |
| **typed_thread_pool** | Thread pool with type-based job routing from thread_system |

---

## Appendix C: Revision History

| Version | Date | Author | Description |
|---------|------|--------|-------------|
| 1.0.0 | 2025-12-11 | kcenon@naver.com | Initial SRS creation |
| 1.1.0 | 2025-12-11 | kcenon@naver.com | Added TCP/QUIC transport layer requirements (SRS-TRANS), documented HTTP exclusion rationale |

---

*End of Document*
