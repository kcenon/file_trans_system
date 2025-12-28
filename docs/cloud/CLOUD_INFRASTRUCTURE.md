# Cloud Storage Infrastructure

This document describes the common infrastructure classes used across all cloud storage implementations (S3, GCS, Azure Blob).

## Overview

The cloud storage module uses shared base classes and utilities to reduce code duplication and ensure consistent behavior across different cloud providers.

## Architecture

```
                    ┌─────────────────────────────────┐
                    │   cloud_storage_interface       │
                    └─────────────────────────────────┘
                                    │
                    ┌───────────────┼───────────────┐
                    │               │               │
              ┌─────▼─────┐   ┌─────▼─────┐   ┌─────▼─────┐
              │ s3_storage │   │gcs_storage│   │azure_blob │
              └─────┬─────┘   └─────┬─────┘   └─────┬─────┘
                    │               │               │
                    └───────────────┼───────────────┘
                                    │
                    ┌───────────────▼───────────────┐
                    │      cloud_http_client        │
                    │   (unified HTTP operations)   │
                    └───────────────────────────────┘
```

## Common Components

### 1. cloud_http_client

A unified HTTP client wrapper that provides consistent HTTP operations across all cloud storage implementations.

**Location:** `include/kcenon/file_transfer/cloud/cloud_http_client.h`

**Features:**
- Common GET, POST, PUT, DELETE, HEAD operations
- Retry logic with exponential backoff
- Error handling standardization
- Timeout management

**Usage:**
```cpp
#include "kcenon/file_transfer/cloud/cloud_http_client.h"

auto client = make_cloud_http_client(std::chrono::milliseconds(30000));

// Make a GET request
auto result = client->get(url, query_params, headers);
if (result.has_value()) {
    auto& response = result.value();
    // Process response
}
```

### 2. cloud_stream_base

Base classes for upload and download streams.

**Location:** `include/kcenon/file_transfer/cloud/cloud_stream_base.h`

**Classes:**
- `upload_stream_base<PartResult>` - Template base for multipart uploads
- `download_stream_base` - Base class for download streams
- `http_response_base` - Common HTTP response structure
- `http_client_interface_base` - HTTP client interface

**Features:**
- Concurrent upload management
- Pending part tracking
- Buffer management
- Common member variables (bytes_written, finalized, aborted)

### 3. cloud_utils

Shared utility functions used across all cloud storage implementations.

**Location:** `include/kcenon/file_transfer/cloud/cloud_utils.h`

**Categories:**
- **Encoding:** Base64, URL encoding, hex conversion
- **Cryptography:** SHA256, HMAC-SHA256
- **Time:** ISO 8601, RFC 3339, RFC 1123 formatting
- **Parsing:** XML element extraction, JSON value extraction
- **Content Type:** MIME type detection
- **Retry:** Delay calculation, retryable status detection

## Provider-Specific Adapters

Each cloud provider implements thin adapter classes that convert between the unified interface and provider-specific response types:

- `real_gcs_http_client` - Wraps cloud_http_client for GCS
- `real_azure_http_client` - Wraps cloud_http_client for Azure

## Benefits

1. **Reduced Duplication:** Common HTTP logic is centralized
2. **Consistent Behavior:** All providers use the same retry and error handling
3. **Easier Maintenance:** Bug fixes apply to all providers
4. **Simplified Testing:** Mock the common interface for unit tests
5. **Extensibility:** Easy to add new cloud providers

## Related Files

- `cloud_http_client.h` / `cloud_http_client.cpp`
- `cloud_stream_base.h`
- `cloud_utils.h` / `cloud_utils.cpp`
- `s3_storage.cpp`
- `gcs_storage.cpp`
- `azure_blob_storage.cpp`

## See Also

- [AWS S3 Setup](AWS_S3_SETUP.md)
- [GCS Setup](GCS_SETUP.md)
- [Azure Blob Setup](AZURE_BLOB_SETUP.md)
- [Cloud Storage Server](CLOUD_STORAGE_SERVER.md)
