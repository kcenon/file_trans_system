# Google Cloud Storage Setup Guide

This guide explains how to configure and use the Google Cloud Storage (GCS) backend in the File Transfer System.

## Prerequisites

- OpenSSL development libraries (required for JWT signing)
- Google Cloud account with GCS access
- Service account or Application Default Credentials
- A GCS bucket with appropriate permissions

## Build Requirements

The GCS storage backend requires encryption support:

```bash
cmake -DFILE_TRANS_ENABLE_ENCRYPTION=ON ..
```

## Configuration

### GCS Credentials

The GCS storage backend supports multiple credential sources:

#### 1. Application Default Credentials (ADC)

The recommended method for production environments:

```bash
# Set up ADC
gcloud auth application-default login

# Or set the environment variable
export GOOGLE_APPLICATION_CREDENTIALS=/path/to/service-account.json
```

```cpp
auto credentials = gcs_credential_provider::create_default();
```

#### 2. Service Account JSON Key

```cpp
auto credentials = gcs_credential_provider::create_from_service_account(
    "/path/to/service-account.json"
);
```

#### 3. Service Account JSON String

```cpp
std::string json_key = R"({
    "type": "service_account",
    "project_id": "my-project",
    "private_key_id": "...",
    "private_key": "-----BEGIN PRIVATE KEY-----\n...\n-----END PRIVATE KEY-----\n",
    "client_email": "my-service-account@my-project.iam.gserviceaccount.com",
    ...
})";

auto credentials = gcs_credential_provider::create_from_json(json_key);
```

#### 4. Environment Variable

```bash
export GOOGLE_APPLICATION_CREDENTIALS=/path/to/service-account.json
```

### GCS Configuration Options

```cpp
auto config = cloud_config_builder::gcs()
    .with_bucket("my-bucket")
    .with_project_id("my-project")
    .with_connect_timeout(std::chrono::milliseconds{30000})
    .with_connection_pool_size(25)
    .build_gcs();
```

#### Available Options

| Option | Description | Default |
|--------|-------------|---------|
| `bucket` | GCS bucket name | Required |
| `project_id` | GCP project ID | Auto-detected from credentials |
| `endpoint` | Custom endpoint URL | GCS default |
| `use_ssl` | Enable HTTPS | `true` |
| `verify_ssl` | Verify SSL certificates | `true` |
| `connect_timeout` | Connection timeout | 30 seconds |
| `connection_pool_size` | Max connections | 25 |

## Basic Usage

### Creating GCS Storage

```cpp
#include "kcenon/file_transfer/cloud/gcs_storage.h"

using namespace kcenon::file_transfer;

// Create credentials
auto credentials = gcs_credential_provider::create_default();

// Create configuration
auto config = cloud_config_builder::gcs()
    .with_bucket("my-bucket")
    .with_project_id("my-project")
    .build_gcs();

// Create storage
auto storage = gcs_storage::create(config, std::move(credentials));

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
auto result = storage->upload("path/to/object", data);

// Upload file
auto result = storage->upload_file("/local/file.txt", "remote/file.txt");

// Async upload
auto future = storage->upload_async("key", data);
auto result = future.get();
```

### Download Operations

```cpp
// Download data
auto result = storage->download("path/to/object");
if (result.has_value()) {
    auto& data = result.value();
    // Use data
}

// Download to file
auto result = storage->download_file("remote/file.txt", "/local/file.txt");
```

### Streaming Upload (Resumable Uploads)

GCS uses resumable uploads for large files:

```cpp
auto stream = storage->create_upload_stream("large-file.bin");

while (has_more_data) {
    std::vector<std::byte> chunk = read_chunk();
    stream->write(chunk);
}

auto result = stream->finalize();
```

### Signed URLs

```cpp
presigned_url_options options;
options.method = "GET";
options.expiration = std::chrono::seconds{3600};

auto signed_url = storage->generate_presigned_url("file.bin", options);
// Share URL for direct access
```

### Storage Class Management

GCS supports multiple storage classes:

