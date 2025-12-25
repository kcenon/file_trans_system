# Cloud Storage Server Integration

This document describes how to integrate cloud storage backends with the file transfer server.

## Overview

The file transfer server can be configured to use cloud storage backends (AWS S3, Azure Blob, Google Cloud Storage) for file storage. This enables:

- **Hybrid Storage**: Store files locally and replicate to cloud
- **Cloud-Only Mode**: Use cloud storage as primary storage
- **Storage Tiering**: Automatically move files between storage tiers based on access patterns or age

## Architecture

```
                    ┌─────────────────────────────────────┐
                    │       file_transfer_server          │
                    └─────────────────────────────────────┘
                                      │
                                      ▼
                    ┌─────────────────────────────────────┐
                    │         storage_manager             │
                    │                                     │
                    │  ┌─────────────┐ ┌─────────────┐   │
                    │  │   primary   │ │  secondary  │   │
                    │  │   backend   │ │   backend   │   │
                    │  └──────┬──────┘ └──────┬──────┘   │
                    └─────────┼───────────────┼──────────┘
                              │               │
                    ┌─────────▼─────┐ ┌───────▼─────────┐
                    │     local     │ │     cloud       │
                    │   filesystem  │ │   (S3/Azure/    │
                    │               │ │      GCS)       │
                    └───────────────┘ └─────────────────┘
```

## Storage Modes

### Local Only (Default)

Files are stored on the local filesystem only.

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/storage")
    .with_storage_mode(storage_mode::local_only)
    .build();
```

### Cloud Only

Files are stored directly in cloud storage. A local cache can be enabled for performance.

```cpp
// Create S3 storage
auto s3_config = cloud_config_builder::s3()
    .with_bucket("my-bucket")
    .with_region("us-east-1")
    .build_s3();

auto credentials = s3_credential_provider::create_from_environment();
auto s3_storage = s3_storage::create(s3_config, credentials);

// Configure server
auto server = file_transfer_server::builder()
    .with_storage_mode(storage_mode::cloud_only)
    .with_cloud_storage(std::move(s3_storage))
    .with_cloud_cache(true, 1ULL * 1024 * 1024 * 1024)  // 1GB cache
    .build();
```

### Hybrid Mode

Files are stored locally and replicated to cloud storage. This provides:

- Fast local access for recent files
- Cloud backup for durability
- Cloud fallback when local file is unavailable

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/storage")
    .with_storage_mode(storage_mode::hybrid)
    .with_cloud_storage(s3_storage)
    .with_cloud_replication(true)   // Replicate writes to cloud
    .with_cloud_fallback(true)      // Read from cloud if local fails
    .with_cloud_key_prefix("uploads/")
    .build();
```

## Storage Manager

The `storage_manager` class provides a unified interface for managing storage backends.

### Creating a Storage Manager

```cpp
#include <kcenon/file_transfer/server/storage_manager.h>

// Create local backend
auto local = local_storage_backend::create("/data/storage");

// Create cloud backend
auto s3 = s3_storage::create(s3_config, credentials);
auto cloud = cloud_storage_backend::create(std::move(s3));

// Configure manager
storage_manager_config config;
config.primary_backend = std::move(local);
config.secondary_backend = std::move(cloud);
config.hybrid_storage = true;
config.replicate_writes = true;
config.fallback_reads = true;

auto manager = storage_manager::create(config);
manager->initialize();
```

### Storage Operations

```cpp
// Store data
auto data = /* ... */;
auto result = manager->store("file.bin", data);

// Store file
auto result = manager->store_file("remote/file.txt", "/local/file.txt");

// Retrieve data
auto data_result = manager->retrieve("file.bin");
if (data_result.has_value()) {
    auto& data = data_result.value();
}

// Retrieve to file
auto result = manager->retrieve_file("remote/file.txt", "/local/output.txt");

// Check existence
auto exists = manager->exists("file.bin");

// Get metadata
auto metadata = manager->get_metadata("file.bin");

// List files
list_storage_options options;
options.prefix = "uploads/";
auto list = manager->list(options);

// Remove file
manager->remove("file.bin");
```

### Async Operations

```cpp
// Async store
auto future = manager->store_async("file.bin", data);
auto result = future.get();

// Async retrieve
auto future = manager->retrieve_async("file.bin");
auto data = future.get();
```

### Callbacks

