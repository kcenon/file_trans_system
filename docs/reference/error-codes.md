# Error Codes Reference

Complete reference of error codes used in the **file_trans_system** library.

**Version**: 2.0.0
**Last Updated**: 2025-12-11

## Overview

Error codes are in the range **-700 to -799**, following the ecosystem convention.

| Range | Category |
|-------|----------|
| -700 to -709 | Connection Errors |
| -710 to -719 | Transfer Errors |
| -720 to -739 | Chunk Errors |
| -740 to -749 | Storage Errors |
| -750 to -759 | File I/O Errors |
| -760 to -779 | Resume Errors |
| -780 to -789 | Compression Errors |
| -790 to -799 | Configuration Errors |

---

## Connection Errors (-700 to -709)

| Code | Name | Description | Resolution |
|------|------|-------------|------------|
| **-700** | `connection_failed` | Failed to connect to server | Check network connectivity and server address |
| **-701** | `connection_timeout` | Connection attempt timed out | Check network conditions, increase timeout |
| **-702** | `connection_refused` | Server refused connection | Verify server is running and port is correct |
| **-703** | `connection_lost` | Lost connection to server | Check network stability; auto-reconnect may recover |
| **-704** | `reconnect_failed` | Auto-reconnection failed after max attempts | Manual intervention required; check server availability |
| **-705** | `session_expired` | Server session has expired | Reconnect to establish new session |
| **-706** | `server_busy` | Server at maximum connections | Retry later or increase server capacity |
| **-707** | `protocol_mismatch` | Protocol version incompatible | Update client/server to compatible versions |

### Example: Handling Connection Errors

```cpp
auto result = client->connect(endpoint{"192.168.1.100", 19000});

if (!result) {
    switch (result.error().code()) {
        case error::connection_refused:
            std::cerr << "Server not available. Check server status.\n";
            break;
        case error::connection_timeout:
            std::cerr << "Connection timed out. Check network.\n";
            break;
        case error::server_busy:
            std::cerr << "Server at capacity. Retry in 30 seconds.\n";
            std::this_thread::sleep_for(30s);
            result = client->connect(endpoint);
            break;
        case error::protocol_mismatch:
            std::cerr << "Protocol version mismatch. Update required.\n";
            break;
        default:
            std::cerr << "Connection failed: " << result.error().message() << "\n";
    }
}
```

### Auto-Reconnect Behavior

When `auto_reconnect` is enabled, the client automatically handles connection loss:

```cpp
client->on_disconnected([](disconnect_reason reason) {
    switch (reason) {
        case disconnect_reason::network_error:
            // Auto-reconnect will attempt recovery
            std::cout << "Reconnecting...\n";
            break;
        case disconnect_reason::server_shutdown:
            // Server initiated disconnect
            std::cout << "Server shutting down.\n";
            break;
        case disconnect_reason::max_reconnects_exceeded:
            // Error code -704 will be reported
            std::cerr << "Auto-reconnect failed.\n";
            break;
    }
});
```

---

## Transfer Errors (-710 to -719)

| Code | Name | Description | Resolution |
|------|------|-------------|------------|
| **-710** | `transfer_init_failed` | Failed to initialize transfer | Check file access and server acceptance |
| **-711** | `transfer_cancelled` | Transfer cancelled by user | N/A - user initiated |
| **-712** | `transfer_timeout` | Transfer timed out | Check network conditions, increase timeout |
| **-713** | `upload_rejected` | Upload rejected by server | Check server policy or file restrictions |
| **-714** | `download_rejected` | Download rejected by server | Check file availability or access permissions |
| **-715** | `transfer_already_exists` | Transfer ID already in use | Use unique transfer ID |
| **-716** | `transfer_not_found` | Transfer ID not found | Verify transfer ID is correct |
| **-717** | `transfer_in_progress` | Another transfer for same file is active | Wait for existing transfer or cancel it |

### Example: Handling Transfer Errors

