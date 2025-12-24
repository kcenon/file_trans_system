# Pipeline Architecture Guide

Detailed guide to the multi-stage pipeline architecture in **file_trans_system**.

**Version:** 0.2.0
**Architecture:** Client-Server Model

---

## Overview

The file_trans_system uses a **pipeline architecture** to maximize throughput through parallel processing of different stages. Both the server and client utilize bidirectional pipelines that support upload and download operations concurrently.

### Key Concepts

- **Upload Pipeline**: Client reads file → compresses → sends → Server receives → decompresses → writes
- **Download Pipeline**: Server reads file → compresses → sends → Client receives → decompresses → writes
- **Bidirectional**: Both server and client can handle multiple concurrent upload/download streams
- **Backpressure**: Bounded queues prevent memory overflow under any load condition

---

## Architecture Diagram

### Upload Pipeline (Client → Server)

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                         UPLOAD PIPELINE (Client Side)                            │
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
│  │ read_queue │─▶│chunk_queue │─▶│ comp_queue │─▶│ send_queue │───────────┐   │
│  │   (16)     │  │   (16)     │  │   (32)     │  │   (64)     │           │   │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘           │   │
└───────────────────────────────────────────────────────────────────────────│───┘
                                                                            │
                                        ════════════════════════════════════╪════
                                                     NETWORK                │
                                        ════════════════════════════════════╪════
                                                                            │
┌───────────────────────────────────────────────────────────────────────────│───┐
│                         UPLOAD PIPELINE (Server Side)                     │   │
│                                                                           │   │
│  Stage Queues (Backpressure):                                             │   │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐           │   │
│  │ recv_queue │◀─│decomp_queue│◀─│assem_queue │◀─│write_queue │◀──────────┘   │
│  │   (64)     │  │   (32)     │  │   (16)     │  │   (16)     │               │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘               │
│        │                   │                   │                   │          │
│        ▼                   ▼                   ▼                   ▼          │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐│
│  │   Network    │───▶│     LZ4      │───▶│    Chunk     │───▶│  File Write  ││
│  │   Receive    │    │  Decompress  │    │   Assembly   │    │    Stage     ││
│  │  (network)   │    │(compression) │    │(chunk_process)│   │  (io_write)  ││
│  └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘│
│                                                                               │
│                           SERVER STORAGE                                      │
│  ┌─────────────────────────────────────────────────────────────────────────┐ │
│  │  /storage/files/uploaded_file.dat                                        │ │
│  └─────────────────────────────────────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────────────────────────────┘
```

### Download Pipeline (Server → Client)

```
┌───────────────────────────────────────────────────────────────────────────────┐
│                        DOWNLOAD PIPELINE (Server Side)                         │
│                                                                                │
│                           SERVER STORAGE                                       │
│  ┌─────────────────────────────────────────────────────────────────────────┐  │
│  │  /storage/files/requested_file.dat                                       │  │
│  └─────────────────────────────────────────────────────────────────────────┘  │
│                                                                                │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐ │
│  │  File Read   │───▶│    Chunk     │───▶│     LZ4      │───▶│   Network    │ │
│  │    Stage     │    │   Assembly   │    │  Compress    │    │    Send      │ │
│  │  (io_read)   │    │(chunk_process)│   │(compression) │    │  (network)   │ │
│  └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘ │
│        │                   │                   │                   │           │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐    │           │
│  │ read_queue │─▶│chunk_queue │─▶│ comp_queue │─▶│ send_queue │────┼──────┐    │
│  │   (16)     │  │   (16)     │  │   (32)     │  │   (64)     │    │      │    │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘    │      │    │
└────────────────────────────────────────────────────────────────────│──────│────┘
                                                                     │      │
                                        ═════════════════════════════╪══════╪════
                                                     NETWORK         │      │
                                        ═════════════════════════════╪══════╪════
                                                                     │      │
