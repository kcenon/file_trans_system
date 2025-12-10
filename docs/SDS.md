# File Transfer System - Software Design Specification

## Document Information

| Item | Description |
|------|-------------|
| **Project Name** | file_trans_system |
| **Document Type** | Software Design Specification (SDS) |
| **Version** | 1.0.0 |
| **Status** | Draft |
| **Created** | 2025-12-11 |
| **Author** | kcenon@naver.com |
| **Related Documents** | SRS.md v1.1.0, PRD.md v1.0.0 |

---

## 1. Introduction

### 1.1 Purpose

This Software Design Specification (SDS) document describes the detailed design of the **file_trans_system** library. It translates the software requirements defined in the SRS into a concrete design that can be implemented. This document serves as a blueprint for developers and provides traceability from requirements to design elements.

This document is intended for:
- Software developers implementing the system
- System architects reviewing the design
- QA engineers understanding the test scope
- Maintainers understanding the system structure

### 1.2 Scope

This document covers:
- System architecture and component design
- Data structures and data flow
- Interface specifications
- Algorithm descriptions
- Error handling design
- Traceability to SRS requirements

### 1.3 Design Overview

The file_trans_system is designed as a modular, layered architecture:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          API Layer                                       │
│   file_sender  │  file_receiver  │  transfer_manager                    │
├─────────────────────────────────────────────────────────────────────────┤
│                        Core Layer                                        │
│   sender_pipeline  │  receiver_pipeline  │  progress_tracker            │
├─────────────────────────────────────────────────────────────────────────┤
│                       Service Layer                                      │
│   chunk_manager  │  compression_engine  │  resume_handler               │
├─────────────────────────────────────────────────────────────────────────┤
│                       Transport Layer                                    │
│   transport_interface  │  tcp_transport  │  quic_transport (Phase 2)    │
├─────────────────────────────────────────────────────────────────────────┤
│                      Infrastructure Layer                                │
│   common_system  │  thread_system  │  network_system  │  container_system│
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Architectural Design

### 2.1 Architecture Style

The system employs a **Pipeline Architecture** combined with **Layered Architecture**:

| Style | Application | Rationale |
|-------|-------------|-----------|
| **Pipeline** | Data processing (read→compress→send) | Maximizes throughput via parallel stages |
| **Layered** | System organization | Separation of concerns, testability |
| **Strategy** | Transport, compression | Pluggable implementations |
| **Observer** | Progress notification | Decoupled event handling |

### 2.2 Component Architecture

#### 2.2.1 Component Diagram

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                              file_trans_system                                │
│                                                                               │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────────┐  │
│  │   file_sender   │  │  file_receiver  │  │     transfer_manager        │  │
│  │                 │  │                 │  │                             │  │
│  │  +send_file()   │  │  +start()       │  │  +get_status()              │  │
│  │  +send_files()  │  │  +stop()        │  │  +list_transfers()          │  │
│  │  +cancel()      │  │  +on_request()  │  │  +get_statistics()          │  │
│  │  +pause()       │  │  +on_progress() │  │  +set_bandwidth_limit()     │  │
│  │  +resume()      │  │  +on_complete() │  │  +set_compression()         │  │
│  └────────┬────────┘  └────────┬────────┘  └─────────────┬───────────────┘  │
│           │                    │                          │                  │
│           └────────────────────┼──────────────────────────┘                  │
│                                ▼                                             │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                          Pipeline Layer                               │   │
│  │  ┌──────────────────────┐    ┌──────────────────────┐                │   │
│  │  │   sender_pipeline    │    │  receiver_pipeline   │                │   │
│  │  │                      │    │                      │                │   │
│  │  │ read→chunk→compress  │    │ recv→decompress      │                │   │
│  │  │ →send                │    │ →assemble→write      │                │   │
│  │  └──────────┬───────────┘    └──────────┬───────────┘                │   │
│  │             │                           │                             │   │
│  └─────────────┼───────────────────────────┼─────────────────────────────┘   │
│                ▼                           ▼                                 │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                         Service Layer                                 │   │
│  │  ┌─────────────┐ ┌────────────────────┐ ┌──────────────────────────┐ │   │
│  │  │chunk_manager│ │ compression_engine │ │    resume_handler        │ │   │
│  │  │             │ │                    │ │                          │ │   │
│  │  │ +split()    │ │ +compress()        │ │ +save_state()            │ │   │
│  │  │ +assemble() │ │ +decompress()      │ │ +load_state()            │ │   │
│  │  │ +verify()   │ │ +is_compressible() │ │ +get_missing_chunks()    │ │   │
│  │  └─────────────┘ └────────────────────┘ └──────────────────────────┘ │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                                                               │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                        Transport Layer                                │   │
│  │  ┌────────────────────┐  ┌──────────────┐  ┌────────────────────┐   │   │
│  │  │transport_interface │  │tcp_transport │  │ quic_transport     │   │   │
│  │  │   <<interface>>    │◄─┤              │  │  (Phase 2)         │   │   │
│  │  │                    │  └──────────────┘  └────────────────────┘   │   │
│  │  └────────────────────┘                                              │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 2.3 Design Patterns Used

| Pattern | Usage | SRS Trace |
|---------|-------|-----------|
| **Builder** | file_sender::builder, file_receiver::builder | API usability |
| **Strategy** | transport_interface implementations | SRS-TRANS-001 |
| **Observer** | Progress callbacks, completion events | SRS-PROGRESS-001 |
| **Pipeline** | sender_pipeline, receiver_pipeline | SRS-PIPE-001, SRS-PIPE-002 |
| **Factory** | create_transport() | SRS-TRANS-001 |
| **State** | Transfer state machine | SRS-PROGRESS-002 |
| **Command** | Pipeline jobs | SRS-PIPE-001 |

---

## 3. Component Design

### 3.1 file_sender Component

#### 3.1.1 Responsibility
Provides the public API for sending files to remote endpoints.

#### 3.1.2 SRS Traceability
| SRS Requirement | Design Element |
|-----------------|----------------|
| SRS-CORE-001 | send_file() method |
| SRS-CORE-003 | send_files() method |
| SRS-PROGRESS-001 | on_progress() callback |

