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

### Per-Category Log Level Settings

You can set different log levels for different categories, allowing fine-grained control over logging verbosity.

```cpp
// Set debug level for client category, while keeping others at info
get_logger().set_level(log_level::info);
get_logger().set_category_level(log_category::client, log_level::debug);
get_logger().set_category_level(log_category::chunk, log_level::trace);

// Check if a level is enabled for a specific category
if (get_logger().is_enabled_for(log_level::debug, log_category::client)) {
    // Debug logging for client category is enabled
}

// Get the effective level for a category
log_level client_level = get_logger().get_category_level(log_category::client);

// Clear a category-specific level (reverts to global level)
get_logger().clear_category_level(log_category::client);

// Clear all category-specific levels
get_logger().clear_all_category_levels();

// Get all category-specific levels
auto levels = get_logger().get_all_category_levels();
for (const auto& [category, level] : levels) {
    std::cout << category << ": " << log_level_to_string(level) << "\n";
}
```

### Output Destination Settings

Control where log messages are written: console, file, both, or none (callbacks only).

```cpp
// Set output destination
get_logger().set_output_destination(log_output_destination::console);  // Default
get_logger().set_output_destination(log_output_destination::file);
get_logger().set_output_destination(log_output_destination::both);
get_logger().set_output_destination(log_output_destination::none);  // Only callbacks

// Enable/disable console output
get_logger().set_console_output(true);
if (get_logger().is_console_output_enabled()) {
    // Console output is active
}

// Enable file output with path
get_logger().set_file_output("/var/log/file_transfer.log");
get_logger().set_file_output("/var/log/file_transfer.log", false);  // Overwrite mode

// File output with configuration
file_output_config config;
config.file_path = "/var/log/file_transfer.log";
config.append = true;
config.max_file_size = 10 * 1024 * 1024;  // 10 MB
config.rotate_on_size = true;
get_logger().set_file_output(config);

// Check file output status
if (get_logger().is_file_output_enabled()) {
    std::string path = get_logger().get_file_output_path();
}

// Disable file output
get_logger().disable_file_output();
```

### Configuration API

Apply complete logger configuration at once using the `log_config` struct.

```cpp
// Create and apply a complete configuration
log_config config;
config.global_level = log_level::debug;
config.category_levels[std::string(log_category::client)] = log_level::trace;
config.category_levels[std::string(log_category::chunk)] = log_level::warn;
config.format = log_output_format::json;
config.destination = log_output_destination::both;
config.file_config.file_path = "/var/log/file_transfer.log";
config.file_config.append = true;
config.file_config.max_file_size = 50 * 1024 * 1024;  // 50 MB
config.file_config.rotate_on_size = true;
config.masking = masking_config::all_masked();

bool success = get_logger().configure(config);

// Get current configuration
log_config current = get_logger().get_config();

// Use default configuration
log_config defaults = log_config::defaults();
```

### Output Destinations

| Destination | Description |
|-------------|-------------|
| `console` | Output to stderr only (default) |
| `file` | Output to file only |
| `both` | Output to both console and file |
| `none` | Disable output (only callbacks are called) |

### File Output Configuration

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `file_path` | string | "" | Path to log file |
| `append` | bool | true | Append to file or overwrite |
| `max_file_size` | size_t | 0 | Max file size in bytes (0 = unlimited) |
| `rotate_on_size` | bool | false | Rotate file when max size reached |

## Integration with logger_system

When `KCENON_WITH_LOGGER_SYSTEM` is enabled (or legacy `BUILD_WITH_LOGGER_SYSTEM` and `BUILD_WITH_COMMON_SYSTEM`),
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

## JSON Output Format

The logging system supports complete JSON output for structured logging analysis.

### Enabling JSON Output

```cpp
// Enable JSON output format
get_logger().enable_json_output(true);

// Or set output format directly
get_logger().set_output_format(log_output_format::json);

// Check if JSON output is enabled
if (get_logger().is_json_output_enabled()) {
    // JSON format is active
}
```

### JSON Output Example

```json
{
  "timestamp": "2025-12-13T10:30:00.123Z",
  "level": "INFO",
  "category": "file_transfer.client",
  "message": "Upload completed",
  "transfer_id": "abc-123",
  "filename": "data.zip",
  "size": 1048576,
  "duration_ms": 500,
  "rate_mbps": 2.0
}
```

