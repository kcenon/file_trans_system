# Bandwidth Throttling Guide

Complete guide to bandwidth throttling in the **file_trans_system** library.

## Overview

The file_trans_system provides bandwidth throttling to control upload and download speeds. This feature is useful for:
- Preventing network saturation in shared environments
- Ensuring fair bandwidth allocation across multiple transfers
- Complying with network usage policies
- Reducing impact on other network services

### Token Bucket Algorithm

Bandwidth limiting uses the **token bucket algorithm**, which provides:
- **Smooth rate limiting**: Maintains average rate while allowing short bursts
- **Burst tolerance**: Initial bucket capacity allows temporary speed spikes
- **Fine-grained control**: Microsecond-level timing accuracy
- **Low overhead**: Minimal CPU impact during transfers

---

## Quick Start

### Client-Side Bandwidth Limiting

```cpp
#include <kcenon/file_transfer/client/file_transfer_client.h>

// Create client with bandwidth limits
auto client = file_transfer_client::builder()
    .with_upload_bandwidth_limit(10 * 1024 * 1024)    // 10 MB/s upload
    .with_download_bandwidth_limit(5 * 1024 * 1024)   // 5 MB/s download
    .build();
```

### Server Pipeline Bandwidth Limiting

```cpp
#include <kcenon/file_transfer/server/server_pipeline.h>

// Configure pipeline with bandwidth limits
pipeline_config config = pipeline_config::auto_detect();
config.send_bandwidth_limit = 100 * 1024 * 1024;  // 100 MB/s outbound
config.recv_bandwidth_limit = 50 * 1024 * 1024;   // 50 MB/s inbound

auto pipeline_result = server_pipeline::create(config);
```

---

## Client Configuration

### Builder Methods

| Method | Description | Default |
|--------|-------------|---------|
| `with_upload_bandwidth_limit(bytes_per_sec)` | Limit upload speed | 0 (unlimited) |
| `with_download_bandwidth_limit(bytes_per_sec)` | Limit download speed | 0 (unlimited) |

### Configuration Examples

```cpp
// Unlimited bandwidth (default)
auto client = file_transfer_client::builder()
    .build();

// Upload limited, download unlimited
auto client = file_transfer_client::builder()
    .with_upload_bandwidth_limit(1024 * 1024)  // 1 MB/s
    .build();

// Both limited
auto client = file_transfer_client::builder()
    .with_upload_bandwidth_limit(5 * 1024 * 1024)   // 5 MB/s
    .with_download_bandwidth_limit(10 * 1024 * 1024) // 10 MB/s
    .build();
```

---

## Pipeline Configuration

### Configuration Fields

| Field | Type | Description | Default |
|-------|------|-------------|---------|
| `send_bandwidth_limit` | `std::size_t` | Outbound bandwidth limit (bytes/sec) | 0 (unlimited) |
| `recv_bandwidth_limit` | `std::size_t` | Inbound bandwidth limit (bytes/sec) | 0 (unlimited) |

### Dynamic Adjustment

Bandwidth limits can be changed at runtime:

```cpp
auto pipeline_result = server_pipeline::create(config);
auto& pipeline = pipeline_result.value();

// Start pipeline
pipeline.start();

// Adjust limits dynamically
pipeline.set_send_bandwidth_limit(200 * 1024 * 1024);  // Increase to 200 MB/s
pipeline.set_recv_bandwidth_limit(100 * 1024 * 1024);  // Increase to 100 MB/s

// Query current limits
auto send_limit = pipeline.get_send_bandwidth_limit();
auto recv_limit = pipeline.get_recv_bandwidth_limit();
```

---

## Bandwidth Limiter API

### Class: `bandwidth_limiter`

Low-level bandwidth limiting using the token bucket algorithm.

```cpp
#include <kcenon/file_transfer/core/bandwidth_limiter.h>

// Create limiter with 10 MB/s limit
bandwidth_limiter limiter(10 * 1024 * 1024);

// Acquire tokens before transfer
limiter.acquire(chunk_size);  // Blocks if rate exceeded

// Non-blocking attempt
if (limiter.try_acquire(chunk_size)) {
    // Transfer chunk
}
```

### Constructor

```cpp
explicit bandwidth_limiter(std::size_t bytes_per_second);
```

- `bytes_per_second`: Rate limit in bytes per second
- Value of 0 means unlimited (limiter disabled)

### Methods

| Method | Description |
|--------|-------------|
| `acquire(bytes)` | Block until tokens available |
| `try_acquire(bytes)` | Non-blocking token acquisition |
| `acquire_async(bytes)` | Returns `std::future<void>` |
| `set_limit(bytes_per_sec)` | Change rate limit at runtime |
| `get_limit()` | Get current rate limit |
| `is_enabled()` | Check if limiting is active |
| `enable()` | Re-enable rate limiting |
| `disable()` | Temporarily disable limiting |
| `reset()` | Refill bucket to capacity |
| `available_tokens()` | Get current available tokens |
| `bucket_capacity()` | Get maximum burst size |

### RAII Helper

```cpp
// Automatically acquires tokens on construction
{
    scoped_bandwidth_acquire guard(limiter, chunk_size);
    // Transfer chunk...
} // Automatic cleanup
```

---

## Token Bucket Algorithm

### How It Works

```
                    ┌─────────────────┐
                    │   Token Bucket  │
                    │                 │
    Rate ───────────┤  capacity = rate│
  (tokens/sec)      │                 │
                    │  tokens <= cap  │
                    └────────┬────────┘
                             │
                             ▼
                      ┌──────────────┐
                      │   Acquire    │
                      │   (bytes)    │
                      └──────────────┘
```