#### 3.1.3 Class Design

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
        builder& with_transport(transport_type type);
        [[nodiscard]] auto build() -> Result<file_sender>;

    private:
        pipeline_config         pipeline_config_;
        compression_mode        compression_mode_   = compression_mode::adaptive;
        compression_level       compression_level_  = compression_level::fast;
        std::size_t            chunk_size_         = 256 * 1024;
        std::optional<size_t>  bandwidth_limit_;
        transport_type         transport_type_     = transport_type::tcp;
    };

    // Public interface
    [[nodiscard]] auto send_file(
        const std::filesystem::path& file_path,
        const endpoint& destination,
        const transfer_options& options = {}
    ) -> Result<transfer_handle>;

    [[nodiscard]] auto send_files(
        std::span<const std::filesystem::path> files,
        const endpoint& destination,
        const transfer_options& options = {}
    ) -> Result<batch_transfer_handle>;

    [[nodiscard]] auto cancel(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto pause(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto resume(const transfer_id& id) -> Result<void>;

    void on_progress(std::function<void(const transfer_progress&)> callback);

private:
    // Internal components
    std::unique_ptr<sender_pipeline>        pipeline_;
    std::unique_ptr<transport_interface>    transport_;
    std::unique_ptr<progress_tracker>       progress_tracker_;
    std::unique_ptr<resume_handler>         resume_handler_;

    // Configuration
    file_sender_config                      config_;

    // Active transfers
    std::unordered_map<transfer_id, transfer_context> active_transfers_;
    std::mutex                              transfers_mutex_;

    // Callbacks
    std::function<void(const transfer_progress&)> progress_callback_;
};

} // namespace kcenon::file_transfer
```

#### 3.1.4 Sequence Diagram: send_file()

```
┌──────────┐ ┌──────────────┐ ┌─────────────┐ ┌────────────┐ ┌─────────────┐ ┌───────────┐
│  Client  │ │ file_sender  │ │chunk_manager│ │compression │ │  pipeline   │ │ transport │
└────┬─────┘ └──────┬───────┘ └──────┬──────┘ └─────┬──────┘ └──────┬──────┘ └─────┬─────┘
     │              │                │              │               │              │
     │ send_file()  │                │              │               │              │
     │─────────────>│                │              │               │              │
     │              │                │              │               │              │
     │              │ validate_file()│              │               │              │
     │              │───────────────>│              │               │              │
     │              │                │              │               │              │
     │              │ calc_sha256()  │              │               │              │
     │              │───────────────>│              │               │              │
     │              │                │              │               │              │
     │              │       connect()│              │               │              │
     │              │────────────────────────────────────────────────────────────>│
     │              │                │              │               │              │
     │              │send_transfer_request()        │               │              │
     │              │────────────────────────────────────────────────────────────>│
     │              │                │              │               │              │
     │              │      submit_file_to_pipeline()│               │              │
     │              │──────────────────────────────────────────────>│              │
     │              │                │              │               │              │
     │              │                │   [Pipeline Processing]     │              │
     │              │                │              │               │              │
     │              │                │  read_chunk()│               │              │
     │              │                │<─────────────┼───────────────│              │
     │              │                │              │               │              │
     │              │                │   compress() │               │              │
     │              │                │─────────────>│               │              │
     │              │                │              │               │              │
     │              │                │              │     send()    │              │
     │              │                │              │───────────────┼─────────────>│
     │              │                │              │               │              │
     │  progress()  │                │              │               │              │
     │<─────────────│                │              │               │              │
     │              │                │              │               │              │
     │              │                │  [Repeat for all chunks]    │              │
     │              │                │              │               │              │
     │              │ verify_sha256()│              │               │              │
     │              │────────────────────────────────────────────────────────────>│
     │              │                │              │               │              │
     │   Result     │                │              │               │              │
     │<─────────────│                │              │               │              │
     │              │                │              │               │              │
```

---

### 3.2 file_receiver Component

#### 3.2.1 Responsibility
Listens for incoming file transfers and writes received data to disk.

#### 3.2.2 SRS Traceability
| SRS Requirement | Design Element |
|-----------------|----------------|
| SRS-CORE-002 | Receive processing logic |
| SRS-CHUNK-002 | chunk_assembler integration |
| SRS-PROGRESS-001 | on_progress() callback |

#### 3.2.3 Class Design

```cpp
namespace kcenon::file_transfer {

class file_receiver {
public:
    class builder {
    public:
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_output_directory(const std::filesystem::path& dir);
        builder& with_bandwidth_limit(std::size_t bytes_per_second);
        builder& with_transport(transport_type type);
        [[nodiscard]] auto build() -> Result<file_receiver>;

    private:
        pipeline_config             pipeline_config_;
        std::filesystem::path       output_dir_;
        std::optional<std::size_t>  bandwidth_limit_;
        transport_type              transport_type_ = transport_type::tcp;
    };

    // Lifecycle
    [[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;
    [[nodiscard]] auto stop() -> Result<void>;

    // Configuration
    void set_output_directory(const std::filesystem::path& dir);

    // Callbacks
    void on_transfer_request(std::function<bool(const transfer_request&)> callback);
    void on_progress(std::function<void(const transfer_progress&)> callback);
    void on_complete(std::function<void(const transfer_result&)> callback);

private:
    // State
    enum class receiver_state { stopped, starting, running, stopping };
    std::atomic<receiver_state>             state_{receiver_state::stopped};

    // Components
    std::unique_ptr<receiver_pipeline>      pipeline_;
    std::unique_ptr<transport_interface>    transport_;
    std::unique_ptr<progress_tracker>       progress_tracker_;
    std::unique_ptr<resume_handler>         resume_handler_;

    // Configuration
    file_receiver_config                    config_;
    std::filesystem::path                   output_dir_;

    // Active transfers
    std::unordered_map<transfer_id, receive_context> active_transfers_;
    std::shared_mutex                       transfers_mutex_;

    // Callbacks
    std::function<bool(const transfer_request&)>    request_callback_;
    std::function<void(const transfer_progress&)>   progress_callback_;
    std::function<void(const transfer_result&)>     complete_callback_;

    // Internal methods
    void handle_incoming_connection(connection_ptr conn);
    void process_transfer_request(const transfer_request& req, connection_ptr conn);
    void handle_chunk(const chunk& c, receive_context& ctx);
};

} // namespace kcenon::file_transfer
```

---

### 3.3 chunk_manager Component

#### 3.3.1 Responsibility
Splits files into chunks for sending and reassembles chunks into files on receiving.

#### 3.3.2 SRS Traceability
| SRS Requirement | Design Element |
|-----------------|----------------|
| SRS-CHUNK-001 | chunk_splitter class |
| SRS-CHUNK-002 | chunk_assembler class |
| SRS-CHUNK-003 | CRC32 checksum calculation |
| SRS-CHUNK-004 | SHA-256 file hash calculation |

#### 3.3.3 Class Design

```cpp
namespace kcenon::file_transfer {

// Configuration for chunk operations
struct chunk_config {
    std::size_t chunk_size     = 256 * 1024;  // 256KB default
    std::size_t min_chunk_size = 64 * 1024;   // 64KB minimum
    std::size_t max_chunk_size = 1024 * 1024; // 1MB maximum

    [[nodiscard]] auto validate() const -> Result<void>;
};

// Chunk splitter - splits files into chunks
class chunk_splitter {
public:
    explicit chunk_splitter(const chunk_config& config);

    // Iterator-style interface for memory efficiency
    class chunk_iterator {
    public:
        [[nodiscard]] auto has_next() const -> bool;
        [[nodiscard]] auto next() -> Result<chunk>;
        [[nodiscard]] auto current_index() const -> uint64_t;
        [[nodiscard]] auto total_chunks() const -> uint64_t;