```cpp
// Upload example
auto upload = client->upload_file("/local/data.zip", "data.zip");

if (!upload) {
    switch (upload.error().code()) {
        case error::upload_rejected:
            std::cerr << "Server rejected upload: "
                      << upload.error().message() << "\n";
            break;
        case error::transfer_timeout:
            std::cerr << "Upload timed out. Attempting resume...\n";
            // Resume logic
            break;
        default:
            std::cerr << "Upload failed: " << upload.error().message() << "\n";
    }
}

// Download example
auto download = client->download_file("report.pdf", "/local/report.pdf");

if (!download) {
    switch (download.error().code()) {
        case error::download_rejected:
            std::cerr << "File not available for download\n";
            break;
        default:
            std::cerr << "Download failed: " << download.error().message() << "\n";
    }
}
```

---

## Chunk Errors (-720 to -739)

| Code | Name | Description | Resolution |
|------|------|-------------|------------|
| **-720** | `chunk_checksum_error` | Chunk CRC32 verification failed | Automatic retry typically handles this |
| **-721** | `chunk_sequence_error` | Chunk received out of expected sequence | Wait for missing chunks or request retransmission |
| **-722** | `chunk_size_error` | Chunk size exceeds maximum (1MB) | Verify sender configuration |
| **-723** | `file_hash_mismatch` | SHA-256 verification failed after assembly | Re-transfer file; source may have changed |
| **-724** | `chunk_timeout` | Chunk acknowledgment timeout | Network issues; retry or resume |
| **-725** | `chunk_duplicate` | Duplicate chunk received | Normal - chunks are deduplicated |

### Chunk Error Details

#### -720: chunk_checksum_error

Indicates data corruption during transfer. The CRC32 checksum of received data doesn't match the expected value.

**Automatic Handling:**
- The system automatically requests retransmission (CHUNK_NACK)
- No user intervention typically required

**Manual Intervention:**
```cpp
client->on_complete([](const transfer_result& result) {
    if (result.error && result.error->code() == error::chunk_checksum_error) {
        // Multiple chunks failed verification
        // Consider network issues or hardware problems
        log_error("Data integrity issues detected");
    }
});
```

#### -723: file_hash_mismatch

The complete file's SHA-256 hash doesn't match the expected value. This indicates:
- Source file changed during transfer
- Undetected chunk corruption
- Assembly error

**Resolution:**
```cpp
if (result.error && result.error->code() == error::file_hash_mismatch) {
    // Delete partial file and re-transfer
    std::filesystem::remove(result.local_path);
    // Re-initiate transfer
}
```

---

## Storage Errors (-740 to -749)

| Code | Name | Description | Resolution |
|------|------|-------------|------------|
| **-740** | `storage_error` | General storage error on server | Check server logs |
| **-741** | `storage_unavailable` | Server storage is temporarily unavailable | Retry later |
| **-742** | `storage_quota_exceeded` | Server storage quota exceeded | Administrator must free space or increase quota |
| **-743** | `max_file_size_exceeded` | File exceeds server's maximum allowed size | Split file or request larger limit |
| **-744** | `file_already_exists` | File with same name already exists | Use `overwrite_existing` option or rename file |
| **-745** | `storage_full` | Server disk is full | Administrator must free space |
| **-746** | `file_not_found_on_server` | Requested file not found on server | Verify filename; use `list_files()` to check |
| **-747** | `access_denied` | Access denied by server policy | Check permissions or contact administrator |
| **-748** | `invalid_filename` | Invalid filename (special chars, path traversal) | Use simple filenames without special characters |
| **-749** | `client_quota_exceeded` | Per-client quota exceeded | Wait or request quota increase |

### Example: Handling Storage Errors (Client Side)

```cpp
auto upload = client->upload_file("/local/large_file.zip", "large_file.zip");

if (!upload) {
    switch (upload.error().code()) {
        case error::file_already_exists:
            std::cerr << "File already exists. Overwriting...\n";
            upload = client->upload_file(
                "/local/large_file.zip",
                "large_file.zip",
                upload_options{.overwrite_existing = true}
            );
            break;
        case error::max_file_size_exceeded:
            std::cerr << "File too large for server. Max: "
                      << server_max_size << " bytes\n";
            break;
        case error::storage_quota_exceeded:
            std::cerr << "Server storage full. Contact administrator.\n";
            break;
        case error::invalid_filename:
            std::cerr << "Invalid filename. Use alphanumeric characters.\n";
            break;
        default:
            std::cerr << "Storage error: " << upload.error().message() << "\n";
    }
}
```

