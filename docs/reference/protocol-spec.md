# Protocol Specification

Wire protocol specification for the **file_trans_system** library.

**Version**: 0.2.0
**Last Updated**: 2025-12-11

## Overview

The file_trans_system uses a lightweight custom protocol designed for high-performance **bidirectional file transfer** between clients and a central server. **HTTP is explicitly excluded** due to:
- High header overhead (~800 bytes/request)
- Stateless design conflicting with resume capability
- Unnecessary abstraction for streaming file transfer

### Protocol Features

- **Bidirectional Transfer**: Upload (client→server) and Download (server→client)
- **File Listing**: Query available files on server
- **Connection Management**: Session establishment, heartbeat, auto-reconnect
- **Resume Support**: Checkpoint-based interrupted transfer recovery
- **Compression**: Adaptive LZ4 compression

### Protocol Overhead Comparison

| Protocol | Per-Chunk Overhead | Total (1GB, 256KB chunks) | Percentage |
|----------|-------------------|---------------------------|------------|
| HTTP/1.1 | ~800 bytes | ~3.2 MB | 0.31% |
| **Custom/TCP** | **61 bytes** | **~249 KB** | **0.02%** |
| Custom/QUIC | ~81 bytes | ~331 KB | 0.03% |

> **Note**: Custom protocol overhead includes 13-byte frame header (prefix 4B + type 1B + length 4B + postfix 4B) plus 48-byte CHUNK_DATA payload header.

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
│                      Protocol Frame (v0.2)                      │
├────────────────────────────────────────────────────────────────┤
│ PREFIX (Magic)  │ 4 bytes: 0x46545331 ("FTS1")                  │
├────────────────────────────────────────────────────────────────┤
│ Message Type    │ 1 byte                                        │
├────────────────────────────────────────────────────────────────┤
│ Payload Length  │ 4 bytes (big-endian, unsigned)                │
├────────────────────────────────────────────────────────────────┤
│ Payload         │ Variable length (0 to 2^32-1 bytes)           │
├────────────────────────────────────────────────────────────────┤
│ POSTFIX         │ 4 bytes: Checksum (2B) + Length Echo (2B)     │
└────────────────────────────────────────────────────────────────┘

Total frame overhead: 13 bytes
```

### Byte Layout

```
Offset  Size  Field
------  ----  -----
0       4     prefix (magic number: 0x46545331 = "FTS1")
4       1     message_type
5       4     payload_length (big-endian)
9       N     payload
9+N     2     checksum (sum of bytes [0..9+N-1] mod 65536, big-endian)
11+N    2     length_echo (lower 16 bits of payload_length, big-endian)
```

### Prefix (Magic Number)

```
Value: 0x46545331
ASCII: "FTS1" (File Transfer System v1)

Purpose:
- Frame synchronization in byte streams
- Protocol identification
- Quick rejection of non-protocol data
- Stream recovery after corruption
```

### Postfix (Integrity Check)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field          │ Size    │ Description                          │
├─────────────────────────────────────────────────────────────────┤
│ checksum       │ 2 bytes │ Sum of all preceding bytes mod 65536 │
│ length_echo    │ 2 bytes │ Payload length (lower 16 bits)       │
└─────────────────────────────────────────────────────────────────┘

Checksum Calculation:
  uint16_t checksum = 0;
  for (size_t i = 0; i < 9 + payload_length; i++) {
      checksum += frame_bytes[i];
  }
  // Store as big-endian

Length Echo Purpose:
- Double-verification of payload length
- Detects length field corruption
- Enables frame boundary validation
```

### Frame Validation

Receivers MUST validate frames in this order:

1. **Prefix Check**: First 4 bytes == 0x46545331
2. **Length Sanity**: payload_length <= MAX_PAYLOAD_SIZE (default: 1MB + 48B header)
3. **Read Payload**: Read exactly payload_length bytes
4. **Checksum Verify**: Calculated checksum == stored checksum
5. **Length Echo Verify**: (payload_length & 0xFFFF) == length_echo

If any check fails, discard frame and scan for next valid prefix.

---

## Message Types

### Message Type Enumeration