    private:
        friend class chunk_splitter;
        std::ifstream           file_;
        chunk_config            config_;
        uint64_t               current_index_;
        uint64_t               total_chunks_;
        transfer_id            transfer_id_;
        std::vector<std::byte> buffer_;
    };

    // Create iterator for file
    [[nodiscard]] auto split(
        const std::filesystem::path& file_path,
        const transfer_id& id
    ) -> Result<chunk_iterator>;

    // Calculate file metadata
    [[nodiscard]] auto calculate_metadata(
        const std::filesystem::path& file_path
    ) -> Result<file_metadata>;

private:
    chunk_config config_;

    [[nodiscard]] auto calculate_crc32(std::span<const std::byte> data) const -> uint32_t;
};

// Chunk assembler - reassembles chunks into files
class chunk_assembler {
public:
    explicit chunk_assembler(const std::filesystem::path& output_dir);

    // Process incoming chunk
    [[nodiscard]] auto process_chunk(const chunk& c) -> Result<void>;

    // Check if file is complete
    [[nodiscard]] auto is_complete(const transfer_id& id) const -> bool;

    // Get missing chunk indices
    [[nodiscard]] auto get_missing_chunks(const transfer_id& id) const
        -> std::vector<uint64_t>;

    // Finalize file (verify SHA-256)
    [[nodiscard]] auto finalize(
        const transfer_id& id,
        const std::string& expected_hash
    ) -> Result<std::filesystem::path>;

    // Get assembly progress
    [[nodiscard]] auto get_progress(const transfer_id& id) const
        -> std::optional<assembly_progress>;

private:
    struct assembly_context {
        std::filesystem::path   temp_file_path;
        std::ofstream          file;
        uint64_t               total_chunks;
        std::vector<bool>      received_chunks;  // Bitmap
        uint64_t               bytes_written;
        std::mutex             mutex;
    };

    std::filesystem::path                                   output_dir_;
    std::unordered_map<transfer_id, assembly_context>       contexts_;
    std::shared_mutex                                        contexts_mutex_;

    [[nodiscard]] auto verify_crc32(const chunk& c) const -> bool;
};

// Checksum utilities
class checksum {
public:
    // CRC32 for chunk integrity
    [[nodiscard]] static auto crc32(std::span<const std::byte> data) -> uint32_t;
    [[nodiscard]] static auto verify_crc32(
        std::span<const std::byte> data,
        uint32_t expected
    ) -> bool;

    // SHA-256 for file integrity
    [[nodiscard]] static auto sha256_file(const std::filesystem::path& path)
        -> Result<std::string>;
    [[nodiscard]] static auto verify_sha256(
        const std::filesystem::path& path,
        const std::string& expected
    ) -> bool;

    // Streaming SHA-256 for large files
    class sha256_stream {
    public:
        void update(std::span<const std::byte> data);
        [[nodiscard]] auto finalize() -> std::string;

    private:
        // Implementation details (e.g., OpenSSL context)
    };
};

} // namespace kcenon::file_transfer
```

---

### 3.4 compression_engine Component

#### 3.4.1 Responsibility
Provides LZ4 compression and decompression with adaptive detection.

#### 3.4.2 SRS Traceability
| SRS Requirement | Design Element |
|-----------------|----------------|
| SRS-COMP-001 | lz4_engine::compress() |
| SRS-COMP-002 | lz4_engine::decompress() |
| SRS-COMP-003 | adaptive_compression::is_compressible() |
| SRS-COMP-004 | compression_mode enum |
| SRS-COMP-005 | compression_statistics struct |

#### 3.4.3 Class Design

```cpp
namespace kcenon::file_transfer {

// Compression modes
enum class compression_mode {
    disabled,   // Never compress
    enabled,    // Always compress
    adaptive    // Auto-detect compressibility (default)
};

// Compression levels
enum class compression_level {
    fast,             // LZ4 standard (~400 MB/s)
    high_compression  // LZ4-HC (~50 MB/s, better ratio)
};

// LZ4 compression engine
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
        int level = 9  // 1-12
    ) -> Result<std::size_t>;

    // Decompression (works for both modes)
    [[nodiscard]] static auto decompress(
        std::span<const std::byte> compressed,
        std::span<std::byte> output,
        std::size_t original_size
    ) -> Result<std::size_t>;

    // Calculate maximum compressed size for buffer allocation
    [[nodiscard]] static auto max_compressed_size(std::size_t input_size)
        -> std::size_t;
};

// Adaptive compression detection
class adaptive_compression {
public:
    // Quick compressibility check using sample
    [[nodiscard]] static auto is_compressible(
        std::span<const std::byte> data,
        double threshold = 0.9  // Compress if < 90% of original
    ) -> bool;

    // Check by file extension (heuristic)
    [[nodiscard]] static auto is_likely_compressible(
        const std::filesystem::path& file
    ) -> bool;

private:
    // Known compressed extensions
    static constexpr std::array<std::string_view, 10> compressed_extensions = {
        ".zip", ".gz", ".tar.gz", ".tgz", ".bz2",
        ".jpg", ".jpeg", ".png", ".mp4", ".mp3"
    };
};

// Chunk compressor with statistics
class chunk_compressor {
public:
    explicit chunk_compressor(
        compression_mode mode = compression_mode::adaptive,
        compression_level level = compression_level::fast
    );

    // Compress chunk
    [[nodiscard]] auto compress(const chunk& input) -> Result<chunk>;

    // Decompress chunk
    [[nodiscard]] auto decompress(const chunk& input) -> Result<chunk>;

    // Get compression statistics
    [[nodiscard]] auto get_statistics() const -> compression_statistics;

    // Reset statistics
    void reset_statistics();

private:
    compression_mode    mode_;
    compression_level   level_;

    // Statistics (thread-safe)
    mutable std::mutex  stats_mutex_;
    compression_statistics statistics_;

    // Internal buffer for compression
    std::vector<std::byte> compression_buffer_;
};

// Compression statistics
struct compression_statistics {
    std::atomic<uint64_t> total_raw_bytes{0};
    std::atomic<uint64_t> total_compressed_bytes{0};
    std::atomic<uint64_t> chunks_compressed{0};
    std::atomic<uint64_t> chunks_skipped{0};
    std::atomic<uint64_t> compression_time_us{0};
    std::atomic<uint64_t> decompression_time_us{0};

