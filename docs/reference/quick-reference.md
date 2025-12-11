# Quick Reference Card

Quick reference for common **file_trans_system** operations.

**Version:** 2.0.0
**Architecture:** Client-Server Model

---

## Include Header

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;
```

---

## Server Operations

### Create Server

```cpp
// Minimal
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .build();

// With options
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_max_connections(100)
    .with_max_file_size(10ULL * 1024 * 1024 * 1024)  // 10GB
    .with_storage_quota(1ULL * 1024 * 1024 * 1024 * 1024)  // 1TB
    .build();
```

### Start/Stop

```cpp
server->start(endpoint{"0.0.0.0", 19000});
// ... running ...
server->stop();
```

### Server Callbacks

```cpp
// Validate uploads
server->on_upload_request([](const upload_request& req) {
    return req.file_size < 1e9;  // Accept < 1GB
});

// Validate downloads
server->on_download_request([](const download_request& req) {
    return true;  // Allow all
});

// Connection events
server->on_client_connected([](const client_info& info) {
    std::cout << "Connected: " << info.address << "\n";
});

// Transfer events
server->on_upload_complete([](const transfer_result& result) {
    std::cout << "Uploaded: " << result.filename << "\n";
});
```

### Server Statistics

```cpp
auto stats = server->get_statistics();
std::cout << "Connections: " << stats.active_connections << "\n";
std::cout << "Upload rate: " << stats.upload_throughput_mbps << " MB/s\n";
```

---

## Client Operations

### Create Client

```cpp
// Minimal
auto client = file_transfer_client::builder().build();

// With options
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::adaptive)
    .with_chunk_size(256 * 1024)
    .with_auto_reconnect(true)
    .build();
```

### Connect/Disconnect

```cpp
auto result = client->connect(endpoint{"192.168.1.100", 19000});
if (result) {
    std::cout << "Connected to server\n";
}
// ... operations ...
client->disconnect();
```

### Upload File

```cpp
auto result = client->upload_file("/local/file.dat", "file.dat");

if (result) {
    std::cout << "Upload ID: " << result->id.to_string() << "\n";
} else {
    std::cerr << "Error: " << result.error().message() << "\n";
}
```

### Upload Multiple Files

```cpp
std::vector<upload_entry> files = {
    {"/local/file1.dat", "file1.dat"},
    {"/local/file2.dat", "file2.dat"}
};

auto result = client->upload_files(files);
```

### Download File

```cpp
auto result = client->download_file("remote.dat", "/local/remote.dat");

if (result) {
    std::cout << "Downloaded: " << result->output_path << "\n";
}
```

### List Files

```cpp
auto result = client->list_files();

if (result) {
    for (const auto& file : *result) {
        std::cout << file.name << " (" << file.size << " bytes)\n";
    }
}

// With pattern
auto result = client->list_files("*.pdf");
```

### Progress Callback

```cpp
client->on_progress([](const transfer_progress& p) {
    double percent = 100.0 * p.bytes_transferred / p.total_bytes;
    std::cout << percent << "% - " << p.transfer_rate / 1e6 << " MB/s\n";
});
```

### Control Transfer

```cpp
client->pause(transfer_id);
client->resume(transfer_id);
client->cancel(transfer_id);
```

---

## Reconnection

### Enable Auto-Reconnect

```cpp
auto client = file_transfer_client::builder()
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy::exponential_backoff())
    .build();
```

### Reconnect Policies

| Policy | Initial | Max | Attempts |
|--------|---------|-----|----------|
| `fast()` | 100ms | 5s | 5 |
| `exponential_backoff()` | 1s | 30s | 10 |
| `aggressive()` | 500ms | 60s | 20 |
| `persistent()` | 1s | 5min | infinite |

### Custom Policy

```cpp
reconnect_policy policy{
    .initial_delay = 500ms,
    .max_delay = 30s,
    .multiplier = 1.5,
    .max_attempts = 15
};
```

### Reconnection Callbacks

```cpp
client->on_disconnected([](disconnect_reason reason) {
    std::cout << "Disconnected: " << to_string(reason) << "\n";
});

client->on_reconnecting([](int attempt, duration delay) {
    std::cout << "Retry " << attempt << " in " << delay.count() << "ms\n";
});

client->on_reconnected([]() {
    std::cout << "Reconnected!\n";
});
```

---

## Configuration

### Compression Modes

| Mode | Description |
|------|-------------|
| `compression_mode::disabled` | No compression |
| `compression_mode::enabled` | Always compress |
| `compression_mode::adaptive` | Auto-detect (default) |

### Compression Levels

| Level | Speed | Ratio |
|-------|-------|-------|
| `compression_level::fast` | ~400 MB/s | ~2.1:1 |
| `compression_level::high_compression` | ~50 MB/s | ~2.7:1 |

### Pipeline Configuration

```cpp
pipeline_config config{
    .io_read_workers = 2,
    .compression_workers = 4,
    .network_workers = 2,
    .send_queue_size = 64,
    .recv_queue_size = 64
};