```cpp
// Upload with specific storage class
cloud_transfer_options options;
options.storage_class = "NEARLINE";  // STANDARD, NEARLINE, COLDLINE, ARCHIVE

auto result = storage->upload("file.bin", data, options);

// Get storage class
auto metadata = storage->get_metadata("file.bin");
if (metadata.has_value()) {
    std::cout << "Storage class: " << metadata.value().storage_class.value_or("STANDARD") << "\n";
}
```

## Fake GCS Server (Local Testing)

For local development, use fake-gcs-server:

```bash
# Run with Docker
docker run -d \
    --name fake-gcs-server \
    -p 4443:4443 \
    fsouza/fake-gcs-server \
    -scheme http

# Create bucket
curl -X POST \
    -H "Content-Type: application/json" \
    -d '{"name":"test-bucket"}' \
    "http://localhost:4443/storage/v1/b?project=test-project"
```

Configure for fake-gcs-server:

```cpp
auto config = cloud_config_builder::gcs()
    .with_bucket("test-bucket")
    .with_project_id("test-project")
    .with_endpoint("http://localhost:4443")
    .with_ssl(false, false)
    .build_gcs();

// For fake server, use anonymous or test credentials
auto credentials = gcs_credential_provider::create_anonymous();
```

## IAM Permissions

Minimum required IAM roles for the service account:

| Role | Description |
|------|-------------|
| `roles/storage.objectViewer` | Read objects |
| `roles/storage.objectCreator` | Create objects |
| `roles/storage.objectAdmin` | Full object control |

Or grant specific permissions:

```bash
# Using gcloud
gcloud projects add-iam-policy-binding my-project \
    --member="serviceAccount:my-sa@my-project.iam.gserviceaccount.com" \
    --role="roles/storage.objectAdmin"
```

Required permissions for common operations:

| Permission | Required For |
|------------|--------------|
| `storage.objects.get` | Download objects |
| `storage.objects.create` | Upload objects |
| `storage.objects.delete` | Delete objects |
| `storage.objects.list` | List objects |
| `storage.buckets.get` | Get bucket metadata |

## Error Handling

```cpp
auto result = storage->upload("key", data);
if (!result.has_value()) {
    auto& error = result.error();

    if (error.code == error_code::connection_failed) {
        // Handle connection error
    } else if (error.code == error_code::file_access_denied) {
        // Handle permission error (check IAM permissions)
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

### Authentication Issues

1. Verify service account JSON key is valid
2. Check GOOGLE_APPLICATION_CREDENTIALS path
3. Run `gcloud auth application-default login` for local development
4. Verify service account has required permissions

### Connection Issues

1. Verify bucket name is correct
2. Check network connectivity to GCS endpoints
3. Verify firewall rules allow HTTPS (port 443)
4. Check if VPC Service Controls are blocking access

### Permission Denied

1. Verify service account has required IAM roles
2. Check bucket-level IAM policies
3. Verify project ID is correct
4. Check uniform bucket-level access settings

### SSL/TLS Errors

1. Verify OpenSSL is installed correctly
2. Check CA certificates are up to date
3. For testing, use `.with_ssl(true, false)` to skip verification

## Object Versioning

If bucket versioning is enabled:

```cpp
// Get specific version
auto metadata = storage->get_metadata("file.bin");
if (metadata.has_value()) {
    auto version = metadata.value().version_id;
}

// List versions
list_objects_options options;
options.versions = true;  // Include object versions
auto result = storage->list_objects(options);
```

## Object Lifecycle

Configure lifecycle rules in GCS Console or via gcloud:

```bash
# Example: Delete objects older than 30 days
gcloud storage buckets update gs://my-bucket \
    --lifecycle-file=lifecycle.json
```

```json
{
  "rule": [
    {
      "action": {"type": "Delete"},
      "condition": {"age": 30}
    },
    {
      "action": {"type": "SetStorageClass", "storageClass": "NEARLINE"},
      "condition": {"age": 7}
    }
  ]
}
```

## Example

See `examples/cloud/gcs_example.cpp` for a complete working example.

```bash
# Build
cmake --build build --target gcs_example

# Run
./build/bin/gcs_example my-bucket my-project

# With fake-gcs-server
./build/bin/gcs_example test-bucket test-project http://localhost:4443
```