    // Calculated metrics
    [[nodiscard]] auto compression_ratio() const -> double;
    [[nodiscard]] auto compression_speed_mbps() const -> double;
    [[nodiscard]] auto decompression_speed_mbps() const -> double;
};

} // namespace kcenon::file_transfer
```

---

### 3.5 Pipeline Components

#### 3.5.1 Responsibility
Implements multi-stage parallel processing for maximum throughput.

#### 3.5.2 SRS Traceability
| SRS Requirement | Design Element |
|-----------------|----------------|
| SRS-PIPE-001 | sender_pipeline class |
| SRS-PIPE-002 | receiver_pipeline class |
| SRS-PIPE-003 | Bounded queue implementation |
| SRS-PIPE-004 | pipeline_statistics struct |

#### 3.5.3 Class Design

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

// Pipeline configuration
struct pipeline_config {
    // Worker counts per stage
    std::size_t io_read_workers      = 2;
    std::size_t chunk_workers        = 2;
    std::size_t compression_workers  = 4;  // More for CPU-bound
    std::size_t network_workers      = 2;
    std::size_t io_write_workers     = 2;

    // Queue sizes for backpressure
    std::size_t read_queue_size      = 16;
    std::size_t compress_queue_size  = 32;
    std::size_t send_queue_size      = 64;
    std::size_t decompress_queue_size = 32;
    std::size_t write_queue_size     = 16;

    // Auto-detect based on hardware
    [[nodiscard]] static auto auto_detect() -> pipeline_config;
};

// Base class for pipeline jobs
template<pipeline_stage Stage>
class pipeline_job : public thread::typed_job_t<pipeline_stage> {
public:
    explicit pipeline_job(const std::string& name)
        : typed_job_t<pipeline_stage>(Stage, name) {}

    virtual void execute() = 0;

protected:
    // Metrics
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point end_time_;
};

// Sender pipeline
class sender_pipeline {
public:
    class builder {
    public:
        builder& with_config(const pipeline_config& config);
        builder& with_compressor(std::shared_ptr<chunk_compressor> compressor);
        builder& with_transport(std::shared_ptr<transport_interface> transport);
        [[nodiscard]] auto build() -> Result<sender_pipeline>;
    };

    [[nodiscard]] auto start() -> Result<void>;
    [[nodiscard]] auto stop(bool wait_for_completion = true) -> Result<void>;

    // Submit file for processing
    [[nodiscard]] auto submit(
        const std::filesystem::path& file,
        const transfer_id& id,
        const transfer_options& options
    ) -> Result<void>;

    // Get statistics
    [[nodiscard]] auto get_stats() const -> pipeline_statistics;
    [[nodiscard]] auto get_queue_depths() const -> queue_depth_info;

private:
    // Thread pool (from thread_system)
    std::unique_ptr<thread::typed_thread_pool<pipeline_stage>> thread_pool_;

    // Inter-stage queues with backpressure
    bounded_queue<read_result>      read_queue_;
    bounded_queue<chunk>            chunk_queue_;
    bounded_queue<chunk>            compress_queue_;
    bounded_queue<chunk>            send_queue_;

    // Shared components
    std::shared_ptr<chunk_compressor>    compressor_;
    std::shared_ptr<transport_interface> transport_;

    // Configuration
    pipeline_config config_;

    // State
    std::atomic<bool> running_{false};

    // Statistics
    pipeline_statistics stats_;

    // Job implementations
    class read_job;
    class chunk_job;
    class compress_job;
    class send_job;
};

// Receiver pipeline
class receiver_pipeline {
public:
    class builder {
    public:
        builder& with_config(const pipeline_config& config);
        builder& with_compressor(std::shared_ptr<chunk_compressor> compressor);
        builder& with_assembler(std::shared_ptr<chunk_assembler> assembler);
        [[nodiscard]] auto build() -> Result<receiver_pipeline>;
    };

    [[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;
    [[nodiscard]] auto stop(bool wait_for_completion = true) -> Result<void>;

    // Submit received chunk for processing
    [[nodiscard]] auto submit_chunk(chunk c) -> Result<void>;

    // Get statistics
    [[nodiscard]] auto get_stats() const -> pipeline_statistics;
    [[nodiscard]] auto get_queue_depths() const -> queue_depth_info;

private:
    // Thread pool
    std::unique_ptr<thread::typed_thread_pool<pipeline_stage>> thread_pool_;

    // Inter-stage queues
    bounded_queue<chunk>            recv_queue_;
    bounded_queue<chunk>            decompress_queue_;
    bounded_queue<chunk>            assemble_queue_;
    bounded_queue<chunk>            write_queue_;

    // Shared components
    std::shared_ptr<chunk_compressor>  compressor_;
    std::shared_ptr<chunk_assembler>   assembler_;

    // Configuration
    pipeline_config config_;

    // State
    std::atomic<bool> running_{false};

    // Statistics
    pipeline_statistics stats_;

    // Job implementations
    class receive_job;
    class decompress_job;
    class assemble_job;
    class write_job;
};

// Bounded queue for backpressure
template<typename T>
class bounded_queue {
public:
    explicit bounded_queue(std::size_t max_size);

    // Block until space available
    void push(T item);

    // Block until item available
    [[nodiscard]] auto pop() -> T;

    // Non-blocking variants
    [[nodiscard]] auto try_push(T item) -> bool;
    [[nodiscard]] auto try_pop() -> std::optional<T>;

    // Query state
    [[nodiscard]] auto size() const -> std::size_t;
    [[nodiscard]] auto empty() const -> bool;
    [[nodiscard]] auto full() const -> bool;

private:
    std::queue<T>           queue_;
    std::size_t             max_size_;
    mutable std::mutex      mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
};

// Pipeline statistics
struct pipeline_statistics {
    struct stage_stats {
        std::atomic<uint64_t>    jobs_processed{0};
        std::atomic<uint64_t>    bytes_processed{0};
        std::atomic<uint64_t>    total_latency_us{0};
        std::atomic<std::size_t> current_queue_depth{0};
        std::atomic<std::size_t> max_queue_depth{0};

        [[nodiscard]] auto avg_latency_us() const -> double;
        [[nodiscard]] auto throughput_mbps() const -> double;
    };

    stage_stats io_read_stats;
    stage_stats chunk_stats;
    stage_stats compression_stats;
    stage_stats network_stats;
    stage_stats io_write_stats;

    // Identify bottleneck stage
    [[nodiscard]] auto bottleneck_stage() const -> pipeline_stage;
};

// Queue depth information
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

---

### 3.6 Transport Components

#### 3.6.1 Responsibility
Abstracts network transport protocols (TCP, QUIC).

#### 3.6.2 SRS Traceability
| SRS Requirement | Design Element |
|-----------------|----------------|
| SRS-TRANS-001 | transport_interface class |
| SRS-TRANS-002 | tcp_transport class |
| SRS-TRANS-003 | quic_transport class (Phase 2) |
| SRS-TRANS-004 | Fallback logic in transport_factory |

#### 3.6.3 Class Design

```cpp
namespace kcenon::file_transfer {

// Transport types
enum class transport_type {
    tcp,    // TCP + TLS 1.3 (default)
    quic    // QUIC (Phase 2)
};

// Abstract transport interface
class transport_interface {
public:
    virtual ~transport_interface() = default;

