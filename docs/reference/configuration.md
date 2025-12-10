# Configuration Guide

Complete configuration reference for the **file_trans_system** library.

## Table of Contents

1. [Quick Start](#quick-start)
2. [Sender Configuration](#sender-configuration)
3. [Receiver Configuration](#receiver-configuration)
4. [Pipeline Configuration](#pipeline-configuration)
5. [Compression Configuration](#compression-configuration)
6. [Transport Configuration](#transport-configuration)
7. [Performance Tuning](#performance-tuning)

---

## Quick Start

### Minimal Configuration

```cpp
// Sender with defaults
auto sender = file_sender::builder().build();

// Receiver with output directory
auto receiver = file_receiver::builder()
    .with_output_directory("/downloads")
    .build();
```

### Recommended Configuration

```cpp
// Sender - optimized for most use cases
auto sender = file_sender::builder()
    .with_compression(compression_mode::adaptive)
    .with_chunk_size(256 * 1024)  // 256KB
    .with_pipeline_config(pipeline_config::auto_detect())
    .build();

// Receiver - optimized for most use cases
auto receiver = file_receiver::builder()
    .with_output_directory("/downloads")
    .with_pipeline_config(pipeline_config::auto_detect())
    .build();
```

---

## Sender Configuration

### Builder Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `with_pipeline_config` | `pipeline_config` | auto-detect | Pipeline worker and queue configuration |
| `with_compression` | `compression_mode` | `adaptive` | Compression mode |
| `with_compression_level` | `compression_level` | `fast` | LZ4 compression level |
| `with_chunk_size` | `std::size_t` | 256KB | Chunk size (64KB-1MB) |
| `with_bandwidth_limit` | `std::size_t` | 0 (unlimited) | Bandwidth limit (bytes/sec) |
| `with_transport` | `transport_type` | `tcp` | Transport protocol |

### Examples

#### High Throughput Configuration

```cpp
auto sender = file_sender::builder()
    .with_compression(compression_mode::enabled)
    .with_compression_level(compression_level::fast)
    .with_chunk_size(512 * 1024)  // 512KB for better throughput
    .with_pipeline_config(pipeline_config{
        .io_read_workers = 4,
        .compression_workers = 8,
        .network_workers = 4,
        .send_queue_size = 128
    })
    .build();
```

#### Low Memory Configuration

```cpp
auto sender = file_sender::builder()
    .with_chunk_size(64 * 1024)   // Minimum 64KB
    .with_pipeline_config(pipeline_config{
        .io_read_workers = 1,
        .compression_workers = 2,
        .network_workers = 1,
        .read_queue_size = 4,
        .compress_queue_size = 8,
        .send_queue_size = 16
    })
    .build();
```

#### Bandwidth Limited Configuration

```cpp
auto sender = file_sender::builder()
    .with_bandwidth_limit(10 * 1024 * 1024)  // 10 MB/s
    .with_compression(compression_mode::enabled)  // Maximize effective throughput
    .build();
```

---

## Receiver Configuration

### Builder Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `with_pipeline_config` | `pipeline_config` | auto-detect | Pipeline configuration |
| `with_output_directory` | `std::filesystem::path` | Required | Output directory |
| `with_bandwidth_limit` | `std::size_t` | 0 (unlimited) | Bandwidth limit |
| `with_transport` | `transport_type` | `tcp` | Transport protocol |

### Examples

#### Standard Configuration

```cpp
auto receiver = file_receiver::builder()
    .with_output_directory("/var/data/incoming")
    .with_pipeline_config(pipeline_config::auto_detect())
    .build();
```

#### High Volume Configuration

```cpp
auto receiver = file_receiver::builder()
    .with_output_directory("/fast-ssd/incoming")
    .with_pipeline_config(pipeline_config{
        .network_workers = 4,
        .compression_workers = 8,
        .io_write_workers = 4,
        .write_queue_size = 32
    })
    .build();
```

---

## Pipeline Configuration

### Configuration Structure

```cpp
struct pipeline_config {
    // Worker counts per stage
    std::size_t io_read_workers      = 2;   // File reading
    std::size_t chunk_workers        = 2;   // Chunk processing
    std::size_t compression_workers  = 4;   // LZ4 compress/decompress
    std::size_t network_workers      = 2;   // Network I/O
    std::size_t io_write_workers     = 2;   // File writing

    // Queue sizes (backpressure control)
    std::size_t read_queue_size      = 16;  // Pending read chunks
    std::size_t compress_queue_size  = 32;  // Pending compression
    std::size_t send_queue_size      = 64;  // Pending network sends
    std::size_t decompress_queue_size = 32; // Pending decompression
    std::size_t write_queue_size     = 16;  // Pending file writes
};
```

### Auto-Detection

```cpp
// Automatically configure based on hardware
auto config = pipeline_config::auto_detect();
```

Auto-detection considers:
- Number of CPU cores
- Available memory
- Storage type (SSD vs HDD detection)

### Stage-by-Stage Tuning

#### I/O Read Stage

| Workers | Use Case |
|---------|----------|
| 1 | Single HDD |
| 2 | Single SSD (default) |
| 4 | NVMe SSD or RAID |

```cpp
// For NVMe storage
config.io_read_workers = 4;
config.read_queue_size = 32;
```

#### Compression Stage

| Workers | Use Case |
|---------|----------|
| 2 | Dual-core CPU |
| 4 | Quad-core CPU (default) |
| 8+ | High-core-count server |

```cpp
// For 16-core server
config.compression_workers = 12;
config.compress_queue_size = 64;
```

#### Network Stage

| Workers | Use Case |
|---------|----------|
| 1 | Single connection |
| 2 | Standard use (default) |
| 4 | High-bandwidth network |

```cpp
// For 10Gbps network
config.network_workers = 4;
config.send_queue_size = 128;
```

### Memory Calculation

Memory usage per queue:
```
Queue Memory = queue_size × chunk_size
```

Total pipeline memory (sender):
```
read_queue_size × chunk_size
+ compress_queue_size × chunk_size
+ send_queue_size × chunk_size
```

Example (default configuration, 256KB chunks):
```
16 × 256KB + 32 × 256KB + 64 × 256KB
= 4MB + 8MB + 16MB = 28MB
```

---

## Compression Configuration

### Compression Modes

| Mode | Description | Best For |
|------|-------------|----------|
| `disabled` | No compression | Pre-compressed files (ZIP, media) |
| `enabled` | Always compress | Text, logs, source code |
| `adaptive` | Auto-detect | Mixed content (default) |

### Compression Levels

| Level | Speed | Ratio | Best For |
|-------|-------|-------|----------|
| `fast` | ~400 MB/s | ~2.1:1 | Most use cases |
| `high_compression` | ~50 MB/s | ~2.7:1 | Archival, bandwidth-limited |

### Adaptive Compression Threshold

Adaptive mode compresses data if the sample achieves >= 10% reduction.

```cpp
// Internal logic
bool should_compress = compressed_sample_size < original_sample_size * 0.9;
```

### File Type Heuristics

| File Extensions | Default Action |
|-----------------|----------------|
| `.txt`, `.log`, `.json`, `.xml` | Compress |
| `.cpp`, `.h`, `.py`, `.java` | Compress |
| `.csv`, `.html`, `.css`, `.js` | Compress |
| `.zip`, `.gz`, `.tar.gz`, `.bz2` | Skip |
| `.jpg`, `.png`, `.mp4`, `.mp3` | Skip |
| `.exe`, `.dll`, `.so` | Test (adaptive) |

### Per-Transfer Options

```cpp
// Override global settings for specific transfer
transfer_options opts{
    .compression = compression_mode::enabled,
    .level = compression_level::high_compression
};

sender->send_file(path, endpoint, opts);
```

---

## Transport Configuration

### TCP Transport (Default)

```cpp
struct tcp_transport_config {
    bool        enable_tls      = true;        // TLS 1.3
    bool        tcp_nodelay     = true;        // Disable Nagle's algorithm
    std::size_t send_buffer     = 256 * 1024;  // 256KB
    std::size_t recv_buffer     = 256 * 1024;  // 256KB
    duration    connect_timeout = 10s;
    duration    read_timeout    = 30s;
};
```

#### TCP Tuning Examples

```cpp
// High-latency network (WAN)
tcp_transport_config config{
    .send_buffer = 1024 * 1024,   // 1MB
    .recv_buffer = 1024 * 1024,   // 1MB
    .connect_timeout = 30s,
    .read_timeout = 60s
};
```

```cpp
// Low-latency network (LAN)
tcp_transport_config config{
    .tcp_nodelay = true,          // Minimize latency
    .send_buffer = 128 * 1024,    // 128KB
    .recv_buffer = 128 * 1024,    // 128KB
    .connect_timeout = 5s,
    .read_timeout = 15s
};
```

### QUIC Transport (Phase 2)

```cpp
struct quic_transport_config {
    bool        enable_0rtt         = true;             // Fast reconnection
    std::size_t max_streams         = 100;              // Concurrent streams
    std::size_t initial_window      = 10 * 1024 * 1024; // 10MB
    duration    idle_timeout        = 30s;
    bool        enable_migration    = true;             // IP change support
};
```

#### When to Use QUIC

| Condition | Recommendation |
|-----------|----------------|
| Stable network (LAN) | TCP |
| High packet loss (>0.5%) | QUIC |
| Mobile network | QUIC |
| Multiple concurrent transfers | QUIC |
| Firewall restrictions (UDP blocked) | TCP |

---

## Performance Tuning

### Throughput Optimization

#### 1. Increase Chunk Size

Larger chunks reduce per-chunk overhead.

```cpp
.with_chunk_size(512 * 1024)  // 512KB instead of 256KB
```

#### 2. Scale Compression Workers

Compression is often the bottleneck.

```cpp
// For CPU-bound workloads
config.compression_workers = std::thread::hardware_concurrency() - 2;
```

#### 3. Increase Queue Sizes

More queue depth increases parallelism.

```cpp
config.send_queue_size = 128;  // More in-flight chunks
```

### Memory Optimization

#### 1. Reduce Queue Sizes

```cpp
pipeline_config config{
    .read_queue_size = 4,
    .compress_queue_size = 8,
    .send_queue_size = 16
};
```

#### 2. Use Smaller Chunks

```cpp
.with_chunk_size(64 * 1024)  // 64KB minimum
```

### Latency Optimization

#### 1. Enable TCP_NODELAY

```cpp
tcp_transport_config config{
    .tcp_nodelay = true  // Default
};
```

#### 2. Reduce Queue Sizes

Smaller queues mean faster processing of new data.

```cpp
config.send_queue_size = 16;
```

### Network Optimization

#### High Bandwidth Network

```cpp
// 10Gbps network
pipeline_config config{
    .network_workers = 4,
    .send_queue_size = 256
};

tcp_transport_config tcp_config{
    .send_buffer = 2 * 1024 * 1024,  // 2MB
    .recv_buffer = 2 * 1024 * 1024
};
```

#### High Latency Network

```cpp
// 100ms latency WAN
pipeline_config config{
    .send_queue_size = 256  // Many in-flight chunks
};

tcp_transport_config tcp_config{
    .send_buffer = 4 * 1024 * 1024,  // 4MB
    .read_timeout = 60s
};
```

---

## Configuration Best Practices

### 1. Start with Auto-Detection

```cpp
auto config = pipeline_config::auto_detect();
```

### 2. Measure Before Tuning

```cpp
// Get pipeline statistics
auto stats = sender->get_pipeline_stats();
auto bottleneck = stats.bottleneck_stage();

// Tune the bottleneck stage
```

### 3. Monitor Queue Depths

```cpp
auto depths = sender->get_queue_depths();
if (depths.compress_queue > config.compress_queue_size * 0.9) {
    // Compression is bottleneck - add workers
}
```

### 4. Test Configuration Changes

```cpp
// Benchmark before and after
auto start = std::chrono::steady_clock::now();
sender->send_file(test_file, endpoint);
auto duration = std::chrono::steady_clock::now() - start;
```

---

## Configuration Validation

Configurations are validated at build time:

```cpp
auto sender = file_sender::builder()
    .with_chunk_size(32 * 1024)  // Error: below minimum
    .build();

if (!sender) {
    // Error code: -791 (config_chunk_size_error)
    std::cerr << sender.error().message() << "\n";
}
```

### Validation Rules

| Parameter | Constraint |
|-----------|------------|
| chunk_size | 64KB <= size <= 1MB |
| *_workers | >= 1 |
| *_queue_size | >= 1 |
| bandwidth_limit | >= 0 (0 = unlimited) |

---

*Last updated: 2025-12-11*