┌────────────────────────────────────────────────────────────────────│──────│────┐
│                        DOWNLOAD PIPELINE (Client Side)             │      │    │
│                                                                    │      │    │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐   │      │    │
│  │ recv_queue │◀─│decomp_queue│◀─│assem_queue │◀─│write_queue │◀──┴──────┘    │
│  │   (64)     │  │   (32)     │  │   (16)     │  │   (16)     │               │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘               │
│        │                   │                   │                   │          │
│        ▼                   ▼                   ▼                   ▼          │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐│
│  │   Network    │───▶│     LZ4      │───▶│    Chunk     │───▶│  File Write  ││
│  │   Receive    │    │  Decompress  │    │   Assembly   │    │    Stage     ││
│  │  (network)   │    │(compression) │    │(chunk_process)│   │  (io_write)  ││
│  └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘│
│                                                                               │
│                           CLIENT LOCAL                                        │
│  ┌─────────────────────────────────────────────────────────────────────────┐ │
│  │  /downloads/requested_file.dat                                           │ │
│  └─────────────────────────────────────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────────────────────────────┘
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

### Pipeline Direction by Role

| Role | Operation | Active Stages |
|------|-----------|---------------|
| **Client** | Upload | io_read → chunk → compress → network |
| **Client** | Download | network → decompress → chunk → io_write |
| **Server** | Receive Upload | network → decompress → chunk → io_write |
| **Server** | Send Download | io_read → chunk → compress → network |

---

## How Pipeline Processing Works

### Upload Flow (Client → Server)

#### Client Side (Source)

```
1. File Read Stage
   - Read chunk_size bytes from local file
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
   - Send to server
   - Wait for CHUNK_ACK
```

#### Server Side (Destination)

```
1. Network Receive Stage
   - Receive from client connection
   - Parse chunk header
   - Validate client session
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
   - Write to storage at correct offset
   - Update transfer progress
   - On completion: verify SHA-256
```

### Download Flow (Server → Client)

The download flow is the reverse direction with server reading from storage and client writing to local filesystem.

---

## Server Pipeline Management

### Multiple Concurrent Connections

```
┌─────────────────────────────────────────────────────────────────┐
│                         SERVER                                   │
│                                                                  │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐ │
│  │  Connection 1   │  │  Connection 2   │  │  Connection N   │ │
│  │  (Upload)       │  │  (Download)     │  │  (Upload)       │ │
│  │  Pipeline       │  │  Pipeline       │  │  Pipeline       │ │
│  └────────┬────────┘  └────────┬────────┘  └────────┬────────┘ │
│           │                    │                    │           │
│           ▼                    ▼                    ▼           │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              Shared Thread Pool                           │  │
│  │  ┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐       │  │
│  │  │ Net 1 │ │ Net 2 │ │ Comp 1│ │ Comp 2│ │ IO 1  │ ...   │  │
│  │  └───────┘ └───────┘ └───────┘ └───────┘ └───────┘       │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              Storage Manager                              │  │
│  │  /storage/files/                                          │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### Connection Isolation

Each client connection has independent:
- Upload/download progress tracking
- Chunk reassembly state
- Transfer checkpoints

Shared resources:
- Thread pool workers
- Storage manager
- Compression context pool

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

#### Slow Network (Upload Bottleneck)

```
CLIENT:
read_queue:     [■■■■■■■■■■■■■■■■] FULL
compress_queue: [■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■] FULL
send_queue:     [■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■] FULL

→ File read stage BLOCKS
→ Memory usage stays bounded
→ No runaway memory consumption
```

#### Slow Storage (Download Bottleneck)

```
CLIENT:
recv_queue:     [■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■] FULL
decompress_queue: [■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■] FULL
write_queue:    [■■■■■■■■■■■■■■■■] processing slowly...

