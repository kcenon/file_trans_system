# Cloud Storage Abstraction Layer

## Overview

The cloud storage abstraction layer provides a unified interface for different cloud storage providers (AWS S3, Azure Blob Storage, Google Cloud Storage). This enables seamless integration with multiple cloud backends while maintaining consistent API usage throughout the file transfer system.

## Architecture

```
┌─────────────────────────────────────────┐
│         Application Layer               │
│    (file_transfer_server)               │
└─────────────────┬───────────────────────┘
                  │
┌─────────────────▼───────────────────────┐
│     cloud_storage_interface             │
│  (Abstract base class)                  │
├─────────────────────────────────────────┤
│  - connect() / disconnect()             │
│  - upload() / download()                │
│  - upload_async() / download_async()    │
│  - create_upload_stream()               │
│  - create_download_stream()             │
│  - generate_presigned_url()             │
└─────────────────┬───────────────────────┘
                  │
       ┌──────────┼──────────┐
       │          │          │
┌──────▼──────┐ ┌─▼─────────┐ ┌──────▼──────┐
│  s3_storage │ │azure_blob │ │gcs_storage  │
│             │ │  _storage │ │             │
└─────────────┘ └───────────┘ └─────────────┘
```

## Components

### cloud_storage_interface

The base class that defines the cloud storage contract:

```cpp
#include "kcenon/file_transfer/cloud/cloud_storage_interface.h"

class cloud_storage_interface {
public:
    // Provider identification
    virtual auto provider() const -> cloud_provider = 0;
    virtual auto provider_name() const -> std::string_view = 0;

    // Connection management
    virtual auto connect() -> result<void> = 0;
    virtual auto disconnect() -> result<void> = 0;
    virtual auto is_connected() const -> bool = 0;
    virtual auto state() const -> cloud_storage_state = 0;

    // Synchronous operations
    virtual auto upload(const std::string& key,
                       std::span<const std::byte> data,
                       const cloud_transfer_options& options = {})
        -> result<upload_result> = 0;
    virtual auto download(const std::string& key)
        -> result<std::vector<std::byte>> = 0;
    virtual auto upload_file(const std::filesystem::path& local_path,
                            const std::string& key,
                            const cloud_transfer_options& options = {})
        -> result<upload_result> = 0;
    virtual auto download_file(const std::string& key,
                              const std::filesystem::path& local_path)
        -> result<download_result> = 0;

    // Asynchronous operations
    virtual auto upload_async(...) -> std::future<result<upload_result>> = 0;
    virtual auto download_async(...) -> std::future<result<std::vector<std::byte>>> = 0;

    // Streaming operations
    virtual auto create_upload_stream(const std::string& key,
                                      const cloud_transfer_options& options = {})
        -> std::unique_ptr<cloud_upload_stream> = 0;
    virtual auto create_download_stream(const std::string& key)
        -> std::unique_ptr<cloud_download_stream> = 0;

    // Object operations
    virtual auto exists(const std::string& key) -> result<bool> = 0;
    virtual auto delete_object(const std::string& key) -> result<delete_result> = 0;
    virtual auto list_objects(const list_objects_options& options = {})
        -> result<list_objects_result> = 0;
    virtual auto get_metadata(const std::string& key)
        -> result<cloud_object_metadata> = 0;

    // Presigned URLs
    virtual auto generate_presigned_url(const std::string& key,
                                        const presigned_url_options& options = {})
        -> result<std::string> = 0;
};
```

### Credentials

Multiple credential types are supported for each provider:

```cpp
#include "kcenon/file_transfer/cloud/cloud_credentials.h"

// Static credentials (AWS S3)
static_credentials creds;
creds.access_key_id = "AKIAIOSFODNN7EXAMPLE";
creds.secret_access_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";

// Azure credentials
azure_credentials azure_creds;
azure_creds.account_name = "mystorageaccount";
azure_creds.account_key = "base64-account-key";

// GCS credentials
gcs_credentials gcs_creds;
gcs_creds.service_account_file = "/path/to/service-account.json";

// Assume role (AWS STS)
assume_role_credentials sts_creds;
sts_creds.role_arn = "arn:aws:iam::123456789012:role/MyRole";
sts_creds.role_session_name = "my-session";

// Profile-based credentials
profile_credentials profile_creds;
profile_creds.profile_name = "production";
```

