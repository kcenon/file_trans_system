# File Transfer System - Software Requirements Specification

## Document Information

| Item | Description |
|------|-------------|
| **Project Name** | file_trans_system |
| **Document Type** | Software Requirements Specification (SRS) |
| **Version** | 0.2.0 |
| **Status** | Draft |
| **Created** | 2025-12-11 |
| **Author** | kcenon@naver.com |
| **Related Documents** | PRD.md v0.2.0 |

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

The file_trans_system is a C++20 library implementing a **client-server architecture** providing:
- Central server with file storage repository
- Client connections for upload, download, and file listing
- High-performance bidirectional file transfer
- Chunk-based streaming for large files
- Real-time LZ4 compression/decompression
- Transfer resume functionality
- Multi-file batch operations
- Integration with the existing ecosystem (common_system, thread_system, network_system, etc.)

### 1.3 Definitions, Acronyms, and Abbreviations

| Term | Definition |
|------|------------|
| **Server** | Central component that stores files and handles client requests |
| **Client** | Component that connects to server for file operations |
| **Upload** | Transfer of file from client to server |
| **Download** | Transfer of file from server to client |
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
| PRD.md v0.2.0 | Product Requirements Document for file_trans_system |
| common_system/README.md | Common system interfaces and Result<T> |
| thread_system/README.md | Thread pool and async execution |
| network_system/README.md | TCP/TLS and QUIC transport layer |
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

The file_trans_system operates as a library component within a larger ecosystem, implementing a **client-server architecture**:

