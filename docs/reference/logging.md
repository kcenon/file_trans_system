# Logging System

This document describes the logging system integrated into the File Transfer System.

## Overview

The File Transfer System provides a comprehensive logging infrastructure that supports:
- Log levels (TRACE, DEBUG, INFO, WARN, ERROR, FATAL)
- Log categories for different components
- Structured logging with JSON format
- Runtime log level configuration
- Optional integration with `logger_system`

## Log Categories

The following log categories are defined for different components:

| Category | Description |
|----------|-------------|
| `file_transfer.server` | Server-side operations |
| `file_transfer.client` | Client-side operations |
| `file_transfer.pipeline` | Pipeline processing |
| `file_transfer.compression` | Compression operations |
| `file_transfer.resume` | Resume/recovery operations |
| `file_transfer.transfer` | Transfer operations |
| `file_transfer.chunk` | Chunk processing (TRACE level) |

## Log Levels

Log levels in order of severity:

| Level | Value | Description |
|-------|-------|-------------|
| TRACE | 0 | Detailed debug information (chunk processing) |
| DEBUG | 1 | Development debug information |
| INFO | 2 | General operational information |
| WARN | 3 | Warnings (recoverable issues) |
| ERROR | 4 | Errors (operation failures) |
| FATAL | 5 | Critical errors |

## Usage

### Basic Logging

```cpp
#include <kcenon/file_transfer/core/logging.h>

using namespace kcenon::file_transfer;

// Simple logging
FT_LOG_INFO(log_category::client, "Connected to server");
FT_LOG_ERROR(log_category::server, "Failed to accept connection");
FT_LOG_DEBUG(log_category::pipeline, "Processing stage completed");
```

### Structured Logging with Context

```cpp
// Create a transfer log context
transfer_log_context ctx;
ctx.transfer_id = "abc123";
ctx.filename = "data.zip";
ctx.file_size = 1048576;
ctx.total_chunks = 100;

// Log with context (produces JSON output)
FT_LOG_INFO_CTX(log_category::client, "Upload started", ctx);

// Output:
// 2025-12-13 10:30:00.123 [INFO] [file_transfer.client] Upload started
// {"transfer_id":"abc123","filename":"data.zip","file_size":1048576,"total_chunks":100}
```

### Available Context Fields

```cpp
struct transfer_log_context {
    std::string transfer_id;
    std::string filename;
    std::optional<uint64_t> file_size;
    std::optional<uint64_t> bytes_transferred;
    std::optional<uint32_t> chunk_index;
    std::optional<uint32_t> total_chunks;
    std::optional<double> progress_percent;
    std::optional<double> rate_mbps;
    std::optional<uint64_t> duration_ms;
    std::optional<std::string> error_message;
    std::optional<std::string> client_id;
    std::optional<std::string> server_address;
};
```

## Runtime Configuration

### Setting Log Level

```cpp
// Set minimum log level
get_logger().set_level(log_level::debug);

// Get current log level
log_level current = get_logger().get_level();

// Check if level is enabled
if (get_logger().is_enabled(log_level::trace)) {
    // Expensive trace logging
}
```

### Initializing the Logger

The logger is **automatically initialized** when creating `file_transfer_server` or `file_transfer_client` instances. Manual initialization is only needed if you want to configure the logger before creating these objects.

```cpp
// Manual initialization (optional - called automatically by server/client)
get_logger().initialize();  // Safe to call multiple times

// Shutdown (recommended at application exit)
get_logger().shutdown();

// Check status
if (get_logger().is_initialized()) {
    // Logger is ready
}
```

**Note**: `initialize()` is thread-safe and idempotent. Multiple calls have no effect after the first successful initialization.

### Custom Log Callback

```cpp
// Set a custom callback for log processing
get_logger().set_callback([](log_level level,
                              std::string_view category,
                              std::string_view message,
                              const transfer_log_context* ctx) {
    // Custom log handling
    // e.g., send to monitoring system
});
```

## Integration with logger_system

When both `BUILD_WITH_LOGGER_SYSTEM` and `BUILD_WITH_COMMON_SYSTEM` are enabled,
the logging system automatically integrates with `logger_system` for:

- Asynchronous logging
- Console and file output
- Log rotation
- Performance metrics

If `logger_system` is not available, logs are written to stderr with timestamps.

## Logging Macros

### Simple Logging Macros

```cpp
FT_LOG_TRACE(category, message)
FT_LOG_DEBUG(category, message)
FT_LOG_INFO(category, message)
FT_LOG_WARN(category, message)
FT_LOG_ERROR(category, message)
FT_LOG_FATAL(category, message)
```

### Context Logging Macros

```cpp
FT_LOG_TRACE_CTX(category, message, context)
FT_LOG_DEBUG_CTX(category, message, context)
FT_LOG_INFO_CTX(category, message, context)
FT_LOG_WARN_CTX(category, message, context)
FT_LOG_ERROR_CTX(category, message, context)
FT_LOG_FATAL_CTX(category, message, context)
```

