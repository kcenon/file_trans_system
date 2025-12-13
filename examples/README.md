# file_trans_system Examples

Complete, compilable examples demonstrating the file transfer system library features.

## Building

Examples are enabled by default when building the project:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

To explicitly enable or disable examples:

```bash
# Enable examples
cmake -B build -DFILE_TRANS_BUILD_EXAMPLES=ON

# Disable examples
cmake -B build -DFILE_TRANS_BUILD_EXAMPLES=OFF
```

Built examples are located in `build/bin/`.

## Example Categories

### Basic Examples

| Example | File | Description |
|---------|------|-------------|
| Simple Server | `simple_server.cpp` | Basic server setup, callbacks, and graceful shutdown |
| Simple Client | `simple_client.cpp` | Connection, upload, download, and file listing |

### Upload/Download Examples

| Example | File | Description |
|---------|------|-------------|
| Upload | `upload_example.cpp` | Progress callbacks, compression settings, error handling |
| Download | `download_example.cpp` | Hash verification, overwrite policies, integrity check |
| Batch Upload | `batch_upload_example.cpp` | Parallel uploads, batch progress tracking |
| Batch Download | `batch_download_example.cpp` | Parallel downloads, file selection from server |

### Advanced Examples

| Example | File | Description |
|---------|------|-------------|
| Resume Transfer | `resume_transfer.cpp` | Pause/resume, interruption handling |
| Auto Reconnect | `auto_reconnect.cpp` | Reconnection policy, backoff configuration |
| Custom Pipeline | `custom_pipeline.cpp` | Chunk size, compression modes, LAN/WAN optimization |
| Bandwidth Throttling | `bandwidth_throttling.cpp` | Rate limiting, dynamic adjustment |

### Server Management Examples

| Example | File | Description |
|---------|------|-------------|
| Server Callbacks | `server_callbacks.cpp` | Request validation, access control, event logging |
| Quota Management | `quota_management.cpp` | Storage quotas, usage monitoring, thresholds |

## Quick Start

### Running the Server

```bash
# Start server on default port (8080)
./build/bin/simple_server

# Start server on custom port with custom storage directory
./build/bin/simple_server 9000 /path/to/storage
```

### Running the Client

```bash
# Upload a file
./build/bin/simple_client upload local_file.txt remote_name.txt

# Download a file
./build/bin/simple_client download remote_name.txt local_file.txt

# List files on server
./build/bin/simple_client list

# Connect to different server
./build/bin/simple_client upload file.txt file.txt 192.168.1.100:9000
```

## Example Details

### simple_server.cpp

Basic server setup demonstrating:
- Server builder pattern configuration
- Event callback registration (connection, upload complete, error)
- Signal handling for graceful shutdown
- Command-line argument parsing

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("./storage")
    .with_port(8080)
    .build();

server.on_upload_complete([](const upload_info& info) {
    std::cout << "Upload complete: " << info.filename << std::endl;
});

server.start();
```

### simple_client.cpp

Basic client operations demonstrating:
- Client builder pattern configuration
- Server connection
- File upload and download
- Server file listing
- Progress monitoring

```cpp
auto client = file_transfer_client::builder().build();

client.connect({"localhost", 8080});
client.upload_file("local.txt", "remote.txt").wait();
client.download_file("remote.txt", "downloaded.txt").wait();
client.disconnect();
```

### upload_example.cpp

Detailed upload example demonstrating:
- Compression settings configuration
- Progress callback with transfer speed calculation
- Comprehensive error handling
- Transfer handle usage for control

### download_example.cpp

Detailed download example demonstrating:
- Hash verification after download
- Overwrite policy configuration
- Progress monitoring
- File integrity verification

### batch_upload_example.cpp

Batch upload example demonstrating:
- Uploading multiple files concurrently
- Tracking progress across all files
- Handling individual file failures
- Configuring concurrency limits

### batch_download_example.cpp

Batch download example demonstrating:
- Downloading multiple files in parallel
- Selecting files from server file list
- Progress tracking per file and overall
- Error handling for partial failures

### resume_transfer.cpp

Resume transfer example demonstrating:
- Pausing active transfers
- Resuming interrupted transfers
- Simulating network interruptions
- Progress monitoring during pause/resume

### auto_reconnect.cpp

Auto-reconnection example demonstrating:
- Reconnection policy configuration
- Exponential backoff settings
- Connection state change callbacks
- Graceful handling of network drops

### custom_pipeline.cpp

Pipeline customization example demonstrating:
- Chunk size optimization for different file types
- Compression mode selection (LZ4 fast, LZ4 HC)
- LAN vs WAN configuration profiles
- Performance comparison between settings

### bandwidth_throttling.cpp

Bandwidth throttling example demonstrating:
- Setting upload/download rate limits
- Monitoring actual transfer rates
- Comparing throttled vs unlimited transfers
- Dynamic bandwidth adjustment

### server_callbacks.cpp

Server callbacks example demonstrating:
- Upload/download request validation
- Client connection monitoring
- Access control implementation
- Server event logging
- Transfer progress tracking

### quota_management.cpp

Quota management example demonstrating:
- Storage quota configuration
- Real-time usage monitoring
- Warning threshold implementation
- Rejection when quota exceeded
- Storage statistics display

## Common Patterns

### Error Handling

```cpp
auto result = client.upload_file("file.txt", "remote.txt");
if (!result) {
    std::cerr << "Upload failed: " << result.error().message() << std::endl;
    return 1;
}
```

### Progress Monitoring

```cpp
client.upload_file("large_file.bin", "remote.bin",
    [](const progress_info& progress) {
        std::cout << progress.percent_complete << "% - "
                  << progress.bytes_transferred << "/" << progress.total_bytes
                  << std::endl;
    });
```

### Graceful Shutdown

```cpp
std::atomic<bool> running{true};

std::signal(SIGINT, [](int) { running = false; });

while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

server.stop();
```

## Dependencies

All examples require the file_trans_system library to be built. Some examples may create temporary files in the current directory for demonstration purposes.

## Notes

- Examples include comprehensive error handling as best practice
- Default server address is `localhost:8080`
- Server creates storage directory if it doesn't exist
- Client examples may generate test files for demonstration