```
┌─────────────────────────────────────────────────────────────────────┐
│                      Application Layer                               │
├─────────────────────────────────────────────────────────────────────┤
│                     file_trans_system                                │
│                                                                      │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │                   file_transfer_server                         │  │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────────────────┐  │  │
│  │  │   Server    │ │   Storage   │ │   Client Manager        │  │  │
│  │  │   Handler   │ │   Manager   │ │                         │  │  │
│  │  └──────┬──────┘ └──────┬──────┘ └───────────┬─────────────┘  │  │
│  │         └───────────────┼───────────────────┘                 │  │
│  │                   ┌─────▼─────┐                                │  │
│  │                   │  Storage  │                                │  │
│  │                   │ /data/files/                               │  │
│  │                   └───────────┘                                │  │
│  └───────────────────────────────────────────────────────────────┘  │
│                              ▲                                       │
│                              │ TCP/TLS or QUIC                      │
│                              ▼                                       │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │                   file_transfer_client                         │  │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────────────────┐  │  │
│  │  │   Upload    │ │  Download   │ │     List Handler        │  │  │
│  │  │   Engine    │ │   Engine    │ │                         │  │  │
│  │  └─────────────┘ └─────────────┘ └─────────────────────────┘  │  │
│  └───────────────────────────────────────────────────────────────┘  │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                      Chunk Manager                            │   │
│  │  ┌─────────┐ ┌──────────┐ ┌─────────┐ ┌────────┐ ┌────────┐ │   │
│  │  │Splitter │ │Assembler │ │Checksum │ │ Resume │ │  LZ4   │ │   │
│  │  └─────────┘ └──────────┘ └─────────┘ └────────┘ └────────┘ │   │
│  └──────────────────────────────────────────────────────────────┘   │
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
| Server Startup/Shutdown | Start and gracefully stop server | FR-01 |
| Client Connection | Connect clients to server | FR-02 |
| File Upload (Single) | Client uploads one file to server | FR-03 |
| File Upload (Batch) | Client uploads multiple files | FR-04 |
| File Download (Single) | Client downloads one file from server | FR-05 |
| File Download (Batch) | Client downloads multiple files | FR-06 |
| File Listing | Query server for available files | FR-07 |
| Chunk-based Streaming | Split files into chunks for transfer | FR-08 |
| Transfer Resume | Continue interrupted transfers | FR-09 |
| Progress Monitoring | Real-time progress callbacks | FR-10 |
| Integrity Verification | CRC32/SHA-256 checksums | FR-11 |
| Concurrent Transfers | Multiple simultaneous transfers | FR-12 |
| LZ4 Compression | Per-chunk compression/decompression | FR-14, FR-15 |
| Storage Management | Server file storage management | FR-16 |
| Pipeline Processing | Multi-stage parallel processing | FR-17, FR-18 |

### 2.3 User Characteristics

| User Type | Description | Technical Level |
|-----------|-------------|-----------------|
| Library Integrator | Developers integrating file_trans_system | Advanced C++ |
| Server Administrator | Configures and monitors server | Intermediate |
| Client Developer | Develops client applications | Intermediate C++ |
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
| A-01 | Network connectivity is available between clients and server |
| A-02 | Server file system has sufficient space for storage |
| A-03 | LZ4 library version 1.9.0 or higher is available |
| A-04 | thread_system provides typed_thread_pool functionality |
| A-05 | network_system supports TCP, TLS 1.3, and QUIC connections |

---

## 3. Specific Requirements

### 3.1 Functional Requirements

#### 3.1.1 Server Core (SRS-SERVER)

##### SRS-SERVER-001: Server Initialization
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-SERVER-001 |
| **PRD Trace** | FR-01 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL initialize file_transfer_server with configurable storage directory and settings |

**Inputs:**
- Storage directory path (std::filesystem::path)
- Pipeline configuration (optional)
- Maximum connections (optional)
- Maximum file size (optional)
- Storage quota (optional)

**Processing:**
1. Validate storage directory exists and is writable
2. Initialize storage manager
3. Initialize pipeline workers
4. Prepare client connection manager

**Outputs:**
- Result<file_transfer_server> on success
- Error result with specific error code on failure

**Acceptance Criteria:**
- AC-SERVER-001-1: Server initializes with valid configuration
- AC-SERVER-001-2: Invalid storage path returns error code -743
- AC-SERVER-001-3: Storage quota is enforced if specified

---

##### SRS-SERVER-002: Server Start
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-SERVER-002 |
| **PRD Trace** | FR-01 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL start listening for client connections on specified endpoint |

**Inputs:**
- Listen endpoint (IP address, port)

**Processing:**
1. Bind to specified endpoint
2. Start accepting connections
3. Initialize client manager

**Outputs:**
- Result<void> on success
- Error result on failure (port in use, permission denied)

**Acceptance Criteria:**
- AC-SERVER-002-1: Server starts listening on specified port
- AC-SERVER-002-2: Multiple clients can connect simultaneously
- AC-SERVER-002-3: is_running() returns true after successful start

---

##### SRS-SERVER-003: Server Stop
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-SERVER-003 |
| **PRD Trace** | FR-01 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL gracefully stop the server, completing or aborting active transfers |

**Processing:**
1. Stop accepting new connections
2. Notify active clients of shutdown
3. Wait for active transfers to complete (with timeout)
4. Close all connections
5. Flush pending writes to storage

**Acceptance Criteria:**
- AC-SERVER-003-1: Active transfers complete if within grace period
- AC-SERVER-003-2: All resources properly released
- AC-SERVER-003-3: is_running() returns false after stop

---

##### SRS-SERVER-004: Upload Request Handling
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-SERVER-004 |
| **PRD Trace** | FR-03, FR-04 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL handle upload requests from clients |

**Inputs:**
- upload_request with file metadata
- Registered callback (optional)

**Processing:**
1. Receive UPLOAD_REQUEST from client
2. Validate filename (no path traversal)
3. Check storage quota
4. Invoke on_upload_request callback if registered
5. Send UPLOAD_ACCEPT or UPLOAD_REJECT
6. If accepted, prepare to receive chunks

**Acceptance Criteria:**
- AC-SERVER-004-1: Valid uploads are accepted
- AC-SERVER-004-2: Quota exceeded returns error -745
- AC-SERVER-004-3: Invalid filename returns error -748
- AC-SERVER-004-4: Callback can reject specific uploads

---

##### SRS-SERVER-005: Download Request Handling
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-SERVER-005 |
| **PRD Trace** | FR-05, FR-06 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL handle download requests from clients |

**Inputs:**
- download_request with filename and options
- Registered callback (optional)

**Processing:**
1. Receive DOWNLOAD_REQUEST from client
2. Validate filename exists in storage
3. Invoke on_download_request callback if registered
4. Send DOWNLOAD_ACCEPT with file metadata or DOWNLOAD_REJECT
5. If accepted, begin sending chunks

**Acceptance Criteria:**
- AC-SERVER-005-1: Existing files are downloadable
- AC-SERVER-005-2: Missing file returns error -746
- AC-SERVER-005-3: Callback can reject specific downloads
- AC-SERVER-005-4: File metadata included in accept response

---

##### SRS-SERVER-006: List Request Handling
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-SERVER-006 |
| **PRD Trace** | FR-07 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL handle file list requests from clients |

**Inputs:**
- list_options with filter, sort, pagination

**Processing:**
1. Receive LIST_REQUEST from client
2. Scan storage directory
3. Apply filter pattern (glob)
4. Apply sort order
5. Apply pagination (offset, limit)
6. Send LIST_RESPONSE with file_info array

**Acceptance Criteria:**
- AC-SERVER-006-1: Returns all files when no filter specified
- AC-SERVER-006-2: Filter pattern correctly matches files
- AC-SERVER-006-3: Pagination works correctly
- AC-SERVER-006-4: Response time < 100ms for 10,000 files

---

#### 3.1.2 Client Core (SRS-CLIENT)

##### SRS-CLIENT-001: Client Connection
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CLIENT-001 |
| **PRD Trace** | FR-02 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL connect client to server |

**Inputs:**
- Server endpoint (IP address, port)

**Processing:**
1. Establish network connection (TCP/TLS or QUIC)
2. Perform handshake
3. Verify server identity (TLS)

**Outputs:**
- Result<void> on success
- Error result on failure

**Acceptance Criteria:**
- AC-CLIENT-001-1: Connection established with valid server
- AC-CLIENT-001-2: Connection refused error when server unavailable
- AC-CLIENT-001-3: is_connected() returns true after connect

---

##### SRS-CLIENT-002: Auto-reconnect
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CLIENT-002 |
| **PRD Trace** | FR-02 |
| **Priority** | P1 (High) |
| **Description** | The system SHALL automatically reconnect on connection loss |

**Configuration:**
```cpp
builder& with_auto_reconnect(bool enabled, duration interval = 5s);
```

**Processing:**
1. Detect connection loss
2. Wait for configured interval
3. Attempt reconnection
4. Apply exponential backoff on repeated failures
5. Notify via on_connection_state callback

**Acceptance Criteria:**
- AC-CLIENT-002-1: Automatic reconnection attempts when enabled
- AC-CLIENT-002-2: Exponential backoff applied
- AC-CLIENT-002-3: connection_state::reconnecting reported

---

##### SRS-CLIENT-003: File Upload
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CLIENT-003 |
| **PRD Trace** | FR-03 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL upload a single file from client to server |

**Inputs:**
- Local file path (std::filesystem::path)
- Remote filename (optional, defaults to local filename)
- Transfer options (optional)

**Processing:**
1. Validate local file exists and is readable
2. Calculate SHA-256 hash of entire file
3. Send UPLOAD_REQUEST with file metadata
4. Wait for UPLOAD_ACCEPT or UPLOAD_REJECT
5. If accepted, split file into chunks
6. Calculate CRC32 for each chunk
7. Apply LZ4 compression if enabled
8. Transmit chunks via pipeline
9. Wait for server acknowledgment

**Outputs:**
- Result<transfer_handle> with unique transfer identifier
- Error result on failure with specific error code

**Acceptance Criteria:**
- AC-CLIENT-003-1: File uploaded with 100% data integrity
- AC-CLIENT-003-2: Progress callbacks invoked during upload
- AC-CLIENT-003-3: Server rejection handled gracefully

---

##### SRS-CLIENT-004: File Download
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CLIENT-004 |
| **PRD Trace** | FR-05 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL download a single file from server to client |

**Inputs:**
- Remote filename (std::string)
- Local destination path (std::filesystem::path)
- Transfer options (optional)

**Processing:**
1. Send DOWNLOAD_REQUEST with filename
2. Wait for DOWNLOAD_ACCEPT (with metadata) or DOWNLOAD_REJECT
3. If accepted, prepare to receive chunks
4. Receive chunks via pipeline
5. Verify CRC32 for each chunk
6. Decompress LZ4 if compression flag set
7. Write chunks to output file
8. Verify SHA-256 hash of completed file

**Outputs:**
- Result<transfer_handle> with unique transfer identifier
- Error result on failure

**Acceptance Criteria:**
- AC-CLIENT-004-1: File downloaded with 100% integrity
- AC-CLIENT-004-2: Progress callbacks invoked
- AC-CLIENT-004-3: Missing file error handled

---

##### SRS-CLIENT-005: File Listing
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CLIENT-005 |
| **PRD Trace** | FR-07 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL query server for available files |

**Inputs:**
- list_options (filter, sort, pagination)

**Processing:**
1. Send LIST_REQUEST with options
2. Receive LIST_RESPONSE
3. Parse file_info array

**Outputs:**
- Result<std::vector<file_info>>

**Acceptance Criteria:**
- AC-CLIENT-005-1: File list returned correctly
- AC-CLIENT-005-2: Filter applied server-side
- AC-CLIENT-005-3: Pagination works correctly

---

##### SRS-CLIENT-006: Batch Upload
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CLIENT-006 |
| **PRD Trace** | FR-04 |
| **Priority** | P1 (High) |
| **Description** | The system SHALL upload multiple files in a single batch operation |

**Inputs:**
- Vector of local file paths
- Transfer options

**Processing:**
1. Validate all files exist
2. Create batch transfer session
3. Upload files sequentially or concurrently (configurable)
4. Track individual file progress
5. Handle partial failures (continue with remaining files)

**Outputs:**
- Result<batch_transfer_handle>
- Individual status per file

**Acceptance Criteria:**
- AC-CLIENT-006-1: All files uploaded with individual status
- AC-CLIENT-006-2: Partial failures do not abort entire batch
- AC-CLIENT-006-3: Batch progress includes per-file breakdown

---

##### SRS-CLIENT-007: Batch Download
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CLIENT-007 |
| **PRD Trace** | FR-06 |
| **Priority** | P1 (High) |
| **Description** | The system SHALL download multiple files in a single batch operation |

**Inputs:**
- Vector of remote filenames
- Output directory
- Transfer options

**Processing:**
1. Create batch transfer session
2. Download files sequentially or concurrently
3. Track individual file progress
4. Handle partial failures

**Outputs:**
- Result<batch_transfer_handle>
- Individual status per file

**Acceptance Criteria:**
- AC-CLIENT-007-1: All files downloaded with individual status
- AC-CLIENT-007-2: Partial failures handled gracefully
- AC-CLIENT-007-3: Missing files reported individually

---

#### 3.1.3 Chunk Management (SRS-CHUNK)

##### SRS-CHUNK-001: File Splitting
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CHUNK-001 |
| **PRD Trace** | FR-08 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL split files into fixed-size chunks for streaming transfer |

**Specification:**
```cpp
struct chunk_config {
    std::size_t chunk_size = 256 * 1024;      // Default: 256KB
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
6. Calculate CRC32 of chunk data (before compression)

**Acceptance Criteria:**
- AC-CHUNK-001-1: File correctly reconstructed from chunks
- AC-CHUNK-001-2: Chunk sizes within configured bounds
- AC-CHUNK-001-3: Last chunk may be smaller than chunk_size

---

##### SRS-CHUNK-002: File Assembly
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CHUNK-002 |
| **PRD Trace** | FR-08 |
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
| **PRD Trace** | FR-11 |
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
- AC-CHUNK-003-2: Corrupted chunks rejected with error code -720
- AC-CHUNK-003-3: CRC32 verification adds < 1% overhead

---

##### SRS-CHUNK-004: File Hash Verification
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-CHUNK-004 |
| **PRD Trace** | FR-11 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL verify integrity of complete files using SHA-256 |

**Specification:**
```cpp
// SHA-256 of entire file (original, uncompressed)
std::string calculate_sha256(const std::filesystem::path& file);
bool verify_sha256(const std::filesystem::path& file, const std::string& expected);
```

**Acceptance Criteria:**
- AC-CHUNK-004-1: SHA-256 calculated before upload, verified after download
- AC-CHUNK-004-2: Hash mismatch returns error code -723
- AC-CHUNK-004-3: Hash included in transfer_result

---

#### 3.1.4 Compression (SRS-COMP)

##### SRS-COMP-001: LZ4 Compression
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-COMP-001 |
| **PRD Trace** | FR-14 |
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
| LZ4 (fast) | >= 400 MB/s | >= 1.5 GB/s | ~2.1:1 |
| LZ4-HC | >= 50 MB/s | >= 1.5 GB/s | ~2.7:1 |

**Acceptance Criteria:**
- AC-COMP-001-1: Compression speed >= 400 MB/s (fast mode)
- AC-COMP-001-2: Decompression speed >= 1.5 GB/s
- AC-COMP-001-3: Compressed data decompresses to original exactly

---

##### SRS-COMP-002: LZ4 Decompression
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-COMP-002 |
| **PRD Trace** | FR-14 |
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
| **PRD Trace** | FR-15 |
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
- AC-COMP-003-1: Detection completes in < 100us
- AC-COMP-003-2: Already-compressed files (zip, jpg) detected as incompressible
- AC-COMP-003-3: Text/log files detected as compressible

---

##### SRS-COMP-004: Compression Mode Configuration
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-COMP-004 |
| **PRD Trace** | FR-14, FR-15 |
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

#### 3.1.5 Storage Management (SRS-STORAGE)

##### SRS-STORAGE-001: Storage Directory Management
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-STORAGE-001 |
| **PRD Trace** | FR-16 |
| **Priority** | P1 (High) |
| **Description** | The system SHALL manage server file storage directory |

**Specification:**
```cpp
class storage_manager {
public:
    [[nodiscard]] auto list_files(const list_options& opts = {})
        -> std::vector<file_info>;
    [[nodiscard]] auto get_file_info(const std::string& filename)
        -> Result<file_info>;
    [[nodiscard]] auto delete_file(const std::string& filename)
        -> Result<void>;
    [[nodiscard]] auto get_usage() -> storage_stats;
};
```

**Acceptance Criteria:**
- AC-STORAGE-001-1: Files stored in configured directory
- AC-STORAGE-001-2: File metadata cached for fast queries
- AC-STORAGE-001-3: Concurrent access handled correctly

---

##### SRS-STORAGE-002: Storage Quota Enforcement
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-STORAGE-002 |
| **PRD Trace** | FR-16 |
| **Priority** | P1 (High) |
| **Description** | The system SHALL enforce storage quota limits |

**Processing:**
1. Track total storage usage
2. Before accepting upload, check if quota allows
3. Reject upload if quota exceeded

**Acceptance Criteria:**
- AC-STORAGE-002-1: Uploads rejected when quota exceeded
- AC-STORAGE-002-2: Error code -745 (storage_full) returned
- AC-STORAGE-002-3: Quota check is atomic with upload acceptance

---

#### 3.1.6 Pipeline Processing (SRS-PIPE)

##### SRS-PIPE-001: Upload Pipeline (Client-side)
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-PIPE-001 |
| **PRD Trace** | FR-17 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL implement a multi-stage client upload pipeline |

**Pipeline Stages:**
```
File Read -> Chunk Assembly -> LZ4 Compress -> Network Send
(io_read)   (chunk_process)   (compression)   (network)
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

##### SRS-PIPE-002: Download Pipeline (Client-side)
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-PIPE-002 |
| **PRD Trace** | FR-17 |
| **Priority** | P0 (Critical) |
| **Description** | The system SHALL implement a multi-stage client download pipeline |

**Pipeline Stages:**
```
Network Recv -> LZ4 Decompress -> Chunk Assembly -> File Write
(network)      (compression)     (chunk_process)   (io_write)
```

**Acceptance Criteria:**
- AC-PIPE-002-1: All stages execute concurrently
- AC-PIPE-002-2: Out-of-order chunks handled correctly
- AC-PIPE-002-3: Stage worker counts are configurable

---

##### SRS-PIPE-003: Pipeline Backpressure
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-PIPE-003 |
| **PRD Trace** | FR-18 |
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
- Memory bounded by queue_size x chunk_size

**Acceptance Criteria:**
- AC-PIPE-003-1: Memory usage bounded regardless of file size
- AC-PIPE-003-2: Slow stages cause upstream blocking
- AC-PIPE-003-3: Queue depths available for monitoring

---

#### 3.1.7 Transfer Resume (SRS-RESUME)

##### SRS-RESUME-001: Transfer State Persistence
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-RESUME-001 |
| **PRD Trace** | FR-09 |
| **Priority** | P1 (High) |
| **Description** | The system SHALL persist transfer state for resume capability |

**State Data:**
```cpp
struct transfer_state {
    transfer_id         id;
    transfer_direction  direction;      // upload or download
    std::string         filename;
    uint64_t            file_size;
    std::string         sha256_hash;
    uint64_t            chunks_completed;
    uint64_t            chunks_total;
    std::vector<bool>   chunk_bitmap;   // Completed chunks
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
| **PRD Trace** | FR-09 |
| **Priority** | P1 (High) |
| **Description** | The system SHALL resume interrupted transfers from last checkpoint |

**Processing:**
1. Load transfer_state from persistence
2. Validate file still exists (upload) or partial file valid (download)
3. Calculate missing chunks from chunk_bitmap
4. Resume sending/receiving only missing chunks
5. Verify complete file SHA-256 after all chunks received

**Acceptance Criteria:**
- AC-RESUME-002-1: Resume starts within 1 second
- AC-RESUME-002-2: No data loss or corruption on resume
- AC-RESUME-002-3: Resume works after network disconnection

---

#### 3.1.8 Progress Monitoring (SRS-PROGRESS)

##### SRS-PROGRESS-001: Progress Callbacks
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-PROGRESS-001 |
| **PRD Trace** | FR-10 |
| **Priority** | P1 (High) |
| **Description** | The system SHALL provide real-time progress callbacks |

**Specification:**
```cpp
struct transfer_progress {
    transfer_id         id;
    transfer_direction  direction;          // upload or download
    std::string         filename;
    uint64_t            bytes_transferred;  // Raw bytes
    uint64_t            bytes_on_wire;      // Compressed bytes
    uint64_t            total_bytes;
    double              transfer_rate;      // Bytes/second
    double              effective_rate;     // With compression
    double              compression_ratio;
    duration            elapsed_time;
    duration            estimated_remaining;
    transfer_state      state;
};

void on_progress(std::function<void(const transfer_progress&)> callback);
```

**Acceptance Criteria:**
- AC-PROGRESS-001-1: Callbacks invoked at configurable intervals
- AC-PROGRESS-001-2: Progress includes compression metrics
- AC-PROGRESS-001-3: Callback does not block transfer

---

#### 3.1.9 Transport Layer (SRS-TRANS)

##### SRS-TRANS-001: Transport Abstraction
| Attribute | Value |
|-----------|-------|
| **ID** | SRS-TRANS-001 |
| **PRD Trace** | Section 6 |
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
struct file_info {
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    std::chrono::system_clock::time_point created_time;
    std::chrono::system_clock::time_point modified_time;
};

struct upload_request {
    client_info             client;
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    compression_mode        compression;
};

struct download_request {
    client_info             client;
    std::string             filename;
    uint64_t                offset;         // For resume
    uint64_t                length;         // 0 = entire file
};

struct transfer_result {
    transfer_id             id;
    transfer_direction      direction;
    std::filesystem::path   output_path;
    uint64_t                bytes_transferred;
    uint64_t                bytes_on_wire;
    bool                    verified;       // SHA-256 match
    std::optional<error>    error;
    duration                elapsed_time;
};
```

---

## 4. Interface Requirements

### 4.1 User Interfaces

Not applicable - this is a library component without GUI.

### 4.2 Software Interfaces

#### 4.2.1 Server Interface

```cpp
namespace kcenon::file_transfer {

class file_transfer_server {
public:
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