### Example: Handling Storage Errors (Server Side)

```cpp
server->on_upload_request([](const upload_request& req) {
    // Check custom quotas per client
    auto client_usage = get_client_usage(req.client.id);
    if (client_usage + req.file_size > per_client_quota) {
        // Will result in -749 error on client
        return false;
    }
    return true;
});
```

### Security Note: -748 invalid_filename

This error is returned when path traversal or invalid characters are detected:

```cpp
// These filenames will be rejected:
"../../../etc/passwd"        // Path traversal
"/absolute/path/file.txt"    // Absolute path
"file/../../../secret.txt"   // Embedded path traversal
".hidden_file"               // Hidden file (leading dot)
"file<name>.txt"             // Special characters
"CON"                        // Windows reserved name
"file\x00name.txt"           // Null character

// These filenames are acceptable:
"document.pdf"
"report-2024-q1.csv"
"backup_2024_12_11.zip"
```

---

## File I/O Errors (-750 to -759)

| Code | Name | Description | Resolution |
|------|------|-------------|------------|
| **-750** | `file_read_error` | Failed to read local file | Check file permissions and existence |
| **-751** | `file_write_error` | Failed to write local file | Check disk space and permissions |
| **-752** | `file_permission_error` | Insufficient file permissions | Adjust file/directory permissions |
| **-753** | `file_not_found` | Local source file not found | Verify file path |
| **-754** | `disk_full` | Local disk is full | Free disk space |
| **-755** | `directory_not_found` | Destination directory not found | Create destination directory |
| **-756** | `file_locked` | File is locked by another process | Close other applications using the file |

### Example: Handling I/O Errors

```cpp
auto upload = client->upload_file("/local/data.bin", "data.bin");

if (!upload) {
    switch (upload.error().code()) {
        case error::file_not_found:
            std::cerr << "Local file not found: /local/data.bin\n";
            break;
        case error::file_permission_error:
            std::cerr << "Cannot read file: permission denied\n";
            break;
        case error::file_locked:
            std::cerr << "File is in use by another process\n";
            break;
    }
}

auto download = client->download_file("report.pdf", "/local/downloads/report.pdf");

if (!download) {
    switch (download.error().code()) {
        case error::directory_not_found:
            std::filesystem::create_directories("/local/downloads");
            download = client->download_file("report.pdf", "/local/downloads/report.pdf");
            break;
        case error::disk_full:
            std::cerr << "Not enough disk space\n";
            break;
    }
}
```

---

## Resume Errors (-760 to -779)

| Code | Name | Description | Resolution |
|------|------|-------------|------------|
| **-760** | `resume_state_invalid` | Resume state file is corrupted | Delete state and start fresh |
| **-761** | `resume_file_changed` | Source file modified since last checkpoint | Start fresh transfer |
| **-762** | `resume_state_corrupted` | State data integrity check failed | Delete state and start fresh |
| **-763** | `resume_not_supported` | Server doesn't support resume for this transfer | Fall back to full transfer |
| **-764** | `resume_transfer_not_found` | Transfer ID not found for resume | Start fresh transfer |
| **-765** | `resume_session_mismatch` | Resume attempted from different session | Reconnect and try again |

### Resume Error Handling

```cpp
// Automatic resume on reconnection
client->with_reconnect_policy(reconnect_policy{
    .resume_transfers = true  // Automatically resume interrupted transfers
});

// Manual resume handling
client->on_disconnected([&](disconnect_reason reason) {
    if (reason == disconnect_reason::network_error) {
        // Transfers will resume automatically when reconnected
        std::cout << "Connection lost. Will resume transfers on reconnect.\n";
    }
});

// Handling resume errors
auto result = client->resume_transfer(transfer_id);

if (!result) {
    switch (result.error().code()) {
        case error::resume_file_changed:
            std::cerr << "Source file changed. Starting fresh transfer.\n";
            client->upload_file(path, remote_name);
            break;
        case error::resume_transfer_not_found:
            std::cerr << "Transfer not found. Starting fresh.\n";
            client->upload_file(path, remote_name);
            break;
    }
}
```