→ Network receive stage BLOCKS
→ Server notices and slows down sending
→ System-wide flow control achieved
```

### Memory Bounds

Memory usage is bounded by:

```
max_memory = Σ (queue_size × chunk_size) for all queues
```

**Client memory** (default configuration, 256KB chunks):
```
Upload:   (16 + 16 + 32 + 64) × 256KB = 32MB
Download: (64 + 32 + 16 + 16) × 256KB = 32MB
Bidirectional: 64MB maximum
```

**Server memory** (per connection):
```
Upload receive:   (64 + 32 + 16 + 16) × 256KB = 32MB
Download send:    (16 + 16 + 32 + 64) × 256KB = 32MB
Per connection: ~64MB maximum
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
pool->add_workers(pipeline_stage::io_write, config.io_write_workers);
```

### Pipeline Job Types

The server pipeline uses specialized job classes that inherit from `kcenon::thread::job` for integration with thread_system's thread_pool:

| Job Type | Stage | Purpose |
|----------|-------|---------|
| `decompress_job` | decompress | LZ4 decompression of compressed chunks |
| `verify_job` | chunk_verify | CRC32 checksum verification |
| `write_job` | file_write | Write chunk data to disk |
| `read_job` | file_read | Read chunk data from disk |
| `compress_job` | compress | LZ4 compression (adaptive) |
| `send_job` | network_send | Prepare chunks for network transmission |

### Shared Pipeline Context

All jobs share a `pipeline_context` for accessing thread pool, queues, callbacks, and statistics:

```cpp
struct pipeline_context {
    // Thread pool for job execution
    std::shared_ptr<thread::thread_pool> thread_pool;

    // Bounded job queues for each stage (used for backpressure tracking)
    std::shared_ptr<thread::bounded_job_queue> decompress_queue;
    std::shared_ptr<thread::bounded_job_queue> verify_queue;
    std::shared_ptr<thread::bounded_job_queue> write_queue;
    std::shared_ptr<thread::bounded_job_queue> read_queue;
    std::shared_ptr<thread::bounded_job_queue> compress_queue;
    std::shared_ptr<thread::bounded_job_queue> send_queue;

    // Compression engines for workers (round-robin selection by worker_id)
    std::vector<std::unique_ptr<compression_engine>> compression_engines;

    // Callbacks
    stage_callback on_stage_complete_cb;      // Called when stage completes
    error_callback on_error_cb;               // Called on errors
    completion_callback on_upload_complete_cb; // Called when upload chunk is written
    std::function<void(const pipeline_chunk&)> on_download_ready_cb;

    // Statistics and state
    pipeline_stats* statistics;               // Pointer to shared statistics
    std::atomic<bool>* running;               // Pipeline running state

    // Bandwidth limiters
    bandwidth_limiter* send_limiter;
    bandwidth_limiter* recv_limiter;
};
```

### Job Implementation

Jobs inherit from `pipeline_job_base` which provides:
- Access to shared `pipeline_context`
- Cancellation token handling via `cancellation_token_`
- Common `is_cancelled()` check

```cpp
// Example: decompress_job implementation
// Jobs use worker_id to select compression engine via round-robin
class decompress_job : public pipeline_job_base {
public:
    decompress_job(std::shared_ptr<pipeline_context> context,
                   pipeline_chunk chunk,
                   std::size_t worker_id);

    [[nodiscard]] auto do_work() -> common::VoidResult override {
        if (is_cancelled()) {
            return thread::make_error_result(
                thread::error_code::operation_canceled, "Decompress job cancelled");
        }

        if (!context_->is_running()) {
            return common::ok();  // Early exit if pipeline stopped
        }

        if (chunk_.is_compressed) {
            // Select engine via round-robin based on worker_id
            auto& engine = context_->compression_engines[
                worker_id_ % context_->compression_engines.size()];

            auto result = engine->decompress(
                std::span<const std::byte>(chunk_.data),
                chunk_.original_size);

            if (result.has_value()) {
                chunk_.data = std::move(result.value());
                chunk_.is_compressed = false;
            } else {
                context_->report_error(pipeline_stage::decompress, result.error().message);
                return thread::make_error_result(thread::error_code::job_execution_failed, ...);
            }
        }

        context_->report_stage_complete(pipeline_stage::decompress, chunk_);
        return common::ok();
    }

private:
    pipeline_chunk chunk_;
    std::size_t worker_id_;
};

