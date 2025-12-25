# Azure Blob Storage Setup Guide

This guide explains how to configure and use the Azure Blob Storage backend in the File Transfer System.

## Prerequisites

- OpenSSL development libraries (required for HMAC-SHA256 signing)
- Azure storage account
- Container with appropriate permissions

## Build Requirements

The Azure Blob storage backend requires encryption support:

```bash
cmake -DFILE_TRANS_ENABLE_ENCRYPTION=ON ..
```

## Configuration

### Azure Credentials

The Azure Blob storage backend supports multiple credential sources:

#### 1. Environment Variables

```bash
# Account Key Authentication
export AZURE_STORAGE_ACCOUNT=mystorageaccount
export AZURE_STORAGE_KEY=your-storage-account-key

# Or Connection String
export AZURE_STORAGE_CONNECTION_STRING="DefaultEndpointsProtocol=https;AccountName=...;AccountKey=...;EndpointSuffix=core.windows.net"

# Or SAS Token
export AZURE_STORAGE_SAS_TOKEN="?sv=2020-08-04&ss=b&srt=sco&sp=rwdlacx&..."
```

#### 2. Connection String (Code)

```cpp
auto credentials = azure_blob_credential_provider::create_from_connection_string(
    "DefaultEndpointsProtocol=https;AccountName=mystorageaccount;AccountKey=...;EndpointSuffix=core.windows.net"
);
```

#### 3. Account Key (Code)

```cpp
auto credentials = azure_blob_credential_provider::create_from_account_key(
    "mystorageaccount",
    "your-storage-account-key"
);
```

#### 4. SAS Token (Code)

```cpp
auto credentials = azure_blob_credential_provider::create_from_sas_token(
    "mystorageaccount",
    "?sv=2020-08-04&ss=b&srt=sco&sp=rwdlacx&..."
);
```

### Azure Blob Configuration Options

```cpp
auto config = cloud_config_builder::azure_blob()
    .with_account_name("mystorageaccount")
    .with_bucket("my-container")  // Container name
    .with_connect_timeout(std::chrono::milliseconds{30000})
    .with_connection_pool_size(25)
    .build_azure_blob();
```

#### Available Options

| Option | Description | Default |
|--------|-------------|---------|
| `account_name` | Azure storage account name | Required |
| `bucket` | Container name | Required |
| `endpoint` | Custom endpoint URL | Azure default |
| `use_ssl` | Enable HTTPS | `true` |
| `verify_ssl` | Verify SSL certificates | `true` |
| `connect_timeout` | Connection timeout | 30 seconds |
| `connection_pool_size` | Max connections | 25 |

## Basic Usage

### Creating Azure Blob Storage

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

// Create storage
auto storage = azure_blob_storage::create(config, std::move(credentials));

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
auto result = storage->upload("path/to/blob.bin", data);

// Upload file
auto result = storage->upload_file("/local/file.txt", "remote/file.txt");

// Async upload
auto future = storage->upload_async("key", data);
auto result = future.get();
```

### Download Operations

```cpp
// Download data
auto result = storage->download("path/to/blob.bin");
if (result.has_value()) {
    auto& data = result.value();
    // Use data
}

// Download to file
auto result = storage->download_file("remote/file.txt", "/local/file.txt");
```

### Streaming Upload (Block Blobs)

Azure Blob Storage uses block blobs for large file uploads:

```cpp
auto stream = storage->create_upload_stream("large-file.bin");

while (has_more_data) {
    std::vector<std::byte> chunk = read_chunk();
    stream->write(chunk);
}

auto result = stream->finalize();  // Commits block list
```

### SAS Token Generation

```cpp
// Generate blob SAS URL
presigned_url_options options;
options.method = "GET";
options.expiration = std::chrono::seconds{3600};

auto blob_sas = storage->generate_blob_sas("file.bin", options);

// Generate container SAS URL
auto container_sas = storage->generate_container_sas(options);

// Generate presigned URL (same as blob SAS)
auto url = storage->generate_presigned_url("file.bin", options);
```

### Access Tier Management

Azure Blob Storage supports access tiers for cost optimization:

```cpp
// Get current tier
auto tier = storage->get_access_tier("file.bin");
std::cout << "Current tier: " << tier.value() << "\n";

// Set access tier
storage->set_access_tier("file.bin", "Cool");  // Hot, Cool, Archive
```

## Azurite (Local Emulator)

For local development, use Azurite:

```bash
# Install Azurite
npm install -g azurite

# Run Azurite
azurite --location ./azurite-data
```

Configure for Azurite:

```cpp
auto config = cloud_config_builder::azure_blob()
    .with_account_name("devstoreaccount1")
    .with_bucket("my-container")
    .with_endpoint("http://127.0.0.1:10000/devstoreaccount1")
    .with_ssl(false, false)
    .build_azure_blob();

// Default Azurite credentials
auto credentials = azure_blob_credential_provider::create_from_account_key(
    "devstoreaccount1",
    "Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw=="
);
```

## Container Permissions

Ensure your container has appropriate access level:

| Access Level | Description |
|--------------|-------------|
| Private | No anonymous access (default) |
| Blob | Anonymous read access to blobs only |
| Container | Anonymous read access to container and blobs |

For most use cases, use Private access with SAS tokens or account keys.

## Required Permissions

For account key authentication, the storage account has full access. For SAS tokens, ensure these permissions:

| Permission | Required For |
|------------|--------------|
| Read (r) | Download blobs |
| Write (w) | Upload blobs |
| Delete (d) | Delete blobs |
| List (l) | List blobs |
| Add (a) | Append to blobs |
| Create (c) | Create new blobs |

Example SAS token with all permissions:

```
?sv=2020-08-04&ss=b&srt=sco&sp=rwdlacx&se=2024-12-31T23:59:59Z&st=2024-01-01T00:00:00Z&spr=https&sig=...
```

## Error Handling

```cpp
auto result = storage->upload("key", data);
if (!result.has_value()) {
    auto& error = result.error();

    if (error.code == error_code::connection_failed) {
        // Handle connection error
    } else if (error.code == error_code::file_access_denied) {
        // Handle permission error (check SAS permissions)
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

1. Verify storage account name is correct
2. Check network connectivity to Azure endpoints
3. Verify firewall rules allow HTTPS (port 443)
4. Check if storage account firewall is configured

### Authentication Errors

1. Verify account key or SAS token is correct
2. Check SAS token expiration
3. Verify SAS token has required permissions
4. Ensure container exists

### SSL/TLS Errors

1. Verify OpenSSL is installed correctly
2. Check CA certificates are up to date
3. For testing, use `.with_ssl(true, false)` to skip verification

## Example

See `examples/cloud/azure_blob_example.cpp` for a complete working example.

```bash
# Build
cmake --build build --target azure_blob_example

# Run
./build/bin/azure_blob_example mystorageaccount mycontainer

# With Azurite
./build/bin/azure_blob_example devstoreaccount1 mycontainer http://localhost:10000/devstoreaccount1
```