    // Connection management
    [[nodiscard]] virtual auto connect(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto disconnect() -> Result<void> = 0;
    [[nodiscard]] virtual auto is_connected() const -> bool = 0;

    // Data transfer
    [[nodiscard]] virtual auto send(std::span<const std::byte> data) -> Result<void> = 0;
    [[nodiscard]] virtual auto receive(std::span<std::byte> buffer) -> Result<std::size_t> = 0;

    // QUIC-specific (no-op for TCP)
    [[nodiscard]] virtual auto create_stream() -> Result<stream_id> {
        return stream_id{0};
    }
    [[nodiscard]] virtual auto close_stream(stream_id) -> Result<void> {
        return {};
    }

    // Server-side
    [[nodiscard]] virtual auto listen(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto accept() -> Result<std::unique_ptr<transport_interface>> = 0;
};

// TCP transport configuration
struct tcp_transport_config {
    bool        enable_tls      = true;
    bool        tcp_nodelay     = true;
    std::size_t send_buffer     = 256 * 1024;
    std::size_t recv_buffer     = 256 * 1024;
    duration    connect_timeout = std::chrono::seconds(10);
    duration    read_timeout    = std::chrono::seconds(30);
};

// TCP transport implementation
class tcp_transport : public transport_interface {
public:
    explicit tcp_transport(const tcp_transport_config& config = {});

    [[nodiscard]] auto connect(const endpoint& ep) -> Result<void> override;
    [[nodiscard]] auto disconnect() -> Result<void> override;
    [[nodiscard]] auto is_connected() const -> bool override;

    [[nodiscard]] auto send(std::span<const std::byte> data) -> Result<void> override;
    [[nodiscard]] auto receive(std::span<std::byte> buffer) -> Result<std::size_t> override;

    [[nodiscard]] auto listen(const endpoint& ep) -> Result<void> override;
    [[nodiscard]] auto accept() -> Result<std::unique_ptr<transport_interface>> override;

private:
    tcp_transport_config config_;

    // Uses network_system internally
    std::unique_ptr<network::tcp_socket> socket_;
    std::unique_ptr<network::tls_context> tls_context_;

    std::atomic<bool> connected_{false};
};

// QUIC transport configuration (Phase 2)
struct quic_transport_config {
    bool        enable_0rtt         = true;
    std::size_t max_streams         = 100;
    std::size_t initial_window      = 10 * 1024 * 1024;
    duration    idle_timeout        = std::chrono::seconds(30);
    bool        enable_migration    = true;
};

// QUIC transport implementation (Phase 2)
class quic_transport : public transport_interface {
public:
    explicit quic_transport(const quic_transport_config& config = {});

    [[nodiscard]] auto connect(const endpoint& ep) -> Result<void> override;
    [[nodiscard]] auto disconnect() -> Result<void> override;
    [[nodiscard]] auto is_connected() const -> bool override;

    [[nodiscard]] auto send(std::span<const std::byte> data) -> Result<void> override;
    [[nodiscard]] auto receive(std::span<std::byte> buffer) -> Result<std::size_t> override;

    [[nodiscard]] auto create_stream() -> Result<stream_id> override;
    [[nodiscard]] auto close_stream(stream_id id) -> Result<void> override;

    [[nodiscard]] auto listen(const endpoint& ep) -> Result<void> override;
    [[nodiscard]] auto accept() -> Result<std::unique_ptr<transport_interface>> override;

private:
    quic_transport_config config_;

    // Uses network_system QUIC support
    std::unique_ptr<network::quic_connection> connection_;
    std::unordered_map<stream_id, network::quic_stream> streams_;
};

// Transport factory with fallback support
class transport_factory {
public:
    [[nodiscard]] static auto create(transport_type type)
        -> std::unique_ptr<transport_interface>;

    // Create with automatic fallback (QUIC -> TCP)
    [[nodiscard]] static auto create_with_fallback(
        const endpoint& ep,
        transport_type preferred = transport_type::quic
    ) -> Result<std::unique_ptr<transport_interface>>;
};

} // namespace kcenon::file_transfer
```

---

### 3.7 Resume Handler Component

#### 3.7.1 Responsibility
Manages transfer state persistence for resume capability.

#### 3.7.2 SRS Traceability
| SRS Requirement | Design Element |
|-----------------|----------------|
| SRS-RESUME-001 | transfer_state struct, state persistence |
| SRS-RESUME-002 | resume() method logic |

#### 3.7.3 Class Design

```cpp
namespace kcenon::file_transfer {

// Transfer state for persistence
struct transfer_state {
    transfer_id                             id;
    std::string                             file_path;
    uint64_t                                file_size;
    std::string                             sha256_hash;
    uint64_t                                chunks_completed;
    uint64_t                                chunks_total;
    std::vector<bool>                       chunk_bitmap;  // true = received
    compression_mode                        compression;
    std::chrono::system_clock::time_point   created_at;
    std::chrono::system_clock::time_point   last_update;

    // Serialization
    [[nodiscard]] auto serialize() const -> std::vector<std::byte>;
    [[nodiscard]] static auto deserialize(std::span<const std::byte> data)
        -> Result<transfer_state>;
};

// Resume handler
class resume_handler {
public:
    explicit resume_handler(const std::filesystem::path& state_dir);

    // Save transfer state
    [[nodiscard]] auto save_state(const transfer_state& state) -> Result<void>;

    // Load transfer state
    [[nodiscard]] auto load_state(const transfer_id& id)
        -> Result<transfer_state>;

    // Delete transfer state (on completion)
    [[nodiscard]] auto delete_state(const transfer_id& id) -> Result<void>;

    // List resumable transfers
    [[nodiscard]] auto list_resumable() -> Result<std::vector<transfer_id>>;

    // Update chunk completion
    [[nodiscard]] auto mark_chunk_complete(
        const transfer_id& id,
        uint64_t chunk_index
    ) -> Result<void>;

    // Get missing chunks for resume
    [[nodiscard]] auto get_missing_chunks(const transfer_id& id)
        -> Result<std::vector<uint64_t>>;

    // Validate state integrity
    [[nodiscard]] auto validate_state(const transfer_state& state)
        -> Result<void>;

private:
    std::filesystem::path state_dir_;

    // File path for state
    [[nodiscard]] auto state_file_path(const transfer_id& id) const
        -> std::filesystem::path;

    // Atomic state update
    [[nodiscard]] auto atomic_write(
        const std::filesystem::path& path,
        std::span<const std::byte> data
    ) -> Result<void>;
};

} // namespace kcenon::file_transfer
```

---

## 4. Data Design

### 4.1 Data Structures

#### 4.1.1 Chunk Data Structure

```cpp
// Chunk flags
enum class chunk_flags : uint8_t {
    none            = 0x00,
    first_chunk     = 0x01,     // First chunk of file
    last_chunk      = 0x02,     // Last chunk of file
    compressed      = 0x04,     // LZ4 compressed
    encrypted       = 0x08      // Reserved for TLS
};

// Enable bitwise operations
constexpr chunk_flags operator|(chunk_flags a, chunk_flags b);
constexpr chunk_flags operator&(chunk_flags a, chunk_flags b);
constexpr bool has_flag(chunk_flags flags, chunk_flags test);

// Chunk header (fixed size for wire protocol)
struct chunk_header {
    transfer_id     transfer_id;        // 16 bytes (UUID)
    uint64_t        file_index;         // 8 bytes
    uint64_t        chunk_index;        // 8 bytes
    uint64_t        chunk_offset;       // 8 bytes
    uint32_t        original_size;      // 4 bytes
    uint32_t        compressed_size;    // 4 bytes
    uint32_t        checksum;           // 4 bytes (CRC32)
    chunk_flags     flags;              // 1 byte
    uint8_t         reserved[3];        // 3 bytes padding
    // Total: 56 bytes