// Example: read_job for download pipeline
class read_job : public pipeline_job_base {
public:
    read_job(std::shared_ptr<pipeline_context> context,
             const transfer_id& id,
             uint64_t chunk_index,
             std::filesystem::path file_path,
             uint64_t offset,
             std::size_t size);

    [[nodiscard]] auto do_work() -> common::VoidResult override {
        // Read file at offset, calculate CRC32, report stage complete
        // Then submit compress_job to thread pool for next stage
    }

private:
    transfer_id id_;
    uint64_t chunk_index_;
    std::filesystem::path file_path_;
    uint64_t offset_;
    std::size_t size_;
    pipeline_chunk chunk_;
};
```

### Job Chaining

Jobs can be chained together by submitting the next job when the current one completes:

```cpp
// In pipeline orchestration:
auto on_decompress_complete = [this](pipeline_stage, const pipeline_chunk& chunk) {
    // Submit next stage job
    auto verify = std::make_shared<verify_job>(chunk, context_);
    thread_pool_->submit(std::move(verify));
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

    // Directional statistics
    uint64_t total_uploaded_bytes{0};
    uint64_t total_downloaded_bytes{0};

    [[nodiscard]] auto bottleneck_stage() const -> pipeline_stage;
};
```

### Monitoring Example

```cpp
// Server statistics
auto server_stats = server->get_pipeline_stats();

std::cout << "Server Pipeline Performance:\n";
std::cout << "  Network Recv: " << server_stats.network_stats.throughput_mbps() << " MB/s\n";
std::cout << "  Decompress:   " << server_stats.compression_stats.throughput_mbps() << " MB/s\n";
std::cout << "  File Write:   " << server_stats.io_write_stats.throughput_mbps() << " MB/s\n";
std::cout << "  Total Upload: " << server_stats.total_uploaded_bytes / 1e9 << " GB\n";
std::cout << "  Total Download: " << server_stats.total_downloaded_bytes / 1e9 << " GB\n";

// Client statistics
auto client_stats = client->get_pipeline_stats();

std::cout << "Client Pipeline Performance:\n";
std::cout << "  File Read:    " << client_stats.io_read_stats.throughput_mbps() << " MB/s\n";
std::cout << "  Compress:     " << client_stats.compression_stats.throughput_mbps() << " MB/s\n";
std::cout << "  Network Send: " << client_stats.network_stats.throughput_mbps() << " MB/s\n";

// Identify bottleneck
auto bottleneck = client_stats.bottleneck_stage();
std::cout << "Bottleneck: " << stage_name(bottleneck) << "\n";
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
    .send_queue_size = 64,
    .recv_queue_size = 64,
    .decompress_queue_size = 32,
    .write_queue_size = 16
};
```

#### High-Throughput Server

```cpp
pipeline_config config{
    .io_read_workers = 4,
    .chunk_workers = 4,
    .compression_workers = 16,
    .network_workers = 8,
    .io_write_workers = 4,

    .send_queue_size = 128,
    .recv_queue_size = 128
};
```

#### High-Throughput Client

```cpp
pipeline_config config{
    .io_read_workers = 4,
    .compression_workers = std::thread::hardware_concurrency(),
    .network_workers = 4,

    .compress_queue_size = 64,
    .send_queue_size = 128
};
```

#### Memory-Constrained Client

```cpp
pipeline_config config{
    .io_read_workers = 1,
    .chunk_workers = 1,
    .compression_workers = 2,
    .network_workers = 1,
    .io_write_workers = 1,

    .read_queue_size = 4,
    .compress_queue_size = 8,
    .send_queue_size = 16,
    .recv_queue_size = 16,
    .decompress_queue_size = 8,
    .write_queue_size = 4
};
// Total memory: ~14MB with 256KB chunks
```

---

## Error Handling in Pipeline

### Stage Error Recovery

Each stage handles errors independently:

```cpp
void upload_compress_job::execute() {
    auto result = compressor_.compress(chunk_);

    if (!result) {
        // Compression failed - pass through uncompressed
        chunk_.header.flags &= ~chunk_flags::compressed;
        output_queue_.push(std::move(chunk_));
        return;
    }

    output_queue_.push(std::move(result.value()));
}