```cpp
enum class message_type : uint8_t {
    // Session management (0x01-0x0F)
    connect             = 0x01,
    connect_ack         = 0x02,
    disconnect          = 0x03,
    heartbeat           = 0x04,
    heartbeat_ack       = 0x05,

    // Upload control (0x10-0x1F)
    upload_request      = 0x10,
    upload_accept       = 0x11,
    upload_reject       = 0x12,
    upload_complete     = 0x13,
    upload_ack          = 0x14,

    // Data transfer (0x20-0x2F)
    chunk_data          = 0x20,
    chunk_ack           = 0x21,
    chunk_nack          = 0x22,  // Retransmission request

    // Resume (0x30-0x3F)
    resume_request      = 0x30,
    resume_response     = 0x31,

    // Transfer control (0x40-0x4F)
    transfer_cancel     = 0x40,
    transfer_pause      = 0x41,
    transfer_resume     = 0x42,
    transfer_verify     = 0x43,

    // Download control (0x50-0x5F)
    download_request    = 0x50,
    download_accept     = 0x51,
    download_reject     = 0x52,
    download_complete   = 0x53,
    download_ack        = 0x54,

    // File listing (0x60-0x6F)
    list_request        = 0x60,
    list_response       = 0x61,

    // Control (0xF0-0xFF)
    error               = 0xFF
};
```

### Message Type Summary

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| **Session Management** |
| 0x01 | CONNECT | C→S | Client connection request |
| 0x02 | CONNECT_ACK | S→C | Server acknowledges connection |
| 0x03 | DISCONNECT | C↔S | Graceful disconnect |
| 0x04 | HEARTBEAT | C→S | Keep-alive ping |
| 0x05 | HEARTBEAT_ACK | S→C | Keep-alive pong |
| **Upload (Client → Server)** |
| 0x10 | UPLOAD_REQUEST | C→S | Request to upload a file |
| 0x11 | UPLOAD_ACCEPT | S→C | Upload approved |
| 0x12 | UPLOAD_REJECT | S→C | Upload denied + reason |
| 0x13 | UPLOAD_COMPLETE | C→S | All chunks sent |
| 0x14 | UPLOAD_ACK | S→C | File fully received + verified |
| **Data Transfer** |
| 0x20 | CHUNK_DATA | C↔S | Chunk payload (both directions) |
| 0x21 | CHUNK_ACK | C↔S | Chunk acknowledged |
| 0x22 | CHUNK_NACK | C↔S | Request retransmission |
| **Resume** |
| 0x30 | RESUME_REQUEST | C→S | Request transfer resume |
| 0x31 | RESUME_RESPONSE | S→C | Resume state response |
| **Transfer Control** |
| 0x40 | TRANSFER_CANCEL | C↔S | Cancel transfer |
| 0x41 | TRANSFER_PAUSE | C→S | Pause transfer |
| 0x42 | TRANSFER_RESUME | C→S | Resume paused transfer |
| 0x43 | TRANSFER_VERIFY | S→C | Verification result |
| **Download (Server → Client)** |
| 0x50 | DOWNLOAD_REQUEST | C→S | Request to download a file |
| 0x51 | DOWNLOAD_ACCEPT | S→C | Download approved + metadata |
| 0x52 | DOWNLOAD_REJECT | S→C | Download denied + reason |
| 0x53 | DOWNLOAD_COMPLETE | S→C | All chunks sent |
| 0x54 | DOWNLOAD_ACK | C→S | File fully received + verified |
| **File Listing** |
| 0x60 | LIST_REQUEST | C→S | Request file listing |
| 0x61 | LIST_RESPONSE | S→C | File list + metadata |
| **Control** |
| 0xFF | ERROR | C↔S | Error notification |

---

## Message Payloads

### CONNECT (0x01)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ protocol_version     │ 2 bytes │ Protocol version (major.minor) │
│ capabilities         │ 4 bytes │ Client capabilities bitmap     │
│ client_id            │ 16 bytes│ Client UUID (optional, or 0)   │
└─────────────────────────────────────────────────────────────────┘