## Output Format

### Standard Output

```
2025-12-13 10:30:00.123 [INFO] [file_transfer.client] Connected to server
```

### With Context (JSON)

```
2025-12-13 10:30:00.123 [INFO] [file_transfer.client] Upload completed {"transfer_id":"abc123","filename":"data.zip","file_size":1048576,"duration_ms":500,"rate_mbps":2.0}
```

## Log Points by Module

The following log points are implemented across all modules:

### Server Module (`file_transfer.server`)

| Event | Level | Description |
|-------|-------|-------------|
| Server start | INFO | Server starting with address and port |
| Server started | INFO | Server successfully started |
| Server stop | INFO | Server stopping |
| Server stopped | INFO | Server successfully stopped |
| Client connected | INFO | Client connection event |
| Client disconnected | INFO | Client disconnection event |
| Upload rejected | WARN | Upload rejected due to size/quota |
| Callback registered | DEBUG | Callback registration events |
| Start/stop failures | WARN/ERROR | Server lifecycle errors |

### Client Module (`file_transfer.client`)

| Event | Level | Description |
|-------|-------|-------------|
| Connect to server | INFO | Connection attempt |
| Connected | INFO | Successfully connected |
| Disconnect | INFO | Disconnection event |
| Upload started | INFO | Upload initiated with context |
| Download started | INFO | Download initiated with context |
| Download completed | INFO | Download finished with metrics |
| Download failed | ERROR | Download error with reason |
| Download cancelled | INFO | User-initiated cancellation |
| Reconnection attempt | INFO | Auto-reconnect in progress |
| Reconnection success | INFO | Reconnection successful |
| Reconnection failed | WARN | Reconnection failed |

### Pipeline Module (`file_transfer.pipeline`)

| Event | Level | Description |
|-------|-------|-------------|
| Pipeline start | INFO | Pipeline starting with config |
| Pipeline started | INFO | Pipeline successfully started |
| Pipeline stop | INFO | Pipeline stopping |
| Pipeline stopped | INFO | Pipeline stopped with stats |
| Worker started | DEBUG | Worker thread started |
| Worker stopped | DEBUG | Worker thread stopped |
| Chunk processing | TRACE | Chunk entering each stage |
| Chunk decompressed | TRACE | Decompression completed |
| Chunk verified | TRACE | Checksum verification passed |
| Chunk written | TRACE | Chunk written to file |
| Chunk read | TRACE | Chunk read from file |
| Chunk compressed | TRACE | Compression with ratio |
| Chunk sent | TRACE | Chunk sent to network |
| Checksum mismatch | ERROR | Verification failed |
| File operation error | ERROR | Read/write failures |

### Resume Handler Module (`file_transfer.resume`)

| Event | Level | Description |
|-------|-------|-------------|
| State saved | DEBUG | Transfer state persisted |
| State persisted | TRACE | File write completed |
| State loaded | DEBUG | Transfer state recovered |
| State from cache | TRACE | Cache hit |
| State deleted | DEBUG | Transfer state removed |
| State not found | DEBUG | State file missing |
| State recovered | DEBUG | Recovery with progress info |
| Cleanup started | INFO | Expired state cleanup |
| Cleanup completed | INFO | Cleanup results |
| State expired | DEBUG | Individual state expired |
| List transfers | INFO | Resumable transfer listing |
| Deserialization error | ERROR | State parse failure |

### Compression Module (`file_transfer.compression`)

| Event | Level | Description |
|-------|-------|-------------|
| Compressed | TRACE | Compression with ratio |
| Decompressed | TRACE | Decompression completed |
| Compression failed | ERROR | LZ4 compression error |
| Decompression failed | ERROR | LZ4 decompression error |
| Size mismatch | ERROR | Decompressed size mismatch |
| Pre-compressed detected | TRACE | Format detection skip |
| Compressibility check | TRACE | Sample analysis result |
| LZ4 not enabled | WARN | Build without LZ4 |

## Best Practices

1. **Use appropriate log levels**: TRACE for chunk-level details, DEBUG for development, INFO for operations, WARN/ERROR for issues.

2. **Include context**: Use `transfer_log_context` for transfer-related logs to enable structured analysis.

3. **Check level before expensive operations**:
   ```cpp
   if (get_logger().is_enabled(log_level::trace)) {
       // Build expensive debug message
       FT_LOG_TRACE(category, expensive_message);
   }
   ```

4. **Automatic initialization**: The logger is automatically initialized when creating server or client instances. Manual `initialize()` calls are optional and safe to use for early configuration.

5. **Shutdown properly**: Call `get_logger().shutdown()` before exit to flush pending logs.

6. **Performance impact**: Log points are designed for minimal performance overhead (< 1%). TRACE level logs are disabled by default.