### JSON Callback

```cpp
// Set a callback for JSON log entries
get_logger().set_json_callback([](const structured_log_entry& entry,
                                   const std::string& json) {
    // Send JSON to external system
    send_to_elk_stack(json);
});
```

## Log Entry Builder

The `log_entry_builder` class provides a fluent API for creating structured log entries.

### Basic Usage

```cpp
auto entry = log_entry_builder()
    .with_level(log_level::info)
    .with_category(log_category::client)
    .with_message("Upload completed")
    .with_transfer_id("abc-123")
    .with_filename("data.zip")
    .with_file_size(1048576)
    .with_duration_ms(500)
    .with_rate_mbps(2.0)
    .build();

// Log the entry
get_logger().log(entry);

// Or get JSON directly
std::string json = entry.to_json();
```

### Available Builder Methods

```cpp
log_entry_builder()
    // Required fields
    .with_level(log_level level)
    .with_category(std::string_view category)
    .with_message(std::string_view message)

    // Transfer context
    .with_transfer_id(std::string_view id)
    .with_filename(std::string_view filename)
    .with_file_size(uint64_t size)
    .with_bytes_transferred(uint64_t bytes)
    .with_chunk_index(uint32_t index)
    .with_total_chunks(uint32_t total)
    .with_progress_percent(double percent)
    .with_rate_mbps(double rate)
    .with_duration_ms(uint64_t duration)
    .with_error_message(std::string_view error)
    .with_client_id(std::string_view id)
    .with_server_address(std::string_view address)

    // Source location
    .with_source_location(const char* file, int line, const char* function)

    // Existing context
    .with_context(const transfer_log_context& ctx)

    // Build methods
    .build()           // Returns structured_log_entry
    .build_json()      // Returns JSON string
    .build_json_masked(const sensitive_info_masker& masker)  // With masking
```

## Sensitive Information Masking

The logging system supports masking sensitive information such as IP addresses and file paths.

### Enabling Masking

```cpp
// Enable all masking
get_logger().enable_masking(true);

// Or configure specific masking options
masking_config config;
config.mask_paths = true;      // Mask file paths
config.mask_ips = true;        // Mask IP addresses
config.mask_filenames = true;  // Mask filenames
config.visible_chars = 4;      // Keep first 4 characters visible
config.mask_char = "*";        // Character for masking

get_logger().set_masking_config(config);
```

### Masking Examples

**IP Address Masking:**
```
Before: Connection from 192.168.1.100
After:  Connection from *********.100
```

**Path Masking:**
```
Before: /home/user/documents/secret.txt
After:  ********************/secret.txt
```

**Filename Masking (with mask_filenames=true):**
```
Before: confidential_report.pdf
After:  conf****************.pdf
```

### Using the Masker Directly

```cpp
// Create a masker with configuration
masking_config config = masking_config::all_masked();
sensitive_info_masker masker(config);

// Mask a string
std::string masked = masker.mask("Connection from 192.168.1.100");

// Mask specific types
std::string masked_ip = masker.mask_ip("192.168.1.100");
std::string masked_path = masker.mask_path("/home/user/file.txt");

// Use with log entry builder
std::string json = log_entry_builder()
    .with_level(log_level::info)
    .with_category(log_category::client)
    .with_message("Connected to 192.168.1.100")
    .with_server_address("192.168.1.100")
    .build_json_masked(masker);
```

### Predefined Configurations

```cpp
// All masking enabled
masking_config config = masking_config::all_masked();

// No masking (default)
masking_config config = masking_config::none();
```

## Output Format

### Standard Text Output

```
2025-12-13 10:30:00.123 [INFO] [file_transfer.client] Connected to server
```

### With Context (Text)

```
2025-12-13 10:30:00.123 [INFO] [file_transfer.client] Upload completed {"transfer_id":"abc123","filename":"data.zip","size":1048576,"duration_ms":500,"rate_mbps":2.0}
```

### JSON Output

```json
{"timestamp":"2025-12-13T10:30:00.123Z","level":"INFO","category":"file_transfer.client","message":"Upload completed","transfer_id":"abc123","filename":"data.zip","size":1048576,"duration_ms":500,"rate_mbps":2.0}
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