void download_write_job::execute() {
    auto result = writer_.write_at(chunk_.header.offset, chunk_.data);

    if (!result) {
        // Write failed - report error to transfer manager
        transfer_manager_.report_error(transfer_id_, result.error());
        return;
    }
}
```

### Pipeline Shutdown

Graceful shutdown ensures all in-flight data is processed.

> **Note:** The pipeline destructor always clears all job queues and explicitly breaks circular references to prevent memory leaks. The reference cycle is: jobs hold `shared_ptr<pipeline_context>`, which holds `shared_ptr<thread_pool>`, which holds the job queue containing those jobs. The destructor:
> 1. Clears the thread pool's job queue to remove pending jobs
> 2. Resets `context->thread_pool` to break the reference cycle
> 3. Then shuts down the thread pool safely

```cpp
// Client shutdown
auto client::disconnect(bool wait_for_completion) -> Result<void> {
    if (wait_for_completion) {
        // Wait for active uploads to complete
        upload_pipeline_->drain();

        // Wait for active downloads to complete
        download_pipeline_->drain();
    }

    // Close connection
    connection_->close();

    // Stop pipelines
    upload_pipeline_->stop();
    download_pipeline_->stop();

    return {};
}

// Server shutdown
auto server::stop(bool wait_for_completion) -> Result<void> {
    if (wait_for_completion) {
        // Wait for all client transfers to complete
        for (auto& [client_id, connection] : connections_) {
            connection.pipeline->drain();
        }
    }

    // Stop accepting new connections
    acceptor_->stop();

    // Close all connections
    for (auto& [client_id, connection] : connections_) {
        connection.close();
    }

    // Stop thread pool
    thread_pool_->stop();

    return {};
}
```

---

## Advanced Topics

### Connection-Aware Pipeline

Server pipeline tracks per-connection state:

```cpp
struct connection_pipeline {
    connection_id client_id;

    // Upload state (client → server)
    std::map<transfer_id, upload_state> active_uploads;

    // Download state (server → client)
    std::map<transfer_id, download_state> active_downloads;

    // Shared queues
    bounded_queue<chunk> recv_queue;
    bounded_queue<chunk> send_queue;

    // Statistics per connection
    pipeline_statistics stats;
};
```

### Pipeline Metrics Integration

Export metrics to monitoring_system:

```cpp
class server_pipeline_metrics_exporter {
public:
    void export_to_monitoring() {
        auto stats = server_->get_pipeline_stats();

        // Server-wide metrics
        monitoring::gauge("server.pipeline.network.recv_throughput_mbps",
            stats.network_stats.throughput_mbps());
        monitoring::gauge("server.pipeline.compression.throughput_mbps",
            stats.compression_stats.throughput_mbps());
        monitoring::gauge("server.pipeline.io_write.throughput_mbps",
            stats.io_write_stats.throughput_mbps());

        // Transfer metrics
        monitoring::counter("server.total_uploaded_bytes",
            stats.total_uploaded_bytes);
        monitoring::counter("server.total_downloaded_bytes",
            stats.total_downloaded_bytes);

        // Connection metrics
        monitoring::gauge("server.active_connections",
            server_->get_statistics().active_connections);
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
auto stats = client->get_pipeline_stats();
auto bottleneck = stats.bottleneck_stage();
// Identify actual bottleneck before making changes
```

### 3. Tune One Parameter at a Time

```cpp
// If compression is bottleneck:
config.compression_workers *= 2;
// Re-measure before making additional changes
```

### 4. Consider Both Directions

```cpp
// Client needs resources for both upload and download
// Don't optimize upload at the expense of download
```

### 5. Test with Representative Workloads

```cpp
// Text files have different characteristics than binary
// Test with actual file types you'll transfer
// Test both upload and download scenarios
```

---

*Version: 0.2.1*
*Last updated: 2025-12-25*
