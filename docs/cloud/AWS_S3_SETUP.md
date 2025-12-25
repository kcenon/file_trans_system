# AWS S3 Storage Setup Guide

This guide explains how to configure and use the AWS S3 storage backend in the File Transfer System.

## Prerequisites

- OpenSSL development libraries (required for AWS Signature V4)
- AWS account with S3 access
- IAM credentials with appropriate S3 permissions

## Build Requirements

The S3 storage backend requires encryption support to be enabled:

```bash
cmake -DFILE_TRANS_ENABLE_ENCRYPTION=ON ..
```

## Configuration

### AWS Credentials

The S3 storage backend supports multiple credential sources:

#### 1. Environment Variables

```bash
export AWS_ACCESS_KEY_ID=AKIAIOSFODNN7EXAMPLE
export AWS_SECRET_ACCESS_KEY=wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY
export AWS_SESSION_TOKEN=optional-session-token
export AWS_REGION=us-east-1
```

#### 2. AWS Credentials File

Create `~/.aws/credentials`:

```ini
[default]
aws_access_key_id = AKIAIOSFODNN7EXAMPLE
aws_secret_access_key = wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY

[production]
aws_access_key_id = AKIAI44QH8DHBEXAMPLE
aws_secret_access_key = je7MtGbClwBF/2Zp9Utk/h3yCo8nvbEXAMPLEKEY
```

#### 3. Static Credentials (Code)

```cpp
static_credentials creds;
creds.access_key_id = "AKIAIOSFODNN7EXAMPLE";
creds.secret_access_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";

auto provider = s3_credential_provider::create(creds);
```

### S3 Configuration Options

```cpp
auto config = cloud_config_builder::s3()
    .with_bucket("my-bucket")
    .with_region("us-east-1")
    .with_connect_timeout(std::chrono::milliseconds{30000})
    .with_connection_pool_size(25)
    .with_transfer_acceleration(false)
    .with_dualstack(false)
    .build_s3();
```

#### Available Options

| Option | Description | Default |
|--------|-------------|---------|
| `bucket` | S3 bucket name | Required |
| `region` | AWS region | Required (unless endpoint set) |
| `endpoint` | Custom endpoint URL | AWS default |
| `use_path_style` | Use path-style URLs | `false` |
| `use_ssl` | Enable HTTPS | `true` |
| `verify_ssl` | Verify SSL certificates | `true` |
| `connect_timeout` | Connection timeout | 30 seconds |
| `connection_pool_size` | Max connections | 25 |
| `use_transfer_acceleration` | S3 Transfer Acceleration | `false` |
| `use_dualstack` | IPv4/IPv6 dual-stack | `false` |

## Basic Usage

### Creating S3 Storage

```cpp
#include "kcenon/file_transfer/cloud/s3_storage.h"

using namespace kcenon::file_transfer;

// Create credentials
auto credentials = s3_credential_provider::create_default();

// Create configuration
auto config = cloud_config_builder::s3()
    .with_bucket("my-bucket")
    .with_region("us-east-1")
    .build_s3();

// Create storage
auto storage = s3_storage::create(config, credentials);

// Connect
auto result = storage->connect();
if (result.has_value()) {
    // Ready to use
}
```

### Upload Operations

```cpp
// Upload data directly
std::vector<std::byte> data = /* ... */;
auto result = storage->upload("path/to/file.bin", data);

// Upload file
auto result = storage->upload_file("/local/file.txt", "remote/file.txt");

// Async upload
auto future = storage->upload_async("key", data);
auto result = future.get();
```

### Download Operations

```cpp
// Download data
auto result = storage->download("path/to/file.bin");
if (result.has_value()) {
    auto& data = result.value();
    // Use data
}

// Download to file
auto result = storage->download_file("remote/file.txt", "/local/file.txt");
```

### Streaming Upload (Multipart)

```cpp
auto stream = storage->create_upload_stream("large-file.bin");

while (has_more_data) {
    std::vector<std::byte> chunk = read_chunk();
    stream->write(chunk);
}

auto result = stream->finalize();
```

### Presigned URLs

```cpp
presigned_url_options options;
options.method = "GET";
options.expiration = std::chrono::seconds{3600};

auto url = storage->generate_presigned_url("file.bin", options);
// Share URL for direct access
```

## S3-Compatible Storage (MinIO)

For MinIO or other S3-compatible storage:

```cpp
auto config = cloud_config_builder::s3()
    .with_bucket("my-bucket")
    .with_region("us-east-1")
    .with_endpoint("http://localhost:9000")
    .with_path_style(true)
    .with_ssl(false, false)
    .build_s3();
```

## IAM Permissions

Minimum required IAM permissions:

```json
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Action": [
                "s3:PutObject",
                "s3:GetObject",
                "s3:DeleteObject",
                "s3:ListBucket",
                "s3:HeadObject",
                "s3:GetObjectAttributes"
            ],
            "Resource": [
                "arn:aws:s3:::my-bucket",
                "arn:aws:s3:::my-bucket/*"
            ]
        }
    ]
}
```

For multipart uploads, add:

```json
{
    "Action": [
        "s3:CreateMultipartUpload",
        "s3:UploadPart",
        "s3:CompleteMultipartUpload",
        "s3:AbortMultipartUpload",
        "s3:ListMultipartUploadParts"
    ]
}
```

## Error Handling

```cpp
auto result = storage->upload("key", data);
if (!result.has_value()) {
    auto& error = result.error();

    if (error.code == error_code::connection_failed) {
        // Handle connection error
    } else if (error.code == error_code::file_access_denied) {
        // Handle permission error
    }

    std::cerr << "Error: " << error.message << std::endl;
}
```

## Progress Tracking

```cpp
storage->on_upload_progress([](const upload_progress& p) {
    std::cout << p.percentage() << "% complete\n";
    std::cout << p.bytes_transferred << "/" << p.total_bytes << " bytes\n";
});

storage->on_download_progress([](const download_progress& p) {
    std::cout << p.percentage() << "% complete\n";
});
```

## Statistics

```cpp
auto stats = storage->get_statistics();
std::cout << "Uploaded: " << stats.bytes_uploaded << " bytes\n";
std::cout << "Downloaded: " << stats.bytes_downloaded << " bytes\n";
std::cout << "Errors: " << stats.errors << "\n";

// Reset statistics
storage->reset_statistics();
```

## Troubleshooting

### Connection Issues

1. Verify AWS credentials are set correctly
2. Check region matches bucket location
3. Verify network connectivity to S3 endpoint
4. Check firewall rules for HTTPS (port 443)

### Permission Denied

1. Verify IAM policy includes required actions
2. Check bucket policy allows access
3. Verify bucket and key names are correct

### SSL/TLS Errors

1. Verify OpenSSL is installed correctly
2. Check CA certificates are up to date
3. For testing, use `.with_ssl(true, false)` to skip verification

## Example

See `examples/cloud/s3_example.cpp` for a complete working example.

```bash
# Build
cmake --build build --target s3_example

# Run
./build/bin/s3_example my-bucket us-east-1

# With MinIO
./build/bin/s3_example my-bucket us-east-1 http://localhost:9000
```
