# Cloud Storage Best Practices

This guide covers best practices for using cloud storage in the File Transfer System.

## Connection Management

### Reuse Connections

Cloud storage connections are expensive to establish. Reuse storage instances:

```cpp
// Good: Create once, reuse
class CloudService {
public:
    CloudService() {
        auto credentials = s3_credential_provider::create_default();
        auto config = cloud_config_builder::s3()
            .with_bucket("my-bucket")
            .with_region("us-east-1")
            .build_s3();

        storage_ = s3_storage::create(config, credentials);
        storage_->connect();
    }

    auto upload(const std::string& key, std::span<const std::byte> data) {
        return storage_->upload(key, data);
    }

private:
    std::unique_ptr<s3_storage> storage_;
};

// Bad: Creating new connection for each operation
void upload_file(const std::string& key, std::span<const std::byte> data) {
    auto credentials = s3_credential_provider::create_default();
    auto config = /* ... */;
    auto storage = s3_storage::create(config, credentials);
    storage->connect();       // Expensive!
    storage->upload(key, data);
    storage->disconnect();    // Wasted connection
}
```

### Connection Pool Size

Configure connection pool based on workload:

```cpp
auto config = cloud_config_builder::s3()
    .with_bucket("my-bucket")
    .with_region("us-east-1")
    .with_connection_pool_size(50)  // For high-concurrency workloads
    .build_s3();
```

| Workload | Recommended Pool Size |
|----------|----------------------|
| Low (< 10 req/s) | 10-15 |
| Medium (10-100 req/s) | 25-50 |
| High (> 100 req/s) | 50-100 |

## Large File Handling

### Use Streaming for Large Files

For files > 5MB, use streaming uploads:

```cpp
// Good: Streaming upload for large files
auto stream = storage->create_upload_stream("large-file.bin");

std::ifstream file("large-file.bin", std::ios::binary);
std::vector<std::byte> buffer(5 * 1024 * 1024);  // 5 MB chunks

while (file) {
    file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    auto bytes_read = file.gcount();
    if (bytes_read > 0) {
        stream->write(std::span{buffer.data(), static_cast<size_t>(bytes_read)});
    }
}

stream->finalize();

// Bad: Loading entire file into memory
std::vector<std::byte> data(1024 * 1024 * 1024);  // 1 GB - may cause OOM!
file.read(reinterpret_cast<char*>(data.data()), data.size());
storage->upload("large-file.bin", data);
```

### Chunk Size Recommendations

| Provider | Minimum Chunk | Recommended Chunk | Maximum Parts |
|----------|--------------|-------------------|---------------|
| AWS S3 | 5 MB | 8-16 MB | 10,000 |
| Azure Blob | 4 MB | 8-16 MB | 50,000 |
| GCS | 256 KB | 8-16 MB | 10,000 |

## Error Handling

### Implement Retry Logic

Cloud operations can fail transiently. Implement retry with exponential backoff:

```cpp
template<typename Func>
auto with_retry(Func&& func, int max_retries = 3) -> decltype(func()) {
    int retry = 0;
    while (true) {
        auto result = func();
        if (result.has_value()) {
            return result;
        }

        if (retry >= max_retries) {
            return result;
        }

        // Check if error is retryable
        auto& error = result.error();
        if (error.code == error_code::connection_failed ||
            error.code == error_code::timeout) {
            // Exponential backoff: 100ms, 200ms, 400ms, ...
            auto delay = std::chrono::milliseconds(100 * (1 << retry));
            std::this_thread::sleep_for(delay);
            ++retry;
            continue;
        }

        // Non-retryable error
        return result;
    }
}

// Usage
auto result = with_retry([&]() {
    return storage->upload("key", data);
});
```

### Handle Specific Errors

```cpp
auto result = storage->upload("key", data);
if (!result.has_value()) {
    switch (result.error().code) {
        case error_code::connection_failed:
            // Retry with backoff
            break;
        case error_code::file_access_denied:
            // Check permissions
            break;
        case error_code::storage_quota_exceeded:
            // Handle quota issue
            break;
        case error_code::file_already_exists:
            // Handle conflict
            break;
        default:
            // Log and report
            break;
    }
}
```

## Performance Optimization

### Parallel Uploads

Use async operations for parallel uploads:

```cpp
std::vector<std::future<result<upload_result>>> futures;

for (const auto& file : files) {
    auto data = read_file(file);
    futures.push_back(storage->upload_async(file.filename(), data));
}

// Wait for all uploads
for (auto& future : futures) {
    auto result = future.get();
    if (!result.has_value()) {
        // Handle error
    }
}
```

### Optimize Object Keys

Use hierarchical keys for better listing performance:

```cpp
// Good: Hierarchical keys
storage->upload("2024/01/15/file-001.bin", data);
storage->upload("2024/01/15/file-002.bin", data);

// List efficiently with prefix
list_objects_options options;
options.prefix = "2024/01/15/";
auto result = storage->list_objects(options);

// Bad: Flat key space
storage->upload("file-20240115-001.bin", data);
storage->upload("file-20240115-002.bin", data);
// Listing requires scanning all objects
```

### Use Presigned URLs for Direct Access

For large file downloads, use presigned URLs:

