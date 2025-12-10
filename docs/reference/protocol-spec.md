# Protocol Specification

Wire protocol specification for the **file_trans_system** library.

## Overview

The file_trans_system uses a lightweight custom protocol designed for high-performance file transfer. **HTTP is explicitly excluded** due to:
- High header overhead (~800 bytes/request)
- Stateless design conflicting with resume capability
- Unnecessary abstraction for streaming file transfer

### Protocol Overhead Comparison

| Protocol | Per-Chunk Overhead | Total (1GB, 256KB chunks) | Percentage |
|----------|-------------------|---------------------------|------------|
| HTTP/1.1 | ~800 bytes | ~3.2 MB | 0.31% |
| **Custom/TCP** | **54 bytes** | **~221 KB** | **0.02%** |
| Custom/QUIC | ~74 bytes | ~303 KB | 0.03% |

---

## Transport Layer

### Supported Transports

| Transport | Phase | Default | Description |
|-----------|-------|---------|-------------|
| TCP + TLS 1.3 | Phase 1 | Yes | All environments |
| QUIC | Phase 2 | No | High-loss networks, mobile |

### TLS Configuration

```
Minimum Version: TLS 1.3
Cipher Suites:
  - TLS_AES_256_GCM_SHA384
  - TLS_CHACHA20_POLY1305_SHA256
  - TLS_AES_128_GCM_SHA256
```

---

## Frame Format

### Message Frame Structure

```
┌────────────────────────────────────────────────────────────────┐
│                      Protocol Frame                             │
├────────────────────────────────────────────────────────────────┤
│ Message Type    │ 1 byte                                        │
├────────────────────────────────────────────────────────────────┤
│ Payload Length  │ 4 bytes (big-endian, unsigned)                │
├────────────────────────────────────────────────────────────────┤
│ Payload         │ Variable length (0 to 2^32-1 bytes)           │
└────────────────────────────────────────────────────────────────┘

Total frame overhead: 5 bytes
```

### Byte Layout

```
Offset  Size  Field
------  ----  -----
0       1     message_type
1       4     payload_length (big-endian)
5       N     payload
```

---

## Message Types

### Message Type Enumeration

```cpp
enum class message_type : uint8_t {
    // Session management (0x01-0x0F)
    handshake_request   = 0x01,
    handshake_response  = 0x02,

    // Transfer control (0x10-0x1F)
    transfer_request    = 0x10,
    transfer_accept     = 0x11,
    transfer_reject     = 0x12,
    transfer_cancel     = 0x13,

    // Data transfer (0x20-0x2F)
    chunk_data          = 0x20,
    chunk_ack           = 0x21,
    chunk_nack          = 0x22,  // Retransmission request

    // Resume (0x30-0x3F)
    resume_request      = 0x30,
    resume_response     = 0x31,

    // Completion (0x40-0x4F)
    transfer_complete   = 0x40,
    transfer_verify     = 0x41,

    // Control (0xF0-0xFF)
    keepalive           = 0xF0,
    error               = 0xFF
};
```

### Message Type Summary

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0x01 | HANDSHAKE_REQUEST | C→S | Initiate session |
| 0x02 | HANDSHAKE_RESPONSE | S→C | Session established |
| 0x10 | TRANSFER_REQUEST | C→S | Request file transfer |
| 0x11 | TRANSFER_ACCEPT | S→C | Accept transfer |
| 0x12 | TRANSFER_REJECT | S→C | Reject transfer |
| 0x13 | TRANSFER_CANCEL | C↔S | Cancel transfer |
| 0x20 | CHUNK_DATA | C→S | File chunk data |
| 0x21 | CHUNK_ACK | S→C | Chunk acknowledged |
| 0x22 | CHUNK_NACK | S→C | Request retransmission |
| 0x30 | RESUME_REQUEST | C→S | Request transfer resume |
| 0x31 | RESUME_RESPONSE | S→C | Resume state response |
| 0x40 | TRANSFER_COMPLETE | C→S | Transfer finished |
| 0x41 | TRANSFER_VERIFY | S→C | Verification result |
| 0xF0 | KEEPALIVE | C↔S | Connection keepalive |
| 0xFF | ERROR | C↔S | Error notification |

---

## Message Payloads

### HANDSHAKE_REQUEST (0x01)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ protocol_version     │ 2 bytes │ Protocol version (major.minor) │
│ capabilities         │ 4 bytes │ Client capabilities bitmap     │
│ client_id            │ 16 bytes│ Client UUID                    │
└─────────────────────────────────────────────────────────────────┘

Total: 22 bytes
```

**Capabilities Bitmap:**
```
Bit 0: Compression support (LZ4)
Bit 1: Resume support
Bit 2: Batch transfer support
Bit 3: QUIC support
Bit 4-31: Reserved
```

### HANDSHAKE_RESPONSE (0x02)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ protocol_version     │ 2 bytes │ Agreed protocol version        │
│ capabilities         │ 4 bytes │ Negotiated capabilities        │
│ session_id           │ 16 bytes│ Session UUID                   │
│ max_chunk_size       │ 4 bytes │ Maximum chunk size (bytes)     │
│ max_concurrent       │ 2 bytes │ Max concurrent transfers       │
└─────────────────────────────────────────────────────────────────┘

Total: 28 bytes
```

