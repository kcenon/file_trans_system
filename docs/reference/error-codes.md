# Error Codes Reference

Complete reference of error codes used in the **file_trans_system** library.

## Overview

Error codes are in the range **-700 to -799**, following the ecosystem convention.

| Range | Category |
|-------|----------|
| -700 to -719 | Transfer Errors |
| -720 to -739 | Chunk Errors |
| -740 to -759 | File I/O Errors |
| -760 to -779 | Resume Errors |
| -780 to -789 | Compression Errors |
| -790 to -799 | Configuration Errors |

---

## Transfer Errors (-700 to -719)

| Code | Name | Description | Resolution |
|------|------|-------------|------------|
| **-700** | `transfer_init_failed` | Failed to initialize transfer | Check network connectivity and endpoint availability |
| **-701** | `transfer_cancelled` | Transfer cancelled by user | N/A - user initiated |
| **-702** | `transfer_timeout` | Transfer timed out | Check network conditions, increase timeout |
| **-703** | `transfer_rejected` | Transfer rejected by receiver | Verify receiver accept policy |
| **-704** | `transfer_already_exists` | Transfer ID already in use | Use unique transfer ID |
| **-705** | `transfer_not_found` | Transfer ID not found | Verify transfer ID is correct |

### Example: Handling Transfer Errors

```cpp
auto result = sender->send_file(path, endpoint);

if (!result) {
    switch (result.error().code()) {
        case error::transfer_timeout:
            std::cerr << "Transfer timed out. Attempting resume...\n";
            // Retry with resume
            break;
        case error::transfer_rejected:
            std::cerr << "Receiver rejected transfer\n";
            break;
        default:
            std::cerr << "Transfer failed: " << result.error().message() << "\n";
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
- The system automatically requests retransmission
- No user intervention typically required

**Manual Intervention:**
```cpp
receiver->on_complete([](const transfer_result& result) {
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
    std::filesystem::remove(result.output_path);
    // Re-initiate transfer
}
```

---

## File I/O Errors (-740 to -759)

| Code | Name | Description | Resolution |
|------|------|-------------|------------|
| **-740** | `file_read_error` | Failed to read source file | Check file permissions and existence |
| **-741** | `file_write_error` | Failed to write destination file | Check disk space and permissions |
| **-742** | `file_permission_error` | Insufficient file permissions | Adjust file/directory permissions |
| **-743** | `file_not_found` | Source file not found | Verify file path |
| **-744** | `disk_full` | Insufficient disk space | Free disk space or change destination |
| **-745** | `invalid_path` | Invalid file path (path traversal attempt) | Use safe file names |

### Example: Handling I/O Errors

```cpp
auto result = sender->send_file(path, endpoint);

if (!result) {
    switch (result.error().code()) {
        case error::file_not_found:
            std::cerr << "File not found: " << path << "\n";
            break;
        case error::file_permission_error:
            std::cerr << "Permission denied: " << path << "\n";
            break;
        case error::disk_full:
            std::cerr << "Disk full. Need "
                      << bytes_needed << " bytes\n";
            break;
    }
}
```

### Security Note: -745 invalid_path

This error is returned when path traversal is detected:

```cpp
// These paths will be rejected:
"../../../etc/passwd"
"/absolute/path/file.txt"
"file/../../../secret.txt"

// These paths are acceptable:
"document.pdf"
"subdir/file.txt"
"reports/2024/q1.csv"
```

---

## Resume Errors (-760 to -779)

| Code | Name | Description | Resolution |
|------|------|-------------|------------|
| **-760** | `resume_state_invalid` | Resume state file is corrupted | Delete state and start fresh |
| **-761** | `resume_file_changed` | Source file modified since last checkpoint | Start fresh transfer |
| **-762** | `resume_state_corrupted` | State data integrity check failed | Delete state and start fresh |
| **-763** | `resume_not_supported` | Remote endpoint doesn't support resume | Fall back to full transfer |

### Resume Error Handling

```cpp
auto result = sender->resume(transfer_id);

if (!result) {
    switch (result.error().code()) {
        case error::resume_file_changed:
            std::cerr << "File changed since last transfer. "
                      << "Starting fresh transfer.\n";
            // Clear state and restart
            resume_handler->delete_state(transfer_id);
            sender->send_file(path, endpoint);
            break;

        case error::resume_state_corrupted:
            std::cerr << "Resume state corrupted. "
                      << "Starting fresh transfer.\n";
            resume_handler->delete_state(transfer_id);
            sender->send_file(path, endpoint);
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
auto stats = manager->get_compression_stats();
double failure_rate =
    static_cast<double>(decompression_failures) /
    static_cast<double>(stats.chunks_compressed);

if (failure_rate > 0.01) {  // >1% failure
    log_warning("High decompression failure rate: {}%",
                failure_rate * 100);
    // Consider disabling compression temporarily
    manager->set_default_compression(compression_mode::disabled);
}
```

---

## Configuration Errors (-790 to -799)

| Code | Name | Description | Resolution |
|------|------|-------------|------------|
| **-790** | `config_invalid` | Invalid configuration parameter | Check configuration values |
| **-791** | `config_chunk_size_error` | Chunk size out of valid range | Use 64KB - 1MB |
| **-792** | `config_transport_error` | Transport configuration error | Check transport settings |

### Configuration Validation

```cpp
// Chunk size must be 64KB - 1MB
auto sender = file_sender::builder()
    .with_chunk_size(32 * 1024)  // Error: too small (32KB < 64KB)
    .build();

if (!sender) {
    // Error code: -791 (config_chunk_size_error)
    std::cerr << sender.error().message() << "\n";
}

// Valid configuration
auto sender = file_sender::builder()
    .with_chunk_size(256 * 1024)  // OK: 256KB
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
int code = -720;
std::cout << "Error: " << error::error_message(code) << "\n";
// Output: "Error: Chunk CRC32 verification failed"
```

### Error Categories

```cpp
namespace kcenon::file_transfer::error {

[[nodiscard]] auto is_transfer_error(int code) -> bool {
    return code >= -719 && code <= -700;
}

[[nodiscard]] auto is_chunk_error(int code) -> bool {
    return code >= -739 && code <= -720;
}

[[nodiscard]] auto is_io_error(int code) -> bool {
    return code >= -759 && code <= -740;
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

}
```

---

## Best Practices

### 1. Always Check Results

```cpp
auto result = sender->send_file(path, endpoint);
if (!result) {
    handle_error(result.error());
}
```

### 2. Log Errors with Context

```cpp
if (!result) {
    log_error("Transfer failed: code={}, message={}, file={}, endpoint={}",
              result.error().code(),
              result.error().message(),
              path.string(),
              endpoint.address);
}
```

### 3. Implement Retry Logic for Transient Errors

```cpp
constexpr int max_retries = 3;
int retries = 0;

Result<transfer_handle> result;
do {
    result = sender->send_file(path, endpoint);
    if (!result && error::is_retryable(result.error().code())) {
        std::this_thread::sleep_for(std::chrono::seconds(1 << retries));
        retries++;
    } else {
        break;
    }
} while (retries < max_retries);
```

### 4. Handle Resume Errors Gracefully

```cpp
auto resume_result = sender->resume(id);
if (!resume_result && error::is_resume_error(resume_result.error().code())) {
    // Fall back to fresh transfer
    resume_handler->delete_state(id);
    return sender->send_file(path, endpoint);
}
```

---

*Last updated: 2025-12-11*