Total: 22 bytes
```

**Capabilities Bitmap:**
```
Bit 0: Compression support (LZ4)
Bit 1: Resume support
Bit 2: Batch transfer support (upload)
Bit 3: QUIC support (Phase 2)
Bit 4: Auto-reconnect enabled
Bit 5-31: Reserved
```

### CONNECT_ACK (0x02)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ protocol_version     │ 2 bytes │ Agreed protocol version        │
│ capabilities         │ 4 bytes │ Negotiated capabilities        │
│ session_id           │ 16 bytes│ Session UUID                   │
│ max_chunk_size       │ 4 bytes │ Maximum chunk size (bytes)     │
│ max_file_size        │ 8 bytes │ Maximum file size allowed      │
│ server_name_length   │ 2 bytes │ Server name string length      │
│ server_name          │ variable│ UTF-8 server identifier        │
└─────────────────────────────────────────────────────────────────┘

Minimum: 36 bytes + server_name
```

### DISCONNECT (0x03)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ reason_code          │ 4 bytes │ Disconnect reason code         │
│ message_length       │ 2 bytes │ Message string length          │
│ message              │ variable│ UTF-8 reason message           │
└─────────────────────────────────────────────────────────────────┘
```

### HEARTBEAT (0x04) / HEARTBEAT_ACK (0x05)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ timestamp            │ 8 bytes │ Unix timestamp (microseconds)  │
│ sequence             │ 4 bytes │ Sequence number                │
└─────────────────────────────────────────────────────────────────┘

Total: 12 bytes
```

---

## Upload Messages

### UPLOAD_REQUEST (0x10)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ filename_length      │ 2 bytes │ Remote filename string length  │
│ filename             │ variable│ UTF-8 remote filename          │
│ file_size            │ 8 bytes │ File size in bytes             │
│ sha256_hash          │ 32 bytes│ SHA-256 hash (binary)          │
│ compression_mode     │ 1 byte  │ Requested compression mode     │
│ options_flags        │ 4 bytes │ Transfer options bitmap        │
│ resume_from          │ 8 bytes │ Resume offset (0 if new)       │
└─────────────────────────────────────────────────────────────────┘

Minimum: 71 bytes + filename
```

**Options Flags:**
```
Bit 0: overwrite_existing - Overwrite if file exists
Bit 1: verify_checksum    - Require SHA-256 verification
Bit 2: preserve_timestamp - Preserve modification time
Bit 3-31: Reserved
```

### UPLOAD_ACCEPT (0x11)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ accepted_compression │ 1 byte  │ Agreed compression mode        │
│ chunk_size           │ 4 bytes │ Agreed chunk size              │
│ resume_offset        │ 8 bytes │ Start offset (for resume)      │
└─────────────────────────────────────────────────────────────────┘

Total: 29 bytes
```

### UPLOAD_REJECT (0x12)

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

**Rejection Reason Codes:**
```
-744: file_already_exists
-745: storage_full
-746: file_too_large
-747: access_denied
-748: invalid_filename
-749: quota_exceeded
```

### UPLOAD_COMPLETE (0x13)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ total_chunks         │ 8 bytes │ Total chunks sent              │
│ bytes_sent           │ 8 bytes │ Total raw bytes sent           │
│ bytes_on_wire        │ 8 bytes │ Total compressed bytes sent    │
└─────────────────────────────────────────────────────────────────┘

Total: 40 bytes
```

### UPLOAD_ACK (0x14)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ verified             │ 1 byte  │ SHA-256 verification result    │
│ stored_path_length   │ 2 bytes │ Stored path string length      │
│ stored_path          │ variable│ UTF-8 path where file stored   │
└─────────────────────────────────────────────────────────────────┘

Minimum: 19 bytes + stored_path
```

---

## Download Messages

### DOWNLOAD_REQUEST (0x50)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ filename_length      │ 2 bytes │ Remote filename string length  │
│ filename             │ variable│ UTF-8 filename to download     │
│ compression_mode     │ 1 byte  │ Requested compression mode     │
│ resume_from          │ 8 bytes │ Resume offset (0 if new)       │
└─────────────────────────────────────────────────────────────────┘

