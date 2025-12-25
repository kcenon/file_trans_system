# file_trans_system Benchmarks

Performance benchmarks for the file_trans_system library, built with [Google Benchmark](https://github.com/google/benchmark).

## Building

Benchmarks are disabled by default. Enable them with:

```bash
cmake -B build -DFILE_TRANS_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Running Benchmarks

### Basic Usage

```bash
# Run all throughput benchmarks
./build/bin/throughput_benchmarks

# Run compression benchmarks
./build/bin/compression_benchmarks

# Run latency benchmarks
./build/bin/latency_benchmarks

# Run memory benchmarks
./build/bin/memory_benchmarks

# Run scalability benchmarks
./build/bin/scalability_benchmarks

# Run transport benchmarks (QUIC vs TCP)
./build/bin/transport_benchmarks

# Run encryption benchmarks (requires OpenSSL)
./build/bin/encryption_benchmarks

# Run with detailed output
./build/bin/throughput_benchmarks --benchmark_counters_tabular=true
```

### Output Formats

```bash
# JSON output (for analysis)
./build/bin/throughput_benchmarks --benchmark_format=json --benchmark_out=results.json

# CSV output
./build/bin/throughput_benchmarks --benchmark_format=csv --benchmark_out=results.csv

# Console with JSON file
./build/bin/throughput_benchmarks --benchmark_out=results.json --benchmark_out_format=json
```

### Filtering Benchmarks

```bash
# Run only split throughput benchmarks
./build/bin/throughput_benchmarks --benchmark_filter="BM_SingleFile_Split*"

# Run only CRC32 benchmarks
./build/bin/throughput_benchmarks --benchmark_filter="BM_Checksum_CRC32*"

# Run all chunk operations
./build/bin/throughput_benchmarks --benchmark_filter="BM_Chunk*"
```

### Controlling Iterations

```bash
# Run each benchmark for at least 5 seconds
./build/bin/throughput_benchmarks --benchmark_min_time=5

# Run exactly 100 iterations
./build/bin/throughput_benchmarks --benchmark_repetitions=100
```

## Benchmark Categories

### Throughput Benchmarks (`throughput/`)

| Benchmark | Description | Parameters |
|-----------|-------------|------------|
| `BM_SingleFile_SplitThroughput` | File splitting throughput | File size: 100KB - 100MB |
| `BM_SingleFile_AssemblyThroughput` | Chunk assembly throughput | File size: 100KB - 100MB |
| `BM_SingleFile_RoundTripThroughput` | Complete split + assembly cycle | File size: 100KB - 100MB |
| `BM_SingleFile_ChunkSizeImpact` | Chunk size effect on throughput | Chunk: 64KB - 1MB |

### Chunk Operation Benchmarks

| Benchmark | Description | Parameters |
|-----------|-------------|------------|
| `BM_ChunkSplitter_Split` | Chunk splitting performance | File/chunk size combinations |
| `BM_ChunkAssembler_Process` | Chunk processing performance | File/chunk size combinations |

### Checksum Benchmarks

| Benchmark | Description | Parameters |
|-----------|-------------|------------|
| `BM_Checksum_CRC32` | CRC32 calculation speed | Data size: 1KB - 1MB |
| `BM_Checksum_SHA256` | SHA-256 calculation speed | Data size: 1KB - 1MB |
| `BM_Checksum_SHA256_File` | File hash calculation | File size: 100KB - 100MB |

### Compression Benchmarks (`compression/`)

| Benchmark | Description | Parameters |
|-----------|-------------|------------|
| `BM_LZ4_Compression_Fast` | LZ4 fast compression speed | Data size: 64KB - 16MB |
| `BM_LZ4_Compression_High` | LZ4 HC compression speed | Data size: 64KB - 4MB |
| `BM_LZ4_Decompression` | LZ4 decompression speed | Data size: 64KB - 16MB |
| `BM_Compression_Ratio_Text` | Compression ratio (text) | Data size: 256KB - 4MB |
| `BM_Compression_Ratio_Binary` | Compression ratio (binary) | Data size: 256KB - 4MB |
| `BM_Adaptive_Compression_Check` | is_compressible() overhead | Data size: 4KB - 256KB |
| `BM_Adaptive_Compression_Skip` | Skip rate for random data | Data size: 256KB - 1MB |
| `BM_Adaptive_Compression_Full` | Full adaptive pipeline | Compressibility: 0-100% |

### Latency Benchmarks (`latency/`)

| Benchmark | Description | Parameters |
|-----------|-------------|------------|
| `BM_Connection_Setup` | Client connection time | - |
| `BM_Connection_Teardown` | Client disconnect time | - |
| `BM_FileList_Response` | File listing latency | Files: 100 - 10K |
| `BM_Protocol_RTT` | Protocol round-trip time | - |
| `BM_Upload_TTFB` | Upload time to first byte | File size: 64KB - 1MB |
| `BM_Download_TTFB` | Download time to first byte | File size: 64KB - 1MB |
| `BM_Concurrent_Connections` | Multi-client connection | Clients: 5 - 50 |

### Memory Benchmarks (`memory/`)

| Benchmark | Description | Target |
|-----------|-------------|--------|
| `BM_Memory_Baseline` | Process baseline memory | - |
| `BM_Memory_ServerBaseline` | Server memory usage | < 100 MB |
| `BM_Memory_ClientBaseline` | Client memory usage | < 50 MB |
| `BM_Memory_PerConnection` | Per-connection overhead | < 1 MB |
| `BM_Memory_FileSize_Constant` | Memory vs file size | Constant |

### Scalability Benchmarks (`scalability/`)

| Benchmark | Description | Parameters |
|-----------|-------------|------------|
| `BM_Scalability_ConcurrentConnections` | Connection scaling | 10, 50, 100 connections |
| `BM_Scalability_FileSize` | Performance vs file size | 1MB - 10GB |
| `BM_Scalability_100Connections_Stability` | 100 connection stability | 100 clients |
| `BM_Scalability_MemoryStability` | Long-running memory | 5, 10, 20 cycles |
| `BM_Scalability_ConcurrentUploads` | Concurrent upload throughput | 2, 5, 10 clients |

### Encryption Benchmarks (`encryption/`)

Benchmarks for AES-256-GCM encryption and key derivation performance.
Requires `FILE_TRANS_ENABLE_ENCRYPTION=ON` (enabled by default with OpenSSL).

#### Encryption Throughput Benchmarks

| Benchmark | Description | Target |
|-----------|-------------|--------|
| `BM_AES_GCM_Encryption` | Single-shot encryption throughput | >= 1 GB/s |
| `BM_AES_GCM_Decryption` | Single-shot decryption throughput | >= 1.5 GB/s |
| `BM_AES_GCM_Encryption_With_AAD` | Encryption with additional data | >= 1 GB/s |
| `BM_AES_GCM_Encrypt_Chunk` | Chunk-based encryption | - |
| `BM_AES_GCM_Decrypt_Chunk` | Chunk-based decryption | - |
| `BM_AES_GCM_Stream_Encrypt` | Streaming encryption | - |
| `BM_Encryption_Overhead` | Size expansion overhead | <= 10% |
| `BM_IV_Generation` | IV/nonce generation speed | - |

#### Key Derivation Benchmarks

| Benchmark | Description | Target |
|-----------|-------------|--------|
| `BM_PBKDF2_Key_Derivation` | PBKDF2-SHA256 key derivation | >= 100 ops/sec |
| `BM_PBKDF2_Iterations` | Varying iteration counts | - |
| `BM_Argon2_Key_Derivation` | Argon2id key derivation | - |
| `BM_Argon2_Memory_Cost` | Varying memory costs | - |
| `BM_Argon2_Time_Cost` | Varying time costs | - |
| `BM_KeyManager_Generate_Random` | Random key generation | - |
| `BM_KeyManager_Store_Retrieve` | Key storage access | - |
| `BM_KeyManager_Rotation` | Key rotation | - |
| `BM_Salt_Generation` | Cryptographic salt generation | - |
| `BM_Secure_Zero` | Secure memory zeroing | - |

### Transport Benchmarks (`transport/`)

Benchmarks for QUIC and TCP transport layer comparison.

#### QUIC Transport Benchmarks

| Benchmark | Description | Target |
|-----------|-------------|--------|
| `BM_QUIC_Connection_1RTT` | QUIC 1-RTT connection establishment | - |
| `BM_QUIC_Connection_0RTT` | QUIC 0-RTT connection resumption | <= 50ms |
| `BM_QUIC_SessionTicket_Operations` | Session ticket store/retrieve | - |
| `BM_QUIC_DataPreparation_Throughput` | Data preparation throughput | >= 90% of TCP |
| `BM_QUIC_ConnectionMigration_Preparation` | Migration preparation overhead | < 100ms |
| `BM_QUIC_PathValidation` | Path validation challenge generation | - |
| `BM_QUIC_Statistics_Collection` | Statistics collection overhead | - |
| `BM_QUIC_Transport_Creation` | Transport instance creation time | - |
| `BM_QUIC_Stream_Creation` | Stream creation overhead | - |

#### TCP vs QUIC Comparison Benchmarks

| Benchmark | Description | Comparison |
|-----------|-------------|------------|
| `BM_Comparison_Factory_*` | Factory creation overhead | TCP vs QUIC |
| `BM_Comparison_TransportCreate_*` | Transport instance creation | TCP vs QUIC |
| `BM_Comparison_ConfigBuild_*` | Configuration building | TCP vs QUIC |
| `BM_Comparison_Statistics_*` | Statistics collection overhead | TCP vs QUIC |
| `BM_Comparison_BufferPrep_*` | Buffer preparation throughput | TCP vs QUIC |
| `BM_Comparison_StateCheck_*` | State checking overhead | TCP vs QUIC |
| `BM_Comparison_Reconnect_*` | 1-RTT vs 0-RTT reconnection | >= 50% improvement |
| `BM_Comparison_TypeInfo_*` | Type identification overhead | TCP vs QUIC |

## Performance Targets

Based on SRS requirements:

| Metric | Target | Benchmark | Status |
|--------|--------|-----------|--------|
| LAN Throughput | >= 500 MB/s | `BM_SingleFile_*Throughput` | Verified |
| LZ4 Compression | >= 400 MB/s | `BM_LZ4_Compression_Fast` | Verified |
| LZ4 Decompression | >= 1.5 GB/s | `BM_LZ4_Decompression` | Verified |
| File List (10K files) | < 100ms | `BM_FileList_Response` | Pending |
| Connection Setup | < 100ms | `BM_Connection_Setup` | Pending |
| Server Memory | < 100 MB | `BM_Memory_ServerBaseline` | Verified |
| Client Memory | < 50 MB | `BM_Memory_ClientBaseline` | Verified |
| Per-connection Memory | < 1 MB | `BM_Memory_PerConnection` | Verified |
| Concurrent Connections | >= 100 | `BM_Scalability_ConcurrentConnections` | Verified |
| QUIC Throughput | >= 90% of TCP | `BM_Comparison_BufferPrep_*` | Pending |
| QUIC 0-RTT Reconnection | <= 50ms | `BM_QUIC_Connection_0RTT` | Pending |
| Connection Migration | < 100ms disruption | `BM_QUIC_ConnectionMigration_*` | Pending |
| Encryption Throughput | >= 1 GB/s | `BM_AES_GCM_Encryption` | Pending |
| Decryption Throughput | >= 1.5 GB/s | `BM_AES_GCM_Decryption` | Pending |
| Encryption Overhead | <= 10% | `BM_Encryption_Overhead` | Pending |
| Key Derivation | >= 100 ops/sec | `BM_PBKDF2_Key_Derivation` | Pending |

## Directory Structure

```
benchmarks/
├── CMakeLists.txt           # Benchmark build configuration
├── README.md                # This file
├── utils/
│   ├── benchmark_helpers.h  # Test data generation utilities
│   └── benchmark_helpers.cpp
├── throughput/
│   ├── bench_single_file_throughput.cpp  # End-to-end throughput
│   └── bench_chunk_operations.cpp        # Component benchmarks
├── compression/
│   └── bench_lz4_compression.cpp  # Compression/decompression benchmarks
├── latency/
│   └── bench_latency.cpp          # Connection and response latency
├── memory/
│   └── bench_memory_usage.cpp     # Memory usage benchmarks
├── scalability/
│   └── bench_scalability.cpp      # Scalability benchmarks
├── transport/
│   ├── bench_quic_transport.cpp          # QUIC transport benchmarks
│   └── bench_tcp_vs_quic_comparison.cpp  # TCP vs QUIC comparison
└── encryption/
    ├── bench_encryption_throughput.cpp   # AES-GCM encryption/decryption
    └── bench_key_derivation.cpp          # PBKDF2, Argon2 key derivation
```

## Adding New Benchmarks

1. Create a new `.cpp` file in the appropriate directory
2. Include required headers:
   ```cpp
   #include <benchmark/benchmark.h>
   #include "utils/benchmark_helpers.h"
   ```
3. Write benchmark functions:
   ```cpp
   static void BM_MyBenchmark(::benchmark::State& state) {
       for (auto _ : state) {
           // Code to benchmark
       }
       state.SetBytesProcessed(...);
   }
   BENCHMARK(BM_MyBenchmark)->Arg(1024)->Arg(1024*1024);
   ```
4. Add the file to `CMakeLists.txt`

## Interpreting Results

### Key Metrics

- **Time**: Wall clock time per iteration
- **CPU**: CPU time per iteration
- **bytes_per_second**: Throughput in bytes/second
- **items_per_second**: Operations per second
- **throughput_MB_s**: Custom counter for MB/s throughput

### Example Output

```
-----------------------------------------------------------------------
Benchmark                             Time             CPU   Iterations
-----------------------------------------------------------------------
BM_SingleFile_SplitThroughput/100MB  195 ms         195 ms         10
BM_Checksum_CRC32/1024               2.5 us         2.5 us     280000
```

## CI Integration

### GitHub Actions Workflow

Benchmarks are automatically run via the `benchmark.yml` workflow:

- **Triggers**: Push to main, pull requests, weekly schedule (Sunday 00:00 UTC)
- **Manual trigger**: Use workflow_dispatch with optional filter pattern

**Note**: The following benchmarks are skipped in CI because they require server/client connections:
- `latency_benchmarks` - Connection setup, file list response, TTFB
- `memory_benchmarks` - Server/client memory usage
- `scalability_benchmarks` - Concurrent connections, long-running stability

Run these benchmarks locally for accurate measurements.

```bash
# Trigger manually from GitHub CLI
gh workflow run benchmark.yml --ref main

# With filter pattern
gh workflow run benchmark.yml --ref main -f benchmark_filter="BM_LZ4*"
```

### Automatic Regression Detection

The workflow uses [github-action-benchmark](https://github.com/benchmark-action/github-action-benchmark) for automatic regression detection:

- Results are stored in the `gh-pages` branch
- Historical comparison is available at `https://<owner>.github.io/<repo>/dev/bench/`
- **Alert threshold**: 150% of baseline (configurable)
- PR comments are posted when regressions are detected

### Local CI Simulation

```bash
# Build and run all benchmarks with CI settings
cmake -B build -DFILE_TRANS_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run with JSON output
./build/bin/throughput_benchmarks \
    --benchmark_format=json \
    --benchmark_out=benchmark_results.json \
    --benchmark_repetitions=3 \
    --benchmark_report_aggregates_only=true
```

### Benchmark Artifacts

CI runs upload benchmark results as artifacts:
- **Retention**: 90 days
- **Format**: JSON files per benchmark category
- **Download**: Available from the Actions run page