1. **Token generation**: Tokens are added at `bytes_per_second` rate
2. **Bucket capacity**: Maximum tokens = 1 second worth (allows burst)
3. **Token consumption**: Each byte transferred consumes one token
4. **Blocking**: If tokens insufficient, wait until enough accumulate

### Burst Behavior

The bucket capacity equals one second of bandwidth, allowing temporary bursts:

```cpp
bandwidth_limiter limiter(10 * 1024 * 1024);  // 10 MB/s

// Initial burst: 10 MB can be transferred immediately
limiter.acquire(10 * 1024 * 1024);  // Instant

// Subsequent transfers are rate-limited
limiter.acquire(10 * 1024 * 1024);  // Waits ~1 second
```

---

## Performance Considerations

### Overhead

| Operation | Typical Latency |
|-----------|-----------------|
| `acquire()` (tokens available) | < 1 microsecond |
| `acquire()` (need to wait) | Wait time + < 1 microsecond |
| `try_acquire()` | < 1 microsecond |
| Token refill calculation | < 100 nanoseconds |

### Thread Safety

The `bandwidth_limiter` is fully thread-safe:
- Multiple threads can call `acquire()` concurrently
- `set_limit()` can be called from any thread
- Internal synchronization via mutex and condition variables

### Memory Usage

- Per limiter: ~64 bytes (excluding synchronization primitives)
- No dynamic allocations after construction

---

## Common Use Cases

### Fair Bandwidth Sharing

```cpp
// Multiple transfers sharing 100 MB/s total
auto shared_limiter = std::make_shared<bandwidth_limiter>(100 * 1024 * 1024);

// Each transfer uses the shared limiter
for (auto& transfer : transfers) {
    transfer.set_bandwidth_limiter(shared_limiter);
}
```

### Time-Based Throttling

```cpp
// Reduce bandwidth during business hours
void adjust_bandwidth_for_time() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto tm = std::localtime(&time);

    if (tm->tm_hour >= 9 && tm->tm_hour < 18) {
        // Business hours: 10 MB/s
        pipeline.set_send_bandwidth_limit(10 * 1024 * 1024);
    } else {
        // Off-hours: 100 MB/s
        pipeline.set_send_bandwidth_limit(100 * 1024 * 1024);
    }
}
```

### Gradual Speed Increase

```cpp
// Start slow and increase bandwidth gradually
bandwidth_limiter limiter(1 * 1024 * 1024);  // Start at 1 MB/s

for (int i = 1; i <= 10; ++i) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    limiter.set_limit(i * 1024 * 1024);  // Increase by 1 MB/s every 5 seconds
}
```

---

## Troubleshooting

### Transfer Slower Than Limit

**Possible causes:**
1. Network bandwidth lower than configured limit
2. Disk I/O bottleneck
3. Server-side throttling

**Diagnostic:**
```cpp
// Check actual available tokens
auto available = limiter.available_tokens();
auto capacity = limiter.bucket_capacity();
std::cout << "Tokens: " << available << " / " << capacity << std::endl;
```

### Unexpected Bursts

**Possible causes:**
1. Bucket was full at transfer start
2. Long pause between transfers allowed token accumulation

**Solution:**
```cpp
// Drain bucket before starting time-sensitive transfer
limiter.acquire(limiter.bucket_capacity());
```

### High CPU During Throttling

**Possible causes:**
1. Very small chunk sizes with high-frequency acquire calls
2. Spin-waiting instead of blocking

**Solution:**
- Use larger chunk sizes (recommended: 64KB - 1MB)
- Ensure `acquire()` is called, not `try_acquire()` in a loop

---

## Best Practices

1. **Set realistic limits**: Consider actual network capacity
2. **Use larger chunks**: Reduces overhead from acquire calls
3. **Monitor actual throughput**: Verify limits are effective
4. **Consider burst impact**: First transfer may exceed average rate
5. **Dynamic adjustment**: Adjust limits based on network conditions
6. **Shared limiters**: Use for fair bandwidth allocation across transfers

---

## API Reference Summary

### bandwidth_limiter

```cpp
class bandwidth_limiter {
public:
    explicit bandwidth_limiter(std::size_t bytes_per_second);

    // Token acquisition
    auto acquire(std::size_t bytes) -> void;
    auto try_acquire(std::size_t bytes) -> bool;
    auto acquire_async(std::size_t bytes) -> std::future<void>;

    // Configuration
    auto set_limit(std::size_t bytes_per_second) -> void;
    auto get_limit() const noexcept -> std::size_t;

    // State control
    auto is_enabled() const noexcept -> bool;
    auto enable() -> void;
    auto disable() -> void;
    auto reset() -> void;

    // Inspection
    auto available_tokens() const -> std::size_t;
    auto bucket_capacity() const noexcept -> std::size_t;
};
```

### scoped_bandwidth_acquire

```cpp
class scoped_bandwidth_acquire {
public:
    scoped_bandwidth_acquire(bandwidth_limiter& limiter, std::size_t bytes);
};
```

### pipeline_config bandwidth fields

```cpp
struct pipeline_config {
    std::size_t send_bandwidth_limit = 0;  // bytes/sec, 0 = unlimited
    std::size_t recv_bandwidth_limit = 0;  // bytes/sec, 0 = unlimited
};
```

### server_pipeline bandwidth methods

```cpp
class server_pipeline {
public:
    auto set_send_bandwidth_limit(std::size_t bytes_per_second) -> void;
    auto set_recv_bandwidth_limit(std::size_t bytes_per_second) -> void;
    auto get_send_bandwidth_limit() const -> std::size_t;
    auto get_recv_bandwidth_limit() const -> std::size_t;
};
```
