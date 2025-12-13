# API Reference

Complete API documentation for the **file_trans_system** library.

**Version**: 0.2.1
**Last Updated**: 2025-12-13

## Table of Contents

1. [Core Classes](#core-classes)
   - [file_transfer_server](#file_transfer_server)
   - [file_transfer_client](#file_transfer_client)
   - [transfer_manager](#transfer_manager)
   - [quota_manager](#quota_manager)
2. [Data Types](#data-types)
   - [Enumerations](#enumerations)
   - [Structures](#structures)
3. [Chunk Management](#chunk-management)
4. [Resume Handler](#resume-handler)
   - [transfer_state](#transfer_state)
   - [resume_handler_config](#resume_handler_config)
   - [resume_handler](#resume_handler)
5. [Compression](#compression)
6. [Pipeline](#pipeline)
7. [Transport](#transport)

---

## Core Classes

### file_transfer_server

Central server class for managing file storage and client connections.

#### Builder Pattern

```cpp
namespace kcenon::file_transfer {

class file_transfer_server {
public:
    class builder {
    public:
        // Set the storage directory for uploaded files
        builder& with_storage_directory(const std::filesystem::path& dir);

        // Set maximum number of concurrent client connections
        builder& with_max_connections(std::size_t max_count);

        // Set maximum file size allowed for uploads
        builder& with_max_file_size(uint64_t max_size);

        // Set total storage quota
        builder& with_storage_quota(uint64_t quota);

        // Configure pipeline worker counts and queue sizes
        builder& with_pipeline_config(const pipeline_config& config);

        // Set transport type (tcp, quic)
        builder& with_transport(transport_type type);

        // Build the server instance
        [[nodiscard]] auto build() -> Result<file_transfer_server>;
    };
};

}
```

#### Methods

##### start() / stop()

Start and stop the server.

```cpp
[[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;
[[nodiscard]] auto stop() -> Result<void>;
```

**Example:**
```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_max_connections(100)
    .with_max_file_size(10ULL * 1024 * 1024 * 1024)  // 10GB
    .with_storage_quota(1ULL * 1024 * 1024 * 1024 * 1024)  // 1TB
    .build();

if (server) {
    auto result = server->start(endpoint{"0.0.0.0", 19000});
    if (result) {
        std::cout << "Server started on port 19000\n";
    }

    // ... server running ...

    server->stop();
}
```

##### list_stored_files()

List files in the storage directory.

```cpp
[[nodiscard]] auto list_stored_files(
    const list_options& options = {}
) -> Result<std::vector<file_info>>;
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `options` | `list_options` | Filtering, pagination, and sorting options |

**Returns:** `Result<std::vector<file_info>>` - List of files with metadata

##### delete_file()

Delete a file from storage.

```cpp
[[nodiscard]] auto delete_file(const std::string& filename) -> Result<void>;
```

##### get_statistics()

Get server statistics.

```cpp
[[nodiscard]] auto get_statistics() -> server_statistics;
[[nodiscard]] auto get_storage_stats() -> storage_statistics;
```

#### Callbacks

##### on_upload_request()

Register callback to accept or reject upload requests.

```cpp
void on_upload_request(std::function<bool(const upload_request&)> callback);
```

**Example:**
```cpp
server->on_upload_request([](const upload_request& req) {
    // Reject files larger than 1GB
    if (req.file_size > 1ULL * 1024 * 1024 * 1024) {
        return false;
    }

    // Reject certain file types
    if (req.filename.ends_with(".exe")) {
        return false;
    }

    return true;
});
```

##### on_download_request()

Register callback to accept or reject download requests.

```cpp
void on_download_request(std::function<bool(const download_request&)> callback);
```

**Example:**
```cpp
server->on_download_request([](const download_request& req) {
    // Allow all downloads
    return true;
});
```

##### on_transfer_complete()

Register callback for transfer completion events.

```cpp
void on_transfer_complete(std::function<void(const transfer_result&)> callback);
```

##### on_client_connected() / on_client_disconnected()

Register callbacks for client connection events.

```cpp
void on_client_connected(std::function<void(const session_info&)> callback);
void on_client_disconnected(std::function<void(const session_info&, disconnect_reason)> callback);
```

---

### file_transfer_client

Client class for connecting to a server and transferring files.

#### Builder Pattern

```cpp
class file_transfer_client {
public:
    class builder {
    public:
        // Set compression mode (disabled, enabled, adaptive)
        builder& with_compression(compression_mode mode);

        // Set compression level (fast, high_compression)
        builder& with_compression_level(compression_level level);

        // Set chunk size (64KB - 1MB, default: 256KB)
        builder& with_chunk_size(std::size_t size);

        // Enable automatic reconnection
        builder& with_auto_reconnect(bool enable);

        // Configure reconnection policy
        builder& with_reconnect_policy(const reconnect_policy& policy);

        // Set bandwidth limit in bytes per second (0 = unlimited)
        builder& with_bandwidth_limit(std::size_t bytes_per_second);

        // Configure pipeline
        builder& with_pipeline_config(const pipeline_config& config);

        // Set transport type (tcp, quic)
        builder& with_transport(transport_type type);

        // Build the client instance
        [[nodiscard]] auto build() -> Result<file_transfer_client>;
    };
};
```

#### Connection Methods

##### connect() / disconnect()

Connect to and disconnect from a server.

```cpp
[[nodiscard]] auto connect(const endpoint& server_addr) -> Result<void>;
[[nodiscard]] auto disconnect() -> Result<void>;
[[nodiscard]] auto is_connected() const -> bool;
```

**Example:**
```cpp
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::adaptive)
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy{
        .initial_delay = std::chrono::seconds(1),
        .max_delay = std::chrono::seconds(60),
        .multiplier = 2.0,
        .max_attempts = 10
    })
    .build();

if (client) {
    auto result = client->connect(endpoint{"192.168.1.100", 19000});
    if (result) {
        std::cout << "Connected to server\n";
    }
}
```

#### Transfer Methods

##### upload_file()

Upload a file to the server.

```cpp
[[nodiscard]] auto upload_file(
    const std::filesystem::path& local_path,
    const std::string& remote_name,
    const upload_options& options = {}
) -> Result<transfer_handle>;
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `local_path` | `std::filesystem::path` | Path to the local file to upload |
| `remote_name` | `std::string` | Name to store the file as on server |
| `options` | `upload_options` | Optional upload configuration |

**Returns:** `Result<transfer_handle>` - Handle for tracking the transfer

**Example:**
```cpp
auto handle = client->upload_file(
    "/local/data/report.pdf",
    "report.pdf",
    upload_options{
        .compression = compression_mode::enabled,
        .overwrite_existing = true
    }
);

if (handle) {
    std::cout << "Upload started: " << handle->id.to_string() << "\n";
}
```

##### download_file()

Download a file from the server.

```cpp
[[nodiscard]] auto download_file(
    const std::string& remote_name,
    const std::filesystem::path& local_path,
    const download_options& options = {}
) -> Result<transfer_handle>;
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `remote_name` | `std::string` | Name of the file on server |
| `local_path` | `std::filesystem::path` | Path to save the file locally |
| `options` | `download_options` | Optional download configuration |

**Returns:** `Result<transfer_handle>` - Handle for tracking the transfer

**Example:**
```cpp
auto handle = client->download_file(
    "report.pdf",
    "/local/downloads/report.pdf"
);

if (handle) {
    std::cout << "Download started: " << handle->id.to_string() << "\n";
}
```

##### list_files()

List files available on the server.

```cpp
[[nodiscard]] auto list_files(
    const list_options& options = {}
) -> Result<std::vector<file_info>>;
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `options` | `list_options` | Filtering, pagination, and sorting options |

**Returns:** `Result<std::vector<file_info>>` - List of files with metadata

**Example:**
```cpp
auto result = client->list_files(list_options{
    .pattern = "*.pdf",
    .offset = 0,
    .limit = 100,
    .sort_by = sort_field::modified_time,
    .sort_order = sort_order::descending
});

if (result) {
    for (const auto& file : *result) {
        std::cout << file.filename << " - "
                  << file.file_size << " bytes\n";
    }
}
```

##### upload_files()

Upload multiple files in a batch operation.

```cpp
[[nodiscard]] auto upload_files(
    std::span<const upload_entry> files,
    const batch_options& options = {}
) -> Result<batch_transfer_handle>;
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `files` | `std::span<const upload_entry>` | Files to upload |
| `options` | `batch_options` | Batch transfer configuration |

**Returns:** `Result<batch_transfer_handle>` - Handle for tracking and controlling the batch

**Example:**
```cpp
std::vector<upload_entry> files{
    {"/local/file1.txt", "remote1.txt"},
    {"/local/file2.txt", "remote2.txt"},
    {"/local/file3.txt"}  // Uses local filename
};

batch_options options;
options.max_concurrent = 4;
options.continue_on_error = true;

auto result = client->upload_files(files, options);
if (result) {
    auto& batch = result.value();

    // Monitor progress
    auto progress = batch.get_batch_progress();
    std::cout << progress.completion_percentage() << "% complete\n";

    // Wait for all transfers
    auto batch_result = batch.wait();
    if (batch_result && batch_result->all_succeeded()) {
        std::cout << "All files uploaded successfully\n";
    }
}
```

##### download_files()

Download multiple files in a batch operation.

```cpp
[[nodiscard]] auto download_files(
    std::span<const download_entry> files,
    const batch_options& options = {}
) -> Result<batch_transfer_handle>;
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `files` | `std::span<const download_entry>` | Files to download |
| `options` | `batch_options` | Batch transfer configuration |

**Returns:** `Result<batch_transfer_handle>` - Handle for tracking and controlling the batch

**Example:**
```cpp
std::vector<download_entry> files{
    {"remote1.txt", "/local/downloads/file1.txt"},
    {"remote2.txt", "/local/downloads/file2.txt"}
};

auto result = client->download_files(files);
if (result) {
    auto batch_result = result.value().wait();
    std::cout << "Downloaded " << batch_result->succeeded
              << " of " << batch_result->total_files << " files\n";
}
```

#### Batch Transfer Types

##### upload_entry

Entry for batch upload operation.

```cpp
struct upload_entry {
    std::filesystem::path local_path;  // Local file path to upload
    std::string remote_name;           // Remote filename (optional)

    upload_entry() = default;
    upload_entry(std::filesystem::path path, std::string name = {});
};
```

##### download_entry

Entry for batch download operation.

```cpp
struct download_entry {
    std::string remote_name;           // Remote filename to download
    std::filesystem::path local_path;  // Local destination path

    download_entry() = default;
    download_entry(std::string name, std::filesystem::path path);
};
```

##### batch_options

Configuration for batch transfers.

```cpp
struct batch_options {
    std::size_t max_concurrent = 4;    // Maximum concurrent transfers
    bool continue_on_error = true;     // Continue if individual files fail
    bool overwrite = false;            // Overwrite existing files
    std::optional<compression_mode> compression;  // Compression mode override
};
```

##### batch_progress

Progress information for a batch transfer.

```cpp
struct batch_progress {
    std::size_t total_files;        // Total number of files in batch
    std::size_t completed_files;    // Number of completed files
    std::size_t failed_files;       // Number of failed files
    std::size_t in_progress_files;  // Number of files currently transferring
    uint64_t total_bytes;           // Total bytes across all files
    uint64_t transferred_bytes;     // Total bytes transferred so far
    double overall_rate;            // Overall transfer rate (bytes/sec)

    [[nodiscard]] auto completion_percentage() const noexcept -> double;
    [[nodiscard]] auto pending_files() const noexcept -> std::size_t;
};
```

##### batch_result

Result of a completed batch transfer.

```cpp
struct batch_result {
    std::size_t total_files;        // Total files in batch
    std::size_t succeeded;          // Files that succeeded
    std::size_t failed;             // Files that failed
    uint64_t total_bytes;           // Total bytes transferred
    std::chrono::milliseconds elapsed;  // Total time taken
    std::vector<batch_file_result> file_results;  // Per-file results

    [[nodiscard]] auto all_succeeded() const noexcept -> bool;
};
```

##### batch_file_result

Result of a single file in a batch operation.

```cpp
struct batch_file_result {
    std::string filename;               // Filename
    bool success;                       // Whether this file succeeded
    uint64_t bytes_transferred;         // Bytes transferred for this file
    std::chrono::milliseconds elapsed;  // Time taken
    std::optional<std::string> error_message;  // Error message if failed
};
```

##### batch_transfer_handle

Handle for tracking and controlling batch transfers.

```cpp
class batch_transfer_handle {
public:
    // Get batch ID
    [[nodiscard]] auto get_id() const noexcept -> uint64_t;

    // Check if handle is valid
    [[nodiscard]] auto is_valid() const noexcept -> bool;

    // Get total number of files in batch
    [[nodiscard]] auto get_total_files() const -> std::size_t;

    // Get number of completed files
    [[nodiscard]] auto get_completed_files() const -> std::size_t;

    // Get number of failed files
    [[nodiscard]] auto get_failed_files() const -> std::size_t;

    // Get handles for individual transfers
    [[nodiscard]] auto get_individual_handles() const -> std::vector<transfer_handle>;

    // Get current batch progress
    [[nodiscard]] auto get_batch_progress() const -> batch_progress;

    // Pause all active transfers in batch
    [[nodiscard]] auto pause_all() -> Result<void>;

    // Resume all paused transfers in batch
    [[nodiscard]] auto resume_all() -> Result<void>;

    // Cancel all transfers in batch
    [[nodiscard]] auto cancel_all() -> Result<void>;

    // Wait for all transfers to complete
    [[nodiscard]] auto wait() -> Result<batch_result>;

    // Wait for completion with timeout
    [[nodiscard]] auto wait_for(std::chrono::milliseconds timeout)
        -> Result<batch_result>;
};
```

**Example:**
```cpp
auto batch_result = client->upload_files(files, {.max_concurrent = 4});
if (batch_result) {
    auto& batch = batch_result.value();

    // Monitor progress periodically
    while (batch.get_completed_files() + batch.get_failed_files()
           < batch.get_total_files()) {
        auto progress = batch.get_batch_progress();
        std::cout << progress.completed_files << "/" << progress.total_files
                  << " files complete (" << progress.completion_percentage()
                  << "%), rate: " << progress.overall_rate / (1024*1024)
                  << " MB/s\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Get final result
    auto result = batch.wait();
    if (result) {
        std::cout << "Batch complete: " << result->succeeded << " succeeded, "
                  << result->failed << " failed\n";

        // Check individual file results
        for (const auto& file_result : result->file_results) {
            if (!file_result.success) {
                std::cerr << "Failed: " << file_result.filename << " - "
                          << file_result.error_message.value_or("unknown error")
                          << "\n";
            }
        }
    }
}
```

#### Transfer Control

Transfers can be controlled through the `transfer_handle` returned from upload/download operations.

##### transfer_handle Methods

```cpp
class transfer_handle {
public:
    // Get handle ID
    [[nodiscard]] auto get_id() const noexcept -> uint64_t;

    // Check if handle is valid
    [[nodiscard]] auto is_valid() const noexcept -> bool;

    // Get current transfer status
    [[nodiscard]] auto get_status() const -> transfer_status;

    // Get current transfer progress
    [[nodiscard]] auto get_progress() const -> transfer_progress_info;

    // Pause the transfer (valid: in_progress -> paused)
    [[nodiscard]] auto pause() -> Result<void>;

    // Resume a paused transfer (valid: paused -> in_progress)
    [[nodiscard]] auto resume() -> Result<void>;

    // Cancel the transfer (valid from any non-terminal state)
    [[nodiscard]] auto cancel() -> Result<void>;

    // Wait for transfer completion
    [[nodiscard]] auto wait() -> Result<transfer_result_info>;

    // Wait for transfer completion with timeout
    [[nodiscard]] auto wait_for(std::chrono::milliseconds timeout)
        -> Result<transfer_result_info>;
};
```

##### transfer_status Enum

```cpp
enum class transfer_status {
    pending,       // Waiting to start
    in_progress,   // Transfer in progress
    paused,        // Transfer paused
    completing,    // Finalizing transfer
    completed,     // Transfer completed successfully
    failed,        // Transfer failed
    cancelled      // Transfer cancelled by user
};
```

##### transfer_progress_info

```cpp
struct transfer_progress_info {
    uint64_t bytes_transferred;   // Bytes transferred so far
    uint64_t total_bytes;         // Total file size
    uint64_t chunks_transferred;  // Chunks transferred
    uint64_t total_chunks;        // Total number of chunks
    double transfer_rate;         // Bytes per second
    std::chrono::milliseconds elapsed;  // Time elapsed

    [[nodiscard]] auto completion_percentage() const noexcept -> double;
};
```

##### transfer_result_info

```cpp
struct transfer_result_info {
    bool success;                 // Whether transfer succeeded
    uint64_t bytes_transferred;   // Total bytes transferred
    std::chrono::milliseconds elapsed;  // Total time taken
    std::optional<std::string> error_message;  // Error if failed
};
```

**Example:**
```cpp
// Start upload
auto handle_result = client->upload_file("/local/large_file.zip", "large_file.zip");
if (!handle_result) {
    std::cerr << "Failed to start upload: " << handle_result.error().message << "\n";
    return;
}

auto& handle = handle_result.value();
std::cout << "Upload started with ID: " << handle.get_id() << "\n";

// Monitor progress
while (handle.get_status() == transfer_status::in_progress) {
    auto progress = handle.get_progress();
    std::cout << progress.completion_percentage() << "% complete\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

// Pause if needed
if (need_to_pause) {
    auto pause_result = handle.pause();
    if (pause_result) {
        std::cout << "Transfer paused\n";
    }

    // Later, resume
    handle.resume();
}

// Cancel if needed
if (need_to_cancel) {
    auto cancel_result = handle.cancel();
    if (cancel_result) {
        std::cout << "Transfer cancelled\n";
    }
}

// Wait for completion
auto result = handle.wait();
if (result && result->success) {
    std::cout << "Transfer completed: " << result->bytes_transferred << " bytes\n";
} else {
    std::cerr << "Transfer failed: " << result.error().message << "\n";
}
```

##### Client-level Transfer Control

The client also provides direct methods for transfer control:

```cpp
// Get transfer status by handle ID
[[nodiscard]] auto get_transfer_status(uint64_t handle_id) const -> transfer_status;

// Get transfer progress by handle ID
[[nodiscard]] auto get_transfer_progress(uint64_t handle_id) const -> transfer_progress_info;

// Pause a transfer
[[nodiscard]] auto pause_transfer(uint64_t handle_id) -> Result<void>;

// Resume a paused transfer
[[nodiscard]] auto resume_transfer(uint64_t handle_id) -> Result<void>;

// Cancel a transfer
[[nodiscard]] auto cancel_transfer(uint64_t handle_id) -> Result<void>;

// Wait for transfer completion
[[nodiscard]] auto wait_for_transfer(uint64_t handle_id) -> Result<transfer_result_info>;

// Wait for transfer completion with timeout
[[nodiscard]] auto wait_for_transfer(
    uint64_t handle_id,
    std::chrono::milliseconds timeout) -> Result<transfer_result_info>;
```

##### State Transitions

Valid state transitions for transfer control:

```
                      ┌──────────────────────────┐
                      │         pending          │
                      └────────────┬─────────────┘
                                   │
                                   ▼
                      ┌──────────────────────────┐
          ┌───────────│       in_progress        │───────────┐
          │           └────────────┬─────────────┘           │
          │                        │                         │
          │ pause()                │ complete                │ error
          ▼                        │                         ▼
┌──────────────────┐               │               ┌─────────────────┐
│      paused      │               │               │      failed     │
└────────┬─────────┘               │               └─────────────────┘
         │                         │
         │ resume()                │
         └─────────────────────────┤
                                   ▼
                      ┌──────────────────────────┐
                      │       completing         │
                      └────────────┬─────────────┘
                                   │
                                   ▼
                      ┌──────────────────────────┐
                      │       completed          │
                      └──────────────────────────┘

cancel() can be called from any non-terminal state → cancelled
```

#### Callbacks

##### on_progress()

Register a callback for progress updates.

```cpp
void on_progress(std::function<void(const transfer_progress&)> callback);
```

**Example:**
```cpp
client->on_progress([](const transfer_progress& p) {
    double percent = 100.0 * p.bytes_transferred / p.total_bytes;
    std::cout << std::fixed << std::setprecision(1)
              << percent << "% - "
              << p.transfer_rate / (1024*1024) << " MB/s"
              << " (compression: " << p.compression_ratio << ":1)\n";
});
```

##### on_complete()

Register callback for transfer completion.

```cpp
void on_complete(std::function<void(const transfer_result&)> callback);
```

##### on_error()

Register callback for transfer errors.

```cpp
void on_error(std::function<void(const transfer_id&, const error&)> callback);
```

##### on_disconnected()

Register callback for disconnection events.

```cpp
void on_disconnected(std::function<void(disconnect_reason)> callback);
```

---

### transfer_manager

Manages multiple concurrent transfers and provides statistics.

#### Builder Pattern

```cpp
class transfer_manager {
public:
    class builder {
    public:
        builder& with_max_concurrent(std::size_t max_count);
        builder& with_default_compression(compression_mode mode);
        builder& with_global_bandwidth_limit(std::size_t bytes_per_second);
        [[nodiscard]] auto build() -> Result<transfer_manager>;
    };
};
```

#### Methods

##### get_status()

Get status of a specific transfer.

```cpp
[[nodiscard]] auto get_status(const transfer_id& id) -> Result<transfer_status>;
```

##### list_transfers()

List all active transfers.

```cpp
[[nodiscard]] auto list_transfers() -> Result<std::vector<transfer_info>>;
```

##### get_statistics()

Get aggregate transfer statistics.

```cpp
[[nodiscard]] auto get_statistics() -> transfer_statistics;
[[nodiscard]] auto get_compression_stats() -> compression_statistics;
```

##### Configuration

```cpp
void set_bandwidth_limit(std::size_t bytes_per_second);
void set_max_concurrent_transfers(std::size_t max_count);
void set_default_compression(compression_mode mode);
```

---

### quota_manager

Server storage quota management with warning thresholds and automatic cleanup.

#### Creation

```cpp
namespace kcenon::file_transfer {

class quota_manager {
public:
    // Create a quota manager for a storage directory
    [[nodiscard]] static auto create(
        const std::filesystem::path& storage_path,
        uint64_t total_quota = 0  // 0 = unlimited
    ) -> result<quota_manager>;
};

}
```

**Example:**
```cpp
auto manager_result = quota_manager::create("/data/storage", 100ULL * 1024 * 1024 * 1024);  // 100GB
if (manager_result.has_value()) {
    auto& manager = manager_result.value();
    manager.set_max_file_size(10ULL * 1024 * 1024 * 1024);  // 10GB max per file
}
```

#### Configuration Methods

##### set_total_quota() / get_total_quota()

Configure the total storage quota.

```cpp
auto set_total_quota(uint64_t bytes) -> void;
[[nodiscard]] auto get_total_quota() const -> uint64_t;
```

##### set_max_file_size() / get_max_file_size()

Configure the maximum allowed file size.

```cpp
auto set_max_file_size(uint64_t bytes) -> void;
[[nodiscard]] auto get_max_file_size() const -> uint64_t;
```

#### Usage Tracking

##### get_usage()

Get current quota usage information.

```cpp
[[nodiscard]] auto get_usage() const -> quota_usage;
```

**Returns:** `quota_usage` structure with:
- `total_quota` - Total quota in bytes
- `used_bytes` - Bytes currently used
- `available_bytes` - Bytes remaining
- `usage_percent` - Usage percentage (0.0 to 100.0)
- `file_count` - Number of files stored

##### check_quota()

Check if storage can accommodate the required bytes.

```cpp
[[nodiscard]] auto check_quota(uint64_t required_bytes) -> result<void>;
```

**Example:**
```cpp
auto check = manager.check_quota(file_size);
if (!check.has_value()) {
    std::cerr << "Quota exceeded: " << check.error().message << "\n";
}
```

##### check_file_size()

Check if a file size is within the configured limit.

```cpp
[[nodiscard]] auto check_file_size(uint64_t file_size) -> result<void>;
```

#### Warning Thresholds

##### set_warning_thresholds()

Configure percentage thresholds for warnings.

```cpp
auto set_warning_thresholds(const std::vector<double>& percentages) -> void;
```

**Example:**
```cpp
manager.set_warning_thresholds({80.0, 90.0, 95.0});  // Warn at 80%, 90%, 95%
```

##### on_quota_warning()

Register callback for threshold warnings.

```cpp
auto on_quota_warning(std::function<void(const quota_usage&)> callback) -> void;
```

**Example:**
```cpp
manager.on_quota_warning([](const quota_usage& usage) {
    std::cout << "Warning: Storage at " << usage.usage_percent << "% capacity\n";
});
```

##### on_quota_exceeded()

Register callback for quota exceeded events.

```cpp
auto on_quota_exceeded(std::function<void(const quota_usage&)> callback) -> void;
```

#### Cleanup Policy

##### cleanup_policy Structure

```cpp
struct cleanup_policy {
    bool enabled = false;                 // Enable automatic cleanup
    double trigger_threshold = 90.0;      // Start cleanup at this %
    double target_threshold = 80.0;       // Cleanup until this %
    bool delete_oldest_first = true;      // Delete oldest files first
    std::chrono::hours min_file_age{0};   // Only delete files older than this
    std::vector<std::string> exclusions;  // Filename patterns to exclude
};
```

##### set_cleanup_policy()

Configure automatic cleanup behavior.

```cpp
auto set_cleanup_policy(const cleanup_policy& policy) -> void;
```

**Example:**
```cpp
cleanup_policy policy;
policy.enabled = true;
policy.trigger_threshold = 90.0;
policy.target_threshold = 80.0;
policy.delete_oldest_first = true;
policy.min_file_age = std::chrono::hours(24);
policy.exclusions = {"important", "keep"};
manager.set_cleanup_policy(policy);
```

##### execute_cleanup()

Manually trigger cleanup according to policy.

```cpp
auto execute_cleanup() -> uint64_t;  // Returns bytes freed
```

#### quota_usage Structure

```cpp
struct quota_usage {
    uint64_t total_quota = 0;
    uint64_t used_bytes = 0;
    uint64_t available_bytes = 0;
    double usage_percent = 0.0;
    std::size_t file_count = 0;

    [[nodiscard]] auto is_exceeded() const -> bool;
    [[nodiscard]] auto is_threshold_reached(double threshold) const -> bool;
};
```

#### Server Integration

The `file_transfer_server` class integrates with `quota_manager` automatically.

```cpp
// Access quota manager from server
auto& quota_mgr = server->get_quota_manager();

// Get quota usage
auto usage = server->get_quota_usage();

// Check if upload is allowed
auto allowed = server->check_upload_allowed(file_size);

// Register quota callbacks
server->on_quota_warning([](const quota_usage& usage) {
    std::cout << "Server storage warning: " << usage.usage_percent << "%\n";
});

server->on_quota_exceeded([](const quota_usage& usage) {
    std::cerr << "Server storage exceeded!\n";
});
```

---

## Data Types

### Enumerations

#### compression_mode

```cpp
enum class compression_mode {
    disabled,   // No compression
    enabled,    // Always compress
    adaptive    // Auto-detect compressibility (default)
};
```

#### compression_level

```cpp
enum class compression_level {
    fast,             // LZ4 standard (~400 MB/s)
    high_compression  // LZ4-HC (~50 MB/s, better ratio)
};
```

#### transport_type

```cpp
enum class transport_type {
    tcp,    // TCP + TLS 1.3 (default)
    quic    // QUIC (Phase 2)
};
```

#### transfer_state_enum

```cpp
enum class transfer_state_enum {
    pending,        // Waiting to start
    requested,      // Request sent, waiting for response
    transferring,   // Active transfer
    verifying,      // Verifying integrity
    completed,      // Successfully completed
    failed,         // Transfer failed
    cancelled,      // User cancelled
    rejected        // Server rejected request
};
```

#### transfer_direction

```cpp
enum class transfer_direction {
    upload,     // Client to server
    download    // Server to client
};
```

#### connection_state

```cpp
enum class connection_state {
    disconnected,   // Not connected
    connecting,     // Connection in progress
    connected,      // Connected and ready
    reconnecting,   // Automatic reconnection in progress
    failed          // Connection failed permanently
};
```

#### sort_field

```cpp
enum class sort_field {
    name,           // Sort by filename
    size,           // Sort by file size
    modified_time,  // Sort by modification time
    created_time    // Sort by creation time
};
```

#### sort_order

```cpp
enum class sort_order {
    ascending,
    descending
};
```

#### chunk_flags

```cpp
enum class chunk_flags : uint8_t {
    none            = 0x00,  // No flags set
    first_chunk     = 0x01,  // Bit 0: First chunk of file
    last_chunk      = 0x02,  // Bit 1: Last chunk of file
    compressed      = 0x04,  // Bit 2: Data is LZ4 compressed
    encrypted       = 0x08   // Bit 3: Reserved for encryption
};

// Usage: Combine flags with bitwise OR
// Example: First compressed chunk = chunk_flags::first_chunk | chunk_flags::compressed (0x05)
```

#### pipeline_stage

```cpp
enum class pipeline_stage {
    network_recv,   // Network receive stage
    decompress,     // LZ4 decompression stage
    chunk_verify,   // CRC32 verification stage
    file_write,     // Disk write stage
    network_send,   // Network send stage
    file_read,      // Disk read stage
    compress        // LZ4 compression stage
};

// Convert to string representation
[[nodiscard]] constexpr auto to_string(pipeline_stage stage) -> const char*;
```

---

### Structures

#### protocol_version

```cpp
struct protocol_version {
    uint8_t major;    // Breaking changes
    uint8_t minor;    // New features (backward compatible)
    uint8_t patch;    // Bug fixes
    uint8_t build;    // Build/revision number

    // Encode to 4-byte wire format (big-endian)
    [[nodiscard]] auto to_wire() const -> uint32_t {
        return (static_cast<uint32_t>(major) << 24) |
               (static_cast<uint32_t>(minor) << 16) |
               (static_cast<uint32_t>(patch) << 8)  |
               static_cast<uint32_t>(build);
    }

    // Decode from 4-byte wire format
    [[nodiscard]] static auto from_wire(uint32_t wire) -> protocol_version {
        return {
            static_cast<uint8_t>(wire >> 24),
            static_cast<uint8_t>(wire >> 16),
            static_cast<uint8_t>(wire >> 8),
            static_cast<uint8_t>(wire)
        };
    }

    [[nodiscard]] auto to_string() const -> std::string;
    [[nodiscard]] auto is_compatible_with(const protocol_version& other) const -> bool;
};

// Current protocol version: v0.2.0.0
inline constexpr uint32_t PROTOCOL_VERSION_WIRE = 0x00020000;
inline constexpr protocol_version PROTOCOL_VERSION = {0, 2, 0, 0};
```

#### frame_header

```cpp
// Protocol frame constants
inline constexpr uint32_t FRAME_MAGIC = 0x46545331;  // "FTS1"
inline constexpr std::size_t FRAME_HEADER_SIZE = 9;   // prefix(4) + type(1) + length(4)
inline constexpr std::size_t FRAME_FOOTER_SIZE = 4;   // checksum(2) + length_echo(2)
inline constexpr std::size_t FRAME_OVERHEAD = 13;     // header + footer

struct frame_header {
    uint32_t magic;           // Must be FRAME_MAGIC (0x46545331)
    uint8_t  message_type;    // Message type enumeration
    uint32_t payload_length;  // Payload size in bytes (big-endian)
};

struct frame_footer {
    uint16_t checksum;        // Sum of bytes [0..8+payload_length] mod 65536
    uint16_t length_echo;     // Lower 16 bits of payload_length
};

// Frame validation helper
[[nodiscard]] auto validate_frame(
    const frame_header& header,
    const std::span<const uint8_t>& payload,
    const frame_footer& footer
) -> bool;
```

#### endpoint

```cpp
struct endpoint {
    std::string address;
    uint16_t    port;
};
```

#### transfer_id

```cpp
struct transfer_id {
    std::array<uint8_t, 16> bytes;  // UUID

    [[nodiscard]] static auto generate() -> transfer_id;
    [[nodiscard]] auto to_string() const -> std::string;
    [[nodiscard]] static auto from_string(std::string_view str) -> Result<transfer_id>;
};
```

#### file_info

```cpp
struct file_info {
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    std::chrono::system_clock::time_point created_time;
    std::chrono::system_clock::time_point modified_time;
};
```

#### upload_request

```cpp
struct upload_request {
    transfer_id             id;
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    session_info            client;
    compression_mode        compression;
};
```

#### download_request

```cpp
struct download_request {
    transfer_id             id;
    std::string             filename;
    session_info            client;
    compression_mode        compression;
};
```

#### upload_options

```cpp
struct upload_options {
    compression_mode            compression     = compression_mode::adaptive;
    compression_level           level           = compression_level::fast;
    bool                        overwrite_existing = false;
    bool                        verify_checksum = true;
    std::optional<std::size_t>  bandwidth_limit;
    std::optional<int>          priority;
};
```

#### download_options

```cpp
struct download_options {
    compression_mode            compression     = compression_mode::adaptive;
    bool                        verify_checksum = true;
    std::optional<std::size_t>  bandwidth_limit;
    std::optional<int>          priority;
};
```

#### list_options

```cpp
struct list_options {
    std::string     pattern     = "*";      // Glob pattern
    std::size_t     offset      = 0;        // Pagination offset
    std::size_t     limit       = 1000;     // Max items to return
    sort_field      sort_by     = sort_field::name;
    sort_order      order       = sort_order::ascending;
};
```

#### reconnect_policy

```cpp
struct reconnect_policy {
    std::chrono::milliseconds   initial_delay   = std::chrono::seconds(1);
    std::chrono::milliseconds   max_delay       = std::chrono::seconds(60);
    double                      multiplier      = 2.0;
    uint32_t                    max_attempts    = 10;
    bool                        resume_transfers = true;
};
```

#### session_info

```cpp
struct session_info {
    session_id                  id;
    endpoint                    remote_address;
    std::chrono::system_clock::time_point connected_at;
    uint64_t                    bytes_uploaded;
    uint64_t                    bytes_downloaded;
    std::size_t                 active_transfers;
};
```

#### transfer_handle

```cpp
struct transfer_handle {
    transfer_id             id;
    transfer_direction      direction;

    // Blocking wait for completion
    [[nodiscard]] auto wait() -> Result<transfer_result>;

    // Non-blocking status check
    [[nodiscard]] auto get_status() -> transfer_status;
};
```

#### transfer_progress

```cpp
struct transfer_progress {
    transfer_id         id;
    transfer_direction  direction;
    std::string         filename;
    uint64_t            bytes_transferred;      // Raw bytes
    uint64_t            bytes_on_wire;          // Compressed bytes
    uint64_t            total_bytes;
    double              transfer_rate;          // Bytes/second
    double              effective_rate;         // With compression
    double              compression_ratio;
    duration            elapsed_time;
    duration            estimated_remaining;
    transfer_state_enum state;
    std::optional<std::string> error_message;
};
```

#### transfer_result

```cpp
struct transfer_result {
    transfer_id             id;
    transfer_direction      direction;
    std::string             filename;
    std::filesystem::path   local_path;         // For downloads
    std::string             stored_path;        // For uploads (on server)
    uint64_t                bytes_transferred;
    uint64_t                bytes_on_wire;
    bool                    verified;           // SHA-256 match
    std::optional<error>    error;
    duration                elapsed_time;
    compression_statistics  compression_stats;
};
```

#### server_statistics

```cpp
struct server_statistics {
    std::size_t             active_connections;
    std::size_t             total_connections;
    std::size_t             active_uploads;
    std::size_t             active_downloads;
    uint64_t                total_bytes_received;
    uint64_t                total_bytes_sent;
    std::size_t             files_stored;
    uint64_t                storage_used;
    uint64_t                storage_available;
    duration                uptime;
};
```

#### storage_statistics

```cpp
struct storage_statistics {
    std::size_t             file_count;
    uint64_t                total_size;
    uint64_t                quota;
    uint64_t                available;
    double                  usage_percent;
};
```

#### pipeline_config

```cpp
struct pipeline_config {
    std::size_t io_workers = 2;            // Number of I/O worker threads
    std::size_t compression_workers = 4;   // Number of compression worker threads
    std::size_t network_workers = 2;       // Number of network worker threads
    std::size_t queue_size = 64;           // Maximum queue size per stage
    std::size_t max_memory_per_transfer = 32 * 1024 * 1024;  // ~32MB per transfer

    // Auto-detect optimal configuration based on hardware
    [[nodiscard]] static auto auto_detect() -> pipeline_config;

    // Validate configuration
    [[nodiscard]] auto is_valid() const -> bool;
};
```

#### pipeline_stats

```cpp
struct pipeline_stats {
    std::atomic<uint64_t> chunks_processed{0};
    std::atomic<uint64_t> bytes_processed{0};
    std::atomic<uint64_t> compression_saved_bytes{0};
    std::atomic<uint64_t> stalls_detected{0};
    std::atomic<uint64_t> backpressure_events{0};

    auto reset() -> void;
};
```

#### pipeline_chunk

```cpp
struct pipeline_chunk {
    transfer_id id;
    uint64_t chunk_index;
    std::vector<std::byte> data;
    uint32_t checksum;
    bool is_compressed;
    std::size_t original_size;

    pipeline_chunk() = default;
    explicit pipeline_chunk(const chunk& c);
};
```

#### compression_statistics

```cpp
struct compression_statistics {
    std::atomic<uint64_t> total_raw_bytes{0};
    std::atomic<uint64_t> total_compressed_bytes{0};
    std::atomic<uint64_t> chunks_compressed{0};
    std::atomic<uint64_t> chunks_skipped{0};
    std::atomic<uint64_t> compression_time_us{0};
    std::atomic<uint64_t> decompression_time_us{0};

    [[nodiscard]] auto compression_ratio() const -> double;
    [[nodiscard]] auto compression_speed_mbps() const -> double;
    [[nodiscard]] auto decompression_speed_mbps() const -> double;
};
```

---

## Chunk Management

### chunk_splitter

Splits files into chunks for streaming transfer.

```cpp
class chunk_splitter {
public:
    explicit chunk_splitter(const chunk_config& config);

    [[nodiscard]] auto split(
        const std::filesystem::path& file_path,
        const transfer_id& id
    ) -> Result<chunk_iterator>;

    [[nodiscard]] auto calculate_metadata(
        const std::filesystem::path& file_path
    ) -> Result<file_metadata>;
};
```

### chunk_assembler

Reassembles received chunks into files.

```cpp
class chunk_assembler {
public:
    explicit chunk_assembler(const std::filesystem::path& output_dir);

    [[nodiscard]] auto process_chunk(const chunk& c) -> Result<void>;
    [[nodiscard]] auto is_complete(const transfer_id& id) const -> bool;
    [[nodiscard]] auto get_missing_chunks(const transfer_id& id) const
        -> std::vector<uint64_t>;
    [[nodiscard]] auto finalize(
        const transfer_id& id,
        const std::string& expected_hash
    ) -> Result<std::filesystem::path>;
};
```

### checksum

Integrity verification utilities.

```cpp
class checksum {
public:
    // CRC32 for chunks
    [[nodiscard]] static auto crc32(std::span<const std::byte> data) -> uint32_t;
    [[nodiscard]] static auto verify_crc32(
        std::span<const std::byte> data,
        uint32_t expected
    ) -> bool;

    // SHA-256 for files
    [[nodiscard]] static auto sha256_file(const std::filesystem::path& path)
        -> Result<std::string>;
    [[nodiscard]] static auto verify_sha256(
        const std::filesystem::path& path,
        const std::string& expected
    ) -> bool;
};
```

---

## Resume Handler

The resume handler provides functionality for persisting and resuming interrupted file transfers.

### transfer_state

Structure containing all information needed to resume an interrupted transfer.

```cpp
struct transfer_state {
    transfer_id id;                    // Unique transfer identifier
    std::string filename;              // Original filename
    uint64_t total_size;               // Total file size in bytes
    uint64_t transferred_bytes;        // Bytes successfully transferred
    uint32_t total_chunks;             // Total number of chunks
    std::vector<bool> chunk_bitmap;    // Bitmap of received chunks
    std::string sha256;                // SHA-256 hash of the file
    std::chrono::system_clock::time_point started_at;    // Transfer start time
    std::chrono::system_clock::time_point last_activity; // Last activity time

    // Constructor for new transfer
    transfer_state(
        const transfer_id& id,
        std::string filename,
        uint64_t file_size,
        uint32_t num_chunks,
        std::string file_hash);

    // Query methods
    [[nodiscard]] auto received_chunk_count() const -> uint32_t;
    [[nodiscard]] auto completion_percentage() const -> double;
    [[nodiscard]] auto is_complete() const -> bool;
};
```

### resume_handler_config

Configuration for the resume handler.

```cpp
struct resume_handler_config {
    std::filesystem::path state_directory;  // Directory for state files
    uint32_t checkpoint_interval = 10;      // Save state every N chunks
    std::chrono::seconds state_ttl{86400};  // State file TTL (default: 24h)
    bool auto_cleanup = true;               // Auto cleanup expired states

    resume_handler_config();
    explicit resume_handler_config(std::filesystem::path dir);
};
```

### resume_handler

Handler class for resumable file transfers.

```cpp
class resume_handler {
public:
    explicit resume_handler(const resume_handler_config& config);

    // State persistence
    [[nodiscard]] auto save_state(const transfer_state& state) -> result<void>;
    [[nodiscard]] auto load_state(const transfer_id& id) -> result<transfer_state>;
    [[nodiscard]] auto delete_state(const transfer_id& id) -> result<void>;
    [[nodiscard]] auto has_state(const transfer_id& id) const -> bool;

    // Chunk tracking
    [[nodiscard]] auto mark_chunk_received(
        const transfer_id& id,
        uint32_t chunk_index) -> result<void>;
    [[nodiscard]] auto mark_chunks_received(
        const transfer_id& id,
        const std::vector<uint32_t>& chunk_indices) -> result<void>;
    [[nodiscard]] auto get_missing_chunks(const transfer_id& id)
        -> result<std::vector<uint32_t>>;
    [[nodiscard]] auto is_chunk_received(
        const transfer_id& id,
        uint32_t chunk_index) const -> bool;

    // State query
    [[nodiscard]] auto list_resumable_transfers() -> std::vector<transfer_state>;
    [[nodiscard]] auto cleanup_expired_states() -> std::size_t;
    [[nodiscard]] auto config() const -> const resume_handler_config&;

    // Transfer progress
    [[nodiscard]] auto update_transferred_bytes(
        const transfer_id& id,
        uint64_t bytes) -> result<void>;
};
```

**Example:**
```cpp
#include <kcenon/file_transfer/core/resume_handler.h>

using namespace kcenon::file_transfer;

// Create resume handler
resume_handler_config config("/data/transfer_states");
config.checkpoint_interval = 5;  // Save every 5 chunks
resume_handler handler(config);

// Create new transfer state
auto id = transfer_id::generate();
transfer_state state(id, "large_file.dat", file_size, num_chunks, sha256_hash);

// Save initial state
handler.save_state(state);

// During transfer, mark chunks as received
handler.mark_chunk_received(id, 0);
handler.mark_chunk_received(id, 1);
// ... (auto-checkpoints at interval)

// On reconnection, get missing chunks
auto missing = handler.get_missing_chunks(id);
if (missing) {
    for (auto chunk_idx : *missing) {
        // Request retransmission of missing chunks
    }
}

// List all resumable transfers
auto transfers = handler.list_resumable_transfers();
for (const auto& t : transfers) {
    std::cout << t.filename << ": "
              << t.completion_percentage() << "% complete\n";
}

// Cleanup old states
auto removed = handler.cleanup_expired_states();
```

---

## Compression

### lz4_engine

Low-level LZ4 compression interface.

```cpp
class lz4_engine {
public:
    // Standard LZ4 compression (~400 MB/s)
    [[nodiscard]] static auto compress(
        std::span<const std::byte> input,
        std::span<std::byte> output
    ) -> Result<std::size_t>;

    // LZ4-HC compression (~50 MB/s, better ratio)
    [[nodiscard]] static auto compress_hc(
        std::span<const std::byte> input,
        std::span<std::byte> output,
        int level = 9
    ) -> Result<std::size_t>;

    // Decompression (~1.5 GB/s)
    [[nodiscard]] static auto decompress(
        std::span<const std::byte> compressed,
        std::span<std::byte> output,
        std::size_t original_size
    ) -> Result<std::size_t>;

    // Buffer sizing
    [[nodiscard]] static auto max_compressed_size(std::size_t input_size)
        -> std::size_t;
};
```

### adaptive_compression

Automatic compressibility detection.

```cpp
class adaptive_compression {
public:
    // Sample-based compressibility check (<100us)
    [[nodiscard]] static auto is_compressible(
        std::span<const std::byte> data,
        double threshold = 0.9
    ) -> bool;

    // File extension heuristic
    [[nodiscard]] static auto is_likely_compressible(
        const std::filesystem::path& file
    ) -> bool;
};
```

### chunk_compressor

High-level chunk compression with statistics.

```cpp
class chunk_compressor {
public:
    explicit chunk_compressor(
        compression_mode mode = compression_mode::adaptive,
        compression_level level = compression_level::fast
    );

    [[nodiscard]] auto compress(const chunk& input) -> Result<chunk>;
    [[nodiscard]] auto decompress(const chunk& input) -> Result<chunk>;
    [[nodiscard]] auto get_statistics() const -> compression_statistics;
    void reset_statistics();
};
```

---

## Pipeline

### server_pipeline

Multi-stage pipeline for server-side processing with backpressure control.

```cpp
class server_pipeline {
public:
    // Factory method
    [[nodiscard]] static auto create(const pipeline_config& config = pipeline_config{})
        -> result<server_pipeline>;

    // Non-copyable, movable
    server_pipeline(const server_pipeline&) = delete;
    server_pipeline(server_pipeline&&) noexcept;

    // Lifecycle
    [[nodiscard]] auto start() -> result<void>;
    [[nodiscard]] auto stop(bool wait_for_completion = true) -> result<void>;
    [[nodiscard]] auto is_running() const -> bool;

    // Upload pipeline: decompress -> verify -> write
    [[nodiscard]] auto submit_upload_chunk(pipeline_chunk data) -> result<void>;
    [[nodiscard]] auto try_submit_upload_chunk(pipeline_chunk data) -> bool;

    // Download pipeline: read -> compress -> send
    [[nodiscard]] auto submit_download_request(
        const transfer_id& id,
        uint64_t chunk_index,
        const std::filesystem::path& file_path,
        uint64_t offset,
        std::size_t size) -> result<void>;

    // Callbacks
    auto on_stage_complete(stage_callback callback) -> void;
    auto on_error(error_callback callback) -> void;
    auto on_upload_complete(completion_callback callback) -> void;
    auto on_download_ready(std::function<void(const pipeline_chunk&)> callback) -> void;

    // Statistics
    [[nodiscard]] auto stats() const -> const pipeline_stats&;
    auto reset_stats() -> void;
    [[nodiscard]] auto queue_sizes() const
        -> std::vector<std::pair<pipeline_stage, std::size_t>>;
    [[nodiscard]] auto config() const -> const pipeline_config&;
};

// Callback types
using stage_callback = std::function<void(pipeline_stage, const pipeline_chunk&)>;
using error_callback = std::function<void(pipeline_stage, const std::string&)>;
using completion_callback = std::function<void(const transfer_id&, uint64_t bytes)>;
```

### client_pipeline

Multi-stage pipeline for client-side processing.

```cpp
class client_pipeline {
public:
    class builder {
    public:
        builder& with_config(const pipeline_config& config);
        builder& with_compressor(std::shared_ptr<chunk_compressor> compressor);
        builder& with_transport(std::shared_ptr<transport_interface> transport);
        [[nodiscard]] auto build() -> Result<client_pipeline>;
    };

    // Upload (send to server)
    [[nodiscard]] auto submit_upload(
        const std::filesystem::path& file,
        const transfer_id& id,
        const upload_options& options
    ) -> Result<void>;

    // Download (receive from server)
    [[nodiscard]] auto submit_download(
        const std::string& remote_name,
        const std::filesystem::path& local_path,
        const transfer_id& id,
        const download_options& options
    ) -> Result<void>;

    [[nodiscard]] auto start() -> Result<void>;
    [[nodiscard]] auto stop(bool wait_for_completion = true) -> Result<void>;
    [[nodiscard]] auto get_stats() const -> pipeline_statistics;
    [[nodiscard]] auto get_queue_depths() const -> queue_depth_info;
};
```

---

## Transport

### transport_interface

Abstract transport layer interface.

```cpp
class transport_interface {
public:
    virtual ~transport_interface() = default;

    // Client operations
    [[nodiscard]] virtual auto connect(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto disconnect() -> Result<void> = 0;
    [[nodiscard]] virtual auto is_connected() const -> bool = 0;

    // Data transfer
    [[nodiscard]] virtual auto send(std::span<const std::byte> data) -> Result<void> = 0;
    [[nodiscard]] virtual auto receive(std::span<std::byte> buffer) -> Result<std::size_t> = 0;

    // Server operations
    [[nodiscard]] virtual auto listen(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto accept() -> Result<std::unique_ptr<transport_interface>> = 0;

    // QUIC-specific (no-op for TCP)
    [[nodiscard]] virtual auto create_stream() -> Result<stream_id>;
    [[nodiscard]] virtual auto close_stream(stream_id) -> Result<void>;
};
```

### Transport Configurations

```cpp
struct tcp_transport_config {
    bool        enable_tls      = true;
    bool        tcp_nodelay     = true;
    std::size_t send_buffer     = 256 * 1024;
    std::size_t recv_buffer     = 256 * 1024;
    duration    connect_timeout = 10s;
    duration    read_timeout    = 30s;
};

struct quic_transport_config {
    bool        enable_0rtt         = true;
    std::size_t max_streams         = 100;
    std::size_t initial_window      = 10 * 1024 * 1024;
    duration    idle_timeout        = 30s;
    bool        enable_migration    = true;
};
```

### transport_factory

Factory for creating transport instances.

```cpp
class transport_factory {
public:
    [[nodiscard]] static auto create(transport_type type)
        -> std::unique_ptr<transport_interface>;

    [[nodiscard]] static auto create_with_fallback(
        const endpoint& ep,
        transport_type preferred = transport_type::quic
    ) -> Result<std::unique_ptr<transport_interface>>;
};
```

---

## Complete Example

### Server Example

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

int main() {
    // Create and configure server
    auto server = file_transfer_server::builder()
        .with_storage_directory("/data/files")
        .with_max_connections(100)
        .with_max_file_size(10ULL * 1024 * 1024 * 1024)  // 10GB
        .with_storage_quota(1ULL * 1024 * 1024 * 1024 * 1024)  // 1TB
        .with_pipeline_config(pipeline_config::auto_detect())
        .build();

    if (!server) {
        std::cerr << "Failed to create server: " << server.error().message() << "\n";
        return 1;
    }

    // Set up callbacks
    server->on_upload_request([](const upload_request& req) {
        std::cout << "Upload request: " << req.filename
                  << " (" << req.file_size << " bytes)\n";
        return req.file_size < 1ULL * 1024 * 1024 * 1024;  // Accept < 1GB
    });

    server->on_download_request([](const download_request& req) {
        std::cout << "Download request: " << req.filename << "\n";
        return true;
    });

    server->on_transfer_complete([](const transfer_result& result) {
        if (result.verified) {
            std::cout << "Transfer complete: " << result.filename << "\n";
        } else {
            std::cerr << "Transfer failed: " << result.error->message << "\n";
        }
    });

    // Start server
    auto result = server->start(endpoint{"0.0.0.0", 19000});
    if (!result) {
        std::cerr << "Failed to start server: " << result.error().message() << "\n";
        return 1;
    }

    std::cout << "Server running on port 19000. Press Enter to stop.\n";
    std::cin.get();

    server->stop();
    return 0;
}
```

### Client Example

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

int main() {
    // Create and configure client
    auto client = file_transfer_client::builder()
        .with_compression(compression_mode::adaptive)
        .with_auto_reconnect(true)
        .with_reconnect_policy(reconnect_policy{
            .initial_delay = std::chrono::seconds(1),
            .max_delay = std::chrono::seconds(60),
            .multiplier = 2.0,
            .max_attempts = 10
        })
        .build();

    if (!client) {
        std::cerr << "Failed to create client: " << client.error().message() << "\n";
        return 1;
    }

    // Set up callbacks
    client->on_progress([](const transfer_progress& p) {
        double percent = 100.0 * p.bytes_transferred / p.total_bytes;
        std::cout << "\r" << p.filename << ": "
                  << std::fixed << std::setprecision(1) << percent << "% - "
                  << p.transfer_rate / (1024*1024) << " MB/s" << std::flush;
    });

    client->on_complete([](const transfer_result& result) {
        std::cout << "\n";
        if (result.verified) {
            std::cout << "Transfer complete: " << result.filename
                      << " (compression: " << result.compression_stats.compression_ratio()
                      << ":1)\n";
        } else {
            std::cerr << "Transfer failed: " << result.error->message << "\n";
        }
    });

    client->on_disconnected([](disconnect_reason reason) {
        std::cerr << "Disconnected: " << static_cast<int>(reason) << "\n";
    });

    // Connect to server
    auto connect_result = client->connect(endpoint{"192.168.1.100", 19000});
    if (!connect_result) {
        std::cerr << "Failed to connect: " << connect_result.error().message() << "\n";
        return 1;
    }

    // List files
    auto files = client->list_files();
    if (files) {
        std::cout << "Files on server:\n";
        for (const auto& file : *files) {
            std::cout << "  " << file.filename << " - " << file.file_size << " bytes\n";
        }
    }

    // Upload a file
    auto upload = client->upload_file("/local/data.zip", "data.zip");
    if (upload) {
        upload->wait();  // Wait for completion
    }

    // Download a file
    auto download = client->download_file("report.pdf", "/local/report.pdf");
    if (download) {
        download->wait();  // Wait for completion
    }

    client->disconnect();
    return 0;
}
```

---

*Last updated: 2025-12-13*
*Version: 0.2.1*