    // Serialization
    [[nodiscard]] auto to_bytes() const -> std::array<std::byte, 56>;
    [[nodiscard]] static auto from_bytes(std::span<const std::byte, 56> data)
        -> chunk_header;
};

// Complete chunk
struct chunk {
    chunk_header            header;
    std::vector<std::byte>  data;

    // Validation
    [[nodiscard]] auto is_valid() const -> bool;
    [[nodiscard]] auto verify_checksum() const -> bool;
};
```

#### 4.1.2 Transfer Data Structures

```cpp
// Transfer identifier (UUID)
struct transfer_id {
    std::array<uint8_t, 16> bytes;

    [[nodiscard]] static auto generate() -> transfer_id;
    [[nodiscard]] auto to_string() const -> std::string;
    [[nodiscard]] static auto from_string(std::string_view str)
        -> Result<transfer_id>;

    auto operator<=>(const transfer_id&) const = default;
};

// File metadata
struct file_metadata {
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    std::filesystem::perms  permissions;
    std::chrono::system_clock::time_point modified_time;
    bool                    compressible_hint;

    [[nodiscard]] auto serialize() const -> std::vector<std::byte>;
    [[nodiscard]] static auto deserialize(std::span<const std::byte> data)
        -> Result<file_metadata>;
};

// Transfer options
struct transfer_options {
    compression_mode            compression     = compression_mode::adaptive;
    compression_level           level           = compression_level::fast;
    std::size_t                 chunk_size      = 256 * 1024;
    bool                        verify_checksum = true;
    std::optional<std::size_t>  bandwidth_limit;
    std::optional<int>          priority;
};

// Transfer request
struct transfer_request {
    transfer_id                     id;
    std::vector<file_metadata>      files;
    transfer_options                options;

