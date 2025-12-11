# Architecture Overview

This document provides a comprehensive overview of the **file_trans_system** architecture, including system layers, component relationships, and design patterns.

## Table of Contents

1. [System Overview](#system-overview)
2. [Layered Architecture](#layered-architecture)
3. [Component Diagram](#component-diagram)
4. [Pipeline Architecture](#pipeline-architecture)
5. [Data Flow](#data-flow)
6. [Design Patterns](#design-patterns)
7. [Threading Model](#threading-model)
8. [Memory Management](#memory-management)

---

## System Overview

file_trans_system is a high-performance file transfer library built on a **multi-stage pipeline architecture**. The design prioritizes:

- **Throughput**: Maximize data transfer rate through parallelism
- **Reliability**: Ensure data integrity with checksums and verification
- **Resumability**: Support interrupted transfer recovery
- **Scalability**: Handle many concurrent transfers efficiently
- **Memory Efficiency**: Bounded memory usage regardless of file size

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              APPLICATION                                     │
│                                                                              │
│         ┌─────────────────────┐       ┌─────────────────────┐               │
│         │    file_sender      │       │   file_receiver     │               │
│         └──────────┬──────────┘       └──────────┬──────────┘               │
│                    │                             │                           │
├────────────────────┼─────────────────────────────┼───────────────────────────┤
│                    │        CORE LAYER           │                           │
│         ┌──────────▼──────────┐       ┌──────────▼──────────┐               │
│         │   sender_pipeline   │       │  receiver_pipeline  │               │
│         │                     │       │                     │               │
│         │ ┌─────┐ ┌─────┐    │       │ ┌─────┐ ┌─────┐    │               │
│         │ │Read │→│Chunk│→   │       │ │Recv │→│Decomp│→  │               │
│         │ └─────┘ └─────┘    │       │ └─────┘ └─────┘    │               │
│         │ ┌─────┐ ┌─────┐    │       │ ┌─────┐ ┌─────┐    │               │
│         │ │Comp │→│Send │    │       │ │Assem│→│Write│    │               │
│         │ └─────┘ └─────┘    │       │ └─────┘ └─────┘    │               │
│         └──────────┬──────────┘       └──────────┬──────────┘               │
│                    │                             │                           │
├────────────────────┼─────────────────────────────┼───────────────────────────┤
│                    │       SERVICE LAYER         │                           │
│    ┌───────────────┴───────────────┬─────────────┴───────────────┐          │
│    │                               │                             │          │
│    ▼                               ▼                             ▼          │
│ ┌──────────────┐           ┌──────────────┐           ┌──────────────┐      │
│ │chunk_manager │           │chunk_compressor│         │   checksum   │      │
│ │              │           │              │           │              │      │
│ │ ├ splitter   │           │ ├ lz4_engine │           │ ├ crc32      │      │
│ │ └ assembler  │           │ └ adaptive   │           │ └ sha256     │      │
│ └──────────────┘           └──────────────┘           └──────────────┘      │
│                                                                              │
├──────────────────────────────────────────────────────────────────────────────┤
│                           TRANSPORT LAYER                                    │
│                                                                              │
│    ┌──────────────────────────────────────────────────────────────────┐     │
│    │                    transport_interface                            │     │
│    └─────────────────────────────┬────────────────────────────────────┘     │
│                                  │                                           │
│              ┌───────────────────┼───────────────────┐                      │
│              │                   │                   │                      │
│              ▼                   ▼                   ▼                      │
│       ┌────────────┐      ┌────────────┐      ┌────────────┐               │
│       │tcp_transport│      │quic_transport│    │   mock     │               │
│       │ (TLS 1.3)  │      │  (Phase 2) │      │ (testing)  │               │
│       └────────────┘      └────────────┘      └────────────┘               │
│                                                                              │
├──────────────────────────────────────────────────────────────────────────────┤
│                          INFRASTRUCTURE LAYER                                │
│                                                                              │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐            │
│  │common_system│  │thread_system│ │network_system│ │container_  │            │
│  │            │  │            │  │            │  │   system   │            │
│  │ Result<T>  │  │typed_thread │  │   socket   │  │  bounded   │            │
│  │ error      │  │   _pool    │  │   buffer   │  │   queue    │            │
│  │ time       │  │ job_queue  │  │            │  │            │            │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘            │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## Layered Architecture

### Layer 1: API Layer

The **API Layer** provides the public interface for applications.

| Class | Purpose | Key Methods |
|-------|---------|-------------|
| `file_sender` | Send files to remote endpoints | `send_file()`, `send_files()`, `cancel()`, `pause()`, `resume()` |
| `file_receiver` | Receive files from remote senders | `start()`, `stop()`, `set_output_directory()` |
| `transfer_manager` | Manage concurrent transfers | `get_status()`, `list_transfers()`, `get_statistics()` |

**Design Principle**: Builder pattern for fluent configuration, callback-based event handling.

### Layer 2: Core Layer

The **Core Layer** implements the pipeline processing logic.

| Component | Purpose |
|-----------|---------|
| `sender_pipeline` | Orchestrates file reading, compression, and sending |
| `receiver_pipeline` | Orchestrates receiving, decompression, and writing |

**Design Principle**: Stage-based parallel processing with bounded queues.

### Layer 3: Service Layer

The **Service Layer** provides reusable processing components.

| Component | Purpose |
|-----------|---------|
| `chunk_splitter` | Splits files into chunks |
| `chunk_assembler` | Reassembles chunks into files |
| `chunk_compressor` | Compresses/decompresses chunks |
| `lz4_engine` | Low-level LZ4 operations |
| `adaptive_compression` | Compressibility detection |
| `checksum` | CRC32 and SHA-256 operations |

**Design Principle**: Single responsibility, stateless where possible.

### Layer 4: Transport Layer

The **Transport Layer** abstracts network communication and is **implemented using network_system**.

| Component | Purpose |
|-----------|---------|
| `transport_interface` | Abstract transport API |
| `tcp_transport` | TCP + TLS 1.3 implementation **(via network_system)** |
| `quic_transport` | QUIC implementation **(via network_system, Phase 2)** |
| `transport_factory` | Factory for transport creation |

**Design Principle**: Strategy pattern for pluggable transports.

> **Note**: Both TCP and QUIC transports are provided by **network_system**. No external transport library is required. This ensures consistent behavior and optimal performance across all transport types.

### Layer 5: Infrastructure Layer

The **Infrastructure Layer** provides foundational utilities from the kcenon ecosystem.

| System | Components Used |
|--------|----------------|
| common_system | `Result<T>`, error codes, time utilities |
| thread_system | `typed_thread_pool<pipeline_stage>` for parallel pipeline processing |
| **network_system** | **TCP/TLS transport, QUIC transport, socket management, buffers** |
| container_system | `bounded_queue<T>` for backpressure |

> **Important**: The entire transport layer is built on **network_system**, which provides production-ready TCP (with TLS 1.3) and QUIC implementations.

---

## Component Diagram

### Sender Components

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              file_sender                                     │
│                                                                              │
│  Configuration:                    Methods:                                  │
│  ├ compression_mode               ├ send_file()                             │
│  ├ compression_level              ├ send_files()                            │
│  ├ chunk_size                     ├ cancel()                                │
│  ├ bandwidth_limit                ├ pause()                                 │
│  └ transport_type                 ├ resume()                                │
│                                   └ on_progress()                           │
└─────────────────────────────────────────────────────────────────────────────┘
         │
         │ creates
         ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            sender_pipeline                                   │
│                                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │   io_read    │  │chunk_process │  │ compression  │  │   network    │    │
│  │    stage     │──│    stage     │──│    stage     │──│    stage     │    │
│  │              │  │              │  │              │  │              │    │
│  │ Workers: 2   │  │ Workers: 2   │  │ Workers: 4   │  │ Workers: 2   │    │
│  │ Queue: 16    │  │ Queue: 16    │  │ Queue: 32    │  │ Queue: 64    │    │
│  └──────────────┘  └──────────────┘  └──────────────┘  └──────────────┘    │
│         │                 │                 │                 │             │
│         └─────────────────┴─────────────────┴─────────────────┘             │
│                                     │                                        │
│                                     ▼                                        │
│                    ┌────────────────────────────────┐                       │
│                    │ typed_thread_pool<pipeline_stage>│                      │
│                    └────────────────────────────────┘                       │
└─────────────────────────────────────────────────────────────────────────────┘
         │
         │ uses
         ▼
┌───────────────────┬───────────────────┬───────────────────┐
│  chunk_splitter   │ chunk_compressor  │ tcp_transport     │
│                   │                   │                   │
│  split()          │  compress()       │  connect()        │
│  calculate_       │  get_statistics() │  send()           │
│    metadata()     │                   │  receive()        │
└───────────────────┴───────────────────┴───────────────────┘
```

### Receiver Components

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            file_receiver                                     │
│                                                                              │
│  Configuration:                    Callbacks:                                │
│  ├ output_directory               ├ on_transfer_request()                   │
│  ├ bandwidth_limit                ├ on_progress()                           │
│  └ transport_type                 └ on_complete()                           │
│                                                                              │
│  Methods:                                                                    │
│  ├ start()                                                                   │
│  └ stop()                                                                    │
└─────────────────────────────────────────────────────────────────────────────┘
         │
         │ creates
         ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          receiver_pipeline                                   │
│                                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │   network    │  │ compression  │  │chunk_process │  │  io_write    │    │
│  │   receive    │──│  decompress  │──│   assemble   │──│    stage     │    │
│  │              │  │              │  │              │  │              │    │
│  │ Workers: 2   │  │ Workers: 4   │  │ Workers: 2   │  │ Workers: 2   │    │
│  │ Queue: 64    │  │ Queue: 32    │  │ Queue: 16    │  │ Queue: 16    │    │
│  └──────────────┘  └──────────────┘  └──────────────┘  └──────────────┘    │
│         │                 │                 │                 │             │
│         └─────────────────┴─────────────────┴─────────────────┘             │
│                                     │                                        │
│                                     ▼                                        │
│                    ┌────────────────────────────────┐                       │
│                    │ typed_thread_pool<pipeline_stage>│                      │
│                    └────────────────────────────────┘                       │
└─────────────────────────────────────────────────────────────────────────────┘
         │
         │ uses
         ▼
┌───────────────────┬───────────────────┬───────────────────┐
│ chunk_assembler   │ chunk_compressor  │ tcp_transport     │
│                   │                   │                   │
│  process_chunk()  │  decompress()     │  listen()         │
│  is_complete()    │  get_statistics() │  accept()         │
│  finalize()       │                   │  receive()        │
└───────────────────┴───────────────────┴───────────────────┘
```

---

## Pipeline Architecture

### Stage Definition

```cpp
enum class pipeline_stage : uint8_t {
    io_read,        // File read operations
    chunk_process,  // Chunk assembly/disassembly
    compression,    // LZ4 compress/decompress
    network,        // Network send/receive
    io_write        // File write operations
};
```

### Sender Pipeline Flow

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  io_read    │     │chunk_process│     │ compression │     │   network   │
│   stage     │────▶│    stage    │────▶│    stage    │────▶│    stage    │
│             │     │             │     │             │     │             │
│ Read file   │     │ Create      │     │ LZ4 compress│     │ Send over   │
│ in chunks   │     │ chunk header│     │ (adaptive)  │     │ transport   │
│             │     │ Add CRC32   │     │             │     │             │
└──────┬──────┘     └──────┬──────┘     └──────┬──────┘     └──────┬──────┘
       │                   │                   │                   │
       ▼                   ▼                   ▼                   ▼
  ┌─────────┐         ┌─────────┐         ┌─────────┐         ┌─────────┐
  │read_queue│         │chunk_queue│       │comp_queue│        │send_queue│
  │   (16)  │         │   (16)  │         │   (32)  │         │   (64)  │
  └─────────┘         └─────────┘         └─────────┘         └─────────┘
```

### Receiver Pipeline Flow

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   network   │     │ compression │     │chunk_process│     │  io_write   │
│   receive   │────▶│  decompress │────▶│   assemble  │────▶│   stage     │
│             │     │             │     │             │     │             │
│ Receive     │     │ LZ4         │     │ Verify CRC32│     │ Write to    │
│ from        │     │ decompress  │     │ Handle order│     │ file at     │
│ transport   │     │             │     │             │     │ offset      │
└──────┬──────┘     └──────┬──────┘     └──────┬──────┘     └──────┬──────┘
       │                   │                   │                   │
       ▼                   ▼                   ▼                   ▼
  ┌─────────┐         ┌─────────┐         ┌─────────┐         ┌─────────┐
  │recv_queue│        │decomp_queue│       │assem_queue│       │write_queue│
  │   (64)  │         │   (32)  │         │   (16)  │         │   (16)  │
  └─────────┘         └─────────┘         └─────────┘         └─────────┘
```

---

## Data Flow

### Chunk Structure

```
┌─────────────────────────────────────────────────────────────────┐
│                         CHUNK HEADER                             │
├──────────────────┬──────────────────────────────────────────────┤
│ transfer_id      │ 16 bytes - UUID identifying the transfer     │
├──────────────────┼──────────────────────────────────────────────┤
│ file_index       │  8 bytes - Index in batch (multi-file)       │
├──────────────────┼──────────────────────────────────────────────┤
│ chunk_index      │  8 bytes - Sequential chunk number           │
├──────────────────┼──────────────────────────────────────────────┤
│ chunk_offset     │  8 bytes - Byte offset in file               │
├──────────────────┼──────────────────────────────────────────────┤
│ original_size    │  4 bytes - Uncompressed data size            │
├──────────────────┼──────────────────────────────────────────────┤
│ compressed_size  │  4 bytes - Compressed data size              │
├──────────────────┼──────────────────────────────────────────────┤
│ checksum         │  4 bytes - CRC32 of data                     │
├──────────────────┼──────────────────────────────────────────────┤
│ flags            │  1 byte  - Chunk flags (first/last/compressed)│
├──────────────────┼──────────────────────────────────────────────┤
│ reserved         │  3 bytes - Future use                        │
├──────────────────┴──────────────────────────────────────────────┤
│                          DATA                                    │
│                  (compressed or raw bytes)                       │
└─────────────────────────────────────────────────────────────────┘
```

### Transfer State Machine

```
                                    ┌──────────────┐
                                    │   pending    │
                                    └──────┬───────┘
                                           │
                                           │ start()
                                           ▼
                                    ┌──────────────┐
                           ┌───────│ initializing │───────┐
                           │       └──────┬───────┘       │
                           │              │               │
                     error │              │ connected     │ error
                           │              ▼               │
                           │       ┌──────────────┐       │
                           │   ┌──▶│ transferring │──┐    │
                           │   │   └──────┬───────┘  │    │
                           │   │          │          │    │
                           │   │ resume   │ complete │    │
                           │   │          ▼          │    │
                           │   │   ┌──────────────┐  │    │
                           │   └───│  verifying   │  │    │
                           │       └──────┬───────┘  │    │
                           │              │          │    │
                           │              │ verified │    │
                           │              ▼          │    │
                           │       ┌──────────────┐  │    │
                           │       │  completed   │  │    │
                           │       └──────────────┘  │    │
                           │                         │    │
                           │    cancel()             │    │
                           ▼                         ▼    ▼
                    ┌──────────────┐          ┌──────────────┐
                    │   failed     │          │  cancelled   │
                    └──────────────┘          └──────────────┘
```

---

## Design Patterns

### 1. Builder Pattern

Used for configuring complex objects with many options.

```cpp
auto sender = file_sender::builder()
    .with_compression(compression_mode::adaptive)
    .with_chunk_size(256 * 1024)
    .with_bandwidth_limit(10 * 1024 * 1024)
    .with_transport(transport_type::tcp)
    .build();
```

### 2. Pipeline Pattern

Stages process data independently with queues for decoupling.

```cpp
// Each stage is a typed job in the thread pool
class compress_job : public typed_job_t<pipeline_stage::compression> {
    void execute() override {
        auto compressed = compressor_.compress(chunk_);
        output_queue_.push(std::move(compressed));
    }
};
```

### 3. Strategy Pattern

Pluggable transport implementations.

```cpp
class transport_interface {
    virtual auto send(span<const byte> data) -> Result<void> = 0;
    virtual auto receive(span<byte> buffer) -> Result<size_t> = 0;
};

class tcp_transport : public transport_interface { ... };
class quic_transport : public transport_interface { ... };
```

### 4. Observer Pattern

Callback-based event notification.

```cpp
sender->on_progress([](const transfer_progress& p) {
    // Handle progress update
});

receiver->on_complete([](const transfer_result& result) {
    // Handle completion
});
```

### 5. Factory Pattern

Creating transport instances.

```cpp
auto transport = transport_factory::create(transport_type::tcp);
```

---

## Threading Model

### Thread Pool Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                   typed_thread_pool<pipeline_stage>                          │
│                                                                              │
│  Stage: io_read                Stage: compression               Stage: network│
│  ┌─────────────┐               ┌─────────────┐                 ┌─────────────┐│
│  │  Worker 0   │               │  Worker 0   │                 │  Worker 0   ││
│  ├─────────────┤               ├─────────────┤                 ├─────────────┤│
│  │  Worker 1   │               │  Worker 1   │                 │  Worker 1   ││
│  └─────────────┘               ├─────────────┤                 └─────────────┘│
│                                │  Worker 2   │                                │
│                                ├─────────────┤                                │
│                                │  Worker 3   │                                │
│                                └─────────────┘                                │
│                                                                              │
│  Job Routing: Jobs are routed to workers based on their stage type          │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Concurrency Model

| Aspect | Implementation |
|--------|----------------|
| **Inter-stage Communication** | Bounded queues with backpressure |
| **Intra-stage Parallelism** | Multiple workers per stage |
| **Thread Safety** | Lock-free queues where possible |
| **Graceful Shutdown** | Drain queues before stopping |

---

## Memory Management

### Memory Bounds

Memory usage is deterministic and bounded:

```
max_memory = Σ (queue_size × chunk_size) for all queues

Default (256KB chunks):
  Sender:   (16 + 16 + 32 + 64) × 256KB = 32MB
  Receiver: (64 + 32 + 16 + 16) × 256KB = 32MB
  Total:    64MB maximum
```

### Buffer Pooling

Chunk buffers are pooled to reduce allocation overhead:

```cpp
class chunk_buffer_pool {
    // Pre-allocated buffer pool
    // Reuse buffers across chunks
    // Zero-copy where possible
};
```

### Backpressure

When downstream is slow, upstream blocks:

```
Fast reader, slow network:
  read_queue:     [████████████████] FULL ← Reader BLOCKS
  compress_queue: [████████████████████████████████] FULL
  send_queue:     [████████████████████████████████████████████████████████████████] FULL
                                                            ↑ Slow network
```

---

## Security Considerations

### Transport Security

- **TLS 1.3** encryption by default
- Certificate validation
- Perfect forward secrecy

### Data Integrity

- **CRC32** per-chunk integrity
- **SHA-256** per-file verification
- Automatic retransmission on corruption

### Path Safety

- Path traversal prevention
- Output directory validation
- No arbitrary file overwrites

---

*Last updated: 2025-12-11*