```cpp
// Generate presigned URL
presigned_url_options options;
options.method = "GET";
options.expiration = std::chrono::seconds{3600};

auto url = storage->generate_presigned_url("large-file.bin", options);

// Client downloads directly from cloud
// No bandwidth through your server
```

## Security Best Practices

### Credential Management

```cpp
// Good: Use environment or IAM roles
auto credentials = s3_credential_provider::create_default();

// Good: Use short-lived credentials
auto credentials = s3_credential_provider::create_from_session(
    access_key,
    secret_key,
    session_token,  // Temporary credentials
    expiration
);

// Bad: Hardcoded credentials
auto credentials = s3_credential_provider::create(static_credentials{
    .access_key_id = "AKIAIOSFODNN7EXAMPLE",  // Never do this!
    .secret_access_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY"
});
```

### Least Privilege

Configure minimal required permissions:

```json
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Action": [
                "s3:GetObject",
                "s3:PutObject"
            ],
            "Resource": "arn:aws:s3:::my-bucket/uploads/*"
        }
    ]
}
```

### SSL/TLS

Always use SSL in production:

```cpp
// Good: SSL enabled (default)
auto config = cloud_config_builder::s3()
    .with_bucket("my-bucket")
    .with_region("us-east-1")
    .build_s3();

// Development only: Skip SSL verification
auto config = cloud_config_builder::s3()
    .with_bucket("my-bucket")
    .with_region("us-east-1")
    .with_ssl(true, false)  // SSL on, verification off
    .build_s3();
```

## Cost Optimization

### Storage Classes

Use appropriate storage classes:

| Class | Use Case | Retrieval Time |
|-------|----------|----------------|
| Standard | Frequently accessed | Immediate |
| Infrequent Access | Monthly access | Immediate |
| Glacier/Archive | Yearly access | Hours |

```cpp
cloud_transfer_options options;
options.storage_class = "STANDARD_IA";  // For infrequent access

storage->upload("archive/old-file.bin", data, options);
```

### Lifecycle Policies

Configure lifecycle rules for automatic tiering:

| Age | Action |
|-----|--------|
| 30 days | Move to IA/Cool |
| 90 days | Move to Glacier/Archive |
| 365 days | Delete |

### Reduce API Calls

Batch operations when possible:

```cpp
// Good: Batch delete
std::vector<std::string> keys = {"file1.bin", "file2.bin", "file3.bin"};
storage->delete_objects(keys);

// Less efficient: Individual deletes
for (const auto& key : keys) {
    storage->delete_object(key);
}
```

## Monitoring and Observability

### Track Statistics

```cpp
// Periodically check statistics
auto stats = storage->get_statistics();
logger.info("Cloud storage stats: uploaded={} downloaded={} errors={}",
    stats.bytes_uploaded,
    stats.bytes_downloaded,
    stats.errors);
```

### Implement Health Checks

```cpp
bool is_healthy() {
    try {
        // Test connectivity
        auto result = storage->list_objects({.max_keys = 1});
        return result.has_value();
    } catch (...) {
        return false;
    }
}
```

### Monitor Progress

```cpp
storage->on_upload_progress([&metrics](const upload_progress& p) {
    metrics.record_upload_progress(p.bytes_transferred, p.total_bytes);
});

storage->on_state_changed([&metrics](cloud_storage_state state) {
    metrics.record_state_change(to_string(state));
});
```

## Multi-Cloud Strategies

### Failover Pattern

```cpp
class multi_cloud_storage {
public:
    auto upload(const std::string& key, std::span<const std::byte> data) {
        // Try primary
        auto result = primary_->upload(key, data);
        if (result.has_value()) {
            return result;
        }

        // Failover to secondary
        logger.warn("Primary failed, failing over to secondary");
        return secondary_->upload(key, data);
    }

private:
    std::unique_ptr<cloud_storage_interface> primary_;
    std::unique_ptr<cloud_storage_interface> secondary_;
};
```

### Replication Pattern

```cpp
auto replicate(const std::string& key, std::span<const std::byte> data) {
    // Upload to all providers in parallel
    auto f1 = storage_s3_->upload_async(key, data);
    auto f2 = storage_azure_->upload_async(key, data);
    auto f3 = storage_gcs_->upload_async(key, data);

    // Wait for all
    auto r1 = f1.get();
    auto r2 = f2.get();
    auto r3 = f3.get();

    // Require majority success
    int success_count = (r1.has_value() ? 1 : 0) +
                       (r2.has_value() ? 1 : 0) +
                       (r3.has_value() ? 1 : 0);

    return success_count >= 2;
}
```

## Summary Checklist

- [ ] Reuse storage connections
- [ ] Configure appropriate connection pool size
- [ ] Use streaming for files > 5 MB
- [ ] Implement retry with exponential backoff
- [ ] Handle errors appropriately
- [ ] Use parallel uploads for multiple files
- [ ] Optimize object key structure
- [ ] Use presigned URLs for client access
- [ ] Follow credential management best practices
- [ ] Enable SSL/TLS in production
- [ ] Use appropriate storage classes
- [ ] Configure lifecycle policies
- [ ] Batch API operations
- [ ] Monitor statistics and health
- [ ] Consider multi-cloud strategies
