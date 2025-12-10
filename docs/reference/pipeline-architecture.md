# Pipeline Architecture Guide

Detailed guide to the multi-stage pipeline architecture in **file_trans_system**.

## Overview

The file_trans_system uses a **pipeline architecture** to maximize throughput through parallel processing of different stages. This design allows I/O-bound and CPU-bound operations to execute concurrently without blocking each other.

---

## Architecture Diagram

### Sender Pipeline

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                           SENDER PIPELINE                                        │
│                                                                                  │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐  │
│  │  File Read   │───▶│    Chunk     │───▶│     LZ4      │───▶│   Network    │  │
│  │    Stage     │    │   Assembly   │    │  Compress    │    │    Send      │  │
│  │  (io_read)   │    │(chunk_process)│   │(compression) │    │  (network)   │  │
│  └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘  │
│        │                   │                   │                   │            │
│        ▼                   ▼                   ▼                   ▼            │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │                    typed_thread_pool<pipeline_stage>                      │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │  │
│  │  │  IO      │  │  IO      │  │  Compute │  │  Compute │  │ Network  │   │  │
│  │  │ Worker 1 │  │ Worker 2 │  │ Worker 1 │  │ Worker 2 │  │ Worker 1 │   │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                  │
│  Stage Queues (Backpressure):                                                   │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐               │
│  │ read_queue │─▶│chunk_queue │─▶│ comp_queue │─▶│ send_queue │               │
│  │   (16)     │  │   (16)     │  │   (32)     │  │   (64)     │               │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘               │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### Receiver Pipeline

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                           RECEIVER PIPELINE                                      │
│                                                                                  │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐  │
│  │   Network    │───▶│     LZ4      │───▶│    Chunk     │───▶│  File Write  │  │
│  │   Receive    │    │  Decompress  │    │   Assembly   │    │    Stage     │  │
│  │  (network)   │    │(compression) │    │(chunk_process)│   │  (io_write)  │  │
│  └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘  │
│        │                   │                   │                   │            │
│        ▼                   ▼                   ▼                   ▼            │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │                    typed_thread_pool<pipeline_stage>                      │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │  │
│  │  │ Network  │  │  Compute │  │  Compute │  │  IO      │  │  IO      │   │  │
│  │  │ Worker 1 │  │ Worker 1 │  │ Worker 2 │  │ Worker 1 │  │ Worker 2 │   │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                  │
│  Stage Queues (Backpressure):                                                   │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐               │
│  │ recv_queue │─▶│decomp_queue│─▶│assem_queue │─▶│write_queue │               │
│  │   (64)     │  │   (32)     │  │   (16)     │  │   (16)     │               │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘               │
└─────────────────────────────────────────────────────────────────────────────────┘
```

---

## Pipeline Stages

### Stage Types

```cpp
enum class pipeline_stage : uint8_t {
    io_read,        // File read operations (I/O bound)
    chunk_process,  // Chunk assembly/disassembly (CPU light)
    compression,    // LZ4 compress/decompress (CPU bound)
    network,        // Network send/receive (I/O bound)
    io_write        // File write operations (I/O bound)
};
```

### Stage Characteristics

| Stage | Type | Default Workers | Bottleneck Factor |
|-------|------|-----------------|-------------------|
| `io_read` | I/O bound | 2 | Storage speed |
| `chunk_process` | CPU light | 2 | Minimal |
| `compression` | CPU bound | 4 | CPU cores |
| `network` | I/O bound | 2 | Network bandwidth |
| `io_write` | I/O bound | 2 | Storage speed |

---

## How Pipeline Processing Works

### Data Flow (Sender)

```
1. File Read Stage
   - Read chunk_size bytes from file
   - Create chunk with metadata (offset, index)
   - Enqueue to read_queue

2. Chunk Assembly Stage
   - Dequeue from read_queue
   - Calculate CRC32 checksum
   - Set chunk flags (first/last)
   - Enqueue to compress_queue

3. Compression Stage
   - Dequeue from compress_queue
   - Apply adaptive compression check
   - Compress with LZ4 (if beneficial)
   - Set compressed flag
   - Enqueue to send_queue

4. Network Send Stage
   - Dequeue from send_queue
   - Serialize chunk header + data
   - Send over transport
   - Wait for ACK
```

### Data Flow (Receiver)

```
1. Network Receive Stage
   - Receive from transport
   - Parse chunk header
   - Enqueue to decompress_queue

2. Decompression Stage
   - Dequeue from decompress_queue
   - Decompress if compressed flag set
   - Verify CRC32
   - Enqueue to assemble_queue

3. Chunk Assembly Stage
   - Dequeue from assemble_queue
   - Handle out-of-order chunks
   - Track received chunks
   - Enqueue ready chunks to write_queue

4. File Write Stage
   - Dequeue from write_queue
   - Write to file at correct offset
   - Update progress
