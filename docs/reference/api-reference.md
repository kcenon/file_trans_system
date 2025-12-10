# API Reference

Complete API documentation for the **file_trans_system** library.

## Table of Contents

1. [Core Classes](#core-classes)
   - [file_sender](#file_sender)
   - [file_receiver](#file_receiver)
   - [transfer_manager](#transfer_manager)
2. [Data Types](#data-types)
   - [Enumerations](#enumerations)
   - [Structures](#structures)
3. [Chunk Management](#chunk-management)
4. [Compression](#compression)
5. [Pipeline](#pipeline)
6. [Transport](#transport)

---

## Core Classes

### file_sender

Primary class for sending files to remote endpoints.

#### Builder Pattern

```cpp
namespace kcenon::file_transfer {

class file_sender {
public:
    class builder {
    public:
        // Configure pipeline worker counts and queue sizes
        builder& with_pipeline_config(const pipeline_config& config);

        // Set compression mode (disabled, enabled, adaptive)
        builder& with_compression(compression_mode mode);

        // Set compression level (fast, high_compression)
        builder& with_compression_level(compression_level level);

        // Set chunk size (64KB - 1MB, default: 256KB)
        builder& with_chunk_size(std::size_t size);

        // Set bandwidth limit in bytes per second (0 = unlimited)
        builder& with_bandwidth_limit(std::size_t bytes_per_second);

        // Set transport type (tcp, quic)
        builder& with_transport(transport_type type);

        // Build the sender instance
        [[nodiscard]] auto build() -> Result<file_sender>;
    };
};

}
```

#### Methods

##### send_file()

Send a single file to a remote endpoint.

```cpp
[[nodiscard]] auto send_file(
    const std::filesystem::path& file_path,
    const endpoint& destination,
    const transfer_options& options = {}
) -> Result<transfer_handle>;
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `file_path` | `std::filesystem::path` | Path to the file to send |
| `destination` | `endpoint` | Remote endpoint (IP, port) |
| `options` | `transfer_options` | Optional transfer configuration |

**Returns:** `Result<transfer_handle>` - Handle for tracking the transfer

**Example:**
```cpp
auto sender = file_sender::builder()
    .with_compression(compression_mode::adaptive)
    .with_chunk_size(512 * 1024)  // 512KB chunks
    .build();

if (sender) {
    auto handle = sender->send_file(
        "/path/to/large_file.dat",
        endpoint{"192.168.1.100", 8080}
    );

    if (handle) {
        std::cout << "Transfer ID: " << handle->id.to_string() << "\n";
    }
}
```

##### send_files()

Send multiple files in a batch operation.

```cpp
[[nodiscard]] auto send_files(
    std::span<const std::filesystem::path> files,
    const endpoint& destination,
    const transfer_options& options = {}
) -> Result<batch_transfer_handle>;
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `files` | `std::span<const path>` | List of file paths to send |
| `destination` | `endpoint` | Remote endpoint |
| `options` | `transfer_options` | Optional transfer configuration |

**Returns:** `Result<batch_transfer_handle>` - Handle for tracking the batch

##### cancel()

Cancel an active transfer.

```cpp
[[nodiscard]] auto cancel(const transfer_id& id) -> Result<void>;
```

##### pause() / resume()

Pause and resume a transfer.

```cpp
[[nodiscard]] auto pause(const transfer_id& id) -> Result<void>;
[[nodiscard]] auto resume(const transfer_id& id) -> Result<void>;
```

##### on_progress()

Register a callback for progress updates.

```cpp
void on_progress(std::function<void(const transfer_progress&)> callback);
```

**Example:**
```cpp
sender->on_progress([](const transfer_progress& p) {
    double percent = 100.0 * p.bytes_transferred / p.total_bytes;
    std::cout << std::fixed << std::setprecision(1)
              << percent << "% - "
              << p.transfer_rate / (1024*1024) << " MB/s"
              << " (compression: " << p.compression_ratio << ":1)\n";
});
```

---

### file_receiver

Primary class for receiving files from remote senders.

#### Builder Pattern

```cpp
class file_receiver {
public:
    class builder {
    public:
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_output_directory(const std::filesystem::path& dir);
        builder& with_bandwidth_limit(std::size_t bytes_per_second);
        builder& with_transport(transport_type type);
        [[nodiscard]] auto build() -> Result<file_receiver>;
    };
};
```

#### Methods

##### start() / stop()

Start and stop the receiver.

```cpp
[[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;
[[nodiscard]] auto stop() -> Result<void>;
```

**Example:**
```cpp
auto receiver = file_receiver::builder()
    .with_output_directory("/downloads")
    .build();

if (receiver) {
    receiver->start(endpoint{"0.0.0.0", 8080});

    // ... wait for transfers ...

    receiver->stop();
}
```

##### set_output_directory()

Change the output directory at runtime.

```cpp
void set_output_directory(const std::filesystem::path& dir);
```

##### Callbacks

```cpp
// Accept or reject incoming transfers
void on_transfer_request(std::function<bool(const transfer_request&)> callback);

// Progress updates
void on_progress(std::function<void(const transfer_progress&)> callback);

// Transfer completion
void on_complete(std::function<void(const transfer_result&)> callback);
```

**Example:**
```cpp
receiver->on_transfer_request([](const transfer_request& req) {
    // Check total size
    uint64_t total_size = 0;
    for (const auto& file : req.files) {
        total_size += file.file_size;
    }

    // Accept if under 10GB
    return total_size < 10ULL * 1024 * 1024 * 1024;
});

receiver->on_complete([](const transfer_result& result) {
    if (result.verified) {
        std::cout << "Received: " << result.output_path << "\n";
        std::cout << "Compression ratio: "
                  << result.compression_stats.compression_ratio() << ":1\n";
    } else {
        std::cerr << "Transfer failed: " << result.error->message << "\n";
    }
});
```

---

### transfer_manager

Manages multiple concurrent transfers and provides statistics.

#### Builder Pattern

```cpp
class transfer_manager {
public:
    class builder {
    public:
        builder& with_max_concurrent(std::size_t max_count);
        builder& with_default_compression(compression_mode mode);
        builder& with_global_bandwidth_limit(std::size_t bytes_per_second);
        [[nodiscard]] auto build() -> Result<transfer_manager>;
    };
};
```

#### Methods

##### get_status()

Get status of a specific transfer.

```cpp
[[nodiscard]] auto get_status(const transfer_id& id) -> Result<transfer_status>;
```

##### list_transfers()

List all active transfers.

```cpp
[[nodiscard]] auto list_transfers() -> Result<std::vector<transfer_info>>;
```

##### get_statistics()

Get aggregate transfer statistics.

```cpp
[[nodiscard]] auto get_statistics() -> transfer_statistics;
[[nodiscard]] auto get_compression_stats() -> compression_statistics;
```

##### Configuration

```cpp
void set_bandwidth_limit(std::size_t bytes_per_second);
void set_max_concurrent_transfers(std::size_t max_count);
void set_default_compression(compression_mode mode);
```

---

## Data Types

### Enumerations

#### compression_mode

```cpp
enum class compression_mode {
    disabled,   // No compression
    enabled,    // Always compress
    adaptive    // Auto-detect compressibility (default)
};
```

#### compression_level

```cpp
enum class compression_level {
    fast,             // LZ4 standard (~400 MB/s)
    high_compression  // LZ4-HC (~50 MB/s, better ratio)
};
```

#### transport_type

```cpp
enum class transport_type {
    tcp,    // TCP + TLS 1.3 (default)
    quic    // QUIC (Phase 2)
};
```

#### transfer_state_enum

```cpp
enum class transfer_state_enum {
    pending,        // Waiting to start
    initializing,   // Setting up connection
    transferring,   // Active transfer
    verifying,      // Verifying integrity
    completed,      // Successfully completed
    failed,         // Transfer failed
    cancelled       // User cancelled
};
```

#### chunk_flags

```cpp
enum class chunk_flags : uint8_t {
    none            = 0x00,
    first_chunk     = 0x01,
    last_chunk      = 0x02,
    compressed      = 0x04,
    encrypted       = 0x08
};
```

#### pipeline_stage

```cpp
enum class pipeline_stage : uint8_t {
    io_read,        // File read operations
    chunk_process,  // Chunk assembly/disassembly
    compression,    // LZ4 compress/decompress
    network,        // Network send/receive
    io_write        // File write operations
};
```

---

### Structures

#### endpoint

```cpp
struct endpoint {
    std::string address;
    uint16_t    port;
};
```

#### transfer_id

```cpp
struct transfer_id {
    std::array<uint8_t, 16> bytes;  // UUID

    [[nodiscard]] static auto generate() -> transfer_id;
    [[nodiscard]] auto to_string() const -> std::string;
    [[nodiscard]] static auto from_string(std::string_view str) -> Result<transfer_id>;
};
```

#### transfer_options

```cpp
struct transfer_options {
    compression_mode            compression     = compression_mode::adaptive;
    compression_level           level           = compression_level::fast;
    std::size_t                 chunk_size      = 256 * 1024;  // 256KB
    bool                        verify_checksum = true;
    std::optional<std::size_t>  bandwidth_limit;
    std::optional<int>          priority;
};
```

#### transfer_progress

```cpp
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
```

#### transfer_result

```cpp
struct transfer_result {
    transfer_id             id;
    std::filesystem::path   output_path;
    uint64_t                bytes_transferred;
    uint64_t                bytes_on_wire;
    bool                    verified;           // SHA-256 match
    std::optional<error>    error;
    duration                elapsed_time;
    compression_statistics  compression_stats;
};
```

#### file_metadata

```cpp
struct file_metadata {
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    std::filesystem::perms  permissions;
    std::chrono::system_clock::time_point modified_time;
    bool                    compressible_hint;
};
```

#### chunk_config

```cpp
struct chunk_config {
    std::size_t chunk_size     = 256 * 1024;    // 256KB default
    std::size_t min_chunk_size = 64 * 1024;     // 64KB minimum
    std::size_t max_chunk_size = 1024 * 1024;   // 1MB maximum
};
```

#### pipeline_config

```cpp
struct pipeline_config {
    // Worker counts per stage
    std::size_t io_read_workers      = 2;
    std::size_t chunk_workers        = 2;
    std::size_t compression_workers  = 4;
    std::size_t network_workers      = 2;
    std::size_t io_write_workers     = 2;

    // Queue sizes (backpressure)
    std::size_t read_queue_size      = 16;
    std::size_t compress_queue_size  = 32;
    std::size_t send_queue_size      = 64;
    std::size_t decompress_queue_size = 32;
    std::size_t write_queue_size     = 16;

    [[nodiscard]] static auto auto_detect() -> pipeline_config;
};
```

#### compression_statistics

```cpp
struct compression_statistics {
    std::atomic<uint64_t> total_raw_bytes{0};
    std::atomic<uint64_t> total_compressed_bytes{0};
    std::atomic<uint64_t> chunks_compressed{0};
    std::atomic<uint64_t> chunks_skipped{0};
    std::atomic<uint64_t> compression_time_us{0};
    std::atomic<uint64_t> decompression_time_us{0};

    [[nodiscard]] auto compression_ratio() const -> double;
    [[nodiscard]] auto compression_speed_mbps() const -> double;
    [[nodiscard]] auto decompression_speed_mbps() const -> double;
};
```

---

## Chunk Management

### chunk_splitter

Splits files into chunks for streaming transfer.

```cpp
class chunk_splitter {
public:
    explicit chunk_splitter(const chunk_config& config);

    [[nodiscard]] auto split(
        const std::filesystem::path& file_path,
        const transfer_id& id
    ) -> Result<chunk_iterator>;

    [[nodiscard]] auto calculate_metadata(
        const std::filesystem::path& file_path
    ) -> Result<file_metadata>;
};
```

### chunk_assembler

Reassembles received chunks into files.

```cpp
class chunk_assembler {
public:
    explicit chunk_assembler(const std::filesystem::path& output_dir);

    [[nodiscard]] auto process_chunk(const chunk& c) -> Result<void>;
    [[nodiscard]] auto is_complete(const transfer_id& id) const -> bool;
    [[nodiscard]] auto get_missing_chunks(const transfer_id& id) const
        -> std::vector<uint64_t>;
    [[nodiscard]] auto finalize(
        const transfer_id& id,
        const std::string& expected_hash
    ) -> Result<std::filesystem::path>;
};
```

### checksum

Integrity verification utilities.

```cpp
class checksum {
public:
    // CRC32 for chunks
    [[nodiscard]] static auto crc32(std::span<const std::byte> data) -> uint32_t;
    [[nodiscard]] static auto verify_crc32(
        std::span<const std::byte> data,
        uint32_t expected
    ) -> bool;

    // SHA-256 for files
    [[nodiscard]] static auto sha256_file(const std::filesystem::path& path)
        -> Result<std::string>;
    [[nodiscard]] static auto verify_sha256(
        const std::filesystem::path& path,
        const std::string& expected
    ) -> bool;
};
```

---

## Compression

### lz4_engine

Low-level LZ4 compression interface.

```cpp
class lz4_engine {
public:
    // Standard LZ4 compression (~400 MB/s)
    [[nodiscard]] static auto compress(
        std::span<const std::byte> input,
        std::span<std::byte> output
    ) -> Result<std::size_t>;

    // LZ4-HC compression (~50 MB/s, better ratio)
    [[nodiscard]] static auto compress_hc(
        std::span<const std::byte> input,
        std::span<std::byte> output,
        int level = 9
    ) -> Result<std::size_t>;

    // Decompression (~1.5 GB/s)
    [[nodiscard]] static auto decompress(
        std::span<const std::byte> compressed,
        std::span<std::byte> output,
        std::size_t original_size
    ) -> Result<std::size_t>;

    // Buffer sizing
    [[nodiscard]] static auto max_compressed_size(std::size_t input_size)
        -> std::size_t;
};
```

### adaptive_compression

Automatic compressibility detection.

```cpp
class adaptive_compression {
public:
    // Sample-based compressibility check (<100us)
    [[nodiscard]] static auto is_compressible(
        std::span<const std::byte> data,
        double threshold = 0.9
    ) -> bool;

    // File extension heuristic
    [[nodiscard]] static auto is_likely_compressible(
        const std::filesystem::path& file
    ) -> bool;
};
```

### chunk_compressor

High-level chunk compression with statistics.

```cpp
class chunk_compressor {
public:
    explicit chunk_compressor(
        compression_mode mode = compression_mode::adaptive,
        compression_level level = compression_level::fast
    );

    [[nodiscard]] auto compress(const chunk& input) -> Result<chunk>;
    [[nodiscard]] auto decompress(const chunk& input) -> Result<chunk>;
    [[nodiscard]] auto get_statistics() const -> compression_statistics;
    void reset_statistics();
};
```

---

## Pipeline

### sender_pipeline

Multi-stage sender processing pipeline.

```cpp
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

    [[nodiscard]] auto submit(
        const std::filesystem::path& file,
        const transfer_id& id,
        const transfer_options& options
    ) -> Result<void>;

    [[nodiscard]] auto get_stats() const -> pipeline_statistics;
    [[nodiscard]] auto get_queue_depths() const -> queue_depth_info;
};
```

### receiver_pipeline

Multi-stage receiver processing pipeline.

```cpp
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

    [[nodiscard]] auto submit_chunk(chunk c) -> Result<void>;
    [[nodiscard]] auto get_stats() const -> pipeline_statistics;
    [[nodiscard]] auto get_queue_depths() const -> queue_depth_info;
};
```

---

## Transport

### transport_interface

Abstract transport layer interface.

```cpp
class transport_interface {
public:
    virtual ~transport_interface() = default;

    [[nodiscard]] virtual auto connect(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto disconnect() -> Result<void> = 0;
    [[nodiscard]] virtual auto is_connected() const -> bool = 0;

    [[nodiscard]] virtual auto send(std::span<const std::byte> data) -> Result<void> = 0;
    [[nodiscard]] virtual auto receive(std::span<std::byte> buffer) -> Result<std::size_t> = 0;

    // QUIC-specific (no-op for TCP)
    [[nodiscard]] virtual auto create_stream() -> Result<stream_id>;
    [[nodiscard]] virtual auto close_stream(stream_id) -> Result<void>;

    [[nodiscard]] virtual auto listen(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto accept() -> Result<std::unique_ptr<transport_interface>> = 0;
};
```

### Transport Configurations

```cpp
struct tcp_transport_config {
    bool        enable_tls      = true;
    bool        tcp_nodelay     = true;
    std::size_t send_buffer     = 256 * 1024;
    std::size_t recv_buffer     = 256 * 1024;
    duration    connect_timeout = 10s;
    duration    read_timeout    = 30s;
};

struct quic_transport_config {
    bool        enable_0rtt         = true;
    std::size_t max_streams         = 100;
    std::size_t initial_window      = 10 * 1024 * 1024;
    duration    idle_timeout        = 30s;
    bool        enable_migration    = true;
};
```

### transport_factory

Factory for creating transport instances.

```cpp
class transport_factory {
public:
    [[nodiscard]] static auto create(transport_type type)
        -> std::unique_ptr<transport_interface>;

    [[nodiscard]] static auto create_with_fallback(
        const endpoint& ep,
        transport_type preferred = transport_type::quic
    ) -> Result<std::unique_ptr<transport_interface>>;
};
```

---

*Last updated: 2025-12-11*