### Configuration

Provider-specific configurations with builder pattern:

```cpp
#include "kcenon/file_transfer/cloud/cloud_config.h"

// AWS S3 configuration
auto s3_config = cloud_config_builder::s3()
    .with_bucket("my-bucket")
    .with_region("us-east-1")
    .with_connect_timeout(std::chrono::milliseconds{5000})
    .with_transfer_acceleration(true)
    .build_s3();

// Azure Blob configuration
auto azure_config = cloud_config_builder::azure_blob()
    .with_bucket("my-container")
    .with_account_name("mystorageaccount")
    .with_access_tier("Hot")
    .build_azure_blob();

// Google Cloud Storage configuration
auto gcs_config = cloud_config_builder::gcs()
    .with_bucket("my-gcs-bucket")
    .with_project_id("my-project-123")
    .with_region("us-central1")
    .build_gcs();

// S3-compatible storage (MinIO, etc.)
auto minio_config = cloud_config_builder::s3()
    .with_bucket("my-bucket")
    .with_endpoint("http://localhost:9000")
    .with_path_style(true)
    .with_ssl(false, false)
    .build_s3();
```

### Retry Policy

Configure automatic retry behavior:

```cpp
cloud_retry_policy retry;
retry.max_attempts = 5;
retry.initial_delay = std::chrono::milliseconds{500};
retry.max_delay = std::chrono::milliseconds{30000};
retry.backoff_multiplier = 2.0;
retry.use_jitter = true;
retry.retry_on_rate_limit = true;
retry.retry_on_connection_error = true;

auto config = cloud_config_builder::s3()
    .with_bucket("my-bucket")
    .with_retry_policy(retry)
    .build_s3();
```

### Multipart Upload

Configure multipart upload for large files:

```cpp
multipart_config multipart;
multipart.enabled = true;
multipart.threshold = 100 * 1024 * 1024;     // 100MB
multipart.part_size = 10 * 1024 * 1024;       // 10MB
multipart.max_concurrent_parts = 8;           // Parallel upload threads
multipart.part_timeout = std::chrono::milliseconds{300000};  // 5 minutes
multipart.max_part_retries = 3;               // Retry failed parts

auto config = cloud_config_builder::s3()
    .with_bucket("my-bucket")
    .with_multipart(multipart)
    .build_s3();
```

#### Features

- **Automatic Part Management**: Files are automatically split into parts based on `part_size`
- **Concurrent Uploads**: Multiple parts are uploaded simultaneously based on `max_concurrent_parts`
- **Retry with Exponential Backoff**: Failed part uploads are retried with exponential backoff and jitter
- **Abort on Failure**: If any part fails after all retries, the multipart upload is automatically aborted
- **Progress Tracking**: Per-part progress is tracked and aggregated for overall progress reporting

#### S3 Multipart Upload Workflow

```
┌─────────────────────────────────────────────────────────┐
│                   create_upload_stream()                 │
└─────────────────────────┬───────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│           POST /{bucket}/{key}?uploads                   │
│           (Initiate Multipart Upload)                    │
│           Returns: UploadId                              │
└─────────────────────────┬───────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│                    write() loop                          │
│  ┌────────────────────────────────────────────────────┐ │
│  │   PUT /{bucket}/{key}?partNumber=N&uploadId=X      │ │
│  │   (Upload Part - concurrent)                       │ │
│  │   Returns: ETag                                    │ │
│  └────────────────────────────────────────────────────┘ │
└─────────────────────────┬───────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│                    finalize()                            │
│           POST /{bucket}/{key}?uploadId=X               │
│           (Complete Multipart Upload)                    │
│           Body: <Part><PartNumber>N</PartNumber>        │
│                 <ETag>etag</ETag></Part>                │
└─────────────────────────────────────────────────────────┘
```