### TRANSFER_REQUEST (0x10)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ file_count           │ 2 bytes │ Number of files                │
│ compression_mode     │ 1 byte  │ Requested compression mode     │
│ compression_level    │ 1 byte  │ Compression level              │
│ options_flags        │ 4 bytes │ Transfer options bitmap        │
│ file_metadata[]      │ variable│ Array of file metadata         │
└─────────────────────────────────────────────────────────────────┘
```

**File Metadata Entry:**
```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ filename_length      │ 2 bytes │ Filename string length         │
│ filename             │ variable│ UTF-8 encoded filename         │
│ file_size            │ 8 bytes │ File size in bytes             │
│ sha256_hash          │ 32 bytes│ SHA-256 hash (binary)          │
│ permissions          │ 4 bytes │ File permissions               │
│ modified_time        │ 8 bytes │ Unix timestamp (microseconds)  │
│ compressible_hint    │ 1 byte  │ Compressibility hint           │
└─────────────────────────────────────────────────────────────────┘
```

### TRANSFER_ACCEPT (0x11)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ accepted_compression │ 1 byte  │ Agreed compression mode        │
│ chunk_size           │ 4 bytes │ Agreed chunk size              │
└─────────────────────────────────────────────────────────────────┘

Total: 21 bytes
```

### TRANSFER_REJECT (0x12)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ reason_code          │ 4 bytes │ Rejection reason code          │
│ message_length       │ 2 bytes │ Message string length          │
│ message              │ variable│ UTF-8 error message            │
└─────────────────────────────────────────────────────────────────┘
```

### CHUNK_DATA (0x20)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ file_index           │ 8 bytes │ File index in batch            │
│ chunk_index          │ 8 bytes │ Chunk sequence number          │
│ chunk_offset         │ 8 bytes │ Byte offset in file            │
│ original_size        │ 4 bytes │ Original (uncompressed) size   │
│ compressed_size      │ 4 bytes │ Compressed size                │
│ checksum             │ 4 bytes │ CRC32 of original data         │
│ flags                │ 1 byte  │ Chunk flags                    │
│ reserved             │ 3 bytes │ Padding for alignment          │
│ data                 │ variable│ Chunk data (compressed or raw) │
└─────────────────────────────────────────────────────────────────┘

Header: 56 bytes + data
```

**Chunk Flags:**
```
Bit 0: first_chunk   - First chunk of file
Bit 1: last_chunk    - Last chunk of file
Bit 2: compressed    - Data is LZ4 compressed
Bit 3: encrypted     - Reserved for TLS
Bit 4-7: Reserved
```

### CHUNK_ACK (0x21)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ file_index           │ 8 bytes │ File index                     │
│ chunk_index          │ 8 bytes │ Acknowledged chunk index       │
└─────────────────────────────────────────────────────────────────┘

Total: 32 bytes
```

### CHUNK_NACK (0x22)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ file_index           │ 8 bytes │ File index                     │
│ chunk_count          │ 4 bytes │ Number of missing chunks       │
│ chunk_indices[]      │ variable│ Array of missing chunk indices │
└─────────────────────────────────────────────────────────────────┘
```

### RESUME_REQUEST (0x30)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID to resume        │
│ bitmap_size          │ 4 bytes │ Chunk bitmap size in bytes     │
│ chunk_bitmap         │ variable│ Bitmap of received chunks      │
└─────────────────────────────────────────────────────────────────┘
```

### RESUME_RESPONSE (0x31)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ can_resume           │ 1 byte  │ Resume possible (0/1)          │
│ missing_count        │ 4 bytes │ Number of missing chunks       │
│ missing_indices[]    │ variable│ Array of missing chunk indices │
└─────────────────────────────────────────────────────────────────┘
```

### TRANSFER_COMPLETE (0x40)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ file_index           │ 8 bytes │ Completed file index           │
│ bytes_sent           │ 8 bytes │ Total raw bytes sent           │
│ bytes_on_wire        │ 8 bytes │ Total compressed bytes sent    │
└─────────────────────────────────────────────────────────────────┘

Total: 40 bytes
```

### TRANSFER_VERIFY (0x41)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ file_index           │ 8 bytes │ Verified file index            │
│ verified             │ 1 byte  │ SHA-256 verification result    │
│ received_hash        │ 32 bytes│ Computed SHA-256 hash          │
└─────────────────────────────────────────────────────────────────┘

Total: 57 bytes
```

### KEEPALIVE (0xF0)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ timestamp            │ 8 bytes │ Unix timestamp (microseconds)  │
└─────────────────────────────────────────────────────────────────┘

Total: 8 bytes
```

### ERROR (0xFF)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Related transfer (or zeros)    │
│ error_code           │ 4 bytes │ Error code (signed)            │
│ message_length       │ 2 bytes │ Message string length          │
│ message              │ variable│ UTF-8 error message            │
└─────────────────────────────────────────────────────────────────┘
```