Minimum: 27 bytes + filename
```

### DOWNLOAD_ACCEPT (0x51)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ file_size            │ 8 bytes │ File size in bytes             │
│ sha256_hash          │ 32 bytes│ SHA-256 hash (binary)          │
│ accepted_compression │ 1 byte  │ Agreed compression mode        │
│ chunk_size           │ 4 bytes │ Agreed chunk size              │
│ total_chunks         │ 8 bytes │ Total number of chunks         │
│ resume_offset        │ 8 bytes │ Start offset (for resume)      │
│ modified_time        │ 8 bytes │ File modification timestamp    │
└─────────────────────────────────────────────────────────────────┘

Total: 85 bytes
```

### DOWNLOAD_REJECT (0x52)

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

**Rejection Reason Codes:**
```
-746: file_not_found_on_server
-747: access_denied
```

### DOWNLOAD_COMPLETE (0x53)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ total_chunks         │ 8 bytes │ Total chunks sent              │
│ bytes_sent           │ 8 bytes │ Total raw bytes sent           │
│ bytes_on_wire        │ 8 bytes │ Total compressed bytes sent    │
└─────────────────────────────────────────────────────────────────┘

Total: 40 bytes
```

### DOWNLOAD_ACK (0x54)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ verified             │ 1 byte  │ SHA-256 verification result    │
│ bytes_received       │ 8 bytes │ Total bytes received           │
└─────────────────────────────────────────────────────────────────┘

Total: 25 bytes
```

---

## File Listing Messages

### LIST_REQUEST (0x60)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ request_id           │ 16 bytes│ Request UUID                   │
│ pattern_length       │ 2 bytes │ Pattern string length          │
│ pattern              │ variable│ UTF-8 glob pattern (e.g., "*") │
│ offset               │ 4 bytes │ Pagination offset              │
│ limit                │ 4 bytes │ Max items to return            │
│ sort_by              │ 1 byte  │ Sort field (0=name, 1=size, 2=time) │
│ sort_order           │ 1 byte  │ Sort order (0=asc, 1=desc)     │
└─────────────────────────────────────────────────────────────────┘

Minimum: 28 bytes + pattern
```

### LIST_RESPONSE (0x61)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ request_id           │ 16 bytes│ Request UUID                   │
│ total_count          │ 4 bytes │ Total matching files           │
│ returned_count       │ 4 bytes │ Files in this response         │
│ has_more             │ 1 byte  │ More results available         │
│ file_entries[]       │ variable│ Array of file_entry            │
└─────────────────────────────────────────────────────────────────┘
```

**File Entry:**
```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ filename_length      │ 2 bytes │ Filename string length         │
│ filename             │ variable│ UTF-8 filename                 │
│ file_size            │ 8 bytes │ File size in bytes             │
│ sha256_hash          │ 32 bytes│ SHA-256 hash (binary)          │
│ created_time         │ 8 bytes │ Creation timestamp             │
│ modified_time        │ 8 bytes │ Modification timestamp         │
└─────────────────────────────────────────────────────────────────┘

Per-entry: 58 bytes + filename
```

---

## Data Transfer Messages

### CHUNK_DATA (0x20)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ chunk_index          │ 8 bytes │ Chunk sequence number          │
│ chunk_offset         │ 8 bytes │ Byte offset in file            │
│ original_size        │ 4 bytes │ Original (uncompressed) size   │
│ compressed_size      │ 4 bytes │ Compressed size                │
│ checksum             │ 4 bytes │ CRC32 of original data         │
│ flags                │ 1 byte  │ Chunk flags                    │
│ reserved             │ 3 bytes │ Padding for alignment          │
│ data                 │ variable│ Chunk data (compressed or raw) │
└─────────────────────────────────────────────────────────────────┘

Header: 48 bytes + data
```

**Chunk Flags (1 byte):**

| Bit | Hex Value | Name        | Description                    |
|-----|-----------|-------------|--------------------------------|
| 0   | `0x01`    | first_chunk | First chunk of file            |
| 1   | `0x02`    | last_chunk  | Last chunk of file             |
| 2   | `0x04`    | compressed  | Data is LZ4 compressed         |
| 3   | `0x08`    | encrypted   | Reserved for encryption        |
| 4-7 | -         | Reserved    | Must be 0                      |

**Flag Combinations (Examples):**
```
0x00 = No flags (middle chunk, uncompressed)
0x01 = First chunk only
0x02 = Last chunk only
0x03 = Single chunk file (first + last)
0x04 = Compressed middle chunk
0x05 = First compressed chunk (first + compressed)
0x06 = Last compressed chunk (last + compressed)
0x07 = Single compressed file (first + last + compressed)
```

### CHUNK_ACK (0x21)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ chunk_index          │ 8 bytes │ Acknowledged chunk index       │
└─────────────────────────────────────────────────────────────────┘

Total: 24 bytes
```

