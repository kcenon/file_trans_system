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

## Performance Targets

Based on SRS requirements:

| Metric | Target | Benchmark | Status |
|--------|--------|-----------|--------|
| LAN Throughput | >= 500 MB/s | `BM_SingleFile_*Throughput` | Verified |
| LZ4 Compression | >= 400 MB/s | `BM_LZ4_Compression_Fast` | Verified |
| LZ4 Decompression | >= 1.5 GB/s | `BM_LZ4_Decompression` | Verified |
| File List (10K files) | < 100ms | `BM_FileList_Response` | Pending |
| Connection Setup | < 100ms | `BM_Connection_Setup` | Pending |
| Server Memory | < 100 MB | (Future: memory benchmarks) | - |
| Client Memory | < 50 MB | (Future: memory benchmarks) | - |

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
└── latency/
    └── bench_latency.cpp          # Connection and response latency
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

Benchmarks can be run in CI with:

```bash
./build/bin/throughput_benchmarks \
    --benchmark_format=json \
    --benchmark_out=benchmark_results.json \
    --benchmark_repetitions=3
```

Use the JSON output for regression detection by comparing against baseline results.