    [[nodiscard]] auto serialize() const -> std::vector<std::byte>;
    [[nodiscard]] static auto deserialize(std::span<const std::byte> data)
        -> Result<transfer_request>;
};

// Transfer state enumeration
enum class transfer_state_enum {
    pending,        // Waiting to start
    initializing,   // Setting up connection
    transferring,   // Active transfer
    verifying,      // Verifying integrity
    completed,      // Successfully completed
    failed,         // Transfer failed
    cancelled       // User cancelled
};

// Transfer progress
struct transfer_progress {
    transfer_id         id;
    uint64_t            bytes_transferred;      // Raw bytes
    uint64_t            bytes_on_wire;          // Compressed bytes
    uint64_t            total_bytes;
    double              transfer_rate;          // Bytes/second
    double              effective_rate;         // With compression
    double              compression_ratio;
    duration            elapsed_time;
    duration            estimated_remaining;
    transfer_state_enum state;
    std::optional<std::string> error_message;
};

// Transfer result
struct transfer_result {
    transfer_id             id;
    std::filesystem::path   output_path;
    uint64_t                bytes_transferred;
    uint64_t                bytes_on_wire;
    bool                    verified;
    std::optional<error>    error;
    duration                elapsed_time;
    compression_statistics  compression_stats;
};
```

### 4.2 Data Flow

#### 4.2.1 Sender Data Flow

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  File on    │────▶│   Chunk     │────▶│ Compressed  │────▶│   Network   │
│    Disk     │     │   Buffer    │     │   Chunk     │     │   Buffer    │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
                           │                   │                   │
                           ▼                   ▼                   ▼
                    ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
                    │ CRC32 calc  │     │ LZ4 compress│     │ TCP/QUIC    │
                    │             │     │ (if mode    │     │   send      │
                    │             │     │  enabled)   │     │             │
                    └─────────────┘     └─────────────┘     └─────────────┘

Data sizes (256KB chunk):
- Raw chunk:        262,144 bytes
- After CRC32:      262,144 bytes + 4 byte checksum
- After compress:   ~130,000 bytes (typical for text, ~50% reduction)
- Wire format:      56 byte header + compressed data
```

#### 4.2.2 Receiver Data Flow

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   Network   │────▶│ Decompressed│────▶│  Reassembly │────▶│  File on    │
│   Buffer    │     │    Chunk    │     │    Buffer   │     │    Disk     │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
       │                   │                   │                   │
       ▼                   ▼                   ▼                   ▼
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│ Header parse│     │LZ4 decomp.  │     │ CRC32 verify│     │ SHA256      │
│             │     │(if compress │     │             │     │ verify      │
│             │     │  flag set)  │     │             │     │             │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
```

---

## 5. Interface Design

### 5.1 Protocol Interface

#### 5.1.1 Message Format

```
┌────────────────────────────────────────────────────────────────┐
│                      Protocol Frame                             │
├────────────────────────────────────────────────────────────────┤
│ Message Type (1 byte)                                          │
├────────────────────────────────────────────────────────────────┤
│ Payload Length (4 bytes, big-endian)                           │
├────────────────────────────────────────────────────────────────┤
│ Payload (variable length)                                       │
└────────────────────────────────────────────────────────────────┘

Total frame overhead: 5 bytes
```

#### 5.1.2 Message Types

```cpp
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
```

#### 5.1.3 Protocol State Machine

```
                    ┌─────────────────┐
                    │   DISCONNECTED  │
                    └────────┬────────┘
                             │ connect()
                             ▼
                    ┌─────────────────┐
                    │  CONNECTING     │
                    └────────┬────────┘
                             │ handshake complete
                             ▼
                    ┌─────────────────┐
          ┌────────│   CONNECTED     │────────┐
          │        └────────┬────────┘        │
          │                 │ transfer_request│
          │                 ▼                 │
          │        ┌─────────────────┐        │
          │ error  │  TRANSFERRING   │ cancel │
          │        └────────┬────────┘        │
          │                 │ complete        │
          │                 ▼                 │
          │        ┌─────────────────┐        │
          │        │   VERIFYING     │        │
          │        └────────┬────────┘        │
          │                 │ verified        │
          │                 ▼                 │
          │        ┌─────────────────┐        │
          └───────▶│   COMPLETED     │◀───────┘
                   └─────────────────┘
                             │ disconnect()
                             ▼
                   ┌─────────────────┐
                   │  DISCONNECTED   │
                   └─────────────────┘
```

### 5.2 Error Codes

Following SRS Section 6.4, error codes are in range **-700 to -799**:

```cpp
namespace kcenon::file_transfer::error {

// Transfer errors (-700 to -719)
constexpr int transfer_init_failed      = -700;
constexpr int transfer_cancelled        = -701;
constexpr int transfer_timeout          = -702;
constexpr int transfer_rejected         = -703;
constexpr int transfer_already_exists   = -704;
constexpr int transfer_not_found        = -705;

// Chunk errors (-720 to -739)
constexpr int chunk_checksum_error      = -720;
constexpr int chunk_sequence_error      = -721;
constexpr int chunk_size_error          = -722;
constexpr int file_hash_mismatch        = -723;
constexpr int chunk_timeout             = -724;
constexpr int chunk_duplicate           = -725;

// File I/O errors (-740 to -759)
constexpr int file_read_error           = -740;
constexpr int file_write_error          = -741;
constexpr int file_permission_error     = -742;
constexpr int file_not_found            = -743;
constexpr int disk_full                 = -744;
constexpr int invalid_path              = -745;

// Resume errors (-760 to -779)
constexpr int resume_state_invalid      = -760;
constexpr int resume_file_changed       = -761;
constexpr int resume_state_corrupted    = -762;
constexpr int resume_not_supported      = -763;

// Compression errors (-780 to -789)
constexpr int compression_failed        = -780;
constexpr int decompression_failed      = -781;
constexpr int compression_buffer_error  = -782;
constexpr int invalid_compression_data  = -783;

// Configuration errors (-790 to -799)
constexpr int config_invalid            = -790;
constexpr int config_chunk_size_error   = -791;
constexpr int config_transport_error    = -792;

// Helper function
[[nodiscard]] auto error_message(int code) -> std::string_view;

} // namespace kcenon::file_transfer::error
```

---

## 6. Algorithm Design

### 6.1 Adaptive Compression Algorithm

**SRS Trace**: SRS-COMP-003

```cpp
// Algorithm: Determine if data chunk is worth compressing
bool adaptive_compression::is_compressible(
    std::span<const std::byte> data,
    double threshold
) {
    // Step 1: Sample first 1KB (or less for small chunks)
    const auto sample_size = std::min(data.size(), std::size_t{1024});
    auto sample = data.first(sample_size);

    // Step 2: Allocate compression buffer
    auto max_size = lz4_engine::max_compressed_size(sample_size);
    std::vector<std::byte> compressed_buffer(max_size);

    // Step 3: Try compressing the sample
    auto result = lz4_engine::compress(sample, compressed_buffer);
    if (!result) {
        // Compression failed, assume incompressible
        return false;
    }

    auto compressed_size = result.value();

    // Step 4: Compare sizes
    // Only compress if we get at least (1-threshold) reduction
    // With threshold=0.9, we compress if compressed < 90% of original
    return static_cast<double>(compressed_size) <
           static_cast<double>(sample_size) * threshold;
}
```

**Complexity**: O(sample_size) - constant time for fixed sample size
**Latency**: < 100μs (per SRS requirement PERF-011)

### 6.2 Chunk Assembly Algorithm

**SRS Trace**: SRS-CHUNK-002

```cpp
// Algorithm: Reassemble file from possibly out-of-order chunks
Result<void> chunk_assembler::process_chunk(const chunk& c) {
    std::unique_lock lock(contexts_mutex_);

    // Step 1: Get or create assembly context
    auto& ctx = get_or_create_context(c.header.transfer_id);
    std::lock_guard ctx_lock(ctx.mutex);

    // Step 2: Verify CRC32 checksum
    if (!verify_crc32(c)) {
        return error::chunk_checksum_error;
    }

    // Step 3: Check for duplicate
    if (ctx.received_chunks[c.header.chunk_index]) {
        // Duplicate chunk - ignore (idempotent operation)
        return {};
    }

    // Step 4: Decompress if needed
    std::span<const std::byte> data_to_write = c.data;
    std::vector<std::byte> decompressed;

    if (has_flag(c.header.flags, chunk_flags::compressed)) {
        decompressed.resize(c.header.original_size);
        auto result = lz4_engine::decompress(
            c.data, decompressed, c.header.original_size);
        if (!result) {
            return error::decompression_failed;
        }
        data_to_write = decompressed;
    }

    // Step 5: Write to file at correct offset
    ctx.file.seekp(c.header.chunk_offset);
    ctx.file.write(
        reinterpret_cast<const char*>(data_to_write.data()),
        data_to_write.size()
    );

    // Step 6: Mark chunk as received
    ctx.received_chunks[c.header.chunk_index] = true;
    ctx.bytes_written += data_to_write.size();

    return {};
}
```

### 6.3 Pipeline Backpressure Algorithm

**SRS Trace**: SRS-PIPE-003

```cpp
// Bounded queue with backpressure
template<typename T>
void bounded_queue<T>::push(T item) {
    std::unique_lock lock(mutex_);

    // Block until space available (backpressure)
    not_full_.wait(lock, [this] {
        return queue_.size() < max_size_;
    });

    queue_.push(std::move(item));

    lock.unlock();
    not_empty_.notify_one();
}

template<typename T>
T bounded_queue<T>::pop() {
    std::unique_lock lock(mutex_);

    // Block until item available
    not_empty_.wait(lock, [this] {
        return !queue_.empty();
    });

    T item = std::move(queue_.front());
    queue_.pop();

    lock.unlock();
    not_full_.notify_one();

    return item;
}
```

**Memory Bound**: max_queue_size × chunk_size
**Example**: 16 queue items × 256KB = 4MB per queue

### 6.4 Transfer Resume Algorithm

**SRS Trace**: SRS-RESUME-002

```cpp
// Algorithm: Resume interrupted transfer
Result<void> resume_transfer(
    const transfer_id& id,
    const endpoint& destination
) {
    // Step 1: Load persisted state
    auto state_result = resume_handler_->load_state(id);
    if (!state_result) {
        return state_result.error();
    }
    auto state = state_result.value();

    // Step 2: Validate source file still exists and unchanged
    if (!std::filesystem::exists(state.file_path)) {
        return error::file_not_found;
    }

    auto current_hash = checksum::sha256_file(state.file_path);
    if (!current_hash || current_hash.value() != state.sha256_hash) {
        return error::resume_file_changed;
    }

    // Step 3: Connect and send resume request
    auto connect_result = transport_->connect(destination);
    if (!connect_result) {
        return connect_result.error();
    }

    // Step 4: Send resume request with chunk bitmap
    resume_request req{
        .transfer_id = id,
        .chunk_bitmap = state.chunk_bitmap
    };
    send_message(message_type::resume_request, req.serialize());

    // Step 5: Receive resume response with missing chunks
    auto response = receive_resume_response();
    if (!response) {
        return response.error();
    }

    // Step 6: Send only missing chunks
    auto splitter = chunk_splitter(config_.chunk_config);
    auto iterator = splitter.split(state.file_path, id);

    while (iterator.has_next()) {
        auto chunk = iterator.next();
        if (!chunk) {
            return chunk.error();
        }

        // Skip already-received chunks
        if (state.chunk_bitmap[chunk->header.chunk_index]) {
            continue;
        }

        // Send chunk through pipeline
        pipeline_->submit_chunk(std::move(chunk.value()));
    }

    return {};
}
```

---

## 7. Security Design

### 7.1 TLS Configuration

**SRS Trace**: SEC-001

```cpp
// TLS 1.3 configuration
struct tls_config {
    // Minimum TLS version
    static constexpr int min_version = TLS1_3_VERSION;

