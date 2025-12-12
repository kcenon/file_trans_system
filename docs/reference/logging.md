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

```cpp
// Initialize (enables logger_system integration if available)
get_logger().initialize();

// Shutdown
get_logger().shutdown();

// Check status
if (get_logger().is_initialized()) {
    // Logger is ready
}
```

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

4. **Initialize early**: Call `get_logger().initialize()` at application startup for optimal performance.

5. **Shutdown properly**: Call `get_logger().shutdown()` before exit to flush pending logs.
