# LZ4 Compression Guide

Complete guide to LZ4 compression in the **file_trans_system** library.

## Overview

The file_trans_system uses **LZ4** for real-time, per-chunk compression to increase effective throughput over the network.

### Why LZ4?

| Algorithm | Compress Speed | Decompress Speed | Ratio | License |
|-----------|----------------|------------------|-------|---------|
| **LZ4** | ~500 MB/s | ~2 GB/s | 2.1:1 | BSD |
| LZ4-HC | ~50 MB/s | ~2 GB/s | 2.7:1 | BSD |
| zstd | ~400 MB/s | ~1 GB/s | 2.9:1 | BSD |
| gzip | ~30 MB/s | ~300 MB/s | 2.7:1 | - |
| snappy | ~400 MB/s | ~800 MB/s | 1.8:1 | BSD |

LZ4 offers the best balance of:
- **Speed**: Near memory bandwidth compression/decompression
- **Simplicity**: Single-header library, minimal dependencies
- **License**: BSD license (commercially friendly)
- **Maturity**: Battle-tested (Linux kernel, ZFS, etc.)

---

## Compression Modes

### Mode Enumeration

```cpp
enum class compression_mode {
    disabled,   // No compression
    enabled,    // Always compress
    adaptive    // Auto-detect compressibility (default)
};
```

### Mode Comparison

| Mode | Description | CPU Overhead | Best For |
|------|-------------|--------------|----------|
| `disabled` | No compression | None | Pre-compressed files (ZIP, media) |
| `enabled` | Always compress | Moderate | Text, logs, source code |
| `adaptive` | Auto-detect | Low-Moderate | Mixed content |

### Usage Examples

```cpp
// Disable compression for media files
auto sender = file_sender::builder()
    .with_compression(compression_mode::disabled)
    .build();

// Always compress for log files
auto sender = file_sender::builder()
    .with_compression(compression_mode::enabled)
    .build();

// Let the system decide (default)
auto sender = file_sender::builder()
    .with_compression(compression_mode::adaptive)
    .build();
```

---

## Compression Levels

### Level Enumeration

```cpp
enum class compression_level {
    fast,             // LZ4 standard
    high_compression  // LZ4-HC
};
```

### Level Comparison

| Level | Algorithm | Compress Speed | Ratio | Use Case |
|-------|-----------|----------------|-------|----------|
| `fast` | LZ4 | ~400 MB/s | ~2.1:1 | Real-time transfer |
| `high_compression` | LZ4-HC | ~50 MB/s | ~2.7:1 | Archival, bandwidth-limited |

### Performance Trade-offs

```
Throughput = min(Network_Speed × Compression_Ratio, Compression_Speed)
```

**Example: 100 Mbps Network**

| Level | Compress Speed | Ratio | Effective Throughput |
|-------|----------------|-------|---------------------|
| `fast` | 400 MB/s | 2.1:1 | 26.25 MB/s (network limited) |
| `high_compression` | 50 MB/s | 2.7:1 | 33.75 MB/s (network limited) |

**Example: 10 Gbps Network**

| Level | Compress Speed | Ratio | Effective Throughput |
|-------|----------------|-------|---------------------|
| `fast` | 400 MB/s | 2.1:1 | 400 MB/s (CPU limited) |
| `high_compression` | 50 MB/s | 2.7:1 | 50 MB/s (CPU limited) |

**Recommendation:**
- Use `fast` for high-bandwidth networks (>1 Gbps)
- Use `high_compression` for bandwidth-limited networks (<100 Mbps)

---

## Adaptive Compression

### How It Works

Adaptive compression samples the first 1KB of each chunk to determine compressibility:

```cpp
bool is_compressible(std::span<const std::byte> data, double threshold = 0.9) {
    // Sample first 1KB
    const auto sample_size = std::min(data.size(), std::size_t{1024});
    auto sample = data.first(sample_size);

    // Try compressing sample
    auto compressed = lz4_compress(sample);

    // Only compress if >= 10% reduction
    return compressed.size() < sample.size() * threshold;
}
```

### Detection Time

- **Target**: < 100 microseconds per chunk
- **Actual**: Typically 20-50 microseconds for 1KB sample

### File Type Heuristics

In addition to sampling, file extensions provide hints:

| Category | Extensions | Action |
|----------|------------|--------|
| **Compressible** | `.txt`, `.log`, `.json`, `.xml`, `.csv` | Likely compress |
| **Compressible** | `.cpp`, `.h`, `.py`, `.java`, `.js` | Likely compress |
| **Compressed** | `.zip`, `.gz`, `.tar.gz`, `.bz2`, `.xz` | Skip |
| **Media** | `.jpg`, `.png`, `.gif`, `.mp4`, `.mp3` | Skip |
| **Binary** | `.exe`, `.dll`, `.so`, `.bin` | Test (adaptive) |