```

---

## Backpressure Mechanism

### How Backpressure Works

Bounded queues create natural flow control:

```cpp
template<typename T>
class bounded_queue {
    std::size_t max_size_;
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;

public:
    void push(T item) {
        std::unique_lock lock(mutex_);
        // BLOCK if queue is full
        not_full_.wait(lock, [this] {
            return queue_.size() < max_size_;
        });
        queue_.push(std::move(item));
        not_empty_.notify_one();
    }

    T pop() {
        std::unique_lock lock(mutex_);
        // BLOCK if queue is empty
        not_empty_.wait(lock, [this] {
            return !queue_.empty();
        });
        T item = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return item;
    }
};
```

### Backpressure Scenarios

#### Slow Network (Network Stage Bottleneck)

```
read_queue:     [■■■■■■■■■■■■■■■■] FULL
compress_queue: [■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■] FULL
send_queue:     [■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■] FULL

→ File read stage BLOCKS
→ Memory usage stays bounded
→ No runaway memory consumption
```

#### Slow Compression (Compression Stage Bottleneck)

```
read_queue:     [■■■■■■■■■■■■■■■■] FULL
compress_queue: [■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■] processing...
send_queue:     [■■■□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□] waiting

→ Network stage has capacity
→ Add more compression workers
```

### Memory Bounds

Memory usage is bounded by:

```
max_memory = Σ (queue_size × chunk_size) for all queues
```

Default configuration (256KB chunks):
```
Sender:   (16 + 16 + 32 + 64) × 256KB = 32MB
Receiver: (64 + 32 + 16 + 16) × 256KB = 32MB
Total:    64MB maximum
```

---

## typed_thread_pool Integration

### Stage-Based Job Routing

The pipeline uses `typed_thread_pool` from thread_system for stage-specific worker pools:

```cpp
// Create typed thread pool with stage types
auto pool = std::make_unique<thread::typed_thread_pool<pipeline_stage>>();

// Configure workers per stage
pool->add_workers(pipeline_stage::io_read, config.io_read_workers);
pool->add_workers(pipeline_stage::compression, config.compression_workers);
pool->add_workers(pipeline_stage::network, config.network_workers);
```

### Job Implementation

```cpp
template<pipeline_stage Stage>
class pipeline_job : public thread::typed_job_t<pipeline_stage> {
public:
    explicit pipeline_job(const std::string& name)
        : typed_job_t<pipeline_stage>(Stage, name) {}

    virtual void execute() = 0;
};

// Concrete job example
class compress_job : public pipeline_job<pipeline_stage::compression> {
public:
    compress_job(chunk c, bounded_queue<chunk>& output_queue)
        : pipeline_job("compress_job")
        , chunk_(std::move(c))
        , output_queue_(output_queue)
    {}

    void execute() override {
        auto compressed = compressor_.compress(chunk_);
        output_queue_.push(std::move(compressed));
    }

private:
    chunk chunk_;
    bounded_queue<chunk>& output_queue_;
};
```

---

## Pipeline Statistics

### Statistics Structure

```cpp
struct pipeline_statistics {
    struct stage_stats {
        std::atomic<uint64_t> jobs_processed{0};
        std::atomic<uint64_t> bytes_processed{0};
        std::atomic<uint64_t> total_latency_us{0};
        std::atomic<std::size_t> current_queue_depth{0};
        std::atomic<std::size_t> max_queue_depth{0};

        [[nodiscard]] auto avg_latency_us() const -> double {
            return jobs_processed > 0
                ? static_cast<double>(total_latency_us) / jobs_processed
                : 0.0;
        }

        [[nodiscard]] auto throughput_mbps() const -> double;
    };

    stage_stats io_read_stats;
    stage_stats chunk_stats;
    stage_stats compression_stats;
    stage_stats network_stats;
    stage_stats io_write_stats;

    [[nodiscard]] auto bottleneck_stage() const -> pipeline_stage;
};
```

### Monitoring Example

```cpp
// Get current statistics
auto stats = sender->get_pipeline_stats();

// Print stage performance
std::cout << "Stage Performance:\n";
std::cout << "  IO Read:     " << stats.io_read_stats.throughput_mbps() << " MB/s\n";
std::cout << "  Compression: " << stats.compression_stats.throughput_mbps() << " MB/s\n";
std::cout << "  Network:     " << stats.network_stats.throughput_mbps() << " MB/s\n";

// Identify bottleneck
auto bottleneck = stats.bottleneck_stage();
std::cout << "Bottleneck: " << stage_name(bottleneck) << "\n";