---

## Compression Errors (-780 to -789)

| Code | Name | Description | Resolution |
|------|------|-------------|------------|
| **-780** | `compression_failed` | LZ4 compression failed | Rare; check input data |
| **-781** | `decompression_failed` | LZ4 decompression failed | Data corruption; request retransmission |
| **-782** | `compression_buffer_error` | Output buffer too small | Internal error; report bug |
| **-783** | `invalid_compression_data` | Compressed data is malformed | Data corruption; request retransmission |

### Compression Error Details

#### -781: decompression_failed

Most common compression error. Indicates:
- Corrupted compressed data
- Truncated chunk
- Protocol desync

**Automatic Handling:**
- System falls back to uncompressed transfer for affected chunks
- Retransmission requested automatically

```cpp
// Monitor compression health
auto stats = client->get_compression_stats();
double failure_rate =
    static_cast<double>(stats.chunks_skipped) /
    static_cast<double>(stats.chunks_compressed + stats.chunks_skipped);

if (failure_rate > 0.01) {  // >1% failure
    log_warning("High compression failure rate: {}%", failure_rate * 100);
    // Consider disabling compression temporarily
}
```

---

## Configuration Errors (-790 to -799)

| Code | Name | Description | Resolution |
|------|------|-------------|------------|
| **-790** | `config_invalid` | Invalid configuration parameter | Check configuration values |
| **-791** | `config_chunk_size_error` | Chunk size out of valid range | Use 64KB - 1MB |
| **-792** | `config_transport_error` | Transport configuration error | Check transport settings |
| **-793** | `config_storage_path_error` | Invalid storage directory | Ensure directory exists and is writable |
| **-794** | `config_quota_error` | Invalid quota configuration | Quota must be > 0 |
| **-795** | `config_reconnect_error` | Invalid reconnect policy | Check reconnect parameters |

### Configuration Validation

```cpp
// Server configuration errors
auto server = file_transfer_server::builder()
    .with_storage_directory("/nonexistent/path")  // Error: -793
    .build();

if (!server) {
    std::cerr << "Server config error: " << server.error().message() << "\n";
}

// Client configuration errors
auto client = file_transfer_client::builder()
    .with_chunk_size(32 * 1024)  // Error: -791 (32KB < 64KB minimum)
    .build();

if (!client) {
    std::cerr << "Client config error: " << client.error().message() << "\n";
}

// Valid configurations
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")  // Valid
    .with_max_connections(100)
    .build();

auto client = file_transfer_client::builder()
    .with_chunk_size(256 * 1024)  // Valid: 256KB
    .with_auto_reconnect(true)
    .build();
```

---

## Error Helper Functions

### Get Error Message

```cpp
namespace kcenon::file_transfer::error {

[[nodiscard]] auto error_message(int code) -> std::string_view;

}

// Usage
int code = -744;
std::cout << "Error: " << error::error_message(code) << "\n";
// Output: "Error: File already exists on server"
```

### Error Categories

```cpp
namespace kcenon::file_transfer::error {

[[nodiscard]] auto is_connection_error(int code) -> bool {
    return code >= -709 && code <= -700;
}

[[nodiscard]] auto is_transfer_error(int code) -> bool {
    return code >= -719 && code <= -710;
}

[[nodiscard]] auto is_chunk_error(int code) -> bool {
    return code >= -739 && code <= -720;
}

[[nodiscard]] auto is_storage_error(int code) -> bool {
    return code >= -749 && code <= -740;
}

[[nodiscard]] auto is_io_error(int code) -> bool {
    return code >= -759 && code <= -750;
}

[[nodiscard]] auto is_resume_error(int code) -> bool {
    return code >= -779 && code <= -760;
}

[[nodiscard]] auto is_compression_error(int code) -> bool {
    return code >= -789 && code <= -780;
}

[[nodiscard]] auto is_config_error(int code) -> bool {
    return code >= -799 && code <= -790;
}

[[nodiscard]] auto is_retryable(int code) -> bool;  // Can operation be retried?

[[nodiscard]] auto is_client_error(int code) -> bool;   // Client-side issue
[[nodiscard]] auto is_server_error(int code) -> bool;   // Server-side issue

}
```