#### Error Handling

```cpp
// Abort on failure (automatic)
auto stream = storage->create_upload_stream("large-file.bin");
for (const auto& chunk : chunks) {
    auto result = stream->write(chunk);
    if (!result.has_value()) {
        // Abort is called automatically on destruction
        // or explicitly:
        stream->abort();
        break;
    }
}
```

## Error Handling

Cloud-specific error codes in the -800 to -899 range:

```cpp
#include "kcenon/file_transfer/cloud/cloud_error.h"

// Error code ranges
// -800 to -809: Authentication errors
// -810 to -819: Authorization errors
// -820 to -829: Connection/Network errors
// -830 to -839: Bucket/Container errors
// -840 to -849: Object/Blob errors
// -850 to -859: Transfer errors
// -860 to -869: Quota/Limit errors
// -870 to -879: Provider-specific errors
// -880 to -889: Configuration errors
// -890 to -899: Internal errors

// Check error types
if (is_cloud_retryable(error_code)) {
    // Retry the operation
}

if (is_auth_error(error_code)) {
    // Refresh credentials
}

if (is_bucket_error(error_code)) {
    // Handle bucket-related issues
}
```

## Progress Tracking

Monitor upload and download progress:

```cpp
// Set upload progress callback
storage->on_upload_progress([](const upload_progress& progress) {
    std::cout << "Upload: " << progress.percentage() << "% "
              << "(" << progress.bytes_transferred << "/" << progress.total_bytes << ")"
              << " Speed: " << progress.speed_bps / 1024 << " KB/s"
              << std::endl;
});

// Set download progress callback
storage->on_download_progress([](const download_progress& progress) {
    std::cout << "Download: " << progress.percentage() << "%" << std::endl;
});
```

## Streaming Operations

For large files, use streaming to avoid loading entire files into memory:

```cpp
// Streaming upload
auto stream = storage->create_upload_stream("large-file.bin");
if (stream) {
    while (has_more_data) {
        auto data = read_chunk();
        auto result = stream->write(data);
        if (!result.has_value()) {
            stream->abort();
            break;
        }
    }
    auto upload_result = stream->finalize();
}

// Streaming download
auto stream = storage->create_download_stream("large-file.bin");
if (stream) {
    std::vector<std::byte> buffer(64 * 1024);
    while (stream->has_more()) {
        auto bytes_read = stream->read(buffer);
        if (bytes_read.has_value()) {
            process_chunk(buffer, bytes_read.value());
        }
    }
}
```

## Presigned URLs

Generate temporary URLs for direct client access:

```cpp
presigned_url_options options;
options.expiration = std::chrono::seconds{3600};  // 1 hour
options.method = "GET";  // or "PUT" for uploads

auto url = storage->generate_presigned_url("my-file.txt", options);
if (url.has_value()) {
    std::cout << "Download URL: " << url.value() << std::endl;
}
```

## Provider Support

| Feature | AWS S3 | Azure Blob | GCS |
|---------|--------|------------|-----|
| Upload/Download | Yes | Yes | Yes |
| Multipart Upload | Yes | Yes | Yes |
| Streaming | Yes | Yes | Yes |
| Presigned URLs | Yes | Yes | Yes |
| Server-side Encryption | Yes | Yes | Yes |
| Customer-provided Keys | Yes | Yes | Yes |
| Transfer Acceleration | Yes | No | No |
| Versioning | Yes | Yes | Yes |

## Files

- `include/kcenon/file_transfer/cloud/cloud_storage_interface.h` - Main interface
- `include/kcenon/file_transfer/cloud/cloud_config.h` - Configuration structures
- `include/kcenon/file_transfer/cloud/cloud_credentials.h` - Credential management
- `include/kcenon/file_transfer/cloud/cloud_error.h` - Error codes

## See Also

- [Transport Layer](transport-layer.md) - Transport abstraction
- [Encryption Layer](encryption-layer.md) - Encryption support
- [Error Codes](error-codes.md) - Complete error code reference
