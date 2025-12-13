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
| **Compressed** | `.zip`, `.gz`, `.tar.gz`, `.bz2`, `.xz`, `.7z` | Skip |
| **Documents** | `.pdf` | Skip (internally compressed) |
| **Media** | `.jpg`, `.png`, `.gif`, `.mp4`, `.mp3`, `.mov`, `.webp` | Skip |
| **Binary** | `.exe`, `.dll`, `.so`, `.bin` | Test (adaptive) |

### Magic Bytes Detection

The compression engine detects pre-compressed formats using magic bytes:

| Format | Magic Bytes | Offset |
|--------|-------------|--------|
| ZIP | `50 4B 03 04` | 0 |
| GZIP | `1F 8B` | 0 |
| PDF | `25 50 44 46` (`%PDF`) | 0 |
| PNG | `89 50 4E 47 0D 0A 1A 0A` | 0 |
| JPEG | `FF D8 FF` | 0 |
| MP4/MOV | `66 74 79 70` (`ftyp`) | 4 |
| 7-Zip | `37 7A BC AF 27 1C` | 0 |
| LZ4 | `04 22 4D 18` | 0 |
| ZSTD | `28 B5 2F FD` | 0 |
| BZIP2 | `42 5A 68` | 0 |
| XZ | `FD 37 7A 58 5A 00` | 0 |
| GIF | `47 49 46 38` | 0 |
| WEBP | `52 49 46 46` (RIFF) | 0 |

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

### compression_engine

The core compression engine with adaptive compression support:

```cpp
class compression_engine {
public:
    explicit compression_engine(compression_level level = compression_level::fast);

    // Standard compression
    [[nodiscard]] auto compress(std::span<const std::byte> input)
        -> result<std::vector<std::byte>>;

    // Standard decompression
    [[nodiscard]] auto decompress(std::span<const std::byte> input,
                                  std::size_t original_size)
        -> result<std::vector<std::byte>>;

    // Check if data is worth compressing (magic bytes + sample test)
    [[nodiscard]] auto is_compressible(std::span<const std::byte> data) const
        -> bool;

    // Adaptive compression with automatic decision
    // Returns (compressed_data, was_compressed) pair
    [[nodiscard]] auto compress_adaptive(std::span<const std::byte> input,
                                         compression_mode mode = compression_mode::adaptive)
        -> result<std::pair<std::vector<std::byte>, bool>>;

    // Record a skipped compression for statistics
    auto record_skipped(std::size_t data_size) -> void;

    // Statistics and configuration
    [[nodiscard]] auto stats() const -> compression_stats;
    auto reset_stats() -> void;
    [[nodiscard]] auto level() const -> compression_level;
    auto set_level(compression_level level) -> void;
    [[nodiscard]] static auto max_compressed_size(std::size_t input_size)
        -> std::size_t;
};
```

### Usage Examples

```cpp
compression_engine engine(compression_level::fast);

// Method 1: Manual adaptive compression
if (engine.is_compressible(data)) {
    auto result = engine.compress(data);
    // Use compressed data
} else {
    engine.record_skipped(data.size());  // Track statistics
    // Use original data
}

// Method 2: Automatic adaptive compression (recommended)
auto result = engine.compress_adaptive(data, compression_mode::adaptive);
if (result.has_value()) {
    auto [output_data, was_compressed] = result.value();
    if (was_compressed) {
        // Data was compressed
    } else {
        // Data was skipped (pre-compressed or low ratio)
    }
}

// Check statistics
auto stats = engine.stats();
std::cout << "Bytes saved: " << stats.bytes_saved() << "\n";
std::cout << "Skip rate: " << stats.skip_rate() << "%\n";
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
struct compression_stats {
    uint64_t total_input_bytes = 0;      // Total bytes before compression
    uint64_t total_output_bytes = 0;     // Total bytes after compression
    uint64_t compression_calls = 0;      // Number of compression operations
    uint64_t decompression_calls = 0;    // Number of decompression operations
    uint64_t skipped_compressions = 0;   // Chunks skipped (pre-compressed format)
    uint64_t total_chunks = 0;           // Total chunks processed
    uint64_t compressed_chunks = 0;      // Chunks actually compressed

    // Compression ratio (output/input, lower is better)
    [[nodiscard]] auto compression_ratio() const -> double;

    // Average compression ratio across all chunks
    [[nodiscard]] auto average_ratio() const -> double;

    // Bytes saved by compression (input - output)
    [[nodiscard]] auto bytes_saved() const -> uint64_t;

    // Skip rate percentage (skipped/total * 100)
    [[nodiscard]] auto skip_rate() const -> double;
};
```

### Statistics Fields

| Field | Description | Use Case |
|-------|-------------|----------|
| `total_input_bytes` | Raw bytes before compression | Throughput calculation |
| `total_output_bytes` | Bytes after compression | Bandwidth savings |
| `compression_calls` | Number of compress() calls | Performance monitoring |
| `skipped_compressions` | Chunks skipped as pre-compressed | Adaptive effectiveness |
| `total_chunks` | All chunks processed | Progress tracking |
| `compressed_chunks` | Chunks that were compressed | Compression rate |
| `bytes_saved()` | Input - Output bytes | Bandwidth savings |
| `skip_rate()` | Percentage of skipped chunks | Adaptive effectiveness |

### Monitoring Example

```cpp
auto stats = engine.stats();

std::cout << "Compression Statistics:\n";
std::cout << "  Input bytes:       " << stats.total_input_bytes << "\n";
std::cout << "  Output bytes:      " << stats.total_output_bytes << "\n";
std::cout << "  Bytes saved:       " << stats.bytes_saved() << "\n";
std::cout << "  Compression ratio: " << stats.compression_ratio() << "\n";
std::cout << "  Total chunks:      " << stats.total_chunks << "\n";
std::cout << "  Compressed chunks: " << stats.compressed_chunks << "\n";
std::cout << "  Skipped chunks:    " << stats.skipped_compressions << "\n";
std::cout << "  Skip rate:         " << stats.skip_rate() << "%\n";
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

*Last updated: 2025-12-13*