### Adaptive Behavior

```cpp
// Adaptive mode decision tree
if (mode == compression_mode::adaptive) {
    if (is_known_compressed_extension(file)) {
        skip_compression();
    } else if (is_known_compressible_extension(file)) {
        compress();
    } else {
        // Test first chunk
        if (is_compressible(first_chunk)) {
            compress_all_chunks();
        } else {
            skip_compression();
        }
    }
}
```

---

## API Reference

### lz4_engine

Low-level compression API:

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
        int level = 9  // 1-12
    ) -> Result<std::size_t>;

    // Decompression (~1.5 GB/s)
    [[nodiscard]] static auto decompress(
        std::span<const std::byte> compressed,
        std::span<std::byte> output,
        std::size_t original_size
    ) -> Result<std::size_t>;

    // Calculate maximum compressed size
    [[nodiscard]] static auto max_compressed_size(std::size_t input_size)
        -> std::size_t;
};
```

### Usage Example

```cpp
// Compression
std::vector<std::byte> input = read_file("data.txt");
std::vector<std::byte> output(lz4_engine::max_compressed_size(input.size()));

auto result = lz4_engine::compress(input, output);
if (result) {
    output.resize(result.value());  // Shrink to actual size
}

// Decompression
std::vector<std::byte> decompressed(original_size);
auto result = lz4_engine::decompress(output, decompressed, original_size);
```

### chunk_compressor

High-level chunk compression with statistics:

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

### adaptive_compression

Compressibility detection utilities:

```cpp
class adaptive_compression {
public:
    // Sample-based detection (<100us)
    [[nodiscard]] static auto is_compressible(
        std::span<const std::byte> data,
        double threshold = 0.9
    ) -> bool;

    // Extension-based heuristic
    [[nodiscard]] static auto is_likely_compressible(
        const std::filesystem::path& file
    ) -> bool;
};
```

---

## Compression Statistics

### Statistics Structure

```cpp
struct compression_statistics {
    std::atomic<uint64_t> total_raw_bytes{0};
    std::atomic<uint64_t> total_compressed_bytes{0};
    std::atomic<uint64_t> chunks_compressed{0};
    std::atomic<uint64_t> chunks_skipped{0};
    std::atomic<uint64_t> compression_time_us{0};
    std::atomic<uint64_t> decompression_time_us{0};

    [[nodiscard]] auto compression_ratio() const -> double {
        return total_compressed_bytes > 0
            ? static_cast<double>(total_raw_bytes) / total_compressed_bytes
            : 1.0;
    }

    [[nodiscard]] auto compression_speed_mbps() const -> double {
        return compression_time_us > 0
            ? (total_raw_bytes / 1e6) / (compression_time_us / 1e6)
            : 0.0;
    }

    [[nodiscard]] auto decompression_speed_mbps() const -> double {
        return decompression_time_us > 0
            ? (total_raw_bytes / 1e6) / (decompression_time_us / 1e6)
            : 0.0;
    }
};
```

### Monitoring Example

```cpp
auto stats = sender->get_compression_stats();

std::cout << "Compression Statistics:\n";
std::cout << "  Raw bytes:        " << stats.total_raw_bytes << "\n";
std::cout << "  Compressed bytes: " << stats.total_compressed_bytes << "\n";
std::cout << "  Compression ratio: " << stats.compression_ratio() << ":1\n";
std::cout << "  Compress speed:    " << stats.compression_speed_mbps() << " MB/s\n";
std::cout << "  Decompress speed:  " << stats.decompression_speed_mbps() << " MB/s\n";
std::cout << "  Chunks compressed: " << stats.chunks_compressed << "\n";
std::cout << "  Chunks skipped:    " << stats.chunks_skipped << "\n";
```

---

## Wire Format

### Chunk Header Compression Fields

```cpp
struct chunk_header {
    // ... other fields ...
    uint32_t    original_size;      // Original (uncompressed) size
    uint32_t    compressed_size;    // Size after compression
    chunk_flags flags;              // Includes compressed flag
};

enum class chunk_flags : uint8_t {
    compressed = 0x04    // Data is LZ4 compressed
};
```

### Compression Flag Logic

```cpp
// Sender side
if (compressed && compressed.size() < original.size()) {
    header.original_size = original.size();
    header.compressed_size = compressed.size();
    header.flags |= chunk_flags::compressed;
    data = compressed;
} else {
    header.original_size = original.size();
    header.compressed_size = original.size();  // Same
    // compressed flag NOT set
    data = original;
}