// Get queue depths
auto depths = sender->get_queue_depths();
std::cout << "Queue Depths:\n";
std::cout << "  Read:     " << depths.read_queue << "\n";
std::cout << "  Compress: " << depths.compress_queue << "\n";
std::cout << "  Send:     " << depths.send_queue << "\n";
```

### Bottleneck Detection

```cpp
auto bottleneck_stage() const -> pipeline_stage {
    // Stage with highest average latency relative to throughput
    // indicates the bottleneck

    double max_bottleneck_score = 0;
    pipeline_stage bottleneck = pipeline_stage::io_read;

    for (const auto& [stage, stats] : stage_stats_) {
        double score = stats.avg_latency_us() / stats.throughput_mbps();
        if (score > max_bottleneck_score) {
            max_bottleneck_score = score;
            bottleneck = stage;
        }
    }

    return bottleneck;
}
```

---

## Tuning Guidelines

### Bottleneck-Based Tuning

| Bottleneck | Symptoms | Solution |
|------------|----------|----------|
| `io_read` | read_queue often empty, high read latency | Faster storage, more read workers |
| `compression` | compress_queue full, CPU at 100% | More compression workers, or disable compression |
| `network` | send_queue full, network saturated | Higher bandwidth, or increase compression |
| `io_write` | write_queue full | Faster storage, more write workers |

### Recommended Configurations

#### Balanced Configuration (Default)

```cpp
pipeline_config config{
    .io_read_workers = 2,
    .chunk_workers = 2,
    .compression_workers = 4,
    .network_workers = 2,
    .io_write_workers = 2,

    .read_queue_size = 16,
    .compress_queue_size = 32,
    .send_queue_size = 64
};
```

#### CPU-Optimized (For compressible data)

```cpp
pipeline_config config{
    .io_read_workers = 2,
    .chunk_workers = 2,
    .compression_workers = std::thread::hardware_concurrency(),
    .network_workers = 2,
    .io_write_workers = 2,

    .compress_queue_size = 64
};
```

#### Network-Optimized (For high-bandwidth network)

```cpp
pipeline_config config{
    .io_read_workers = 4,
    .compression_workers = 4,
    .network_workers = 4,

    .send_queue_size = 128
};
```

#### Memory-Constrained

```cpp
pipeline_config config{
    .io_read_workers = 1,
    .chunk_workers = 1,
    .compression_workers = 2,
    .network_workers = 1,
    .io_write_workers = 1,

    .read_queue_size = 4,
    .compress_queue_size = 8,
    .send_queue_size = 16
};
// Total memory: ~7MB with 256KB chunks
```

---

## Error Handling in Pipeline

### Stage Error Recovery

Each stage handles errors independently:

```cpp
void compress_job::execute() {
    auto result = compressor_.compress(chunk_);

    if (!result) {
        // Compression failed - pass through uncompressed
        chunk_.header.flags &= ~chunk_flags::compressed;
        output_queue_.push(std::move(chunk_));
        return;
    }

    output_queue_.push(std::move(result.value()));
}
```

### Pipeline Shutdown

Graceful shutdown ensures all in-flight data is processed:

```cpp
auto stop(bool wait_for_completion = true) -> Result<void> {
    if (wait_for_completion) {
        // Signal no more input
        read_queue_.close();

        // Wait for queues to drain
        compress_queue_.wait_until_empty();
        send_queue_.wait_until_empty();
    }

    // Stop thread pool
    thread_pool_->stop();

    return {};
}
```

---

## Advanced Topics

### Custom Pipeline Stages

For specialized processing, you can extend the pipeline:

```cpp
// Custom encryption stage
enum class extended_stage : uint8_t {
    io_read = 0,
    chunk_process = 1,
    compression = 2,
    encryption = 3,  // Custom stage
    network = 4,
    io_write = 5
};

// Add encryption workers
pool->add_workers(extended_stage::encryption, 2);
```

### Pipeline Metrics Integration

Export metrics to monitoring_system:

```cpp
class pipeline_metrics_exporter {
public:
    void export_to_monitoring() {
        auto stats = pipeline_->get_stats();

        monitoring::gauge("pipeline.io_read.throughput_mbps",
            stats.io_read_stats.throughput_mbps());
        monitoring::gauge("pipeline.compression.throughput_mbps",
            stats.compression_stats.throughput_mbps());
        monitoring::gauge("pipeline.network.throughput_mbps",
            stats.network_stats.throughput_mbps());

        monitoring::gauge("pipeline.bottleneck",
            static_cast<int>(stats.bottleneck_stage()));
    }
};
```

---

## Best Practices

### 1. Start with auto_detect()

```cpp
auto config = pipeline_config::auto_detect();
```

### 2. Monitor Before Tuning

```cpp
// Run with default config and observe
auto stats = sender->get_stats();
// Identify actual bottleneck before making changes
```

### 3. Tune One Parameter at a Time

```cpp
// If compression is bottleneck:
config.compression_workers *= 2;
// Re-measure before making additional changes
```

### 4. Consider the Entire System

```cpp
// Storage, CPU, and network form a system
// Optimizing one may shift bottleneck to another
```

### 5. Test with Representative Workloads

```cpp
// Text files have different characteristics than binary
// Test with actual file types you'll transfer
```

---

*Last updated: 2025-12-11*
