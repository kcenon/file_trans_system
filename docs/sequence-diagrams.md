# Sequence Diagrams

This document provides detailed sequence diagrams for the key operations in **file_trans_system**.

**Version:** 0.2.0
**Architecture:** Client-Server Model

---

## Table of Contents

1. [Connection Flow](#connection-flow)
2. [Upload Flow](#upload-flow)
3. [Download Flow](#download-flow)
4. [File Listing](#file-listing)
5. [Auto-Reconnection](#auto-reconnection)
6. [Transfer Resume](#transfer-resume)
7. [Chunk Transfer](#chunk-transfer)
8. [Error Handling](#error-handling)
9. [Pipeline Processing](#pipeline-processing)

---

## Connection Flow

### Client Connection to Server

```
┌────────┐          ┌────────────┐          ┌────────────┐
│ Client │          │  Network   │          │   Server   │
│  App   │          │  Layer     │          │            │
└───┬────┘          └─────┬──────┘          └─────┬──────┘
    │                     │                       │
    │  connect(endpoint)  │                       │
    │────────────────────▶│                       │
    │                     │                       │
    │                     │  TCP/TLS connection   │
    │                     │──────────────────────▶│
    │                     │                       │
    │                     │  connection accepted  │
    │                     │◀──────────────────────│
    │                     │                       │
    │                     │  CONNECT              │
    │                     │  ┌────────────────────────────────┐
    │                     │  │ protocol_version: 0x0200      │
    │                     │  │ capabilities: 0x0000001F      │
    │                     │  │ client_id: <16 bytes UUID>    │
    │                     │  └────────────────────────────────┘
    │                     │──────────────────────▶│
    │                     │                       │
    │                     │                       │ Validate protocol
    │                     │                       │ version
    │                     │                       │─────┐
    │                     │                       │     │
    │                     │                       │◀────┘
    │                     │                       │
    │                     │  CONNECT_ACK          │
    │                     │  ┌────────────────────────────────┐
    │                     │  │ protocol_version: 0x0200      │
    │                     │  │ capabilities: 0x0000001F      │
    │                     │  │ session_id: <16 bytes UUID>   │
    │                     │  │ max_file_size: 10GB           │
    │                     │  │ max_chunk_size: 1MB           │
    │                     │  └────────────────────────────────┘
    │                     │◀──────────────────────│
    │                     │                       │
    │  on_connected()     │                       │
    │◀────────────────────│                       │
    │                     │                       │
    │  Result<server_info>│                       │ on_client_connected()
    │◀────────────────────│                       │
    │                     │                       │
    │  Connection established                     │
    │                     │                       │
    ▼                     ▼                       ▼
```

### Client Disconnection

```
┌────────┐          ┌────────────┐          ┌────────────┐
│ Client │          │  Network   │          │   Server   │
└───┬────┘          └─────┬──────┘          └─────┬──────┘
    │                     │                       │
    │  disconnect()       │                       │
    │────────────────────▶│                       │
    │                     │                       │
    │                     │  Wait for active      │
    │                     │  transfers to complete│
    │                     │─────┐                 │
    │                     │     │                 │
    │                     │◀────┘                 │
    │                     │                       │
    │                     │  DISCONNECT           │
    │                     │  ┌────────────────────────────────┐
    │                     │  │ reason: client_initiated      │
    │                     │  └────────────────────────────────┘
    │                     │──────────────────────▶│
    │                     │                       │
    │                     │                       │ on_client_disconnected()
    │                     │                       │
    │                     │  close connection     │
    │                     │◀──────────────────────│
    │                     │                       │
    │  on_disconnected()  │                       │
    │◀────────────────────│                       │
    │                     │                       │
    ▼                     ▼                       ▼
```

---

## Upload Flow

### Complete Upload (Client → Server)

```
┌────────┐          ┌────────────┐          ┌────────────┐          ┌────────┐
│ Client │          │  Client    │          │   Server   │          │ Server │
│  App   │          │  Pipeline  │          │  Pipeline  │          │  App   │
└───┬────┘          └─────┬──────┘          └─────┬──────┘          └───┬────┘
    │                     │                       │                     │
    │  upload_file(local, remote)                 │                     │
    │────────────────────▶│                       │                     │
    │                     │                       │                     │
    │                     │  UPLOAD_REQUEST       │                     │
    │                     │  ┌──────────────────────────────────────┐   │
    │                     │  │ filename: "report.pdf"              │   │
    │                     │  │ file_size: 104857600                │   │
    │                     │  │ file_hash: <sha256>                 │   │
    │                     │  │ chunk_count: 400                    │   │
    │                     │  │ chunk_size: 262144                  │   │
    │                     │  │ compression: adaptive               │   │
    │                     │  └──────────────────────────────────────┘   │
    │                     │──────────────────────▶│                     │
    │                     │                       │                     │
    │                     │                       │  on_upload_request()│
    │                     │                       │────────────────────▶│
    │                     │                       │                     │
    │                     │                       │  return true (accept)
    │                     │                       │◀────────────────────│
    │                     │                       │                     │
    │                     │                       │  Create temp file   │
    │                     │                       │─────┐               │
    │                     │                       │     │               │
    │                     │                       │◀────┘               │
    │                     │                       │                     │
    │                     │  UPLOAD_ACCEPT        │                     │
    │                     │  ┌──────────────────────────────────────┐   │
    │                     │  │ transfer_id: <uuid>                 │   │
    │                     │  └──────────────────────────────────────┘   │
    │                     │◀──────────────────────│                     │
    │                     │                       │                     │
    │                     │                       │                     │
    │                     │    CHUNK_DATA [0]     │                     │
    │                     │──────────────────────▶│                     │
    │  on_progress(0.25%) │                       │                     │
    │◀────────────────────│    CHUNK_ACK [0]      │                     │
    │                     │◀──────────────────────│                     │
    │                     │                       │                     │
    │                     │    CHUNK_DATA [1..N]  │                     │
    │                     │ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ▶│                     │
    │                     │                       │                     │
    │  on_progress(100%)  │   CHUNK_ACK [1..N]    │                     │
    │◀────────────────────│◀─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─│                     │
    │                     │                       │                     │
    │                     │  UPLOAD_COMPLETE      │                     │
    │                     │──────────────────────▶│                     │
    │                     │                       │                     │
    │                     │                       │  Verify SHA-256     │
    │                     │                       │  Move to storage    │
    │                     │                       │─────┐               │
    │                     │                       │     │               │
    │                     │                       │◀────┘               │
    │                     │                       │                     │
    │                     │  UPLOAD_VERIFY        │  on_upload_complete()
    │                     │  (verified=true)      │────────────────────▶│
    │  Result<transfer>   │◀──────────────────────│                     │
    │◀────────────────────│                       │                     │
    │                     │                       │                     │
    ▼                     ▼                       ▼                     ▼
```

### Upload Rejection

```
┌────────┐          ┌────────────┐          ┌────────────┐          ┌────────┐
│ Client │          │  Network   │          │   Server   │          │ Server │
│  App   │          │            │          │            │          │  App   │
└───┬────┘          └─────┬──────┘          └─────┬──────┘          └───┬────┘
    │                     │                       │                     │
    │  upload_file(10GB_file)                     │                     │
    │────────────────────▶│                       │                     │
    │                     │                       │                     │
    │                     │  UPLOAD_REQUEST       │                     │
    │                     │  (file_size: 10GB)    │                     │
    │                     │──────────────────────▶│                     │
    │                     │                       │                     │
    │                     │                       │  on_upload_request()│
    │                     │                       │────────────────────▶│
    │                     │                       │                     │
    │                     │                       │  return false       │
    │                     │                       │  (file too large)   │
    │                     │                       │◀────────────────────│
    │                     │                       │                     │
    │                     │  UPLOAD_REJECT        │                     │
    │                     │  ┌──────────────────────────────────────┐   │
    │                     │  │ error_code: -713 (upload_rejected)  │   │
    │                     │  │ reason: "File size exceeds limit"   │   │
    │                     │  └──────────────────────────────────────┘   │
    │                     │◀──────────────────────│                     │
    │                     │                       │                     │
    │  Result<error>      │                       │                     │
    │  (-713: upload_rejected)                    │                     │
    │◀────────────────────│                       │                     │
    │                     │                       │                     │
    ▼                     ▼                       ▼                     ▼
```

---

## Download Flow

### Complete Download (Server → Client)

```
┌────────┐          ┌────────────┐          ┌────────────┐          ┌────────┐
│ Client │          │  Client    │          │   Server   │          │ Server │
│  App   │          │  Pipeline  │          │  Pipeline  │          │  App   │
└───┬────┘          └─────┬──────┘          └─────┬──────┘          └───┬────┘
    │                     │                       │                     │
    │  download_file(remote, local)               │                     │
    │────────────────────▶│                       │                     │
    │                     │                       │                     │
    │                     │  DOWNLOAD_REQUEST     │                     │
    │                     │  ┌──────────────────────────────────────┐   │
    │                     │  │ filename: "report.pdf"              │   │
    │                     │  └──────────────────────────────────────┘   │
    │                     │──────────────────────▶│                     │
    │                     │                       │                     │
    │                     │                       │ on_download_request()
    │                     │                       │────────────────────▶│
    │                     │                       │                     │
    │                     │                       │  return true (allow)│
    │                     │                       │◀────────────────────│
    │                     │                       │                     │
    │                     │                       │  Read file metadata │
    │                     │                       │─────┐               │
    │                     │                       │     │               │
    │                     │                       │◀────┘               │
    │                     │                       │                     │
    │                     │  DOWNLOAD_ACCEPT      │                     │
    │                     │  ┌──────────────────────────────────────┐   │
    │                     │  │ transfer_id: <uuid>                 │   │
    │                     │  │ file_size: 104857600                │   │
    │                     │  │ file_hash: <sha256>                 │   │
    │                     │  │ chunk_count: 400                    │   │
    │                     │  │ chunk_size: 262144                  │   │
    │                     │  │ compression: adaptive               │   │
    │                     │  └──────────────────────────────────────┘   │
    │                     │◀──────────────────────│                     │
    │                     │                       │                     │
    │                     │  Create local file    │                     │
    │                     │─────┐                 │                     │
    │                     │     │                 │                     │
    │                     │◀────┘                 │                     │
    │                     │                       │                     │
    │                     │    CHUNK_DATA [0]     │                     │
    │                     │◀──────────────────────│                     │
    │  on_progress(0.25%) │                       │                     │
    │◀────────────────────│    CHUNK_ACK [0]      │                     │
    │                     │──────────────────────▶│                     │
    │                     │                       │                     │
    │                     │    CHUNK_DATA [1..N]  │                     │
    │                     │◀─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─│                     │
    │                     │                       │                     │
    │  on_progress(100%)  │   CHUNK_ACK [1..N]    │                     │
    │◀────────────────────│─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ▶│                     │
    │                     │                       │                     │
    │                     │  DOWNLOAD_COMPLETE    │                     │
    │                     │◀──────────────────────│                     │
    │                     │                       │                     │
    │                     │  Verify SHA-256       │                     │
    │                     │─────┐                 │                     │
    │                     │     │                 │                     │
    │                     │◀────┘                 │                     │
    │                     │                       │                     │
    │                     │  DOWNLOAD_VERIFY      │ on_download_complete()
    │                     │  (verified=true)      │────────────────────▶│
    │  Result<transfer>   │──────────────────────▶│                     │
    │◀────────────────────│                       │                     │
    │                     │                       │                     │
    ▼                     ▼                       ▼                     ▼
```

### Download - File Not Found

```
┌────────┐          ┌────────────┐          ┌────────────┐
│ Client │          │  Network   │          │   Server   │
└───┬────┘          └─────┬──────┘          └─────┬──────┘
    │                     │                       │
    │  download_file("nonexistent.pdf")           │
    │────────────────────▶│                       │
    │                     │                       │
    │                     │  DOWNLOAD_REQUEST     │
    │                     │  (filename: "nonexistent.pdf")
    │                     │──────────────────────▶│
    │                     │                       │
    │                     │                       │  Check storage
    │                     │                       │  File not found
    │                     │                       │─────┐
    │                     │                       │     │
    │                     │                       │◀────┘
    │                     │                       │
    │                     │  DOWNLOAD_REJECT      │
    │                     │  ┌──────────────────────────────────────┐
    │                     │  │ error_code: -746                    │
    │                     │  │ (file_not_found_on_server)          │
    │                     │  │ reason: "File does not exist"       │
    │                     │  └──────────────────────────────────────┘
    │                     │◀──────────────────────│
    │                     │                       │
    │  Result<error>      │                       │
    │  (-746: file_not_found_on_server)           │
    │◀────────────────────│                       │
    │                     │                       │
    ▼                     ▼                       ▼
```

---

## File Listing

### List Files on Server

```
┌────────┐          ┌────────────┐          ┌────────────┐
│ Client │          │  Network   │          │   Server   │
└───┬────┘          └─────┬──────┘          └─────┬──────┘
    │                     │                       │
    │  list_files("*.pdf")│                       │
    │────────────────────▶│                       │
    │                     │                       │
    │                     │  LIST_REQUEST         │
    │                     │  ┌──────────────────────────────────────┐
    │                     │  │ pattern: "*.pdf"                    │
    │                     │  │ offset: 0                           │
    │                     │  │ limit: 100                          │
    │                     │  └──────────────────────────────────────┘
    │                     │──────────────────────▶│
    │                     │                       │
    │                     │                       │  Query storage
    │                     │                       │  manager
    │                     │                       │─────┐
    │                     │                       │     │
    │                     │                       │◀────┘
    │                     │                       │
    │                     │  LIST_RESPONSE        │
    │                     │  ┌──────────────────────────────────────┐
    │                     │  │ total_count: 42                     │
    │                     │  │ returned_count: 42                  │
    │                     │  │ files: [                            │
    │                     │  │   {name: "report.pdf",              │
    │                     │  │    size: 1048576,                   │
    │                     │  │    modified: "2025-12-11T10:00:00Z"},│
    │                     │  │   {name: "invoice.pdf", ...},       │
    │                     │  │   ...                               │
    │                     │  │ ]                                   │
    │                     │  └──────────────────────────────────────┘
    │                     │◀──────────────────────│
    │                     │                       │
    │  Result<vector<file_info>>                  │
    │◀────────────────────│                       │
    │                     │                       │
    ▼                     ▼                       ▼
```

### Paginated Listing

```
┌────────┐          ┌────────────┐          ┌────────────┐
│ Client │          │  Network   │          │   Server   │
└───┬────┘          └─────┬──────┘          └─────┬──────┘
    │                     │                       │
    │  list_files("*", offset=0, limit=50)        │
    │────────────────────▶│                       │
    │                     │  LIST_REQUEST         │
    │                     │──────────────────────▶│
    │                     │                       │
    │                     │  LIST_RESPONSE        │
    │                     │  (total=200, returned=50, files[0-49])
    │                     │◀──────────────────────│
    │  files[0-49]        │                       │
    │◀────────────────────│                       │
    │                     │                       │
    │  list_files("*", offset=50, limit=50)       │
    │────────────────────▶│                       │
    │                     │  LIST_REQUEST         │
    │                     │──────────────────────▶│
    │                     │                       │
    │                     │  LIST_RESPONSE        │
    │                     │  (total=200, returned=50, files[50-99])
    │                     │◀──────────────────────│
    │  files[50-99]       │                       │
    │◀────────────────────│                       │
    │                     │                       │
    │  ... continue until all pages retrieved ... │
    │                     │                       │
    ▼                     ▼                       ▼
```

---

## Auto-Reconnection

### Reconnection After Network Failure

```
┌────────┐          ┌────────────┐          ┌────────────┐
│ Client │          │  Network   │          │   Server   │
│  App   │          │            │          │            │
└───┬────┘          └─────┬──────┘          └─────┬──────┘
    │                     │                       │
    │  Connected, upload in progress              │
    │                     │                       │
    │  CHUNK_DATA [0-99]  │                       │
    │────────────────────▶│──────────────────────▶│
    │                     │                       │
    │             ╔═══════════════════╗           │
    │             ║  NETWORK FAILURE  ║           │
    │             ╚═══════════════════╝           │
    │                     │                       │
    │                     ╳                       │
    │                     │                       │
    │  on_disconnected(connection_lost)           │
    │◀────────────────────│                       │
    │                     │                       │
    │  on_reconnecting(attempt=1, delay=1s)       │
    │◀────────────────────│                       │
    │                     │                       │
    │  ═══ Wait 1 second ═══                      │
    │                     │                       │
    │                     │  TCP connect          │
    │                     │──────────────────────╳│
    │                     │                       │
    │  on_reconnecting(attempt=2, delay=2s)       │
    │◀────────────────────│                       │
    │                     │                       │
    │  ═══ Wait 2 seconds ═══                     │
    │                     │                       │
    │                     │  TCP connect          │
    │                     │──────────────────────▶│
    │                     │                       │
    │                     │  CONNECT              │
    │                     │──────────────────────▶│
    │                     │                       │
    │                     │  CONNECT_ACK          │
    │                     │◀──────────────────────│
    │                     │                       │
    │  on_reconnected()   │                       │
    │◀────────────────────│                       │
    │                     │                       │
    │  Resume active transfers                    │
    │                     │                       │
    │                     │  RESUME_REQUEST       │
    │                     │  (transfer_id, received_chunks)
    │                     │──────────────────────▶│
    │                     │                       │
    │                     │  RESUME_RESPONSE      │
    │                     │  (resumable, missing_chunks)
    │                     │◀──────────────────────│
    │                     │                       │
    │  Continue from chunk 100                    │
    │  CHUNK_DATA [100..N]│                       │
    │────────────────────▶│──────────────────────▶│
    │                     │                       │
    ▼                     ▼                       ▼
```

### Reconnection Failure (Max Attempts Exhausted)

```
┌────────┐          ┌────────────┐
│ Client │          │  Network   │
│  App   │          │            │
└───┬────┘          └─────┬──────┘
    │                     │
    │  Connection lost    │
    │                     │
    │  on_disconnected()  │
    │◀────────────────────│
    │                     │
    │  on_reconnecting(1, 1s)
    │◀────────────────────│
    │                     │
    │  ═══ Wait 1s ═══    │
    │                     │
    │  Connect attempt    ╳
    │                     │
    │  on_reconnecting(2, 2s)
    │◀────────────────────│
    │                     │
    │  ═══ Wait 2s ═══    │
    │                     │
    │  Connect attempt    ╳
    │                     │
    │      ... continue   │
    │                     │
    │  on_reconnecting(10, 30s)
    │◀────────────────────│
    │                     │
    │  ═══ Wait 30s ═══   │
    │                     │
    │  Connect attempt    ╳
    │                     │
    │  Max attempts reached
    │                     │
    │  on_reconnect_failed()
    │◀────────────────────│
    │                     │
    │  Active transfers   │
    │  marked as failed   │
    │                     │
    ▼                     ▼
```

---

## Transfer Resume

### Resume After Reconnection

```
┌────────┐          ┌────────────┐          ┌────────┐
│ Client │          │  Network   │          │ Server │
└───┬────┘          └─────┬──────┘          └───┬────┘
    │                     │                     │
    │  Reconnected after disconnection          │
    │                     │                     │
    │  Has active transfer:                     │
    │  - transfer_id: <uuid>                    │
    │  - chunks_sent: [0-99]                    │
    │                     │                     │
    │  RESUME_REQUEST     │                     │
    │  ┌─────────────────────────────────┐      │
    │  │ transfer_id: <uuid>             │      │
    │  │ file_hash: <sha256>             │      │
    │  │ direction: upload               │      │
    │  │ chunks_sent: [0-99]             │      │
    │  └─────────────────────────────────┘      │
    │──────────────────────────────────────────▶│
    │                     │                     │
    │                     │                     │ Check checkpoint
    │                     │                     │ Verify file hash
    │                     │                     │─────┐
    │                     │                     │     │
    │                     │                     │◀────┘
    │                     │                     │
    │  RESUME_RESPONSE    │                     │
    │  ┌─────────────────────────────────┐      │
    │  │ status: RESUMABLE               │      │
    │  │ chunks_received: [0-99]         │      │
    │  │ chunks_missing: [100-399]       │      │
    │  │ total_chunks: 400               │      │
    │  └─────────────────────────────────┘      │
    │◀──────────────────────────────────────────│
    │                     │                     │
    │  Resume from chunk 100                    │
    │                     │                     │
    │  CHUNK_DATA [100-399]                     │
    │─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ▶│
    │                     │                     │
    │  CHUNK_ACK [100-399]│                     │
    │◀─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ │
    │                     │                     │
    │  UPLOAD_COMPLETE    │                     │
    │─────────────────────────────────────────▶│
    │                     │                     │
    │  UPLOAD_VERIFY      │                     │
    │◀──────────────────────────────────────────│
    │                     │                     │
    ▼                     ▼                     ▼
```

### Resume States

```
RESUME_RESPONSE status values:

┌───────────────────────────────────────────────────────────────────────┐
│  RESUMABLE          │  Transfer can be resumed from checkpoint        │
├─────────────────────┼─────────────────────────────────────────────────┤
│  NOT_FOUND          │  Transfer ID not found on server                │
├─────────────────────┼─────────────────────────────────────────────────┤
│  FILE_CHANGED       │  Source file hash doesn't match                 │
├─────────────────────┼─────────────────────────────────────────────────┤
│  STATE_CORRUPTED    │  Checkpoint data is corrupted                   │
├─────────────────────┼─────────────────────────────────────────────────┤
│  EXPIRED            │  Checkpoint expired (configurable timeout)      │
└───────────────────────────────────────────────────────────────────────┘
```

---

## Chunk Transfer

### Single Chunk Lifecycle (Upload)

```
┌────────┐    ┌────────────┐    ┌────────────┐    ┌────────────┐    ┌────────┐
│  File  │    │  io_read   │    │compression │    │  network   │    │ Server │
│  Disk  │    │   stage    │    │   stage    │    │   stage    │    │        │
└───┬────┘    └─────┬──────┘    └─────┬──────┘    └─────┬──────┘    └───┬────┘
    │               │                 │                 │               │
    │  read(offset) │                 │                 │               │
    │◀──────────────│                 │                 │               │
    │               │                 │                 │               │
    │  data[256KB]  │                 │                 │               │
    │──────────────▶│                 │                 │               │
    │               │                 │                 │               │
    │               │  Create chunk   │                 │               │
    │               │  Calculate CRC32│                 │               │
    │               │─────┐           │                 │               │
    │               │     │           │                 │               │
    │               │◀────┘           │                 │               │
    │               │                 │                 │               │
    │               │  Push to queue  │                 │               │
    │               │────────────────▶│                 │               │
    │               │                 │                 │               │
    │               │                 │  Adaptive check │               │
    │               │                 │  compressible?  │               │
    │               │                 │─────┐           │               │
    │               │                 │     │           │               │
    │               │                 │◀────┘           │               │
    │               │                 │                 │               │
    │               │                 │  LZ4 compress   │               │
    │               │                 │─────┐           │               │
    │               │                 │     │           │               │
    │               │                 │◀────┘           │               │
    │               │                 │                 │               │
    │               │                 │  Push compressed│               │
    │               │                 │────────────────▶│               │
    │               │                 │                 │               │
    │               │                 │                 │  Serialize    │
    │               │                 │                 │  CHUNK_DATA   │
    │               │                 │                 │─────┐         │
    │               │                 │                 │     │         │
    │               │                 │                 │◀────┘         │
    │               │                 │                 │               │
    │               │                 │                 │  Send         │
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
│ Client │                                    │ Server │
└───┬────┘                                    └───┬────┘
    │                                             │
    │  CHUNK_DATA [chunk_idx=42]                  │
    │  ┌─────────────────────────────────┐        │
    │  │ data + checksum (CRC32)         │        │
    │  └─────────────────────────────────┘        │
    │────────────────────────────────────────────▶│
    │                                             │
    │                                             │ Verify CRC32
    │                                             │─────┐
    │                                             │     │ MISMATCH!
    │                                             │◀────┘
    │                                             │
    │  CHUNK_NACK [chunk_idx=42]                  │
    │  ┌─────────────────────────────────┐        │
    │  │ error_code: -720                │        │
    │  │ (chunk_checksum_error)          │        │
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
    │                                             │ Verify CRC32
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

## Error Handling

### Connection Error

```
┌────────┐          ┌────────────┐          ┌────────┐
│ Client │          │  Network   │          │ Server │
│  App   │          │            │          │        │
└───┬────┘          └─────┬──────┘          └───┬────┘
    │                     │                     │
    │  connect()          │                     │
    │────────────────────▶│                     │
    │                     │                     │
    │                     │  TCP connect        │
    │                     │─────────────────────╳ (Connection refused)
    │                     │                     │
    │                     │  Retry with backoff │
    │                     │  (if auto_reconnect)│
    │                     │─────────────────────╳
    │                     │                     │
    │                     │─────────────────────╳
    │                     │                     │
    │  Result<error>      │                     │
    │  (-700: connection_failed)                │
    │◀────────────────────│                     │
    │                     │                     │
    ▼                     ▼                     ▼
```

### Storage Full Error

```
┌────────┐          ┌────────────┐          ┌────────┐
│ Client │          │  Network   │          │ Server │
└───┬────┘          └─────┬──────┘          └───┬────┘
    │                     │                     │
    │  UPLOAD_REQUEST     │                     │
    │  (file_size: 50GB)  │                     │
    │────────────────────▶│────────────────────▶│
    │                     │                     │
    │                     │                     │ Check storage quota
    │                     │                     │ (90GB used / 100GB quota)
    │                     │                     │─────┐
    │                     │                     │     │
    │                     │                     │◀────┘
    │                     │                     │
    │  UPLOAD_REJECT      │                     │
    │  ┌──────────────────────────────────────┐ │
    │  │ error_code: -745 (storage_full)     │ │
    │  │ reason: "Insufficient storage space"│ │
    │  │ available: 10GB                     │ │
    │  │ requested: 50GB                     │ │
    │  └──────────────────────────────────────┘ │
    │◀────────────────────│◀────────────────────│
    │                     │                     │
    │  Result<error>      │                     │
    │  (-745: storage_full)                     │
    │◀────────────────────│                     │
    │                     │                     │
    ▼                     ▼                     ▼
```

---

## Pipeline Processing

### Parallel Stage Execution

```
Time ───────────────────────────────────────────────────────────────────▶

io_read    │▓▓▓▓│    │▓▓▓▓│    │▓▓▓▓│    │▓▓▓▓│    │▓▓▓▓│    │▓▓▓▓│
stage      │ C0 │    │ C1 │    │ C2 │    │ C3 │    │ C4 │    │ C5 │
           └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘

compress   │    │▓▓▓▓▓▓▓▓│    │▓▓▓▓▓▓▓▓│    │▓▓▓▓▓▓▓▓│    │▓▓▓▓▓▓▓▓│
stage      │    │   C0   │    │   C1   │    │   C2   │    │   C3   │
           └────┴────────┴────┴────────┴────┴────────┴────┴────────┘

network    │    │    │▓▓▓▓▓▓▓▓▓▓▓▓│    │▓▓▓▓▓▓▓▓▓▓▓▓│    │▓▓▓▓▓▓▓▓│
send       │    │    │     C0     │    │     C1     │    │   C2   │
           └────┴────┴────────────┴────┴────────────┴────┴────────┘

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

T=5  read_queue:    [C1 C2   ]
     compress_queue:[C0 C1   ]
     send_queue:    [C0      ]     Network starts sending

T=20 read_queue:    [████████████████]  FULL (16)
     compress_queue:[████████████████████████████████]  FULL (32)
     send_queue:    [████████████████████████████████████████████████████████████████]  FULL (64)
                                       ↑ Network bottleneck

T=21 io_read stage BLOCKS waiting for queue space
     Memory usage stays bounded at ~32MB
     No runaway memory consumption!

T=22 Network sends one chunk
     send_queue:    [███████████████████████████████████████████████████████████████ ]
                                                                              ↑ Space freed

     Compression stage can now push
     io_read stage unblocks
```

---

## Heartbeat and Connection Management

### Heartbeat Mechanism

```
┌────────┐                                    ┌────────┐
│ Client │                                    │ Server │
└───┬────┘                                    └───┬────┘
    │                                             │
    │  Connection established                     │
    │                                             │
    │  ═══ No data for 15 seconds ═══             │
    │                                             │
    │  HEARTBEAT                                  │
    │────────────────────────────────────────────▶│
    │                                             │
    │  HEARTBEAT_ACK                              │
    │◀────────────────────────────────────────────│
    │                                             │
    │  Connection confirmed alive                 │
    │                                             │
    │  ═══ No data for 15 seconds ═══             │
    │                                             │
    │  HEARTBEAT                                  │
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

*Version: 0.2.0*
*Last updated: 2025-12-11*