### CHUNK_NACK (0x22)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ chunk_count          │ 4 bytes │ Number of missing chunks       │
│ chunk_indices[]      │ variable│ Array of missing chunk indices │
└─────────────────────────────────────────────────────────────────┘
```

---

## Resume Messages

### RESUME_REQUEST (0x30)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID to resume        │
│ direction            │ 1 byte  │ 0=upload, 1=download           │
│ bytes_received       │ 8 bytes │ Bytes successfully received    │
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
│ resume_offset        │ 8 bytes │ Offset to resume from          │
│ missing_count        │ 4 bytes │ Number of missing chunks       │
│ missing_indices[]    │ variable│ Array of missing chunk indices │
└─────────────────────────────────────────────────────────────────┘
```

---

## Transfer Control Messages

### TRANSFER_CANCEL (0x40)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ reason_code          │ 4 bytes │ Cancellation reason            │
└─────────────────────────────────────────────────────────────────┘

Total: 20 bytes
```

### TRANSFER_VERIFY (0x43)

```
┌─────────────────────────────────────────────────────────────────┐
│ Field                │ Size    │ Description                    │
├─────────────────────────────────────────────────────────────────┤
│ transfer_id          │ 16 bytes│ Transfer UUID                  │
│ verified             │ 1 byte  │ SHA-256 verification result    │
│ received_hash        │ 32 bytes│ Computed SHA-256 hash          │
└─────────────────────────────────────────────────────────────────┘

Total: 49 bytes
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

### Connection Sequence

```
    Client                                      Server
      │                                           │
      │──────────── CONNECT ─────────────────────▶│
      │                                           │  Validate capabilities
      │◀────────── CONNECT_ACK ──────────────────│
      │                                           │
      │◀──────────────────────────────────────────│
      │          (session established)            │
      │                                           │
```

### Upload Sequence (Client → Server)

```
    Client                                      Server
      │                                           │
      │──────── UPLOAD_REQUEST ─────────────────▶│
      │         (filename, size, hash)            │  Check quota, validate
      │                                           │
      │◀─────── UPLOAD_ACCEPT ──────────────────│
      │         (chunk_size, resume_offset)       │
      │                                           │
      │──────── CHUNK_DATA (chunk 0) ───────────▶│
      │◀─────── CHUNK_ACK (chunk 0) ────────────│
      │                                           │
      │──────── CHUNK_DATA (chunk 1) ───────────▶│
      │◀─────── CHUNK_ACK (chunk 1) ────────────│
      │                                           │
      │            ... more chunks ...            │
      │                                           │
      │──────── CHUNK_DATA (last, flags=0x02) ──▶│
      │◀─────── CHUNK_ACK ─────────────────────│
      │                                           │
      │──────── UPLOAD_COMPLETE ────────────────▶│
      │                                           │  Verify SHA-256
      │◀─────── UPLOAD_ACK ────────────────────│
      │         (verified, stored_path)           │
      │                                           │
```

### Download Sequence (Server → Client)

```
    Client                                      Server
      │                                           │
      │──────── DOWNLOAD_REQUEST ───────────────▶│
      │         (filename)                        │  Check file exists
      │                                           │
      │◀─────── DOWNLOAD_ACCEPT ────────────────│
      │         (size, hash, total_chunks)        │
      │                                           │
      │◀─────── CHUNK_DATA (chunk 0) ───────────│
      │──────── CHUNK_ACK (chunk 0) ────────────▶│
      │                                           │
      │◀─────── CHUNK_DATA (chunk 1) ───────────│
      │──────── CHUNK_ACK (chunk 1) ────────────▶│
      │                                           │
      │            ... more chunks ...            │
      │                                           │
      │◀─────── CHUNK_DATA (last, flags=0x02) ──│
      │──────── CHUNK_ACK ─────────────────────▶│
      │                                           │
      │◀─────── DOWNLOAD_COMPLETE ──────────────│
      │                                           │  Client verifies SHA-256
      │──────── DOWNLOAD_ACK ──────────────────▶│
      │         (verified)                        │
      │                                           │
```