// Receiver side
if (has_flag(header.flags, chunk_flags::compressed)) {
    auto decompressed = lz4_decompress(data, header.original_size);
    // Use decompressed data
} else {
    // Use data directly
}
```

---

## Performance Benchmarks

### Compression Throughput

| Data Type | LZ4 Fast | LZ4-HC | Ratio (Fast) | Ratio (HC) |
|-----------|----------|--------|--------------|------------|
| Text/Logs | 450 MB/s | 55 MB/s | 3.2:1 | 4.1:1 |
| JSON | 420 MB/s | 50 MB/s | 2.8:1 | 3.5:1 |
| Source Code | 430 MB/s | 52 MB/s | 3.0:1 | 3.8:1 |
| Binary (random) | 380 MB/s | 45 MB/s | 1.0:1 | 1.0:1 |
| Binary (structured) | 400 MB/s | 48 MB/s | 1.5:1 | 1.8:1 |

### Decompression Throughput

| Data Type | Decompression Speed |
|-----------|---------------------|
| All types | 1.5 - 2.0 GB/s |

### Memory Usage

```
Compression buffer = max_compressed_size(chunk_size)
                   ≈ chunk_size + (chunk_size / 255) + 16

For 256KB chunk: ~257KB buffer needed
```

---

## Best Practices

### 1. Use Adaptive Mode by Default

```cpp
// Good - let the system decide
.with_compression(compression_mode::adaptive)

// Avoid unless you know file types
.with_compression(compression_mode::enabled)  // May waste CPU on media files
```

### 2. Match Compression Level to Network

```cpp
// High-bandwidth network (>1 Gbps)
.with_compression_level(compression_level::fast)

// Low-bandwidth network (<100 Mbps)
.with_compression_level(compression_level::high_compression)
```

### 3. Monitor Compression Ratio

```cpp
auto stats = sender->get_compression_stats();
if (stats.compression_ratio() < 1.1) {
    // Data is mostly incompressible
    // Consider disabling compression
    log_warning("Low compression ratio: {}", stats.compression_ratio());
}
```

### 4. Pre-Sort Files by Type

```cpp
// Separate batch by compressibility
std::vector<std::filesystem::path> compressible;
std::vector<std::filesystem::path> incompressible;

for (const auto& file : files) {
    if (adaptive_compression::is_likely_compressible(file)) {
        compressible.push_back(file);
    } else {
        incompressible.push_back(file);
    }
}

// Send with appropriate settings
sender->send_files(compressible, endpoint,
    {.compression = compression_mode::enabled});
sender->send_files(incompressible, endpoint,
    {.compression = compression_mode::disabled});
```

### 5. Scale Compression Workers

```cpp
// Compression is often the bottleneck
// Scale workers to available cores
pipeline_config config{
    .compression_workers = std::thread::hardware_concurrency() - 2
};
```

---

## Troubleshooting

### Low Compression Ratio

**Symptoms:** Compression ratio near 1.0

**Causes:**
- Data is already compressed (ZIP, media)
- Random/encrypted data
- Small chunk size (less data to work with)

**Solutions:**
- Use adaptive mode to skip incompressible data
- Increase chunk size for better compression

### High CPU Usage

**Symptoms:** CPU at 100% during transfer

**Causes:**
- Too many compression workers
- high_compression level on fast network

**Solutions:**
- Reduce compression workers
- Switch to fast compression level
- Disable compression for pre-compressed files

### Slow Decompression

**Symptoms:** Receiver CPU-bound

**Causes:**
- Insufficient decompression workers
- Very high compression ratio data

**Solutions:**
- Increase compression workers (used for decompression too)
- Check for corrupted compressed data

---

## Error Handling

### Compression Errors

| Error Code | Description | Recovery |
|------------|-------------|----------|
| -780 | `compression_failed` | Send uncompressed |
| -781 | `decompression_failed` | Request retransmission |
| -782 | `compression_buffer_error` | Internal error |
| -783 | `invalid_compression_data` | Request retransmission |

### Fallback Behavior

```cpp
// Compression failure fallback
auto result = lz4_engine::compress(input, output);
if (!result) {
    // Send uncompressed
    chunk.header.flags &= ~chunk_flags::compressed;
    chunk.data = input;
    // Continue transfer
}

// Decompression failure fallback
auto result = lz4_engine::decompress(compressed, output, original_size);
if (!result) {
    // Request retransmission
    send_chunk_nack(chunk.header.chunk_index);
}
```

---

*Last updated: 2025-12-11*
