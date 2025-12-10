# Quick Reference Card

Quick reference for common **file_trans_system** operations.

---

## Include Header

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;
```

---

## Sender Operations

### Create Sender

```cpp
// Minimal
auto sender = file_sender::builder().build();

// With options
auto sender = file_sender::builder()
    .with_compression(compression_mode::adaptive)
    .with_chunk_size(256 * 1024)
    .with_bandwidth_limit(10 * 1024 * 1024)  // 10 MB/s
    .build();
```

### Send File

```cpp
auto result = sender->send_file(
    "/path/to/file.dat",
    endpoint{"192.168.1.100", 8080}
);

if (result) {
    std::cout << "Transfer ID: " << result->id.to_string() << "\n";
} else {
    std::cerr << "Error: " << result.error().message() << "\n";
}
```

### Send Multiple Files

```cpp
std::vector<std::filesystem::path> files = {
    "/path/to/file1.dat",
    "/path/to/file2.dat"
};

auto result = sender->send_files(files, endpoint{"192.168.1.100", 8080});
```

### Progress Callback

```cpp
sender->on_progress([](const transfer_progress& p) {
    double percent = 100.0 * p.bytes_transferred / p.total_bytes;
    std::cout << percent << "% - " << p.transfer_rate / 1e6 << " MB/s\n";
});
```

### Control Transfer

```cpp
sender->pause(transfer_id);
sender->resume(transfer_id);
sender->cancel(transfer_id);
```

---

## Receiver Operations

### Create Receiver

```cpp
auto receiver = file_receiver::builder()
    .with_output_directory("/downloads")
    .build();
```

### Start/Stop

```cpp
receiver->start(endpoint{"0.0.0.0", 8080});
// ... receiving ...
receiver->stop();
```

### Callbacks

```cpp
// Accept/reject transfers
receiver->on_transfer_request([](const transfer_request& req) {
    return req.files[0].file_size < 1e9;  // Accept < 1GB
});

// Progress updates
receiver->on_progress([](const transfer_progress& p) {
    std::cout << p.bytes_transferred << "/" << p.total_bytes << "\n";
});

// Completion
receiver->on_complete([](const transfer_result& result) {
    if (result.verified) {
        std::cout << "Received: " << result.output_path << "\n";
    }
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
    .send_queue_size = 64
};

// Or auto-detect
auto config = pipeline_config::auto_detect();
```

### Transfer Options

```cpp
transfer_options opts{
    .compression = compression_mode::enabled,
    .level = compression_level::fast,
    .chunk_size = 512 * 1024,
    .verify_checksum = true,
    .bandwidth_limit = 10 * 1024 * 1024
};

sender->send_file(path, endpoint, opts);
```

---

## Statistics

### Transfer Statistics

```cpp
auto stats = manager->get_statistics();
std::cout << "Total transferred: " << stats.total_bytes_transferred << "\n";
std::cout << "Active transfers: " << stats.active_transfer_count << "\n";
```

### Compression Statistics

```cpp
auto stats = manager->get_compression_stats();
std::cout << "Ratio: " << stats.compression_ratio() << ":1\n";
std::cout << "Speed: " << stats.compression_speed_mbps() << " MB/s\n";
```

### Pipeline Statistics

```cpp
auto stats = sender->get_pipeline_stats();
std::cout << "Bottleneck: " << stage_name(stats.bottleneck_stage()) << "\n";
```

---

## Error Handling

### Check Result

```cpp
auto result = sender->send_file(path, endpoint);
if (!result) {
    switch (result.error().code()) {
        case error::file_not_found:
            // Handle missing file
            break;
        case error::transfer_timeout:
            // Handle timeout
            break;
        default:
            std::cerr << result.error().message() << "\n";
    }
}
```

### Common Error Codes

| Code | Name | Description |
|------|------|-------------|
| -700 | `transfer_init_failed` | Connection failed |
| -702 | `transfer_timeout` | Transfer timed out |
| -720 | `chunk_checksum_error` | Data corruption |
| -723 | `file_hash_mismatch` | File verification failed |
| -743 | `file_not_found` | Source file not found |
| -781 | `decompression_failed` | LZ4 decompression error |

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
| Concurrent Transfers | >= 100 |

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

## Transfer States

```
pending → initializing → transferring → verifying → completed
                 ↓              ↓
             failed ←──────────┘
                 ↑
           cancelled
```

---

## Memory Estimation

### Sender Memory

```
(read_queue + compress_queue + send_queue) × chunk_size
= (16 + 32 + 64) × 256KB
= 28 MB
```

### Receiver Memory

```
(recv_queue + decompress_queue + write_queue) × chunk_size
= (64 + 32 + 16) × 256KB
= 28 MB
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
    class file_sender;
    class file_receiver;
    class transfer_manager;

    enum class compression_mode;
    enum class compression_level;
    enum class transport_type;

    struct transfer_options;
    struct transfer_progress;
    struct transfer_result;
    struct pipeline_config;
    struct compression_statistics;
}
```

---

*file_trans_system v1.0.0 | Last updated: 2025-12-11*
