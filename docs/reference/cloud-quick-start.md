# Cloud Storage Quick Start Guide

This guide helps you get started with cloud storage integration in the File Transfer System.

## Overview

The File Transfer System supports three major cloud storage providers:

| Provider | Class | Configuration |
|----------|-------|---------------|
| AWS S3 | `s3_storage` | `s3_config` |
| Azure Blob Storage | `azure_blob_storage` | `azure_blob_config` |
| Google Cloud Storage | `gcs_storage` | `gcs_config` |

All providers implement the common `cloud_storage_interface`, allowing for consistent usage patterns.

## Prerequisites

### Build Requirements

Enable encryption support for cloud storage (required for request signing):

```bash
cmake -DFILE_TRANS_ENABLE_ENCRYPTION=ON ..
cmake --build .
```

### Provider-Specific Setup

- **AWS S3**: See [AWS S3 Setup Guide](../cloud/AWS_S3_SETUP.md)
- **Azure Blob**: See [Azure Blob Setup Guide](../cloud/AZURE_BLOB_SETUP.md)
- **Google Cloud Storage**: See [GCS Setup Guide](../cloud/GCS_SETUP.md)

## Quick Start Examples

### AWS S3

```cpp
#include "kcenon/file_transfer/cloud/s3_storage.h"

using namespace kcenon::file_transfer;

// Create credentials (from environment or AWS credentials file)
auto credentials = s3_credential_provider::create_default();

// Create configuration
auto config = cloud_config_builder::s3()
    .with_bucket("my-bucket")
    .with_region("us-east-1")
    .build_s3();

// Create and connect
auto storage = s3_storage::create(config, credentials);
storage->connect();

// Upload
std::vector<std::byte> data = /* your data */;
auto result = storage->upload("path/to/file.bin", data);

// Download
auto downloaded = storage->download("path/to/file.bin");

// Cleanup
storage->disconnect();
```

### Azure Blob Storage

```cpp
#include "kcenon/file_transfer/cloud/azure_blob_storage.h"

using namespace kcenon::file_transfer;

// Create credentials
auto credentials = azure_blob_credential_provider::create_from_environment();

// Create configuration
auto config = cloud_config_builder::azure_blob()
    .with_account_name("mystorageaccount")
    .with_bucket("my-container")
    .build_azure_blob();

// Create and connect
auto storage = azure_blob_storage::create(config, std::move(credentials));
storage->connect();

// Use same interface as S3
auto result = storage->upload("path/to/file.bin", data);
```

### Google Cloud Storage

```cpp
#include "kcenon/file_transfer/cloud/gcs_storage.h"

using namespace kcenon::file_transfer;

// Create credentials (from service account or ADC)
auto credentials = gcs_credential_provider::create_default();

// Create configuration
auto config = cloud_config_builder::gcs()
    .with_bucket("my-bucket")
    .with_project_id("my-project")
    .build_gcs();

// Create and connect
auto storage = gcs_storage::create(config, std::move(credentials));
storage->connect();

// Use same interface as S3/Azure
auto result = storage->upload("path/to/file.bin", data);
```

## Common Operations

### Upload Data

```cpp
// Upload bytes directly
std::vector<std::byte> data = /* ... */;
auto result = storage->upload("key", data);

// Upload from file
auto result = storage->upload_file("/local/path/file.txt", "remote/file.txt");

// Async upload
auto future = storage->upload_async("key", data);
auto result = future.get();  // Wait for completion
```

### Download Data

```cpp
// Download to memory
auto result = storage->download("key");
if (result.has_value()) {
    auto& data = result.value();
    // Use data
}

// Download to file
auto result = storage->download_file("remote/file.txt", "/local/path/file.txt");
```

### Streaming (Large Files)

```cpp
// Streaming upload
auto stream = storage->create_upload_stream("large-file.bin");
while (has_more_data) {
    std::vector<std::byte> chunk = read_chunk();
    stream->write(chunk);
}
auto result = stream->finalize();

// Streaming download
auto stream = storage->create_download_stream("large-file.bin");
while (stream->has_more()) {
    std::vector<std::byte> buffer(1024 * 1024);
    auto bytes = stream->read(buffer);
}
```

### List Objects

```cpp
list_objects_options options;
options.prefix = "folder/";
options.max_keys = 100;

auto result = storage->list_objects(options);
if (result.has_value()) {
    for (const auto& obj : result.value().objects) {
        std::cout << obj.key << " (" << obj.size << " bytes)\n";
    }
}
```

### Delete Objects

```cpp
// Single object
auto result = storage->delete_object("path/to/file.bin");

// Multiple objects
std::vector<std::string> keys = {"file1.bin", "file2.bin", "file3.bin"};
auto results = storage->delete_objects(keys);
```

### Presigned URLs

```cpp
presigned_url_options options;
options.method = "GET";
options.expiration = std::chrono::seconds{3600};  // 1 hour

auto url = storage->generate_presigned_url("file.bin", options);
// Share URL for direct access without credentials
```

## Progress Tracking

```cpp
// Upload progress
storage->on_upload_progress([](const upload_progress& p) {
    std::cout << p.percentage() << "% complete\n";
    std::cout << p.bytes_transferred << "/" << p.total_bytes << " bytes\n";
    std::cout << "Speed: " << p.speed_bps << " bytes/sec\n";
});

// Download progress
storage->on_download_progress([](const download_progress& p) {
    std::cout << p.percentage() << "% complete\n";
});

// State changes
storage->on_state_changed([](cloud_storage_state state) {
    std::cout << "State: " << to_string(state) << "\n";
});
```

## Error Handling

```cpp
auto result = storage->upload("key", data);
if (!result.has_value()) {
    auto& error = result.error();

    switch (error.code) {
        case error_code::connection_failed:
            // Handle network error
            break;
        case error_code::file_access_denied:
            // Handle permission error
            break;
        case error_code::file_not_found:
            // Handle missing file
            break;
        default:
            std::cerr << "Error: " << error.message << "\n";
    }
}
```

## Statistics

```cpp
auto stats = storage->get_statistics();
std::cout << "Uploaded: " << stats.bytes_uploaded << " bytes\n";
std::cout << "Downloaded: " << stats.bytes_downloaded << " bytes\n";
std::cout << "Operations: " << stats.upload_count + stats.download_count << "\n";
std::cout << "Errors: " << stats.errors << "\n";

// Reset counters
storage->reset_statistics();
```

## Examples

The `examples/cloud/` directory contains complete working examples:

| Example | Description |
|---------|-------------|
| `s3_example.cpp` | AWS S3 basic operations |
| `azure_blob_example.cpp` | Azure Blob Storage operations |
| `gcs_example.cpp` | Google Cloud Storage operations |
| `hybrid_storage_example.cpp` | Local + cloud hybrid storage |
| `multi_cloud_failover_example.cpp` | Multi-cloud failover |
| `large_file_transfer_example.cpp` | Streaming large file uploads |

Build and run:

```bash
cmake --build build --target s3_example
./build/bin/s3_example my-bucket us-east-1
```

## Next Steps

- Read the [Cloud Storage Best Practices](cloud-best-practices.md) guide
- Explore [Cloud Storage Architecture](cloud-storage-layer.md) documentation
- Check provider-specific setup guides for advanced configuration