    // Lifecycle
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
    void on_upload_request(std::function<bool(const upload_request&)> callback);
    void on_download_request(std::function<bool(const download_request&)> callback);

    // Event callbacks
    void on_transfer_progress(std::function<void(const transfer_progress&)> callback);
    void on_transfer_complete(std::function<void(const transfer_result&)> callback);
    void on_client_connected(std::function<void(const client_info&)> callback);
    void on_client_disconnected(std::function<void(const client_info&)> callback);

    // Statistics
    [[nodiscard]] auto get_statistics() -> server_statistics;
    [[nodiscard]] auto get_connected_clients() -> std::vector<client_info>;
};

} // namespace kcenon::file_transfer
```

#### 4.2.2 Client Interface

```cpp
namespace kcenon::file_transfer {

class file_transfer_client {
public:
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

    // Connection
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
    [[nodiscard]] auto list_files(const list_options& options = {})
        -> Result<std::vector<file_info>>;

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

### 4.3 Communication Interfaces

#### 4.3.1 Network Protocol

**HTTP is explicitly excluded** from this system for the following reasons:
- Unnecessary abstraction layer for streaming file transfer
- High header overhead (~800 bytes per request)
- Stateless design conflicts with connection-based resume capability

**Supported Transport Protocols:**

| Layer | Protocol | Phase | Description |
|-------|----------|-------|-------------|
| Transport (Primary) | TCP + TLS 1.3 | Phase 1 | Default, all environments |
| Transport (Optional) | QUIC | Phase 2 | High-loss networks, mobile |
| Application | Custom chunk-based protocol | - | Minimal overhead (54 bytes/chunk) |

#### 4.3.2 Message Format

```
+---------------------------------------------------------+
|                    Message Frame                          |
+---------------------------------------------------------+
| Message Type (1 byte)                                    |
|   0x01 = HANDSHAKE_REQUEST                               |
|   0x02 = HANDSHAKE_RESPONSE                              |
|   0x10 = UPLOAD_REQUEST                                  |
|   0x11 = UPLOAD_ACCEPT                                   |
|   0x12 = UPLOAD_REJECT                                   |
|   0x13 = UPLOAD_CANCEL                                   |
|   0x20 = CHUNK_DATA                                      |
|   0x21 = CHUNK_ACK                                       |
|   0x22 = CHUNK_NACK                                      |
|   0x30 = RESUME_REQUEST                                  |
|   0x31 = RESUME_RESPONSE                                 |
|   0x40 = TRANSFER_COMPLETE                               |
|   0x41 = TRANSFER_VERIFY                                 |
|   0x50 = DOWNLOAD_REQUEST                                |
|   0x51 = DOWNLOAD_ACCEPT                                 |
|   0x52 = DOWNLOAD_REJECT                                 |
|   0x53 = DOWNLOAD_CANCEL                                 |
|   0x60 = LIST_REQUEST                                    |
|   0x61 = LIST_RESPONSE                                   |
|   0xF0 = KEEPALIVE                                       |
|   0xFF = ERROR                                           |
+---------------------------------------------------------+
| Payload Length (4 bytes, big-endian)                     |
+---------------------------------------------------------+
| Payload (variable length)                                |
+---------------------------------------------------------+

Total frame overhead: 5 bytes
```

---

## 5. Performance Requirements

### 5.1 Throughput Requirements

| ID | Requirement | Target | Measurement | PRD Trace |
|----|-------------|--------|-------------|-----------|
| PERF-001 | Upload throughput (LAN) | >= 500 MB/s | 1GB file upload time | NFR-01 |
| PERF-002 | Download throughput (LAN) | >= 500 MB/s | 1GB file download time | NFR-02 |
| PERF-003 | WAN throughput | >= 100 MB/s | Network limited | NFR-01, NFR-02 |
| PERF-004 | LZ4 compression speed | >= 400 MB/s | Per core | NFR-07 |
| PERF-005 | LZ4 decompression speed | >= 1.5 GB/s | Per core | NFR-08 |
| PERF-006 | Effective throughput (compressible) | 2-4x baseline | With compression | NFR-09 |

### 5.2 Latency Requirements

| ID | Requirement | Target | PRD Trace |
|----|-------------|--------|-----------|
| PERF-010 | Chunk processing latency | < 10 ms | NFR-03 |
| PERF-011 | Compressibility detection | < 100 us | NFR-09 |
| PERF-012 | Resume start time | < 1 second | FR-09 |
| PERF-013 | File list response | < 100 ms (10K files) | NFR-10 |

### 5.3 Resource Requirements

| ID | Requirement | Target | PRD Trace |
|----|-------------|--------|-----------|
| PERF-020 | Server baseline memory | < 100 MB | NFR-04 |
| PERF-021 | Server memory per connection | < 1 MB | NFR-04 |
| PERF-022 | Client baseline memory | < 50 MB | NFR-05 |
| PERF-023 | CPU utilization | < 30% per core | NFR-06 |
| PERF-024 | Concurrent connections | >= 100 | FR-12 |

### 5.4 Capacity Requirements

| ID | Requirement | Target |
|----|-------------|--------|
| PERF-030 | Maximum file size | Limited by filesystem (tested to 100GB) |
| PERF-031 | Maximum batch size | 10,000 files |
| PERF-032 | Maximum chunk size | 1 MB |
| PERF-033 | Minimum chunk size | 64 KB |
| PERF-034 | Maximum stored files | Limited by filesystem |

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
| network_system | Latest | TCP/TLS and QUIC transport | Project |
| container_system | Latest | Serialization | Project |
| LZ4 | 1.9.0+ | Compression | BSD |

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
| -703 | transfer_rejected | Transfer rejected by server |
| -720 | chunk_checksum_error | Chunk CRC32 verification failed |
| -721 | chunk_sequence_error | Chunk received out of sequence |
| -722 | chunk_size_error | Chunk size exceeds maximum |
| -723 | file_hash_mismatch | SHA-256 verification failed |
| -740 | file_read_error | Failed to read source file |
| -741 | file_write_error | Failed to write destination file |
| -742 | file_permission_error | Insufficient file permissions |
| -743 | file_not_found | Source file not found |
| -744 | file_already_exists | File already exists on server |
| -745 | storage_full | Server storage quota exceeded |
| -746 | file_not_found_on_server | File not found on server |
| -747 | access_denied | Permission denied for operation |
| -748 | invalid_filename | Filename contains invalid characters |
| -749 | connection_refused | Server refused connection |
| -750 | server_busy | Server at max capacity |
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
| REL-001 | Data integrity | 100% (SHA-256 verified) | NFR-11 |
| REL-002 | Resume success rate | 100% | NFR-12 |
| REL-003 | Error recovery | Auto-retry with exponential backoff | NFR-13 |
| REL-004 | Graceful degradation | Reduced throughput under load | NFR-14 |
| REL-005 | Server uptime | 99.9% availability | NFR-15 |

### 7.2 Security

| ID | Requirement | Description | PRD Trace |
|----|-------------|-------------|-----------|
| SEC-001 | Encryption | TLS 1.3 for network transfer | NFR-16 |
| SEC-002 | Authentication | Optional token/certificate-based | NFR-17 |
| SEC-003 | Path traversal prevention | Validate filenames | NFR-18 |
| SEC-004 | Resource limits | Max file size, connection limits | NFR-19 |
| SEC-005 | Filename validation | Sanitize filenames | NFR-20 |

### 7.3 Maintainability

| ID | Requirement | Target |
|----|-------------|--------|
| MAINT-001 | Code coverage | >= 80% |
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
| FR-01 | Server Startup/Shutdown | SRS-SERVER-001, SRS-SERVER-002, SRS-SERVER-003 |
| FR-02 | Client Connection | SRS-CLIENT-001, SRS-CLIENT-002 |
| FR-03 | File Upload (Single) | SRS-CLIENT-003, SRS-SERVER-004 |
| FR-04 | File Upload (Batch) | SRS-CLIENT-006, SRS-SERVER-004 |
| FR-05 | File Download (Single) | SRS-CLIENT-004, SRS-SERVER-005 |
| FR-06 | File Download (Batch) | SRS-CLIENT-007, SRS-SERVER-005 |
| FR-07 | File Listing | SRS-CLIENT-005, SRS-SERVER-006 |
| FR-08 | Chunk-based Transfer | SRS-CHUNK-001, SRS-CHUNK-002 |
| FR-09 | Transfer Resume | SRS-RESUME-001, SRS-RESUME-002 |
| FR-10 | Progress Monitoring | SRS-PROGRESS-001 |
| FR-11 | Integrity Verification | SRS-CHUNK-003, SRS-CHUNK-004 |
| FR-12 | Concurrent Transfers | PERF-024 |
| FR-14 | Real-time LZ4 Compression | SRS-COMP-001, SRS-COMP-002, SRS-COMP-004 |
| FR-15 | Adaptive Compression | SRS-COMP-003 |
| FR-16 | Storage Management | SRS-STORAGE-001, SRS-STORAGE-002 |
| FR-17 | Pipeline-based Processing | SRS-PIPE-001, SRS-PIPE-002 |
| FR-18 | Pipeline Backpressure | SRS-PIPE-003 |

### 8.2 Use Case to SRS Traceability

| Use Case | Description | SRS Requirements |
|----------|-------------|------------------|
| UC-01 | Client uploads file to server | SRS-CLIENT-003, SRS-SERVER-004 |
| UC-02 | Client downloads file from server | SRS-CLIENT-004, SRS-SERVER-005 |
| UC-03 | Batch upload | SRS-CLIENT-006 |
| UC-04 | Batch download | SRS-CLIENT-007 |
| UC-05 | List files on server | SRS-CLIENT-005, SRS-SERVER-006 |
| UC-06 | Resume interrupted transfer | SRS-RESUME-001, SRS-RESUME-002 |
| UC-07 | Monitor progress | SRS-PROGRESS-001 |
| UC-08 | Secure transfer | SEC-001, SEC-002 |
| UC-09 | Server storage management | SRS-STORAGE-001, SRS-STORAGE-002 |
| UC-10 | Compress compressible files | SRS-COMP-001, SRS-COMP-003 |
| UC-11 | Skip compression for compressed files | SRS-COMP-003, SRS-COMP-004 |

---

## Appendix A: Acceptance Test Cases

### A.1 Server Tests

| Test ID | SRS Trace | Description | Expected Result |
|---------|-----------|-------------|-----------------|
| TC-S001 | SRS-SERVER-002 | Start server on port 19000 | Server listening, is_running() true |
| TC-S002 | SRS-SERVER-003 | Stop server with active transfer | Transfer completes or aborts gracefully |
| TC-S003 | SRS-SERVER-004 | Receive upload request | UPLOAD_ACCEPT sent, file received |
| TC-S004 | SRS-SERVER-005 | Receive download request | DOWNLOAD_ACCEPT sent with metadata |
| TC-S005 | SRS-SERVER-006 | List files with filter | Filtered list returned |

### A.2 Client Tests

| Test ID | SRS Trace | Description | Expected Result |
|---------|-----------|-------------|-----------------|
| TC-C001 | SRS-CLIENT-001 | Connect to server | Connection established |
| TC-C002 | SRS-CLIENT-002 | Auto-reconnect on disconnect | Reconnection within 5 seconds |
| TC-C003 | SRS-CLIENT-003 | Upload 1GB file | SHA-256 verified on server |
| TC-C004 | SRS-CLIENT-004 | Download 1GB file | SHA-256 verified locally |
| TC-C005 | SRS-CLIENT-005 | List files | File list received |

### A.3 Compression Tests

| Test ID | SRS Trace | Description | Expected Result |
|---------|-----------|-------------|-----------------|
| TC-COMP-001 | SRS-COMP-001 | Compress text file | Compression ratio >= 2:1 |
| TC-COMP-002 | SRS-COMP-002 | Decompress chunk | Original data restored |
| TC-COMP-003 | SRS-COMP-003 | Upload ZIP file (adaptive) | Compression skipped |
| TC-COMP-004 | SRS-COMP-003 | Upload text file (adaptive) | Compression applied |

### A.4 Pipeline Tests

| Test ID | SRS Trace | Description | Expected Result |
|---------|-----------|-------------|-----------------|
| TC-PIPE-001 | SRS-PIPE-001 | Verify parallel stages | All stages executing concurrently |
| TC-PIPE-002 | SRS-PIPE-003 | Slow receiver backpressure | Sender slows down, memory bounded |

### A.5 Resume Tests

| Test ID | SRS Trace | Description | Expected Result |
|---------|-----------|-------------|-----------------|
| TC-RES-001 | SRS-RESUME-001 | Kill upload at 50% | State persisted |
| TC-RES-002 | SRS-RESUME-002 | Resume killed upload | Completes from 50%, SHA-256 OK |
| TC-RES-003 | SRS-RESUME-002 | Resume killed download | Completes from 50%, SHA-256 OK |

### A.6 Performance Tests

| Test ID | SRS Trace | Description | Expected Result |
|---------|-----------|-------------|-----------------|
| TC-PERF-001 | PERF-001 | 1GB upload LAN | >= 500 MB/s throughput |
| TC-PERF-002 | PERF-002 | 1GB download LAN | >= 500 MB/s throughput |
| TC-PERF-003 | PERF-004 | LZ4 compression benchmark | >= 400 MB/s |
| TC-PERF-004 | PERF-005 | LZ4 decompression benchmark | >= 1.5 GB/s |
| TC-PERF-005 | PERF-024 | 100 concurrent connections | All complete without errors |
| TC-PERF-006 | PERF-013 | List 10,000 files | < 100 ms response |

---

## Appendix B: Glossary

| Term | Definition |
|------|------------|
| **Server** | Central component that stores files and handles client requests |
| **Client** | Component that connects to server for file operations |
| **Upload** | Transfer of file from client to server |
| **Download** | Transfer of file from server to client |
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
| 1.0.0 | 2025-12-11 | kcenon@naver.com | Initial SRS creation (P2P model) |
| 1.1.0 | 2025-12-11 | kcenon@naver.com | Added TCP/QUIC transport layer requirements |
| 0.2.0 | 2025-12-11 | kcenon@naver.com | Complete rewrite for Client-Server architecture |

---

*End of Document*