---

## Protocol Flow

### Normal Transfer Sequence

```
    Sender                                    Receiver
      │                                           │
      │─────── HANDSHAKE_REQUEST ────────────────▶│
      │                                           │
      │◀─────── HANDSHAKE_RESPONSE ──────────────│
      │                                           │
      │─────── TRANSFER_REQUEST ─────────────────▶│
      │                                           │
      │◀─────── TRANSFER_ACCEPT ─────────────────│
      │                                           │
      │─────── CHUNK_DATA (chunk 0) ─────────────▶│
      │◀─────── CHUNK_ACK (chunk 0) ─────────────│
      │                                           │
      │─────── CHUNK_DATA (chunk 1) ─────────────▶│
      │◀─────── CHUNK_ACK (chunk 1) ─────────────│
      │                                           │
      │            ... more chunks ...            │
      │                                           │
      │─────── CHUNK_DATA (last, flags=0x02) ────▶│
      │◀─────── CHUNK_ACK ───────────────────────│
      │                                           │
      │─────── TRANSFER_COMPLETE ────────────────▶│
      │                                           │
      │◀─────── TRANSFER_VERIFY ─────────────────│
      │                                           │
```

### Resume Transfer Sequence

```
    Sender                                    Receiver
      │                                           │
      │─────── HANDSHAKE_REQUEST ────────────────▶│
      │◀─────── HANDSHAKE_RESPONSE ──────────────│
      │                                           │
      │─────── RESUME_REQUEST (with bitmap) ─────▶│
      │                                           │
      │◀─────── RESUME_RESPONSE (missing list) ──│
      │                                           │
      │─────── CHUNK_DATA (missing chunk 5) ─────▶│
      │◀─────── CHUNK_ACK ───────────────────────│
      │                                           │
      │─────── CHUNK_DATA (missing chunk 12) ────▶│
      │◀─────── CHUNK_ACK ───────────────────────│
      │                                           │
      │            ... remaining chunks ...       │
      │                                           │
      │─────── TRANSFER_COMPLETE ────────────────▶│
      │◀─────── TRANSFER_VERIFY ─────────────────│
      │                                           │
```

### Error Handling Sequence

```
    Sender                                    Receiver
      │                                           │
      │─────── CHUNK_DATA (corrupted) ───────────▶│
      │                                           │  CRC32 fails
      │◀─────── CHUNK_NACK (chunk 5) ────────────│
      │                                           │
      │─────── CHUNK_DATA (chunk 5, retry) ──────▶│
      │◀─────── CHUNK_ACK (chunk 5) ─────────────│
      │                                           │
```

---

## State Machine

### Connection States

```
                    ┌─────────────────┐
                    │   DISCONNECTED  │
                    └────────┬────────┘
                             │ connect()
                             ▼
                    ┌─────────────────┐
                    │   CONNECTING    │
                    └────────┬────────┘
                             │ handshake complete
                             ▼
                    ┌─────────────────┐
          ┌────────│    CONNECTED    │────────┐
          │        └────────┬────────┘        │
          │                 │                 │
          │ error           │ transfer_req    │ cancel
          │                 ▼                 │
          │        ┌─────────────────┐        │
          │        │  TRANSFERRING   │        │
          │        └────────┬────────┘        │
          │                 │ complete        │
          │                 ▼                 │
          │        ┌─────────────────┐        │
          │        │    VERIFYING    │        │
          │        └────────┬────────┘        │
          │                 │                 │
          ▼                 ▼                 ▼
          ├─────────────────┴─────────────────┤
          │                                   │
          ▼                                   │
┌─────────────────┐                           │
│     FAILED      │◀──────────────────────────┘
└────────┬────────┘
         │ disconnect()
         ▼
┌─────────────────┐
│  DISCONNECTED   │
└─────────────────┘
```

---

## Versioning

### Protocol Version Format

```
Major: 1 byte (0-255)
Minor: 1 byte (0-255)

Current version: 1.0
```

### Version Compatibility

| Client | Server | Result |
|--------|--------|--------|
| 1.0 | 1.0 | Compatible |
| 1.0 | 1.1 | Compatible (server downgrades) |
| 1.1 | 1.0 | Compatible (client downgrades) |
| 2.0 | 1.x | Incompatible (major version mismatch) |

---

## Security Considerations

### TLS Requirements

- **Minimum Version:** TLS 1.3
- **Certificate Verification:** Required in production
- **Forward Secrecy:** Enabled by default

### Path Traversal Prevention

Filenames in TRANSFER_REQUEST are validated:
- No `..` components
- No absolute paths
- UTF-8 sanitization

### Resource Limits

| Limit | Default | Configurable |
|-------|---------|--------------|
| Max chunk size | 1 MB | Yes |
| Max file size | 100 GB | Yes |
| Max files per batch | 10,000 | Yes |
| Max concurrent transfers | 100 | Yes |

---

*Last updated: 2025-12-11*
