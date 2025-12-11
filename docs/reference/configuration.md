# Configuration Guide

Complete configuration reference for the **file_trans_system** library.

**Version:** 0.2.0
**Architecture:** Client-Server Model

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Server Configuration](#server-configuration)
3. [Client Configuration](#client-configuration)
4. [Storage Configuration](#storage-configuration)
5. [Reconnection Configuration](#reconnection-configuration)
6. [Pipeline Configuration](#pipeline-configuration)
7. [Compression Configuration](#compression-configuration)
8. [Transport Configuration](#transport-configuration)
9. [Security Configuration](#security-configuration)
10. [Performance Tuning](#performance-tuning)

---

## Quick Start

### Minimal Configuration

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

// Server with storage directory
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .build();

// Client with defaults
auto client = file_transfer_client::builder().build();
```

### Recommended Configuration

```cpp
// Server - optimized for production
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_max_connections(100)
    .with_max_file_size(10ULL * 1024 * 1024 * 1024)  // 10GB
    .with_storage_quota(1ULL * 1024 * 1024 * 1024 * 1024)  // 1TB
    .with_pipeline_config(pipeline_config::auto_detect())
    .build();

// Client - optimized for production
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::adaptive)
    .with_chunk_size(256 * 1024)  // 256KB
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy::exponential_backoff())
    .with_pipeline_config(pipeline_config::auto_detect())
    .build();
```

---

## Server Configuration

### Builder Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `with_storage_directory` | `std::filesystem::path` | **Required** | Root directory for file storage |
| `with_max_connections` | `std::size_t` | 100 | Maximum concurrent client connections |
| `with_max_file_size` | `uint64_t` | 10GB | Maximum allowed file size |
| `with_storage_quota` | `uint64_t` | 0 (unlimited) | Total storage quota |
| `with_pipeline_config` | `pipeline_config` | auto-detect | Pipeline worker configuration |
| `with_transport` | `transport_type` | `tcp` | Transport protocol |
| `with_connection_timeout` | `duration` | 30s | Connection idle timeout |
| `with_request_timeout` | `duration` | 10s | Request response timeout |

### Server Examples

#### Basic Server

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/var/data/files")
    .build();

server->start(endpoint{"0.0.0.0", 19000});
```

#### High-Capacity Server

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/fast-nvme/storage")
    .with_max_connections(500)
    .with_max_file_size(50ULL * 1024 * 1024 * 1024)  // 50GB
    .with_storage_quota(10ULL * 1024 * 1024 * 1024 * 1024)  // 10TB
    .with_pipeline_config(pipeline_config{
        .network_workers = 8,
        .compression_workers = 16,
        .io_write_workers = 8,
        .recv_queue_size = 256,
        .write_queue_size = 64
    })
    .build();
```

#### Restricted Server

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/uploads")
    .with_max_connections(10)
    .with_max_file_size(100 * 1024 * 1024)  // 100MB limit
    .with_storage_quota(10ULL * 1024 * 1024 * 1024)  // 10GB total
    .with_connection_timeout(5min)
    .build();

// Custom validation callbacks
server->on_upload_request([](const upload_request& req) {
    // Only allow specific extensions
    auto ext = std::filesystem::path(req.filename).extension();
    return ext == ".pdf" || ext == ".doc" || ext == ".txt";
});

server->on_download_request([](const download_request& req) {
    // Allow all downloads
    return true;
});
```

### Server Callbacks

```cpp
// Upload request validation
server->on_upload_request([](const upload_request& req) -> bool {
    // Validate file size
    if (req.file_size > 1e9) return false;  // Reject > 1GB

    // Validate filename
    if (req.filename.find("..") != std::string::npos) return false;

    return true;
});

// Download request validation
server->on_download_request([](const download_request& req) -> bool {
    // Check access permissions (custom logic)
    return has_access(req.client_id, req.filename);
});

// Connection events
server->on_client_connected([](const client_info& info) {
    log_info("Client connected: {}", info.address);
});

server->on_client_disconnected([](const client_info& info, disconnect_reason reason) {
    log_info("Client disconnected: {} ({})", info.address, to_string(reason));
});

// Transfer events
server->on_upload_complete([](const transfer_result& result) {
    log_info("Upload complete: {} ({} bytes)", result.filename, result.file_size);
});

server->on_download_complete([](const transfer_result& result) {
    log_info("Download complete: {} to {}", result.filename, result.client_address);
});
```

---

## Client Configuration

### Builder Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `with_pipeline_config` | `pipeline_config` | auto-detect | Pipeline configuration |
| `with_compression` | `compression_mode` | `adaptive` | Compression mode |
| `with_compression_level` | `compression_level` | `fast` | LZ4 compression level |
| `with_chunk_size` | `std::size_t` | 256KB | Chunk size (64KB-1MB) |
| `with_bandwidth_limit` | `std::size_t` | 0 (unlimited) | Bandwidth limit (bytes/sec) |
| `with_transport` | `transport_type` | `tcp` | Transport protocol |
| `with_auto_reconnect` | `bool` | true | Enable automatic reconnection |
| `with_reconnect_policy` | `reconnect_policy` | exponential | Reconnection strategy |
| `with_connect_timeout` | `duration` | 10s | Connection timeout |
| `with_request_timeout` | `duration` | 30s | Request timeout |

### Client Examples

#### Basic Client

```cpp
auto client = file_transfer_client::builder().build();

auto result = client->connect(endpoint{"192.168.1.100", 19000});
if (result) {
    client->upload_file("/local/report.pdf", "report.pdf");
}
```

#### High-Throughput Client

```cpp
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::enabled)
    .with_compression_level(compression_level::fast)
    .with_chunk_size(512 * 1024)  // 512KB chunks
    .with_pipeline_config(pipeline_config{
        .io_read_workers = 4,
        .compression_workers = 8,
        .network_workers = 4,
        .send_queue_size = 128
    })
    .build();
```

#### Low-Memory Client

```cpp
auto client = file_transfer_client::builder()
    .with_chunk_size(64 * 1024)  // Minimum 64KB
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

#### Bandwidth-Limited Client

```cpp
auto client = file_transfer_client::builder()
    .with_bandwidth_limit(10 * 1024 * 1024)  // 10 MB/s
    .with_compression(compression_mode::enabled)  // Maximize effective throughput
    .build();
```

#### Mobile/Unreliable Network Client

```cpp
auto client = file_transfer_client::builder()
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy{
        .initial_delay = std::chrono::milliseconds(500),
        .max_delay = std::chrono::seconds(30),
        .multiplier = 1.5,
        .max_attempts = 20
    })
    .with_chunk_size(64 * 1024)  // Smaller chunks for faster recovery
    .build();
```

### Client Callbacks

```cpp
// Connection events
client->on_connected([](const server_info& info) {
    log_info("Connected to server: {}", info.address);
});

client->on_disconnected([](disconnect_reason reason) {
    log_warning("Disconnected: {}", to_string(reason));
});

client->on_reconnecting([](int attempt, duration delay) {
    log_info("Reconnecting (attempt {}, waiting {}ms)", attempt, delay.count());
});

client->on_reconnected([]() {
    log_info("Reconnected successfully");
});

// Transfer progress
client->on_progress([](const transfer_progress& p) {
    double percent = 100.0 * p.bytes_transferred / p.total_bytes;
    log_info("{}% - {} MB/s", percent, p.transfer_rate / 1e6);
});
```

---

## Storage Configuration

Server-side storage management configuration.

### Storage Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `with_storage_directory` | `path` | **Required** | Root storage path |
| `with_storage_quota` | `uint64_t` | 0 | Total quota (0 = unlimited) |
| `with_max_file_size` | `uint64_t` | 10GB | Max single file size |
| `with_temp_directory` | `path` | storage/temp | Temporary upload directory |
| `with_filename_validator` | `function` | default | Custom filename validation |

### Storage Structure

```
storage_directory/
├── files/           # Completed uploads
├── temp/            # In-progress uploads
└── metadata/        # File metadata (optional)
```

### Custom Filename Validation

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_filename_validator([](const std::string& filename) -> Result<void> {
        // Check length
        if (filename.length() > 255) {
            return Error{error::invalid_filename, "Filename too long"};
        }

        // Check for invalid characters
        static const std::string invalid = "<>:\"/\\|?*";
        if (filename.find_first_of(invalid) != std::string::npos) {
            return Error{error::invalid_filename, "Invalid characters in filename"};
        }

        // Check for path traversal
        if (filename.find("..") != std::string::npos) {
            return Error{error::invalid_filename, "Path traversal not allowed"};
        }

        return {};
    })
    .build();
```

### Quota Management

```cpp
// Check storage status
auto status = server->get_storage_status();
log_info("Storage: {} / {} bytes used ({:.1f}%)",
    status.used_bytes,
    status.quota_bytes,
    100.0 * status.used_bytes / status.quota_bytes);

// Monitor storage events
server->on_storage_warning([](const storage_warning& warning) {
    if (warning.percent_used > 90) {
        log_warning("Storage nearly full: {}%", warning.percent_used);
    }
});

server->on_storage_full([]() {
    log_error("Storage quota exceeded");
    // Trigger cleanup or notification
});
```

---

## Reconnection Configuration

Client automatic reconnection settings.

### Reconnection Policy Structure

```cpp
struct reconnect_policy {
    duration    initial_delay = 1s;      // First retry delay
    duration    max_delay     = 30s;     // Maximum delay cap
    double      multiplier    = 2.0;     // Exponential backoff factor
    std::size_t max_attempts  = 10;      // Maximum retry attempts (0 = infinite)
    bool        jitter        = true;    // Add random jitter
};
```

### Preset Policies

```cpp
// Fast reconnection (LAN)
auto policy = reconnect_policy::fast();
// initial_delay=100ms, max_delay=5s, multiplier=1.5, max_attempts=5

// Standard reconnection (default)
auto policy = reconnect_policy::exponential_backoff();
// initial_delay=1s, max_delay=30s, multiplier=2.0, max_attempts=10

// Aggressive reconnection (mobile)
auto policy = reconnect_policy::aggressive();
// initial_delay=500ms, max_delay=60s, multiplier=1.5, max_attempts=20

// Persistent reconnection (critical applications)
auto policy = reconnect_policy::persistent();
// initial_delay=1s, max_delay=5min, multiplier=2.0, max_attempts=0 (infinite)
```

### Custom Policy Examples

```cpp
// Very aggressive for real-time applications
auto client = file_transfer_client::builder()
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy{
        .initial_delay = std::chrono::milliseconds(100),
        .max_delay = std::chrono::seconds(2),
        .multiplier = 1.2,
        .max_attempts = 50,
        .jitter = true
    })
    .build();

// Conservative for batch processing
auto client = file_transfer_client::builder()
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy{
        .initial_delay = std::chrono::seconds(5),
        .max_delay = std::chrono::minutes(10),
        .multiplier = 2.5,
        .max_attempts = 5,
        .jitter = false
    })
    .build();
```

### Backoff Calculation

```
delay(n) = min(initial_delay × multiplier^n, max_delay)

With jitter:
delay(n) = delay(n) × (0.5 + random(0.0, 1.0))
```

Example sequence (default policy):
```
Attempt 1: 1s
Attempt 2: 2s
Attempt 3: 4s
Attempt 4: 8s
Attempt 5: 16s
Attempt 6: 30s (capped)
Attempt 7: 30s (capped)
...
```

---

## Pipeline Configuration

Shared configuration for both server and client data pipelines.

### Configuration Structure

```cpp
struct pipeline_config {
    // Worker counts per stage
    std::size_t io_read_workers      = 2;   // File reading (upload source)
    std::size_t chunk_workers        = 2;   // Chunk processing
    std::size_t compression_workers  = 4;   // LZ4 compress/decompress
    std::size_t network_workers      = 2;   // Network I/O
    std::size_t io_write_workers     = 2;   // File writing (download dest)

    // Queue sizes (backpressure control)
    std::size_t read_queue_size      = 16;  // Pending read chunks
    std::size_t compress_queue_size  = 32;  // Pending compression
    std::size_t send_queue_size      = 64;  // Pending network sends
    std::size_t recv_queue_size      = 64;  // Pending network receives
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
- Network interface speed

### Pipeline Direction

#### Upload Pipeline (Client → Server)

```
[Client]                                    [Server]
Read → Chunk → Compress → Send ──────────→ Recv → Decompress → Write
```

Relevant workers:
- **Client**: io_read_workers, compression_workers (compress), network_workers
- **Server**: network_workers, compression_workers (decompress), io_write_workers

#### Download Pipeline (Server → Client)

```
[Server]                                    [Client]
Read → Chunk → Compress → Send ──────────→ Recv → Decompress → Write
```

Relevant workers:
- **Server**: io_read_workers, compression_workers (compress), network_workers
- **Client**: network_workers, compression_workers (decompress), io_write_workers

### Stage-by-Stage Tuning

#### I/O Stage

| Workers | Use Case |
|---------|----------|
| 1 | Single HDD |
| 2 | Single SSD (default) |
| 4 | NVMe SSD or RAID |
| 8 | High-performance storage array |

```cpp
// For NVMe storage
config.io_read_workers = 4;
config.io_write_workers = 4;
config.read_queue_size = 32;
config.write_queue_size = 32;
```

#### Compression Stage

| Workers | Use Case |
|---------|----------|
| 2 | Dual-core CPU |
| 4 | Quad-core CPU (default) |
| 8 | 8-core CPU |
| 16+ | High-core-count server |

```cpp
// For 32-core server
config.compression_workers = 24;
config.compress_queue_size = 128;
config.decompress_queue_size = 128;
```

#### Network Stage

| Workers | Use Case |
|---------|----------|
| 1 | Single connection |
| 2 | Standard use (default) |
| 4 | High-bandwidth network |
| 8 | 10Gbps+ network |

```cpp
// For 10Gbps network
config.network_workers = 8;
config.send_queue_size = 256;
config.recv_queue_size = 256;
```

### Memory Calculation

Memory usage per queue:
```
Queue Memory = queue_size × chunk_size
```

**Upload Memory (Client)**:
```
read_queue_size × chunk_size
+ compress_queue_size × chunk_size
+ send_queue_size × chunk_size
```

**Download Memory (Client)**:
```
recv_queue_size × chunk_size
+ decompress_queue_size × chunk_size
+ write_queue_size × chunk_size
```

**Example** (default configuration, 256KB chunks):
```
Upload:  16 × 256KB + 32 × 256KB + 64 × 256KB = 28MB
Download: 64 × 256KB + 32 × 256KB + 16 × 256KB = 28MB
Total: 56MB (bidirectional)
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
// Override global settings for specific upload
upload_options opts{
    .compression = compression_mode::enabled,
    .level = compression_level::high_compression
};

client->upload_file(local_path, remote_name, opts);

// Override for specific download
download_options opts{
    .verify_checksum = true,
    .overwrite = true
};

client->download_file(remote_name, local_path, opts);
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
    duration    write_timeout   = 30s;
    duration    keepalive       = 30s;         // TCP keepalive interval
};
```

#### TCP Examples

```cpp
// High-latency network (WAN)
auto client = file_transfer_client::builder()
    .with_transport_config(tcp_transport_config{
        .send_buffer = 1024 * 1024,   // 1MB
        .recv_buffer = 1024 * 1024,   // 1MB
        .connect_timeout = 30s,
        .read_timeout = 60s,
        .write_timeout = 60s
    })
    .build();
```

```cpp
// Low-latency network (LAN)
auto client = file_transfer_client::builder()
    .with_transport_config(tcp_transport_config{
        .tcp_nodelay = true,          // Minimize latency
        .send_buffer = 128 * 1024,    // 128KB
        .recv_buffer = 128 * 1024,    // 128KB
        .connect_timeout = 5s,
        .read_timeout = 15s
    })
    .build();
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

## Security Configuration

### TLS Configuration

```cpp
struct tls_config {
    std::filesystem::path certificate_path;    // Server certificate
    std::filesystem::path private_key_path;    // Server private key
    std::filesystem::path ca_certificate_path; // CA certificate (optional)
    bool                  verify_peer = true;  // Verify client certificates
    tls_version           min_version = tls_version::tls_1_3;
};

// Server with TLS
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_tls(tls_config{
        .certificate_path = "/etc/ssl/server.crt",
        .private_key_path = "/etc/ssl/server.key",
        .ca_certificate_path = "/etc/ssl/ca.crt",
        .verify_peer = false  // Don't require client certificates
    })
    .build();
```

### Client Authentication

```cpp
// Client with certificate authentication
auto client = file_transfer_client::builder()
    .with_tls(tls_config{
        .certificate_path = "/etc/ssl/client.crt",
        .private_key_path = "/etc/ssl/client.key",
        .ca_certificate_path = "/etc/ssl/ca.crt"
    })
    .build();
```

### Access Control

```cpp
// Server-side access control
server->on_upload_request([&auth_service](const upload_request& req) {
    // Validate client token
    if (!auth_service.validate_token(req.auth_token)) {
        return false;
    }

    // Check upload permission
    return auth_service.can_upload(req.client_id, req.filename);
});

server->on_download_request([&auth_service](const download_request& req) {
    // Validate client token
    if (!auth_service.validate_token(req.auth_token)) {
        return false;
    }

    // Check download permission
    return auth_service.can_download(req.client_id, req.filename);
});
```

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
config.recv_queue_size = 128;
```

### Memory Optimization

#### 1. Reduce Queue Sizes

```cpp
pipeline_config config{
    .read_queue_size = 4,
    .compress_queue_size = 8,
    .send_queue_size = 16,
    .recv_queue_size = 16,
    .decompress_queue_size = 8,
    .write_queue_size = 4
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
// 10Gbps network (server)
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_pipeline_config(pipeline_config{
        .network_workers = 8,
        .send_queue_size = 256,
        .recv_queue_size = 256
    })
    .with_transport_config(tcp_transport_config{
        .send_buffer = 2 * 1024 * 1024,  // 2MB
        .recv_buffer = 2 * 1024 * 1024
    })
    .build();
```

#### High Latency Network

```cpp
// 100ms latency WAN (client)
auto client = file_transfer_client::builder()
    .with_pipeline_config(pipeline_config{
        .send_queue_size = 256,  // Many in-flight chunks
        .recv_queue_size = 256
    })
    .with_transport_config(tcp_transport_config{
        .send_buffer = 4 * 1024 * 1024,  // 4MB
        .read_timeout = 60s
    })
    .build();
```

### Concurrent Transfers

```cpp
// Server optimized for many concurrent clients
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_max_connections(500)
    .with_pipeline_config(pipeline_config{
        .network_workers = 16,      // Handle many connections
        .compression_workers = 32,  // Parallel compression
        .io_write_workers = 8       // Parallel I/O
    })
    .build();
```

---

## Configuration Best Practices

### 1. Start with Auto-Detection

```cpp
auto config = pipeline_config::auto_detect();
```

### 2. Measure Before Tuning

```cpp
// Server statistics
auto stats = server->get_statistics();
log_info("Active connections: {}", stats.active_connections);
log_info("Upload throughput: {} MB/s", stats.upload_throughput_mbps);
log_info("Download throughput: {} MB/s", stats.download_throughput_mbps);

// Client statistics
auto stats = client->get_statistics();
log_info("Upload rate: {} MB/s", stats.current_upload_rate_mbps);
log_info("Download rate: {} MB/s", stats.current_download_rate_mbps);
```

### 3. Monitor Pipeline Bottlenecks

```cpp
// Get pipeline statistics
auto pipeline_stats = server->get_pipeline_stats();
auto bottleneck = pipeline_stats.bottleneck_stage();

log_info("Bottleneck stage: {}", stage_name(bottleneck));

// Tune the bottleneck stage
if (bottleneck == pipeline_stage::compression) {
    config.compression_workers *= 2;
}
```

### 4. Monitor Queue Depths

```cpp
auto depths = server->get_queue_depths();
if (depths.compress_queue > config.compress_queue_size * 0.9) {
    log_warning("Compression queue near capacity - consider adding workers");
}
```

### 5. Test Configuration Changes

```cpp
// Benchmark upload
auto start = std::chrono::steady_clock::now();
auto result = client->upload_file(test_file, "benchmark.dat");
auto duration = std::chrono::steady_clock::now() - start;

if (result) {
    auto file_size = std::filesystem::file_size(test_file);
    auto throughput = file_size / duration.count();
    log_info("Upload throughput: {} MB/s", throughput / 1e6);
}
```

---

## Configuration Validation

Configurations are validated at build time:

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("")  // Error: empty path
    .build();

if (!server) {
    // Error code: -790 (config_storage_dir_error)
    std::cerr << server.error().message() << "\n";
}

auto client = file_transfer_client::builder()
    .with_chunk_size(32 * 1024)  // Error: below minimum
    .build();

if (!client) {
    // Error code: -791 (config_chunk_size_error)
    std::cerr << client.error().message() << "\n";
}
```

### Validation Rules

| Parameter | Constraint |
|-----------|------------|
| storage_directory | Non-empty, writable |
| max_connections | >= 1 |
| max_file_size | >= 1KB |
| chunk_size | 64KB <= size <= 1MB |
| *_workers | >= 1 |
| *_queue_size | >= 1 |
| bandwidth_limit | >= 0 (0 = unlimited) |
| reconnect max_attempts | >= 0 (0 = infinite) |
| initial_delay | > 0 |
| multiplier | > 1.0 |

---

*Version: 0.2.0*
*Last updated: 2025-12-11*