// Or auto-detect
auto config = pipeline_config::auto_detect();
```

### Upload Options

```cpp
upload_options opts{
    .compression = compression_mode::enabled,
    .level = compression_level::fast,
    .verify_checksum = true
};

client->upload_file(local, remote, opts);
```

### Download Options

```cpp
download_options opts{
    .verify_checksum = true,
    .overwrite = false
};

client->download_file(remote, local, opts);
```

---

## Error Handling

### Check Result

```cpp
auto result = client->upload_file(path, name);
if (!result) {
    switch (result.error().code()) {
        case error::connection_failed:
            // Handle connection error
            break;
        case error::storage_full:
            // Handle server storage full
            break;
        case error::file_not_found:
            // Handle missing file
            break;
        default:
            std::cerr << result.error().message() << "\n";
    }
}
```

### Common Error Codes

| Code | Name | Description |
|------|------|-------------|
| -700 | `connection_failed` | Cannot connect to server |
| -703 | `connection_lost` | Connection dropped |
| -704 | `reconnect_failed` | Auto-reconnect exhausted |
| -710 | `transfer_init_failed` | Transfer setup failed |
| -712 | `transfer_timeout` | Transfer timed out |
| -713 | `upload_rejected` | Server rejected upload |
| -714 | `download_rejected` | Server rejected download |
| -720 | `chunk_checksum_error` | Data corruption |
| -744 | `file_already_exists` | File exists on server |
| -745 | `storage_full` | Server storage quota exceeded |
| -746 | `file_not_found_on_server` | Remote file not found |
| -750 | `file_not_found` | Local file not found |

---

## Chunk Configuration

### Chunk Size Limits

| Limit | Value |
|-------|-------|
| Minimum | 64 KB |
| Default | 256 KB |
| Maximum | 1 MB |

### Calculate Chunks

```cpp
uint64_t file_size = 1024 * 1024 * 1024;  // 1 GB
uint64_t chunk_size = 256 * 1024;          // 256 KB
uint64_t num_chunks = (file_size + chunk_size - 1) / chunk_size;
// num_chunks = 4096
```

---

## Performance Targets

| Metric | Target |
|--------|--------|
| LAN Throughput | >= 500 MB/s |
| WAN Throughput | >= 100 MB/s |
| LZ4 Compress | >= 400 MB/s |
| LZ4 Decompress | >= 1.5 GB/s |
| Memory Baseline | < 50 MB |
| Concurrent Clients | >= 100 |

---

## Transport Types

```cpp
// TCP (default)
.with_transport(transport_type::tcp)

// QUIC (Phase 2)
.with_transport(transport_type::quic)
```

### When to Use QUIC

- High packet loss (>0.5%)
- Mobile networks
- Frequent IP changes
- Multiple concurrent transfers

---

## Connection States

### Client Connection

```
disconnected → connecting → connected → disconnected
                   ↓             ↓
             reconnecting ←──────┘
                   ↓
            reconnect_failed
```

### Transfer States

```
pending → initializing → transferring → verifying → completed
                ↓              ↓
            failed ←───────────┘
                ↑
          cancelled
```

---

## Memory Estimation

### Client Memory (Upload)

```
(read_queue + compress_queue + send_queue) × chunk_size
= (16 + 32 + 64) × 256KB
= 28 MB
```

### Client Memory (Download)

```
(recv_queue + decompress_queue + write_queue) × chunk_size
= (64 + 32 + 16) × 256KB
= 28 MB
```

### Server Memory (per connection)

```
~56 MB (bidirectional pipeline buffers)
```

---

## Dependencies

| System | Required |
|--------|----------|
| common_system | Yes |
| thread_system | Yes |
| network_system | Yes |
| container_system | Yes |
| LZ4 | Yes |
| logger_system | Optional |
| monitoring_system | Optional |

---

## Namespace

```cpp
namespace kcenon::file_transfer {
    // Core classes
    class file_transfer_server;
    class file_transfer_client;

    // Enums
    enum class compression_mode;
    enum class compression_level;
    enum class transport_type;
    enum class transfer_state;
    enum class disconnect_reason;

    // Server types
    struct upload_request;
    struct download_request;
    struct client_info;
    struct storage_status;

    // Client types
    struct reconnect_policy;
    struct server_info;

    // Shared types
    struct transfer_progress;
    struct transfer_result;
    struct file_info;
    struct pipeline_config;
    struct compression_statistics;

    // Options
    struct upload_options;
    struct download_options;
}
```

---

*file_trans_system v2.0.0 | Last updated: 2025-12-11*