### File Listing Sequence

```
    Client                                      Server
      │                                           │
      │──────── LIST_REQUEST ──────────────────▶│
      │         (pattern="*.pdf", limit=100)      │
      │                                           │
      │◀─────── LIST_RESPONSE ─────────────────│
      │         (total=250, returned=100,         │
      │          has_more=true, files[])          │
      │                                           │
      │──────── LIST_REQUEST ──────────────────▶│  (pagination)
      │         (pattern="*.pdf", offset=100)     │
      │                                           │
      │◀─────── LIST_RESPONSE ─────────────────│
      │         (files[100..199])                 │
      │                                           │
```

### Resume Sequence (Upload)

```
    Client                                      Server
      │                                           │
      │──────── CONNECT ────────────────────────▶│
      │◀─────── CONNECT_ACK ───────────────────│
      │                                           │
      │──────── RESUME_REQUEST ────────────────▶│
      │         (transfer_id, bitmap)             │
      │                                           │
      │◀─────── RESUME_RESPONSE ───────────────│
      │         (missing chunks: [5, 12, 13])     │
      │                                           │
      │──────── CHUNK_DATA (chunk 5) ──────────▶│
      │◀─────── CHUNK_ACK ─────────────────────│
      │                                           │
      │──────── CHUNK_DATA (chunk 12) ─────────▶│
      │◀─────── CHUNK_ACK ─────────────────────│
      │                                           │
      │──────── CHUNK_DATA (chunk 13) ─────────▶│
      │◀─────── CHUNK_ACK ─────────────────────│
      │                                           │
      │──────── UPLOAD_COMPLETE ───────────────▶│
      │◀─────── UPLOAD_ACK ────────────────────│
      │                                           │
```

### Error Handling Sequence

```
    Client                                      Server
      │                                           │
      │──────── CHUNK_DATA (corrupted) ─────────▶│
      │                                           │  CRC32 fails
      │◀─────── CHUNK_NACK (chunk 5) ──────────│
      │                                           │
      │──────── CHUNK_DATA (chunk 5, retry) ────▶│
      │◀─────── CHUNK_ACK (chunk 5) ────────────│
      │                                           │
```

### Heartbeat Sequence

```
    Client                                      Server
      │                                           │
      │──────── HEARTBEAT ─────────────────────▶│
      │         (timestamp, seq=1)                │
      │                                           │
      │◀─────── HEARTBEAT_ACK ─────────────────│
      │         (timestamp, seq=1)                │
      │                                           │
      │          ... 30 seconds later ...         │
      │                                           │
      │──────── HEARTBEAT ─────────────────────▶│
      │         (timestamp, seq=2)                │
      │                                           │
      │◀─────── HEARTBEAT_ACK ─────────────────│
      │         (timestamp, seq=2)                │
      │                                           │
```

---

## State Machine

### Client Connection States

```
                    ┌─────────────────┐
                    │  DISCONNECTED   │
                    └────────┬────────┘
                             │ connect()
                             ▼
                    ┌─────────────────┐
                    │   CONNECTING    │──────────────┐
                    └────────┬────────┘              │
                             │ CONNECT_ACK          │ timeout/error
                             ▼                       │
                    ┌─────────────────┐              │
          ┌────────│    CONNECTED    │──────┐       │
          │        └────────┬────────┘      │       │
          │                 │               │       │
          │ disconnect()    │               │ error │
          │                 ▼               │       │
          │        ┌─────────────────┐      │       │
          │        │  RECONNECTING   │◀─────┼───────┤
          │        │  (auto-retry)   │      │       │
          │        └────────┬────────┘      │       │
          │                 │ success       │       │
          │                 ├───────────────┘       │
          │                 │ max_attempts          │
          │                 │ exceeded              │
          ▼                 ▼                       ▼
          ├─────────────────┴───────────────────────┤
          │                                         │
          ▼                                         │
┌─────────────────┐                                 │
│  DISCONNECTED   │◀────────────────────────────────┘
└─────────────────┘
```