    // Cipher suites (TLS 1.3 only)
    static constexpr std::array<const char*, 3> cipher_suites = {
        "TLS_AES_256_GCM_SHA384",
        "TLS_CHACHA20_POLY1305_SHA256",
        "TLS_AES_128_GCM_SHA256"
    };

    // Certificate verification mode
    enum class verify_mode {
        none,           // No verification (testing only)
        peer,           // Verify peer certificate
        fail_if_no_cert // Require and verify peer certificate
    };

    verify_mode verification = verify_mode::peer;
    std::string cert_file;
    std::string key_file;
    std::string ca_file;
};
```

### 7.2 Path Traversal Prevention

**SRS Trace**: SEC-003

```cpp
// Validate output path to prevent directory traversal
Result<std::filesystem::path> validate_output_path(
    const std::filesystem::path& base_dir,
    const std::string& filename
) {
    // Step 1: Check for path traversal attempts
    if (filename.find("..") != std::string::npos) {
        return error::invalid_path;
    }

    // Step 2: Check for absolute path
    std::filesystem::path requested_path(filename);
    if (requested_path.is_absolute()) {
        return error::invalid_path;
    }

    // Step 3: Construct full path
    auto full_path = base_dir / filename;

    // Step 4: Canonicalize and verify it's under base_dir
    auto canonical = std::filesystem::weakly_canonical(full_path);
    auto canonical_base = std::filesystem::weakly_canonical(base_dir);

    // Check if canonical path starts with base directory
    auto [base_end, _] = std::mismatch(
        canonical_base.begin(), canonical_base.end(),
        canonical.begin()
    );

    if (base_end != canonical_base.end()) {
        return error::invalid_path;
    }

    return canonical;
}
```

---

## 8. Traceability Matrix

### 8.1 SRS to Design Traceability

| SRS ID | SRS Description | Design Component | Design Element |
|--------|-----------------|------------------|----------------|
| SRS-CORE-001 | Single File Send | file_sender | send_file() method |
| SRS-CORE-002 | Single File Receive | file_receiver | process_chunk() method |
| SRS-CORE-003 | Multi-file Batch | file_sender | send_files() method |
| SRS-CHUNK-001 | File Splitting | chunk_splitter | split() method |
| SRS-CHUNK-002 | File Assembly | chunk_assembler | process_chunk() method |
| SRS-CHUNK-003 | Chunk Checksum | checksum | crc32() method |
| SRS-CHUNK-004 | File Hash | checksum | sha256_file() method |
| SRS-COMP-001 | LZ4 Compression | lz4_engine | compress() method |
| SRS-COMP-002 | LZ4 Decompression | lz4_engine | decompress() method |
| SRS-COMP-003 | Adaptive Detection | adaptive_compression | is_compressible() method |
| SRS-COMP-004 | Compression Modes | compression_mode | enum definition |
| SRS-COMP-005 | Compression Stats | compression_statistics | struct definition |
| SRS-PIPE-001 | Sender Pipeline | sender_pipeline | Pipeline stages |
| SRS-PIPE-002 | Receiver Pipeline | receiver_pipeline | Pipeline stages |
| SRS-PIPE-003 | Backpressure | bounded_queue | push()/pop() blocking |
| SRS-PIPE-004 | Pipeline Stats | pipeline_statistics | struct definition |
| SRS-RESUME-001 | State Persistence | resume_handler | save_state() method |
| SRS-RESUME-002 | Transfer Resume | resume_handler | load_state() method |
| SRS-PROGRESS-001 | Progress Callbacks | progress_tracker | on_progress() callback |
| SRS-PROGRESS-002 | Transfer States | transfer_state_enum | enum definition |
| SRS-CONCURRENT-001 | Multiple Transfers | transfer_manager | Active transfers map |
| SRS-CONCURRENT-002 | Bandwidth Throttle | bandwidth_limiter | Token bucket algorithm |
| SRS-TRANS-001 | Transport Abstraction | transport_interface | Abstract class |
| SRS-TRANS-002 | TCP Transport | tcp_transport | Implementation class |
| SRS-TRANS-003 | QUIC Transport | quic_transport | Implementation class |
| SRS-TRANS-004 | Protocol Fallback | transport_factory | create_with_fallback() |

### 8.2 Design to Test Traceability

| Design Component | Test Category | Test File |
|------------------|---------------|-----------|
| chunk_splitter | Unit | chunk_splitter_test.cpp |
| chunk_assembler | Unit | chunk_assembler_test.cpp |
| lz4_engine | Unit | lz4_engine_test.cpp |
| adaptive_compression | Unit | adaptive_compression_test.cpp |
| bounded_queue | Unit | bounded_queue_test.cpp |
| sender_pipeline | Integration | sender_pipeline_test.cpp |
| receiver_pipeline | Integration | receiver_pipeline_test.cpp |
| file_sender | Integration | file_sender_test.cpp |
| file_receiver | Integration | file_receiver_test.cpp |
| End-to-end transfer | E2E | transfer_e2e_test.cpp |
| Compression throughput | Benchmark | compression_benchmark.cpp |
| Pipeline throughput | Benchmark | pipeline_benchmark.cpp |

---

## 9. Deployment Considerations

### 9.1 Build Configuration

```cmake
# CMakeLists.txt excerpt
cmake_minimum_required(VERSION 3.20)
project(file_trans_system VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
find_package(lz4 REQUIRED)
find_package(OpenSSL REQUIRED)

# Core library
add_library(file_trans_system
    src/core/file_sender.cpp
    src/core/file_receiver.cpp
    src/core/transfer_manager.cpp
    src/chunk/chunk_splitter.cpp
    src/chunk/chunk_assembler.cpp
    src/chunk/checksum.cpp
    src/compression/lz4_engine.cpp
    src/compression/adaptive_compression.cpp
    src/compression/chunk_compressor.cpp
    src/pipeline/sender_pipeline.cpp
    src/pipeline/receiver_pipeline.cpp
    src/pipeline/bounded_queue.cpp
    src/transport/tcp_transport.cpp
    src/resume/resume_handler.cpp
)

target_link_libraries(file_trans_system
    PUBLIC
        common_system
        thread_system
        network_system
        container_system
    PRIVATE
        lz4::lz4
        OpenSSL::SSL
        OpenSSL::Crypto
)
```

### 9.2 Platform-Specific Notes

| Platform | Consideration |
|----------|--------------|
| Linux | Use io_uring for async I/O (kernel 5.1+) |
| macOS | Use dispatch_io for async I/O |
| Windows | Use IOCP for async I/O |

---

## Appendix A: Revision History

| Version | Date | Author | Description |
|---------|------|--------|-------------|
| 1.0.0 | 2025-12-11 | kcenon@naver.com | Initial SDS creation |

---

*End of Document*
