> Status: archived 2026-09. No further maintenance; kept for reference.
>
> Selected TCP behavior was rechecked locally in the audit implementation; see the scoped evidence and remaining gaps below.

# file_trans_system

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/std/the-standard)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-0.2.0-green.svg)](https://github.com/kcenon/file_trans_system)

A C++20 library for reliable file transfer with Client-Server architecture, LZ4 compression, resume capability, and multi-stage pipeline processing.

## Key Features

- **Real TCP transfers**: Verified upload, download and metadata listing through public APIs.
- **Compression and integrity**: Adaptive LZ4, chunk CRC32 and file SHA-256.
- **Encryption**: AES-256-GCM chunks with configured shared secrets; this does not satisfy the TLS 1.3 requirement.
- **Recovery**: Disconnect-driven reconnect and opt-in durable transfer checkpoints.
- **Batch and flow control**: Completion-driven batches, throttling, quotas and bounded pipeline admission.
- **Measured limits**: 1 GB round trips pass locally. At 100 clients, downloads pass but uploads hit the server receive budget.
- **Transport status**: TCP is integrated. QUIC, 0-RTT and migration retain a dependency blocker.
- **Cloud status**: S3 passed local MinIO validation; GCS signing passed independent vectors. Other live-cloud operations remain unverified.

[Requirement status and remaining work](docs/IMPLEMENTATION_STATUS.md) ·
[Executed tests](docs/validation/REGRESSION_GATES.md) ·
[Measured performance](docs/validation/PUBLIC_API_BENCHMARKS.md) ·
[Issues #175–#191 implementation report](docs/validation/ISSUES_175_191.md)

## Quick Start

These snippets are synchronized from compiled example sources. Completion is printed only after both transfers reach verified terminal success.

### File Transfer Server

<!-- server:start -->
~~~cpp
#include <kcenon/file_transfer/file_transfer.h>
#include <iostream>

using namespace kcenon::file_transfer;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: quick_start_server STORAGE_DIRECTORY PORT\n";
        return 2;
    }
    try {
        const auto port = std::stoul(argv[2]);
        if (!port || port > 65535) return 2;
        auto built = file_transfer_server::builder()
            .with_storage_directory(argv[1])
            .with_max_connections(100)
            .build();
        if (!built) {
            std::cerr << built.error().message << '\n';
            return 1;
        }
        auto server = std::move(built.value());
        server.on_transfer_complete([](const transfer_result& result) {
            if (result.success)
                std::cout << "Verified transfer: " << result.filename << '\n';
            else
                std::cerr << "Transfer failed: " << result.error_message << '\n';
        });
        auto started = server.start({"127.0.0.1", static_cast<uint16_t>(port)});
        if (!started) {
            std::cerr << started.error().message << '\n';
            return 1;
        }
        std::cout << "READY; press Enter to stop." << std::endl;
        std::string line;
        std::getline(std::cin, line);
        auto stopped = server.stop();
        if (!stopped) std::cerr << stopped.error().message << '\n';
        return stopped ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
~~~
<!-- server:end -->

### File Transfer Client

<!-- client:start -->
~~~cpp
#include <kcenon/file_transfer/file_transfer.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace kcenon::file_transfer;
using namespace std::chrono_literals;

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: quick_start_client PORT SOURCE_FILE DOWNLOAD_PATH\n";
        return 2;
    }
    try {
        const auto port = std::stoul(argv[1]);
        if (!port || port > 65535) return 2;
        auto built = file_transfer_client::builder()
            .with_compression(compression_mode::adaptive)
            .with_auto_reconnect(true, reconnect_policy{})
            .with_connect_timeout(5s)
            .with_transfer_timeout(30s)
            .build();
        if (!built) throw std::runtime_error(built.error().message);
        auto client = std::move(built.value());
        auto connected = client.connect({"127.0.0.1", static_cast<uint16_t>(port)});
        if (!connected) throw std::runtime_error(connected.error().message);

        auto wait_verified = [](result<transfer_handle> pending) {
            if (!pending) throw std::runtime_error(pending.error().message);
            auto finished = pending.value().wait_for(120s);
            if (!finished) throw std::runtime_error(finished.error().message);
            if (!finished.value().success)
                throw std::runtime_error(finished.value().error_message.value_or("Transfer failed"));
            return finished.value().bytes_transferred;
        };
        const auto uploaded = wait_verified(client.upload_file(argv[2], "quick-start.bin"));
        const auto downloaded = wait_verified(client.download_file("quick-start.bin", argv[3]));
        auto disconnected = client.disconnect();
        if (!disconnected) throw std::runtime_error(disconnected.error().message);
        std::cout << "Verified upload: " << uploaded
                  << " bytes; verified download: " << downloaded << " bytes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
~~~
<!-- client:end -->

~~~sh
# Terminal 1 (keep stdin open)
./build/bin/quick_start_server ./storage 8080
# Terminal 2: use an existing input file and a new output path
./build/bin/quick_start_client 8080 ./input.bin ./download.bin
~~~

## Architecture

file_trans_system uses a **Client-Server architecture** with **multi-stage pipeline processing**:

```
                    ┌─────────────────────────────────┐
                    │    file_transfer_server         │
                    │                                 │
                    │  ┌───────────────────────────┐  │
                    │  │    Storage: /data/files   │  │
                    │  └───────────────────────────┘  │
                    │                                 │
                    │  on_upload_request()            │
                    │  on_download_request()          │
                    │  on_transfer_complete()           │
                    │  on_download_complete()         │
                    └────────────┬────────────────────┘
                                 │
         ┌───────────────────────┼───────────────────────┐
         │                       │                       │
         ▼                       ▼                       ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│    Client A     │    │    Client B     │    │    Client C     │
│  upload_file()  │    │ download_file() │    │  list_files()   │
│                 │    │                 │    │                 │
│  Auto-Reconnect │    │  Auto-Reconnect │    │  Auto-Reconnect │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

### Pipeline Architecture

Each transfer uses a multi-stage pipeline for maximum throughput:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       UPLOAD PIPELINE (Server-side)                          │
│                                                                              │
│  Network   ──▶   LZ4       ──▶  AES-GCM   ──▶  Verify  ──▶  File Write     │
│  Receive       Decompress      Decrypt*       CRC32         Stage           │
│ (network)    (compression)   (encryption)   (verify)      (io_write)        │
│                                                                              │
│  * Decryption only when encryption is enabled                               │
│  ┌────────────────────────────────────────────────────────────────────────┐ │
│  │              typed_thread_pool<pipeline_stage>                         │ │
│  │   [IO Workers] [Compute Workers] [Encryption Workers] [Network Workers]│ │
│  └────────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                       DOWNLOAD PIPELINE (Server-side)                        │
│                                                                              │
│  File Read  ──▶  AES-GCM   ──▶   LZ4      ──▶  Network Send                 │
│   Stage          Encrypt*       Compress                                     │
│  (io_read)    (encryption)   (compression)   (network)                       │
│                                                                              │
│  * Encryption only when encryption is enabled                               │
└─────────────────────────────────────────────────────────────────────────────┘
```

Pipeline jobs share a bounded admission budget through the final callback.
[Buffer reservations](docs/protocol/PIPELINE_CAPACITY.md) exclude caller-owned
copies and fixed library overhead. Process RSS is measured separately.
The server runtime receive queue has a separate 8 MiB rejection limit; the
100-upload benchmark exposes its remaining backpressure gap.

## Performance Targets

These are PRD targets, not achieved guarantees. The 1 GB loopback means were 19.27 MB/s upload and 74.39 MB/s download. Dedicated LAN qualification is unmeasured; 100 simultaneous uploads failed on the receive budget.
[Raw data, environment, variance and memory measurements](docs/validation/PUBLIC_API_BENCHMARKS.md).

| Metric | Target |
|--------|--------|
| LAN Throughput (1GB single file) | ≥ 500 MB/s |
| WAN Throughput | ≥ 100 MB/s (network limited) |
| LZ4 Compression Speed | ≥ 400 MB/s |
| LZ4 Decompression Speed | ≥ 1.5 GB/s |
| Memory Baseline | < 50 MB |
| Concurrent Client Connections | ≥ 100 |

## Configuration

### Server Configuration

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")         // Required
    .with_max_connections(100)                     // Default: 100
    .with_max_file_size(10ULL * 1024 * 1024 * 1024) // Default: 10GB
    .with_storage_quota(1ULL * 1024 * 1024 * 1024 * 1024) // 1TB
    .build();
```

### Client Configuration

```cpp
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::adaptive)   // Default
    .with_chunk_size(256 * 1024)                   // 256KB (default)
    .with_encryption(true)                         // Enable AES-256-GCM encryption
    .with_encryption_password("secure_password")   // Derive key from password
    // .with_encryption_key(raw_key)               // Or use raw 32-byte key
    .with_auto_reconnect(true, reconnect_policy{})
    .build();
```

### Encryption Configuration

Both endpoints must have the same application-supplied secret. Configure it
through the actual builders; check build results as in Quick Start.

~~~cpp
auto server_result = file_transfer_server::builder()
    .with_storage_directory("./storage")
    .with_encryption_password(shared_password)
    .with_required_encryption()
    .build();
auto client_result = file_transfer_client::builder()
    .with_encryption_password(shared_password)
    .build();
~~~

Password derivation uses PBKDF2; HKDF derives per-transfer keys. AES-256-GCM
protects chunks, and HMAC authenticates negotiation/completion acknowledgements.
This protocol does not authenticate every control message or replace TLS.
See [security boundaries](docs/protocol/ENCRYPTION.md).

### Compression Modes

| Mode | Description | Use Case |
|------|-------------|----------|
| `disabled` | No compression | Pre-compressed files (video, images) |
| `enabled` | Always compress | Text files, logs, documents |
| `adaptive` | Auto-detect (default) | Mixed workloads |

### Pipeline Configuration

```cpp
pipeline_config config{
    .io_workers = 2,       // File read parallelism
    .compression_workers = 4,    // LZ4 compression threads
    .network_workers = 2,        // Network send/receive
    .queue_size = 64        // Backpressure buffer
};

// Or use auto-detection
auto detected_config = pipeline_config::auto_detect();
```

### Transport Layer

The public builders select a shared transport configuration; TCP is the default.

~~~cpp
auto client_result = file_transfer_client::builder()
    .with_transport(tcp_transport_config{})
    .build();
~~~

QUIC is disabled by default. Enabling it exposes handshake/timeout-cleanup
defects in the pinned network dependency. 0-RTT and migration have component
implementations and are not validated public transfer capabilities.
See [transport evidence and blocker #184](docs/protocol/TRANSPORTS.md).

## Protocol

The library uses a custom lightweight binary protocol optimized for file transfer:

- FTS1 framing with explicit bounds and checksums; overhead depends on message/encryption metadata.
- TCP does not enable TLS by default. Optional shared-key encryption has [documented limits](docs/protocol/ENCRYPTION.md).
- Resume uses durable checkpoints and verified partial prefixes; see [resume behavior](docs/protocol/RESUME.md).

### Message Types

| Code | Message | Direction | Description |
|------|---------|-----------|-------------|
| 0x10 | UPLOAD_REQUEST | C→S | Upload request with metadata |
| 0x11 | UPLOAD_ACCEPT | S→C | Upload approved |
| 0x12 | UPLOAD_REJECT | S→C | Upload rejected with reason |
| 0x50 | DOWNLOAD_REQUEST | C→S | Download request for file |
| 0x51 | DOWNLOAD_ACCEPT | S→C | Download approved with metadata |
| 0x52 | DOWNLOAD_REJECT | S→C | Download rejected |
| 0x60 | LIST_REQUEST | C→S | File listing request |
| 0x61 | LIST_RESPONSE | S→C | File listing response |

## Dependencies

file_trans_system is built on top of the kcenon ecosystem libraries:

| System | Required | Purpose |
|--------|----------|---------|
| common_system | Yes | Result<T>, error handling, time utilities |
| thread_system | Yes | `typed_thread_pool<pipeline_stage>` for parallel pipeline processing |
| **network_system** | Yes | **TCP/TLS 1.3 transport layer (Phase 1), QUIC transport (Phase 2)** |
| container_system | Yes | Bounded queues for backpressure |
| LZ4 | Yes | Compression library (v1.9.0+) |
| **OpenSSL** | Yes | TLS encryption and QUIC transport support (v3.0+) |
| logger_system | Optional | Structured logging |
| monitoring_system | Optional | Metrics export |

> **Note**: The transport layer is implemented using **network_system**, which provides both TCP and QUIC transports. No external transport library is required.

## Documentation

| Document | Description |
|----------|-------------|
| [Quick Reference](docs/reference/quick-reference.md) | Common operations cheat sheet |
| [API Reference](docs/reference/api-reference.md) | Complete API documentation |
| [Protocol Specification](docs/reference/protocol-spec.md) | Wire protocol details |
| [Pipeline Architecture](docs/reference/pipeline-architecture.md) | Pipeline design guide |
| [Configuration Guide](docs/reference/configuration.md) | Tuning options |
| [Error Codes](docs/reference/error-codes.md) | Error code reference |
| [Getting Started](docs/reference/getting-started.md) | Step-by-step tutorial |
| [Sequence Diagrams](docs/reference/sequence-diagrams.md) | Interaction flows |

### Design Documents

| Document | Description |
|----------|-------------|
| [PRD](docs/PRD.md) | Product Requirements Document |
| [SRS](docs/SRS.md) | Software Requirements Specification |
| [SDS](docs/SDS.md) | Software Design Specification |
| [Architecture](docs/architecture.md) | Architecture overview |
| [Verification](docs/Verification.md) | Verification plan |
| [Validation](docs/Validation.md) | Validation plan |

## Building

Use the pinned compatible dependencies for this implementation branch:
~~~sh
python3 tools/bootstrap_dependencies.py --jobs 4
python3 tools/ci_build.py --build build --examples
python3 tools/sync_readme_snippets.py
python3 tools/check_quick_start.py --build build --artifacts build/quick-start-results
~~~
Linux requires CMake, Ninja, a C++20 compiler, OpenSSL, LZ4 and libcurl development
packages. macOS requires the corresponding Homebrew tools/libraries and
CMAKE_PREFIX_PATH set to the Homebrew prefix. Local validation used Apple Clang
21; CI configures Clang 18 on Ubuntu and Apple Clang on macOS. Windows is unverified.


### Requirements

- C++20 compatible compiler:
  - GCC 11+
  - Clang 14+
  - MSVC 19.29+
- CMake 3.20+
- LZ4 library (v1.9.0+)
- OpenSSL (v3.0+) for TLS and QUIC transport support

### Ecosystem Dependencies

file_trans_system requires the following sibling repositories to be present:

```
parent_directory/
├── file_trans_system/     # This repository
├── common_system/         # Required: Core utilities
├── thread_system/         # Required: Thread pool
├── network_system/        # Required: Network transport
└── container_system/      # Required: Data containers
```

Bootstrap the exact sibling revisions instead of checking out arbitrary dependency heads:

~~~sh
python3 tools/bootstrap_dependencies.py --jobs 4
~~~

Revisions are pinned in cmake/dependency-revisions.json. The script verifies
existing checkouts and applies required patches. Use a separate workspace if
your existing siblings have different revisions.

### Build Commands

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

### Testing

Build and run tests:

```bash
mkdir build && cd build
cmake .. -DFILE_TRANS_BUILD_TESTS=ON
cmake --build . --parallel

# Run all tests
ctest --output-on-failure

# Or run specific test executables
./bin/file_trans_unit_tests          # Unit tests
./bin/file_trans_integration_tests   # Integration tests
```

#### Test Categories

| Category | Description | File |
|----------|-------------|------|
| **Unit Tests - Core** | Chunk splitter, assembler, checksum (CRC32, SHA-256) | `tests/unit/core/` |
| **Unit Tests - Compression** | LZ4 compression engine tests | `tests/unit/compression/` |
| **Unit Tests - Protocol** | Protocol types, error codes, result handling, chunk flags | `tests/unit/protocol/` |
| **Unit Tests - State** | Connection/server state, config validation, statistics | `tests/unit/state/` |
| **Integration Tests - Basic** | Server/client lifecycle, connection, basic transfers | `test_basic_scenarios.cpp` |
| **Integration Tests - Advanced** | Error handling, large files, compression, stress tests | `test_error_advanced_scenarios.cpp` |
| **Integration Tests - Concurrency** | Multi-client connections, 100-connection load test, concurrent transfers | `test_concurrency.cpp` |
| **Integration Tests - Cloud** | S3/MinIO integration tests (requires external service) | `cloud/test_s3_integration.cpp` |

#### Disabling Integration Tests

Integration tests require actual network connections and may be unstable in CI environments due to timing issues. To build without integration tests:

```bash
cmake -B build -DFILE_TRANS_BUILD_INTEGRATION_TESTS=OFF
cmake --build build
```

#### Running Cloud Integration Tests

Cloud integration tests require an S3-compatible service (AWS S3 or MinIO):

```bash
# Start MinIO locally with Docker
docker run -p 9000:9000 -p 9001:9001 \
  -e MINIO_ROOT_USER=minioadmin \
  -e MINIO_ROOT_PASSWORD=minioadmin \
  minio/minio server /data --console-address ":9001"

# Create test bucket
mc alias set local http://localhost:9000 minioadmin minioadmin
mc mb local/test-bucket

# Run cloud integration tests
MINIO_ENDPOINT=http://localhost:9000 \
MINIO_ACCESS_KEY=minioadmin \
MINIO_SECRET_KEY=minioadmin \
MINIO_BUCKET=test-bucket \
./bin/file_trans_cloud_integration_tests
```

### Sanitizer Testing

Build with sanitizers to detect memory issues and undefined behavior:

```bash
# AddressSanitizer (memory errors)
cmake -B build -DFILE_TRANS_ENABLE_ASAN=ON
cmake --build build
ASAN_OPTIONS="abort_on_error=1" ctest --test-dir build

# ThreadSanitizer (data races)
cmake -B build -DFILE_TRANS_ENABLE_TSAN=ON
cmake --build build
TSAN_OPTIONS="abort_on_error=1" ctest --test-dir build

# UndefinedBehaviorSanitizer
cmake -B build -DFILE_TRANS_ENABLE_UBSAN=ON
cmake --build build
UBSAN_OPTIONS="print_stacktrace=1:abort_on_error=1" ctest --test-dir build
```

> **Note**: ASAN and TSAN cannot be enabled simultaneously.

### Benchmarks

[Public TCP measurements and runnable harness](docs/validation/PUBLIC_API_BENCHMARKS.md). The splitting/assembly tests below are component microbenchmarks and do not measure network throughput.

Build and run performance benchmarks:

```bash
# Build with benchmarks enabled
cmake -B build -DFILE_TRANS_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run throughput benchmarks
./build/bin/throughput_benchmarks

# Run with JSON output
./build/bin/throughput_benchmarks --benchmark_format=json --benchmark_out=results.json

# Run specific benchmarks
./build/bin/throughput_benchmarks --benchmark_filter="BM_SingleFile*"
```

#### Benchmark Categories

| Category | Description | Target |
|----------|-------------|--------|
| **Throughput** | Local split/assembly microbenchmark | No LAN inference |
| **Chunk Operations** | Chunk splitter and assembler performance | - |
| **Checksum** | CRC32 and SHA-256 performance | - |

### CMake Integration

```cmake
find_package(file_trans_system REQUIRED)
target_link_libraries(your_target PRIVATE kcenon::file_transfer)
```

## Examples

See the [examples/](examples/) directory for:

### Basic Examples
- `simple_server.cpp` - Basic file transfer server setup
- `simple_client.cpp` - Basic file transfer client

### Upload/Download Examples
- `upload_example.cpp` - Single file upload with progress callbacks, compression settings, and error handling
- `download_example.cpp` - Single file download with hash verification and overwrite policy
- `batch_upload_example.cpp` - Multi-file parallel upload with batch progress tracking
- `batch_download_example.cpp` - Multi-file parallel download with pattern matching

### Advanced Examples
- `resume_transfer.cpp` - Transfer pause/resume with interruption handling
- `auto_reconnect.cpp` - Auto-reconnection configuration and callback handling
- `custom_pipeline.cpp` - Pipeline customization (chunk size, compression settings)
- `bandwidth_throttling.cpp` - Upload/download bandwidth limiting
- `server_callbacks.cpp` - Server-side request validation and access control
- `quota_management.cpp` - Storage quota configuration and monitoring

## Cloud Storage Integration

Cloud snippets below illustrate component APIs. Consult the status table for executed service evidence; only S3 has real local MinIO integration validation in this series.

file_trans_system supports cloud storage backends for direct upload/download operations:

### AWS S3 Storage

```cpp
#include <kcenon/file_transfer/cloud/s3_storage.h>

using namespace kcenon::file_transfer;

// Create credentials
static_credentials creds;
creds.access_key_id = "AKIAIOSFODNN7EXAMPLE";
creds.secret_access_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
auto provider = s3_credential_provider::create(creds);

// Create S3 storage
auto config = cloud_config_builder::s3()
    .with_bucket("my-bucket")
    .with_region("us-east-1")
    .build_s3();

auto storage = s3_storage::create(config, provider);
storage->connect();

// Upload data
std::vector<std::byte> data = {...};
auto result = storage->upload("path/to/file.bin", data);
```

### Azure Blob Storage

```cpp
#include <kcenon/file_transfer/cloud/azure_blob_storage.h>

using namespace kcenon::file_transfer;

// Create credentials
azure_credentials creds;
creds.account_name = "mystorageaccount";
creds.account_key = "base64encodedkey...";
auto provider = azure_blob_credential_provider::create(creds);

// Or use connection string
auto provider = azure_blob_credential_provider::create_from_connection_string(
    "DefaultEndpointsProtocol=https;AccountName=...;AccountKey=...;");

// Create Azure Blob storage
auto config = cloud_config_builder::azure_blob()
    .with_account_name("mystorageaccount")
    .with_bucket("mycontainer")
    .build_azure_blob();

auto storage = azure_blob_storage::create(config, provider);
storage->connect();

// Upload file
auto result = storage->upload_file("/local/file.txt", "remote/file.txt");

// Generate SAS token
presigned_url_options options;
options.method = "GET";
options.expiration = std::chrono::hours{1};
auto sas_url = storage->generate_blob_sas("path/to/file.bin", options);
```

### Supported Cloud Providers

| Provider | Status | Features |
|----------|--------|----------|
| **AWS S3** | Validated on local MinIO | Real HTTP, multipart, ranged download and presigned URLs; AWS live untested |
| **Azure Blob** | Component / live unverified | Example compilation does not prove remote persistence |
| **Google Cloud Storage** | Signing vectors validated | V4 RSA GET/PUT signatures; live service unverified |

### Cloud Storage Examples

See the [examples/cloud/](examples/cloud/) directory:
- `s3_example.cpp` - AWS S3 operations with credentials, upload/download, presigned URLs
- `azure_blob_example.cpp` - Azure Blob operations with SAS tokens and access tier management
- `gcs_example.cpp` - Google Cloud Storage with service account authentication
- `hybrid_storage_example.cpp` - Local + cloud hybrid storage patterns
- `large_file_transfer_example.cpp` - Large file multipart uploads
- `multi_cloud_failover_example.cpp` - Multi-cloud redundancy and failover

### Cloud Storage Documentation

| Document | Description |
|----------|-------------|
| [AWS S3 Setup](docs/cloud/AWS_S3_SETUP.md) | S3 configuration and credentials |
| [Azure Blob Setup](docs/cloud/AZURE_BLOB_SETUP.md) | Azure Blob configuration |
| [GCS Setup](docs/cloud/GCS_SETUP.md) | Google Cloud Storage configuration |
| [Quick Start](docs/reference/cloud-quick-start.md) | Getting started with cloud storage |
| [Best Practices](docs/reference/cloud-best-practices.md) | Cloud storage best practices |

## Error Handling

Check both the initial result and the terminal handle result. The maintained Quick Start wait_verified helper handles both failure paths before printing success. error().message is a field. Resume uses the [checkpoint API](docs/protocol/RESUME.md).

### Common Error Codes

| Code | Name | Description |
|------|------|-------------|
| -700 | `transfer_init_failed` | Connection failed |
| -702 | `transfer_timeout` | Transfer timed out |
| -711 | `connection_closed` | Connection closed unexpectedly |
| -712 | `upload_rejected` | Server rejected upload |
| -720 | `chunk_checksum_error` | Data corruption detected |
| -743 | `file_not_found` | Local file not found |
| -745 | `storage_full` | Server storage quota exceeded |
| -746 | `file_not_found_on_server` | Requested file not on server |
| -747 | `access_denied` | Permission denied |
| -748 | `invalid_filename` | Invalid filename |

## Auto-Reconnect

Configure disconnect-driven retries through the public builder. Handle initial connection failure explicitly; transfer recovery also requires resumable checkpoints.

~~~cpp
auto built = file_transfer_client::builder()
    .with_auto_reconnect(true, reconnect_policy{})
    .build();
if (!built) throw std::runtime_error(built.error().message);
auto client = std::move(built.value());
client.on_connection_state_changed([](connection_state state) {
    std::cout << "Connection state: " << static_cast<int>(state) << "\n";
});
~~~

[Reconnect behavior and limits](docs/protocol/RECONNECTION.md).

## License

This project is licensed under the BSD 3-Clause License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please read our contributing guidelines before submitting PRs.

## Roadmap

The seven PRD phases share the scoped evidence in [implementation status](docs/IMPLEMENTATION_STATUS.md). No entire phase is marked complete.

<!-- roadmap:start -->
| PRD phase | Requirements | Current status and evidence | Remaining acceptance |
|---|---|---|---|
| 1. Core infrastructure | FR-08, FR-11, FR-14, FR-17/18 | Validated (scoped): codec, CRC/SHA, LZ4 and bounded pipeline [#175–#178, #188](docs/protocol/PIPELINE_CAPACITY.md) | NFR-03/07/08 absolute performance unmeasured [#190](https://github.com/kcenon/file_trans_system/issues/190) |
| 2. Server foundation | FR-01/03/05/07/16 | Validated (scoped): real storage, requests, listing, quotas [#175–#179](docs/validation/REGRESSION_GATES.md) | Broad platform/service qualification; retention policies have component evidence only |
| 3. Client foundation | FR-02/03/05/07, NFR-13 | Validated (scoped): TCP transfers, failure handling and reconnect [#176–#179, #185](docs/protocol/RECONNECTION.md) | Optional authentication and TLS 1.3 NFR-16/17 remain open; reconnect does not imply resume |
| 4. Compression and resume | FR-09/11/14/15 | Validated (scoped): plain/encrypted process crash recovery and compression [#183, #186](docs/protocol/RESUME.md) | Subsecond recovery only measured on a small loopback fixture; large-prefix validation remains linear |
| 5. Advanced features | FR-04/06/10/12/13/16/18 | Validated (scoped): batch, progress, throttling, quotas and admission [#187–#188](docs/protocol/BATCH_TRANSFERS.md) | “Without degradation” and 5% bandwidth tolerance need qualification; 100-client uploads and downloads now all pass [#190](docs/validation/PUBLIC_API_BENCHMARKS.md) |
| 6. Integration and polish | PRD §10, NFR-01–10/22 | Validated (scoped): public monitoring/quota, logger, cloud/hybrid persistence and compiled examples [#189–#191](docs/validation/CONTINUATION_176_190.md) | Coverage 73.52% <80%; full macOS TSAN fails on reproduced errno runtime reports; leak freedom, Windows and dedicated LAN unverified |
| 7. QUIC (optional) | PRD §9 phase 7, NFR-16 | Component: public transport selection and repaired QUIC timeout cleanup [#184](docs/protocol/TRANSPORTS.md) | QUIC handshake/key installation and certificate/UDP routing remain blocked in pinned dependency; 0-RTT/migration unvalidated |
<!-- roadmap:end -->