### Example: Comprehensive Error Handling

```cpp
void handle_transfer_error(const error& err, file_transfer_client& client) {
    int code = err.code();

    if (error::is_connection_error(code)) {
        // Connection issues - auto-reconnect may help
        std::cerr << "Connection issue: " << err.message() << "\n";
        // Auto-reconnect handles most cases
        return;
    }

    if (error::is_storage_error(code)) {
        // Server storage issues
        switch (code) {
            case error::file_already_exists:
                // Prompt user to overwrite or rename
                break;
            case error::storage_full:
            case error::storage_quota_exceeded:
                // Contact administrator
                break;
            case error::invalid_filename:
                // Sanitize filename and retry
                break;
        }
        return;
    }

    if (error::is_retryable(code)) {
        std::cerr << "Retryable error: " << err.message() << "\n";
        // Implement retry logic
        return;
    }

    // Non-retryable errors
    std::cerr << "Fatal error: " << err.message() << "\n";
}
```

---

## Best Practices

### 1. Always Check Results

```cpp
auto result = client->upload_file(path, remote_name);
if (!result) {
    handle_transfer_error(result.error(), *client);
}
```

### 2. Log Errors with Context

```cpp
if (!result) {
    log_error("Transfer failed: code={}, message={}, file={}, server={}",
              result.error().code(),
              result.error().message(),
              path.string(),
              server_address);
}
```

### 3. Use Auto-Reconnect for Connection Errors

```cpp
auto client = file_transfer_client::builder()
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy{
        .max_attempts = 10,
        .resume_transfers = true
    })
    .build();

client->on_disconnected([](disconnect_reason reason) {
    if (reason == disconnect_reason::max_reconnects_exceeded) {
        // Only manual intervention needed here
        alert_user("Connection lost. Please check network.");
    }
});
```

### 4. Implement Retry Logic for Transient Errors

```cpp
constexpr int max_retries = 3;
int retries = 0;

Result<transfer_handle> result;
do {
    result = client->upload_file(path, remote_name);
    if (!result && error::is_retryable(result.error().code())) {
        auto delay = std::chrono::seconds(1 << retries);  // Exponential backoff
        std::this_thread::sleep_for(delay);
        retries++;
    } else {
        break;
    }
} while (retries < max_retries);
```

### 5. Handle Resume Errors Gracefully

```cpp
// Resume is handled automatically with reconnect_policy.resume_transfers = true
// For manual control:
auto resume_result = client->resume_transfer(id);
if (!resume_result && error::is_resume_error(resume_result.error().code())) {
    // Fall back to fresh transfer
    return client->upload_file(path, remote_name);
}
```

---

## Error Code Quick Reference Table

| Code | Name | Category | Retryable |
|------|------|----------|-----------|
| -700 | connection_failed | Connection | Yes |
| -701 | connection_timeout | Connection | Yes |
| -702 | connection_refused | Connection | Yes |
| -703 | connection_lost | Connection | Yes (auto) |
| -704 | reconnect_failed | Connection | No |
| -710 | transfer_init_failed | Transfer | Yes |
| -711 | transfer_cancelled | Transfer | No |
| -712 | transfer_timeout | Transfer | Yes |
| -713 | upload_rejected | Transfer | No |
| -714 | download_rejected | Transfer | No |
| -720 | chunk_checksum_error | Chunk | Yes (auto) |
| -723 | file_hash_mismatch | Chunk | Yes |
| -744 | file_already_exists | Storage | No* |
| -745 | storage_full | Storage | No |
| -746 | file_not_found_on_server | Storage | No |
| -747 | access_denied | Storage | No |
| -748 | invalid_filename | Storage | No |
| -750 | file_read_error | File I/O | No |
| -753 | file_not_found | File I/O | No |
| -760 | resume_state_invalid | Resume | No** |
| -780 | compression_failed | Compression | Yes (fallback) |
| -791 | config_chunk_size_error | Config | No |

*Can retry with `overwrite_existing = true`
**Can fall back to fresh transfer

---

*Last updated: 2025-12-11*
*Version: 2.0.0*
