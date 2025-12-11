# Sequence Diagrams

This document provides detailed sequence diagrams for the key operations in **file_trans_system**.

## Table of Contents

1. [File Transfer Flow](#file-transfer-flow)
2. [Protocol Handshake](#protocol-handshake)
3. [Chunk Transfer](#chunk-transfer)
4. [Transfer Resume](#transfer-resume)
5. [Error Handling](#error-handling)
6. [Pipeline Processing](#pipeline-processing)
7. [Compression Flow](#compression-flow)

---

## File Transfer Flow

### Complete Single File Transfer

```
┌────────┐          ┌────────────┐          ┌────────────┐          ┌────────┐
│ Client │          │   Sender   │          │  Receiver  │          │ Server │
│  App   │          │  Pipeline  │          │  Pipeline  │          │  App   │
└───┬────┘          └─────┬──────┘          └─────┬──────┘          └───┬────┘
    │                     │                       │                     │
    │  send_file(path)    │                       │                     │
    │────────────────────▶│                       │                     │
    │                     │                       │                     │
    │                     │    HANDSHAKE_REQ      │                     │
    │                     │──────────────────────▶│                     │
    │                     │                       │                     │
    │                     │    HANDSHAKE_RESP     │  on_transfer_request()
    │                     │◀──────────────────────│─────────────────────▶│
    │                     │                       │                     │
    │                     │                       │   return true       │
    │                     │                       │◀────────────────────│
    │                     │                       │                     │
    │                     │   TRANSFER_REQUEST    │                     │
    │                     │──────────────────────▶│                     │
    │                     │                       │                     │
    │                     │   TRANSFER_ACCEPT     │                     │
    │                     │◀──────────────────────│                     │
    │                     │                       │                     │
    │                     │                       │                     │
    │                     │    CHUNK_DATA [0]     │                     │
    │                     │──────────────────────▶│                     │
    │  on_progress(10%)   │                       │                     │
    │◀────────────────────│    CHUNK_ACK [0]      │                     │
    │                     │◀──────────────────────│  on_progress(10%)   │
    │                     │                       │─────────────────────▶│
    │                     │                       │                     │
    │                     │    CHUNK_DATA [1..N]  │                     │
    │                     │ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ▶│                     │
    │                     │                       │                     │
    │  on_progress(100%)  │   CHUNK_ACK [1..N]    │                     │
    │◀────────────────────│◀─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─│                     │
    │                     │                       │                     │
    │                     │  TRANSFER_COMPLETE    │                     │
    │                     │──────────────────────▶│                     │
    │                     │                       │                     │
    │                     │                       │ verify SHA-256      │
    │                     │                       │───────────┐         │
    │                     │                       │           │         │
    │                     │                       │◀──────────┘         │
    │                     │                       │                     │
    │                     │   TRANSFER_VERIFY     │                     │
    │                     │   (verified=true)     │   on_complete()     │
    │  Result<handle>     │◀──────────────────────│────────────────────▶│
    │◀────────────────────│                       │                     │
    │                     │                       │                     │
    ▼                     ▼                       ▼                     ▼
```

### Multi-File Batch Transfer

```
┌────────┐          ┌────────────┐          ┌────────────┐
│ Sender │          │  Transport │          │  Receiver  │
└───┬────┘          └─────┬──────┘          └─────┬──────┘
    │                     │                       │
    │  send_files([f1, f2, f3])                   │
    │────────────────────▶│                       │
    │                     │                       │
    │                     │   TRANSFER_REQUEST    │
    │                     │   (file_count=3)      │
    │                     │──────────────────────▶│
    │                     │                       │
    │                     │   TRANSFER_ACCEPT     │
    │                     │◀──────────────────────│
    │                     │                       │
    │                     │                       │
    │  ╔════════════════════════════════════════════════╗
    │  ║  File 1 Transfer                               ║
    │  ╠════════════════════════════════════════════════╣
    │  ║                  │                       │     ║
    │  ║                  │  CHUNK_DATA           │     ║
    │  ║                  │  (file_idx=0, chunk=*)│     ║
    │  ║                  │──────────────────────▶│     ║
    │  ║                  │                       │     ║
    │  ║                  │  CHUNK_ACK            │     ║
    │  ║                  │◀──────────────────────│     ║
    │  ╚════════════════════════════════════════════════╝
    │                     │                       │
    │  ╔════════════════════════════════════════════════╗
    │  ║  File 2 Transfer                               ║
    │  ╠════════════════════════════════════════════════╣
    │  ║                  │                       │     ║
    │  ║                  │  CHUNK_DATA           │     ║
    │  ║                  │  (file_idx=1, chunk=*)│     ║
    │  ║                  │──────────────────────▶│     ║
    │  ║                  │                       │     ║
    │  ║                  │  CHUNK_ACK            │     ║
    │  ║                  │◀──────────────────────│     ║
    │  ╚════════════════════════════════════════════════╝
    │                     │                       │
    │  ╔════════════════════════════════════════════════╗
    │  ║  File 3 Transfer                               ║
    │  ╠════════════════════════════════════════════════╣
    │  ║                  │                       │     ║
    │  ║                  │  CHUNK_DATA           │     ║
    │  ║                  │  (file_idx=2, chunk=*)│     ║
    │  ║                  │──────────────────────▶│     ║
    │  ║                  │                       │     ║
    │  ║                  │  CHUNK_ACK            │     ║
    │  ║                  │◀──────────────────────│     ║
    │  ╚════════════════════════════════════════════════╝
    │                     │                       │
    │                     │  TRANSFER_COMPLETE    │
    │                     │──────────────────────▶│
    │                     │                       │
    │                     │  TRANSFER_VERIFY      │
    │                     │  (all_files_verified) │
    │                     │◀──────────────────────│
    │                     │                       │
    ▼                     ▼                       ▼
```

---

## Protocol Handshake

### Successful Handshake

```
┌────────┐                                    ┌────────┐
│ Sender │                                    │Receiver│
└───┬────┘                                    └───┬────┘
    │                                             │
    │  HANDSHAKE_REQUEST                          │
    │  ┌────────────────────────────────────┐     │
    │  │ protocol_version: 0x0100           │     │
    │  │ capabilities: 0x0000000F           │     │
    │  │ client_id: <16 bytes UUID>         │     │
    │  └────────────────────────────────────┘     │
    │────────────────────────────────────────────▶│
    │                                             │
    │                                             │ Validate protocol
    │                                             │ version & capabilities
    │                                             │
    │  HANDSHAKE_RESPONSE                         │
    │  ┌────────────────────────────────────┐     │
    │  │ protocol_version: 0x0100           │     │
    │  │ capabilities: 0x0000000F           │     │
    │  │ session_id: <16 bytes UUID>        │     │
    │  │ max_chunk_size: 1048576            │     │
    │  │ max_concurrent: 100                │     │
    │  └────────────────────────────────────┘     │
    │◀────────────────────────────────────────────│
    │                                             │
    │  Session established                        │
    │                                             │
    ▼                                             ▼
```

### Capability Flags

```
Capabilities (32-bit):
┌────────────────────────────────────────────────────────────────┐
│ Bit 0: COMPRESSION_LZ4        - LZ4 compression support        │
│ Bit 1: COMPRESSION_LZ4_HC     - LZ4-HC compression support     │
│ Bit 2: RESUME                 - Transfer resume support        │
│ Bit 3: BATCH                  - Multi-file batch support       │
│ Bit 4-7: Reserved             - Future compression algorithms  │
│ Bit 8-15: Reserved            - Future features               │
│ Bit 16-23: Reserved           - Protocol extensions           │
│ Bit 24-31: Vendor-specific    - Custom implementations        │
└────────────────────────────────────────────────────────────────┘
```

---

## Chunk Transfer

### Single Chunk Lifecycle

```
┌────────┐    ┌────────────┐    ┌────────────┐    ┌────────────┐    ┌────────┐
│  File  │    │  io_read   │    │compression │    │  network   │    │Receiver│
│  Disk  │    │   stage    │    │   stage    │    │   stage    │    │        │
└───┬────┘    └─────┬──────┘    └─────┬──────┘    └─────┬──────┘    └───┬────┘
    │               │                 │                 │               │
    │  read(offset) │                 │                 │               │
    │◀──────────────│                 │                 │               │
    │               │                 │                 │               │
    │  data[256KB]  │                 │                 │               │
    │──────────────▶│                 │                 │               │
    │               │                 │                 │               │
    │               │  create chunk   │                 │               │
    │               │  with header    │                 │               │
    │               │─────┐           │                 │               │
    │               │     │           │                 │               │
    │               │◀────┘           │                 │               │
    │               │                 │                 │               │
    │               │  push to queue  │                 │               │
    │               │────────────────▶│                 │               │
    │               │                 │                 │               │
    │               │                 │  adaptive check │               │
    │               │                 │  is_compressible?               │
    │               │                 │─────┐           │               │
    │               │                 │     │           │               │
    │               │                 │◀────┘           │               │
    │               │                 │                 │               │
    │               │                 │  LZ4 compress   │               │
    │               │                 │─────┐           │               │
    │               │                 │     │           │               │
    │               │                 │◀────┘           │               │
    │               │                 │                 │               │
    │               │                 │  set compressed │               │
    │               │                 │  flag           │               │
    │               │                 │────────────────▶│               │
    │               │                 │                 │               │
    │               │                 │                 │  serialize    │
    │               │                 │                 │  chunk        │
    │               │                 │                 │─────┐         │
    │               │                 │                 │     │         │
    │               │                 │                 │◀────┘         │
    │               │                 │                 │               │
    │               │                 │                 │  CHUNK_DATA   │
    │               │                 │                 │──────────────▶│
    │               │                 │                 │               │
    │               │                 │                 │  CHUNK_ACK    │
    │               │                 │                 │◀──────────────│
    │               │                 │                 │               │
    ▼               ▼                 ▼                 ▼               ▼
```

### Chunk Retransmission (CRC32 Failure)

```
┌────────┐                                    ┌────────┐
│ Sender │                                    │Receiver│
└───┬────┘                                    └───┬────┘
    │                                             │
    │  CHUNK_DATA [chunk_idx=42]                  │
    │  ┌─────────────────────────────────┐        │
    │  │ data + checksum (CRC32)         │        │
    │  └─────────────────────────────────┘        │
    │────────────────────────────────────────────▶│
    │                                             │
    │                                             │ verify CRC32
    │                                             │─────┐
    │                                             │     │ MISMATCH!
    │                                             │◀────┘
    │                                             │
    │  CHUNK_NACK [chunk_idx=42]                  │
    │  ┌─────────────────────────────────┐        │
    │  │ error_code: -720                │        │
    │  │ chunk_checksum_error            │        │
    │  └─────────────────────────────────┘        │
    │◀────────────────────────────────────────────│
    │                                             │
    │  Re-read and resend                         │
    │                                             │
    │  CHUNK_DATA [chunk_idx=42]                  │
    │  ┌─────────────────────────────────┐        │
    │  │ data + checksum (re-computed)   │        │
    │  └─────────────────────────────────┘        │
    │────────────────────────────────────────────▶│
    │                                             │
    │                                             │ verify CRC32
    │                                             │─────┐
    │                                             │     │ OK
    │                                             │◀────┘
    │                                             │
    │  CHUNK_ACK [chunk_idx=42]                   │
    │◀────────────────────────────────────────────│
    │                                             │
    ▼                                             ▼
```

---

## Transfer Resume

### Resume After Disconnection

```
┌────────┐          ┌────────────┐          ┌────────┐
│ Sender │          │  (Network) │          │Receiver│
└───┬────┘          └─────┬──────┘          └───┬────┘
    │                     │                     │
    │  Initial transfer started                 │
    │                     │                     │
    │  CHUNK_DATA [0-99]  │                     │
    │────────────────────▶│────────────────────▶│
    │                     │                     │
    │  CHUNK_ACK [0-99]   │                     │
    │◀────────────────────│◀────────────────────│
    │                     │                     │
    │                     │                     │
    │             ╔═══════════════════╗         │
    │             ║  DISCONNECTION    ║         │
    │             ╚═══════════════════╝         │
    │                     │                     │
    │                     ╳                     │ Save checkpoint
    │                     │                     │ (received chunks
    │                     │                     │  bitmap)
    │                     │                     │
    │  ═══ TIME PASSES ═══                      │
    │                     │                     │
    │  Reconnect          │                     │
    │─────────────────────────────────────────▶ │
    │                     │                     │
    │  HANDSHAKE_REQUEST  │                     │
    │────────────────────▶│────────────────────▶│
    │                     │                     │
    │  HANDSHAKE_RESPONSE │                     │
    │◀────────────────────│◀────────────────────│
    │                     │                     │
    │  RESUME_REQUEST                           │
    │  ┌─────────────────────────────────┐      │
    │  │ transfer_id: <original>         │      │
    │  │ file_hash: <sha256>             │      │
    │  └─────────────────────────────────┘      │
    │────────────────────▶│────────────────────▶│
    │                     │                     │
    │                     │                     │ Check saved state
    │                     │                     │─────┐
    │                     │                     │     │
    │                     │                     │◀────┘
    │                     │                     │
    │  RESUME_RESPONSE                          │
    │  ┌─────────────────────────────────┐      │
    │  │ status: RESUMABLE               │      │
    │  │ received_chunks: [0-99]         │      │
    │  │ missing_chunks: [100-4095]      │      │
    │  │ total_chunks: 4096              │      │
    │  └─────────────────────────────────┘      │
    │◀────────────────────│◀────────────────────│
    │                     │                     │
    │  Continue from chunk 100                  │
    │                     │                     │
    │  CHUNK_DATA [100-4095]                    │
    │────────────────────▶│────────────────────▶│
    │                     │                     │
    │  CHUNK_ACK [100-4095]                     │
    │◀────────────────────│◀────────────────────│
    │                     │                     │
    │  TRANSFER_COMPLETE  │                     │
    │────────────────────▶│────────────────────▶│
    │                     │                     │
    │  TRANSFER_VERIFY    │                     │
    │◀────────────────────│◀────────────────────│
    │                     │                     │
    ▼                     ▼                     ▼
```

### Resume States

```
RESUME_RESPONSE status values:

┌───────────────────────────────────────────────────────────────────────┐
│  RESUMABLE          │  Transfer can be resumed from checkpoint        │
├─────────────────────┼─────────────────────────────────────────────────┤
│  NOT_FOUND          │  Transfer ID not found on receiver              │
├─────────────────────┼─────────────────────────────────────────────────┤
│  FILE_CHANGED       │  Source file hash doesn't match                 │
├─────────────────────┼─────────────────────────────────────────────────┤
│  STATE_CORRUPTED    │  Checkpoint data is corrupted                   │
├─────────────────────┼─────────────────────────────────────────────────┤
│  EXPIRED            │  Checkpoint expired (configurable timeout)      │
└───────────────────────────────────────────────────────────────────────┘
```

---

## Error Handling

### Connection Error

```
┌────────┐          ┌────────────┐          ┌────────┐
│ Client │          │   Sender   │          │ Server │
└───┬────┘          └─────┬──────┘          └───┬────┘
    │                     │                     │
    │  send_file()        │                     │
    │────────────────────▶│                     │
    │                     │                     │
    │                     │  connect()          │
    │                     │─────────────────────╳ (Connection refused)
    │                     │                     │
    │                     │  Retry with backoff │
    │                     │  (1s, 2s, 4s...)    │
    │                     │─────────────────────╳
    │                     │                     │
    │                     │─────────────────────╳
    │                     │                     │
    │  Result<error>      │                     │
    │  (-700: transfer_init_failed)             │
    │◀────────────────────│                     │
    │                     │                     │
    ▼                     ▼                     ▼
```

### Timeout Error

```
┌────────┐          ┌────────────┐          ┌────────┐
│ Sender │          │  Transport │          │Receiver│
└───┬────┘          └─────┬──────┘          └───┬────┘
    │                     │                     │
    │  CHUNK_DATA         │                     │
    │────────────────────▶│────────────────────▶│
    │                     │                     │
    │                     │                     │ Processing...
    │                     │                     │ (very slow)
    │                     │                     │
    │  ┌─────────────────────────────────────┐  │
    │  │  TIMEOUT (30s default)              │  │
    │  └─────────────────────────────────────┘  │
    │                     │                     │
    │  Timeout! Retry...  │                     │
    │────────────────────▶│────────────────────▶│
    │                     │                     │
    │  CHUNK_ACK          │                     │
    │◀────────────────────│◀────────────────────│
    │                     │                     │
    ▼                     ▼                     ▼
```

### Transfer Rejection

```
┌────────┐          ┌────────────┐          ┌────────┐          ┌────────┐
│ Client │          │   Sender   │          │Receiver│          │ Server │
└───┬────┘          └─────┬──────┘          └───┬────┘          └───┬────┘
    │                     │                     │                   │
    │  send_file(10GB)    │                     │                   │
    │────────────────────▶│                     │                   │
    │                     │                     │                   │
    │                     │  TRANSFER_REQUEST   │                   │
    │                     │  (size=10GB)        │                   │
    │                     │────────────────────▶│                   │
    │                     │                     │                   │
    │                     │                     │on_transfer_request│
    │                     │                     │──────────────────▶│
    │                     │                     │                   │
    │                     │                     │  return false     │
    │                     │                     │  (file too large) │
    │                     │                     │◀──────────────────│
    │                     │                     │                   │
    │                     │  TRANSFER_REJECT    │                   │
    │                     │  ┌──────────────────────────┐           │
    │                     │  │ error: -703              │           │
    │                     │  │ transfer_rejected        │           │
    │                     │  │ reason: "size_exceeded"  │           │
    │                     │  └──────────────────────────┘           │
    │                     │◀────────────────────│                   │
    │                     │                     │                   │
    │  Result<error>      │                     │                   │
    │  (-703: transfer_   │                     │                   │
    │   rejected)         │                     │                   │
    │◀────────────────────│                     │                   │
    │                     │                     │                   │
    ▼                     ▼                     ▼                   ▼
```

---

## Pipeline Processing

### Parallel Stage Execution

```
Time ───────────────────────────────────────────────────────────────────▶

io_read    │▓▓▓▓│    │▓▓▓▓│    │▓▓▓▓│    │▓▓▓▓│    │▓▓▓▓│    │▓▓▓▓│
stage      │ C0 │    │ C1 │    │ C2 │    │ C3 │    │ C4 │    │ C5 │
           └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘

chunk_     │    │▓▓▓▓│    │▓▓▓▓│    │▓▓▓▓│    │▓▓▓▓│    │▓▓▓▓│    │
process    │    │ C0 │    │ C1 │    │ C2 │    │ C3 │    │ C4 │    │
           └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘

compress   │    │    │▓▓▓▓▓▓▓▓│    │▓▓▓▓▓▓▓▓│    │▓▓▓▓▓▓▓▓│    │    │
stage      │    │    │   C0   │    │   C1   │    │   C2   │    │    │
           └────┴────┴────────┴────┴────────┴────┴────────┴────┴────┘

network    │    │    │    │    │▓▓▓▓▓▓▓▓▓▓▓▓│    │▓▓▓▓▓▓▓▓▓▓▓▓│    │
stage      │    │    │    │    │     C0     │    │     C1     │    │
           └────┴────┴────┴────┴────────────┴────┴────────────┴────┘

Legend:
  ▓▓▓▓ = Processing chunk
  C0, C1, C2... = Chunk index

Key insight: While network is sending C0, compression is processing C2,
and io_read is reading C5. All stages work in parallel!
```

### Backpressure in Action

```
Scenario: Network is slow, causing backpressure

Queue States Over Time:

T=0  read_queue:    [        ]     Empty
     compress_queue:[        ]     Empty
     send_queue:    [        ]     Empty

T=1  read_queue:    [C0      ]     1 chunk
     compress_queue:[        ]
     send_queue:    [        ]

T=2  read_queue:    [C0 C1   ]     2 chunks
     compress_queue:[C0      ]
     send_queue:    [        ]

T=3  read_queue:    [C1 C2   ]
     compress_queue:[C0 C1   ]
     send_queue:    [C0      ]     Network starts sending

T=10 read_queue:    [████████████████]  FULL (16)
     compress_queue:[████████████████████████████████]  FULL (32)
     send_queue:    [████████████████████████████████████████████████████████████████]  FULL (64)
                                        ↑ Network bottleneck

T=11 io_read stage BLOCKS waiting for queue space
     Memory usage stays bounded at ~32MB
     No runaway memory consumption!

T=12 Network sends one chunk
     send_queue:    [███████████████████████████████████████████████████████████████ ]
                                                                               ↑ Space freed

     Compression stage can now push
     io_read stage unblocks
```

---

## Compression Flow

### Adaptive Compression Decision

```
┌────────┐          ┌────────────────┐          ┌────────────┐
│ Chunk  │          │   Adaptive     │          │ LZ4 Engine │
│ Data   │          │  Compression   │          │            │
└───┬────┘          └───────┬────────┘          └─────┬──────┘
    │                       │                         │
    │  chunk data (256KB)   │                         │
    │──────────────────────▶│                         │
    │                       │                         │
    │                       │  Sample first 4KB      │
    │                       │─────┐                   │
    │                       │     │                   │
    │                       │◀────┘                   │
    │                       │                         │
    │                       │  Compress sample        │
    │                       │────────────────────────▶│
    │                       │                         │
    │                       │  compressed_size        │
    │                       │◀────────────────────────│
    │                       │                         │
    │                       │  Calculate ratio        │
    │                       │  ratio = 4KB / compressed_size
    │                       │─────┐                   │
    │                       │     │                   │
    │                       │◀────┘                   │
    │                       │                         │
    │  ┌─────────────────────────────────────────────────────────┐
    │  │  if (ratio > 0.9) → SKIP compression (incompressible)   │
    │  │  else → COMPRESS full chunk                             │
    │  └─────────────────────────────────────────────────────────┘
    │                       │                         │
    │  [If compressible]    │                         │
    │                       │  Compress full chunk    │
    │                       │────────────────────────▶│
    │                       │                         │
    │                       │  compressed_data        │
    │                       │◀────────────────────────│
    │                       │                         │
    │  compressed chunk     │                         │
    │  (flags: compressed)  │                         │
    │◀──────────────────────│                         │
    │                       │                         │
    │  [If incompressible]  │                         │
    │  original chunk       │                         │
    │  (flags: none)        │                         │
    │◀──────────────────────│                         │
    │                       │                         │
    ▼                       ▼                         ▼
```

### Compression Statistics Collection

```
┌───────────────────────────────────────────────────────────────────────┐
│                    compression_statistics                              │
├───────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  Per-chunk update:                                                     │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │  total_raw_bytes       += original_size                         │  │
│  │  total_compressed_bytes += compressed_size                      │  │
│  │  compression_time_us   += elapsed_time                          │  │
│  │  if (compressed) chunks_compressed++                            │  │
│  │  else chunks_skipped++                                          │  │
│  └─────────────────────────────────────────────────────────────────┘  │
│                                                                        │
│  Computed metrics:                                                     │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │  compression_ratio() = raw_bytes / compressed_bytes             │  │
│  │  compression_speed() = raw_bytes / compression_time_us × 1e6    │  │
│  │  skip_rate() = chunks_skipped / (chunks_compressed + skipped)   │  │
│  └─────────────────────────────────────────────────────────────────┘  │
│                                                                        │
└───────────────────────────────────────────────────────────────────────┘
```

---

## Keepalive and Connection Management

### Keepalive Mechanism

```
┌────────┐                                    ┌────────┐
│ Sender │                                    │Receiver│
└───┬────┘                                    └───┬────┘
    │                                             │
    │  Active transfer in progress                │
    │                                             │
    │  ═══ No data for 15 seconds ═══             │
    │                                             │
    │  KEEPALIVE                                  │
    │────────────────────────────────────────────▶│
    │                                             │
    │  KEEPALIVE                                  │
    │◀────────────────────────────────────────────│
    │                                             │
    │  Connection confirmed alive                 │
    │                                             │
    │  ═══ No data for 15 seconds ═══             │
    │                                             │
    │  KEEPALIVE                                  │
    │────────────────────────────────────────────▶│
    │                                             │
    │  [No response within 30s]                   │
    │                                             │
    │  Connection timeout                         │
    │  Initiate reconnection                      │
    │                                             │
    ▼                                             ▼
```

---

*Last updated: 2025-12-11*