### Transfer States

```
                                    ┌──────────────┐
                                    │   pending    │
                                    └──────┬───────┘
                                           │
                                           │ request sent
                                           ▼
                                    ┌──────────────┐
                           ┌───────│  requested   │───────┐
                           │       └──────┬───────┘       │
                           │              │               │
                     reject│              │ accept        │ timeout
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
                    │   rejected   │          │  cancelled   │
                    └──────────────┘          └──────────────┘
                           │                         │
                           └────────────┬────────────┘
                                        │
                                        ▼
                                 ┌──────────────┐
                                 │    failed    │
                                 └──────────────┘
```

---

## Versioning

### Protocol Version Format

The protocol uses a 4-byte version format aligned with semantic versioning:

```
┌─────────────────────────────────────────────────────────────────┐
│ Field    │ Size   │ Range   │ Description                       │
├─────────────────────────────────────────────────────────────────┤
│ Major    │ 1 byte │ 0-255   │ Breaking changes                  │
│ Minor    │ 1 byte │ 0-255   │ New features (backward compatible)│
│ Patch    │ 1 byte │ 0-255   │ Bug fixes                         │
│ Build    │ 1 byte │ 0-255   │ Build/revision number             │
└─────────────────────────────────────────────────────────────────┘

Current version: 0.2.0.0
Wire encoding: 0x00020000 (4 bytes, big-endian)
```

### Version Encoding

```cpp
// Version structure
struct protocol_version {
    uint8_t major;    // Breaking changes
    uint8_t minor;    // New features
    uint8_t patch;    // Bug fixes
    uint8_t build;    // Build number

    // Encode to 4-byte wire format (big-endian)
    uint32_t to_wire() const {
        return (major << 24) | (minor << 16) | (patch << 8) | build;
    }

    // Decode from 4-byte wire format
    static protocol_version from_wire(uint32_t wire) {
        return {
            static_cast<uint8_t>(wire >> 24),
            static_cast<uint8_t>(wire >> 16),
            static_cast<uint8_t>(wire >> 8),
            static_cast<uint8_t>(wire)
        };
    }
};

// Current version constant
constexpr uint32_t PROTOCOL_VERSION = 0x00020000;  // v0.2.0.0
```

### Version Compatibility

| Client | Server | Result |
|--------|--------|--------|
| 0.2.x.x | 0.2.x.x | Compatible |
| 0.2.0.x | 0.2.1.x | Compatible (server has newer features) |
| 0.2.1.x | 0.2.0.x | Compatible (client downgrades features) |
| 0.3.x.x | 0.2.x.x | Incompatible (minor version mismatch in pre-1.0) |
| 1.x.x.x | 0.x.x.x | Incompatible (major version mismatch) |

> **Note**: In pre-1.0 versions (major=0), minor version changes may contain breaking changes. After 1.0 release, only major version changes will be breaking.

---

## Security Considerations

### TLS Requirements

- **Minimum Version:** TLS 1.3
- **Certificate Verification:** Required in production
- **Forward Secrecy:** Enabled by default

### Filename Validation

Filenames in UPLOAD_REQUEST and DOWNLOAD_REQUEST are validated:
- No `..` components (path traversal prevention)
- No path separators (`/` or `\`)
- No absolute paths
- UTF-8 sanitization
- Maximum length: 255 characters
- No hidden files (leading `.`)
- No control characters

### Resource Limits

| Limit | Default | Configurable |
|-------|---------|--------------|
| Max chunk size | 1 MB | Yes |
| Max file size | 10 GB | Yes |
| Storage quota | 1 TB | Yes |
| Max concurrent connections | 100 | Yes |
| Max concurrent transfers per client | 5 | Yes |
| Heartbeat interval | 30 seconds | Yes |
| Connection timeout | 60 seconds | Yes |

---

*Last updated: 2025-12-11*
*Version: 0.2.0*