```cpp
// Progress callback
manager->on_progress([](const storage_progress& progress) {
    std::cout << "Operation: " << to_string(progress.operation)
              << " " << progress.percentage() << "%" << std::endl;
});

// Error callback
manager->on_error([](const std::string& key, const error& err) {
    std::cerr << "Error for " << key << ": " << err.message << std::endl;
});
```

## Storage Policy

The `storage_policy` class manages automatic storage tiering and lifecycle.

### Tiering Rules

```cpp
#include <kcenon/file_transfer/server/storage_policy.h>

// Age-based tiering
age_tiering_config age_config;
age_config.hot_to_warm_age = std::chrono::hours{24 * 30};   // 30 days
age_config.warm_to_cold_age = std::chrono::hours{24 * 90};  // 90 days
age_config.cold_to_archive_age = std::chrono::hours{24 * 365}; // 1 year

// Size-based tiering
size_tiering_config size_config;
size_config.hot_max_size = 10 * 1024 * 1024;   // 10 MB
size_config.warm_max_size = 100 * 1024 * 1024; // 100 MB

// Custom rule
tiering_rule archive_logs;
archive_logs.name = "archive_old_logs";
archive_logs.trigger = tiering_trigger::age;
archive_logs.key_pattern = "logs/*";
archive_logs.min_age = std::chrono::hours{24 * 90};
archive_logs.target_tier = storage_tier::archive;
archive_logs.action = tiering_action::move;

// Build policy
auto policy = storage_policy::builder()
    .with_age_tiering(age_config)
    .with_size_tiering(size_config)
    .with_rule(archive_logs)
    .build();

// Attach to storage manager
policy->attach(*manager);
```

### Evaluating and Executing

```cpp
// Evaluate single file
auto result = policy->evaluate("file.bin");
if (result.has_value()) {
    std::cout << "Current tier: " << to_string(result.value().current_tier) << std::endl;
    std::cout << "Target tier: " << to_string(result.value().target_tier) << std::endl;
}

// Evaluate all files
auto results = policy->evaluate_all();

// Execute pending tier changes
auto count = policy->execute_pending();
std::cout << "Executed " << count.value() << " tier changes" << std::endl;

// Execute specific action
policy->execute_action("old_file.bin", storage_tier::archive, tiering_action::move);
```

### Retention Policies

```cpp
retention_policy retention;
retention.min_retention = std::chrono::hours{24 * 30};  // 30 days minimum
retention.legal_hold = false;
retention.compliance_mode = false;
retention.exclusions = {"temp/*", "cache/*"};

auto policy = storage_policy::builder()
    .with_retention(retention)
    .build();

// Check if deletion is allowed
auto can_delete = policy->can_delete("important.doc");
```

### Dry Run Mode

Test policies without making changes:

```cpp
auto policy = storage_policy::builder()
    .with_dry_run(true)
    .build();

// Actions are reported but not executed
policy->on_action([](const std::string& key, tiering_action action,
                     storage_tier from, storage_tier to) {
    std::cout << "Would " << to_string(action) << " " << key
              << " from " << to_string(from) << " to " << to_string(to) << std::endl;
});

policy->execute_pending();  // Nothing actually changes
```

## Storage Tiers

| Tier | Description | Use Case |
|------|-------------|----------|
| `hot` | Frequently accessed | Active files |
| `warm` | Occasionally accessed | Recent archives |
| `cold` | Rarely accessed | Long-term storage |
| `archive` | Archival storage | Compliance, backups |

## Best Practices

1. **Start with Hybrid Mode**: Use local storage for performance with cloud backup for durability.

2. **Enable Caching for Cloud-Only**: When using cloud-only mode, enable local caching to reduce latency.

3. **Use Key Prefixes**: Organize files with prefixes (e.g., `uploads/`, `processed/`) for easier management.

4. **Test Policies with Dry Run**: Always test tiering policies in dry run mode before enabling.

5. **Monitor Storage Statistics**: Track storage usage and operations for cost optimization.

```cpp
auto stats = manager->get_statistics();
std::cout << "Stored: " << stats.bytes_stored << " bytes" << std::endl;
std::cout << "Local files: " << stats.local_file_count << std::endl;
std::cout << "Cloud files: " << stats.cloud_file_count << std::endl;
```

## Related Documentation

- [AWS S3 Setup](AWS_S3_SETUP.md) - Configuring AWS S3 storage backend
- [Architecture](../architecture.md) - System architecture overview
