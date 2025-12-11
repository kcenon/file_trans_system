# File Transfer System - Software Design Specification

## Document Information

| Item | Description |
|------|-------------|
| **Project Name** | file_trans_system |
| **Document Type** | Software Design Specification (SDS) |
| **Version** | 0.2.0 |
| **Status** | Approved |
| **Created** | 2025-12-11 |
| **Author** | kcenon@naver.com |
| **Related Documents** | SRS.md v0.2.0, PRD.md v0.2.0 |

---

## 1. Introduction

### 1.1 Purpose

This Software Design Specification (SDS) document describes the detailed design of the **file_trans_system** library. It translates the software requirements defined in the SRS into a concrete design that can be implemented. This document serves as a blueprint for developers and provides traceability from requirements to design elements.

This document is intended for:
- Software developers implementing the system
- System architects reviewing the design
- QA engineers understanding the test scope
- Maintainers understanding the system structure

### 1.2 Scope

This document covers:
- Client-Server system architecture and component design
- Server-side storage management and client handling
- Client-side upload/download operations
- Data structures and data flow
- Interface specifications
- Algorithm descriptions
- Error handling design
- Traceability to SRS requirements

### 1.3 Design Overview

The file_trans_system uses a **Client-Server Architecture**:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                               Server Layer                                    │
│   ┌──────────────────────────────────────────────────────────────────────┐  │
│   │                      file_transfer_server                             │  │
│   │  ┌───────────────┐ ┌──────────────────┐ ┌─────────────────────────┐  │  │
│   │  │ Storage       │ │ Connection       │ │ Server Pipeline         │  │  │
│   │  │ Manager       │ │ Manager          │ │ (upload/download)       │  │  │
│   │  └───────────────┘ └──────────────────┘ └─────────────────────────┘  │  │
│   └──────────────────────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────────────────┤
│                              Client Layer                                    │
│   ┌──────────────────────────────────────────────────────────────────────┐  │
│   │                      file_transfer_client                             │  │
│   │  ┌───────────────┐ ┌──────────────────┐ ┌─────────────────────────┐  │  │
│   │  │ Connection    │ │ Upload/Download  │ │ Client Pipeline         │  │  │
│   │  │ Handler       │ │ Manager          │ │ (send/receive)          │  │  │
│   │  └───────────────┘ └──────────────────┘ └─────────────────────────┘  │  │
│   └──────────────────────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────────────────┤
│                             Service Layer                                    │
│   chunk_manager  │  compression_engine  │  resume_handler  │  checksum      │
├─────────────────────────────────────────────────────────────────────────────┤
│                            Transport Layer                                   │
│   transport_interface  │  tcp_transport  │  quic_transport (Phase 2)        │
├─────────────────────────────────────────────────────────────────────────────┤
│                          Infrastructure Layer                                │
│   common_system  │  thread_system  │  network_system  │  container_system   │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Architectural Design

### 2.1 Architecture Style

The system employs a **Client-Server Architecture** combined with **Pipeline Architecture**:

| Style | Application | Rationale |
|-------|-------------|-----------|
| **Client-Server** | System topology | Central storage, multiple clients |
| **Pipeline** | Data processing (read→compress→send) | Maximizes throughput via parallel stages |
| **Layered** | System organization | Separation of concerns, testability |
| **Strategy** | Transport, compression | Pluggable implementations |
| **Observer** | Progress notification | Decoupled event handling |

### 2.2 Component Architecture

#### 2.2.1 High-Level Component Diagram

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                              file_trans_system                                │
│                                                                               │
│  ┌────────────────────────────────────────────────────────────────────────┐  │
│  │                       file_transfer_server                              │  │
│  │                                                                         │  │
│  │  +start()              +stop()              +on_upload_request()       │  │
│  │  +on_download_request() +on_client_connected() +get_storage_stats()    │  │
│  │                                                                         │  │
│  │  ┌─────────────┐ ┌─────────────────┐ ┌─────────────────────────────┐   │  │
│  │  │  Storage    │ │   Connection    │ │      Request Handler        │   │  │
│  │  │  Manager    │ │   Manager       │ │ (upload/download/list)      │   │  │
│  │  └──────┬──────┘ └────────┬────────┘ └──────────────┬──────────────┘   │  │
│  │         │                 │                          │                  │  │
│  │         └─────────────────┼──────────────────────────┘                  │  │
│  │                           ▼                                             │  │
│  │  ┌──────────────────────────────────────────────────────────────────┐  │  │
│  │  │                    Server Pipeline                                │  │  │
│  │  │  [Upload]  recv→decompress→verify→write                          │  │  │
│  │  │  [Download] read→compress→send                                    │  │  │
│  │  └──────────────────────────────────────────────────────────────────┘  │  │
│  └────────────────────────────────────────────────────────────────────────┘  │
│                                                                               │
│  ┌────────────────────────────────────────────────────────────────────────┐  │
│  │                       file_transfer_client                              │  │
│  │                                                                         │  │
│  │  +connect()          +disconnect()        +upload_file()               │  │
│  │  +download_file()    +list_files()        +on_progress()               │  │
│  │                                                                         │  │
│  │  ┌─────────────┐ ┌─────────────────┐ ┌─────────────────────────────┐   │  │
│  │  │ Connection  │ │ Auto-Reconnect  │ │    Transfer Handler         │   │  │
│  │  │ Manager     │ │ Handler         │ │ (upload/download)           │   │  │
│  │  └──────┬──────┘ └────────┬────────┘ └──────────────┬──────────────┘   │  │
│  │         │                 │                          │                  │  │
│  │         └─────────────────┼──────────────────────────┘                  │  │
│  │                           ▼                                             │  │
│  │  ┌──────────────────────────────────────────────────────────────────┐  │  │
│  │  │                    Client Pipeline                                │  │  │
│  │  │  [Upload]   read→compress→send                                    │  │  │
│  │  │  [Download] recv→decompress→write                                 │  │  │
│  │  └──────────────────────────────────────────────────────────────────┘  │  │
│  └────────────────────────────────────────────────────────────────────────┘  │
│                                                                               │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                         Service Layer                                 │   │
│  │  ┌─────────────┐ ┌────────────────────┐ ┌──────────────────────────┐ │   │
│  │  │chunk_manager│ │ compression_engine │ │    resume_handler        │ │   │
│  │  │             │ │                    │ │                          │ │   │
│  │  │ +split()    │ │ +compress()        │ │ +save_state()            │ │   │
│  │  │ +assemble() │ │ +decompress()      │ │ +load_state()            │ │   │
│  │  │ +verify()   │ │ +is_compressible() │ │ +get_missing_chunks()    │ │   │
│  │  └─────────────┘ └────────────────────┘ └──────────────────────────┘ │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                                                               │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                        Transport Layer                                │   │
│  │  ┌────────────────────┐  ┌──────────────┐  ┌────────────────────┐   │   │
│  │  │transport_interface │  │tcp_transport │  │ quic_transport     │   │   │
│  │  │   <<interface>>    │◄─┤              │  │  (Phase 2)         │   │   │
│  │  │                    │  └──────────────┘  └────────────────────┘   │   │
│  │  └────────────────────┘                                              │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 2.3 Design Patterns Used

| Pattern | Usage | SRS Trace |
|---------|-------|-----------|
| **Builder** | file_transfer_server::builder, file_transfer_client::builder | API usability |
| **Strategy** | transport_interface implementations | SRS-TRANS-001 |
| **Observer** | Progress callbacks, completion events | SRS-PROGRESS-001 |
| **Pipeline** | upload_pipeline, download_pipeline | SRS-PIPE-001, SRS-PIPE-002 |
| **Factory** | create_transport() | SRS-TRANS-001 |
| **State** | Transfer state machine, connection state | SRS-PROGRESS-002 |
| **Command** | Pipeline jobs | SRS-PIPE-001 |

---

## 3. Component Design

### 3.1 file_transfer_server Component

#### 3.1.1 Responsibility
Manages file storage, accepts client connections, and handles upload/download/list requests.

#### 3.1.2 SRS Traceability
| SRS Requirement | Design Element |
|-----------------|----------------|
| SRS-SERVER-001 | Server initialization and storage setup |
| SRS-SERVER-002 | start() method |
| SRS-SERVER-003 | stop() method |
| SRS-SERVER-004 | on_upload_request() callback |
| SRS-SERVER-005 | on_download_request() callback |
| SRS-SERVER-006 | list_files handling |
| SRS-STORAGE-001 | storage_manager component |
| SRS-STORAGE-002 | filename_validator component |

#### 3.1.3 Class Design

```cpp
namespace kcenon::file_transfer {

class file_transfer_server {
public:
    class builder {
    public:
        builder& with_storage_directory(const std::filesystem::path& dir);
        builder& with_max_connections(std::size_t max_count);
        builder& with_max_file_size(uint64_t max_bytes);
        builder& with_storage_quota(uint64_t max_bytes);
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_transport(transport_type type);
        [[nodiscard]] auto build() -> Result<file_transfer_server>;

    private:
        std::filesystem::path       storage_dir_;
        std::size_t                 max_connections_    = 100;
        uint64_t                    max_file_size_      = 10ULL * 1024 * 1024 * 1024;
        uint64_t                    storage_quota_      = 100ULL * 1024 * 1024 * 1024;
        pipeline_config             pipeline_config_;
        transport_type              transport_type_     = transport_type::tcp;
    };

    // Lifecycle
    [[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;
    [[nodiscard]] auto stop() -> Result<void>;

    // Request callbacks
    void on_upload_request(std::function<bool(const upload_request&)> callback);
    void on_download_request(std::function<bool(const download_request&)> callback);

    // Event callbacks
    void on_client_connected(std::function<void(const client_info&)> callback);
    void on_client_disconnected(std::function<void(const client_info&)> callback);
    void on_transfer_complete(std::function<void(const transfer_result&)> callback);

    // Progress monitoring
    void on_progress(std::function<void(const transfer_progress&)> callback);

    // Statistics
    [[nodiscard]] auto get_statistics() -> server_statistics;
    [[nodiscard]] auto get_storage_stats() -> storage_stats;
    [[nodiscard]] auto list_active_transfers() -> std::vector<transfer_info>;

private:
    // State
    enum class server_state { stopped, starting, running, stopping };
    std::atomic<server_state>           state_{server_state::stopped};

    // Components
    std::unique_ptr<storage_manager>    storage_manager_;
    std::unique_ptr<connection_manager> connection_manager_;
    std::unique_ptr<server_pipeline>    pipeline_;
    std::unique_ptr<transport_interface> transport_;
    std::unique_ptr<resume_handler>     resume_handler_;

    // Configuration
    server_config                       config_;

    // Active clients and transfers
    std::unordered_map<client_id, client_context>     clients_;
    std::unordered_map<transfer_id, transfer_context> transfers_;
    std::shared_mutex                   clients_mutex_;
    std::shared_mutex                   transfers_mutex_;

    // Callbacks
    std::function<bool(const upload_request&)>    upload_callback_;
    std::function<bool(const download_request&)>  download_callback_;
    std::function<void(const client_info&)>       connect_callback_;
    std::function<void(const client_info&)>       disconnect_callback_;
    std::function<void(const transfer_result&)>   complete_callback_;
    std::function<void(const transfer_progress&)> progress_callback_;

    // Internal methods
    void accept_connections();
    void handle_client(client_context& ctx);
    void process_upload_request(const upload_request& req, client_context& ctx);
    void process_download_request(const download_request& req, client_context& ctx);
    void process_list_request(const list_request& req, client_context& ctx);
};

} // namespace kcenon::file_transfer
```

#### 3.1.4 Sequence Diagram: Upload Request Processing

```
┌──────────┐ ┌──────────────┐ ┌──────────────┐ ┌────────────┐ ┌─────────────┐
│  Client  │ │    Server    │ │   Storage    │ │  Pipeline  │ │  Callbacks  │
└────┬─────┘ └──────┬───────┘ └──────┬───────┘ └─────┬──────┘ └──────┬──────┘
     │              │                │               │               │
     │UPLOAD_REQUEST│                │               │               │
     │─────────────>│                │               │               │
     │              │                │               │               │
     │              │ validate_filename()            │               │
     │              │───────────────>│               │               │
     │              │                │               │               │
     │              │ check_quota()  │               │               │
     │              │───────────────>│               │               │
     │              │                │               │               │
     │              │          on_upload_request()   │               │
     │              │────────────────────────────────────────────────>│
     │              │                │               │               │
     │              │                │               │   accept/reject
     │              │<───────────────────────────────────────────────│
     │              │                │               │               │
     │ UPLOAD_ACCEPT│                │               │               │
     │<─────────────│                │               │               │
     │              │                │               │               │
     │  CHUNK_DATA  │                │               │               │
     │─────────────>│                │               │               │
     │              │    submit_chunk()              │               │
     │              │──────────────────────────────>│               │
     │              │                │               │               │
     │              │                │  decompress   │               │
     │              │                │<──────────────│               │
     │              │                │               │               │
     │              │                │  write_chunk  │               │
     │              │                │<──────────────│               │
     │              │                │               │               │
     │   CHUNK_ACK  │                │               │               │
     │<─────────────│                │               │               │
     │              │                │               │               │
     │     ...      │    [Repeat for all chunks]    │               │
     │              │                │               │               │
     │TRANSFER_COMPLETE              │               │               │
     │─────────────>│                │               │               │
     │              │ finalize_file()│               │               │
     │              │───────────────>│               │               │
     │              │                │               │               │
     │TRANSFER_VERIFY                │               │               │
     │<─────────────│                │               │               │
```

---

### 3.2 file_transfer_client Component

#### 3.2.1 Responsibility
Connects to server, uploads/downloads files, handles auto-reconnection.

#### 3.2.2 SRS Traceability
| SRS Requirement | Design Element |
|-----------------|----------------|
| SRS-CLIENT-001 | connect() method |
| SRS-CLIENT-002 | auto_reconnect_handler |
| SRS-CLIENT-003 | upload_file() method |
| SRS-CLIENT-004 | download_file() method |
| SRS-CLIENT-005 | list_files() method |
| SRS-CLIENT-006 | upload_files() method |
| SRS-CLIENT-007 | download_files() method |
| SRS-PROGRESS-001 | on_progress() callback |

#### 3.2.3 Class Design

```cpp
namespace kcenon::file_transfer {

// Reconnection policy
struct reconnect_policy {
    std::size_t max_attempts        = 5;
    duration    initial_delay       = std::chrono::seconds(1);
    duration    max_delay           = std::chrono::seconds(30);
    double      backoff_multiplier  = 2.0;
};

// Connection state
enum class connection_state {
    disconnected,
    connecting,
    connected,
    reconnecting
};

class file_transfer_client {
public:
    class builder {
    public:
        builder& with_compression(compression_mode mode);
        builder& with_compression_level(compression_level level);
        builder& with_chunk_size(std::size_t size);
        builder& with_auto_reconnect(bool enable, reconnect_policy policy = {});
        builder& with_upload_bandwidth_limit(std::size_t bytes_per_second);
        builder& with_download_bandwidth_limit(std::size_t bytes_per_second);
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_transport(transport_type type);
        [[nodiscard]] auto build() -> Result<file_transfer_client>;

    private:
        compression_mode            compression_mode_   = compression_mode::adaptive;
        compression_level           compression_level_  = compression_level::fast;
        std::size_t                 chunk_size_         = 256 * 1024;
        bool                        auto_reconnect_     = true;
        reconnect_policy            reconnect_policy_;
        std::optional<std::size_t>  upload_bandwidth_limit_;
        std::optional<std::size_t>  download_bandwidth_limit_;
        pipeline_config             pipeline_config_;
        transport_type              transport_type_     = transport_type::tcp;
    };

    // Connection
    [[nodiscard]] auto connect(const endpoint& server_addr) -> Result<void>;
    [[nodiscard]] auto disconnect() -> Result<void>;
    [[nodiscard]] auto is_connected() const -> bool;

    // Single file operations
    [[nodiscard]] auto upload_file(
        const std::filesystem::path& local_path,
        const std::string& remote_name,
        const upload_options& options = {}
    ) -> Result<transfer_handle>;

    [[nodiscard]] auto download_file(
        const std::string& remote_name,
        const std::filesystem::path& local_path,
        const download_options& options = {}
    ) -> Result<transfer_handle>;

    // Batch operations
    [[nodiscard]] auto upload_files(
        std::span<const upload_entry> files,
        const upload_options& options = {}
    ) -> Result<batch_transfer_handle>;

    [[nodiscard]] auto download_files(
        std::span<const download_entry> files,
        const download_options& options = {}
    ) -> Result<batch_transfer_handle>;

    // File listing
    [[nodiscard]] auto list_files(
        const list_options& options = {}
    ) -> Result<std::vector<file_info>>;

    // Transfer control
    [[nodiscard]] auto cancel(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto pause(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto resume(const transfer_id& id) -> Result<void>;

    // Callbacks
    void on_progress(std::function<void(const transfer_progress&)> callback);
    void on_complete(std::function<void(const transfer_result&)> callback);
    void on_connection_state_changed(std::function<void(connection_state)> callback);

    // Statistics
    [[nodiscard]] auto get_statistics() -> client_statistics;
    [[nodiscard]] auto get_compression_stats() -> compression_statistics;

private:
    // State
    std::atomic<connection_state>       connection_state_{connection_state::disconnected};

    // Components
    std::unique_ptr<client_pipeline>    pipeline_;
    std::unique_ptr<transport_interface> transport_;
    std::unique_ptr<auto_reconnect_handler> reconnect_handler_;
    std::unique_ptr<resume_handler>     resume_handler_;
    std::unique_ptr<chunk_compressor>   compressor_;

    // Configuration
    client_config                       config_;
    endpoint                            server_endpoint_;

    // Active transfers
    std::unordered_map<transfer_id, transfer_context> active_transfers_;
    std::mutex                          transfers_mutex_;

    // Callbacks
    std::function<void(const transfer_progress&)> progress_callback_;
    std::function<void(const transfer_result&)>   complete_callback_;
    std::function<void(connection_state)>         state_callback_;

    // Internal methods
    void handle_connection_loss();
    void attempt_reconnect();
    void send_upload_request(const upload_request& req);
    void send_download_request(const download_request& req);
    void process_download_chunk(const chunk& c, transfer_context& ctx);
};

} // namespace kcenon::file_transfer
```

#### 3.2.4 Sequence Diagram: download_file()

```
┌──────────┐ ┌──────────────┐ ┌─────────────┐ ┌────────────┐ ┌─────────────┐
│   App    │ │    Client    │ │   Server    │ │  Pipeline  │ │    Disk     │
└────┬─────┘ └──────┬───────┘ └──────┬──────┘ └─────┬──────┘ └──────┬──────┘
     │              │                │              │               │
     │download_file()               │              │               │
     │─────────────>│                │              │               │
     │              │                │              │               │
     │              │DOWNLOAD_REQUEST│              │               │
     │              │───────────────>│              │               │
     │              │                │              │               │
     │              │                │ check_file_exists()          │
     │              │                │──────────────────────────────│
     │              │                │              │               │
     │              │DOWNLOAD_ACCEPT │              │               │
     │              │(with metadata) │              │               │
     │              │<───────────────│              │               │
     │              │                │              │               │
     │              │  CHUNK_DATA    │              │               │
     │              │<───────────────│              │               │
     │              │                │              │               │
     │              │    submit_chunk()             │               │
     │              │──────────────────────────────>│               │
     │              │                │              │               │
     │              │                │   decompress │               │
     │              │                │<─────────────│               │
     │              │                │              │               │
     │              │                │    write()   │               │
     │              │                │──────────────────────────────>│
     │              │                │              │               │
     │  progress()  │                │              │               │
     │<─────────────│                │              │               │
     │              │                │              │               │
     │              │     ...        │  [Repeat]    │               │
     │              │                │              │               │
     │              │TRANSFER_COMPLETE              │               │
     │              │<───────────────│              │               │
     │              │                │              │               │
     │              │              verify_sha256()  │               │
     │              │─────────────────────────────────────────────>│
     │              │                │              │               │
     │              │TRANSFER_VERIFY │              │               │
     │              │───────────────>│              │               │
     │              │                │              │               │
     │   Result     │                │              │               │
     │<─────────────│                │              │               │
```

---

### 3.3 storage_manager Component

#### 3.3.1 Responsibility
Manages server-side file storage, quota enforcement, and file validation.

#### 3.3.2 SRS Traceability
| SRS Requirement | Design Element |
|-----------------|----------------|
| SRS-STORAGE-001 | Quota management methods |
| SRS-STORAGE-002 | validate_filename() method |

#### 3.3.3 Class Design

```cpp
namespace kcenon::file_transfer {

// Storage configuration
struct storage_config {
    std::filesystem::path   storage_dir;
    uint64_t                max_total_size  = 100ULL * 1024 * 1024 * 1024;  // 100GB
    uint64_t                max_file_size   = 10ULL * 1024 * 1024 * 1024;   // 10GB
    uint64_t                reserved_space  = 1ULL * 1024 * 1024 * 1024;    // 1GB
};

// Storage statistics
struct storage_stats {
    uint64_t    total_capacity;
    uint64_t    used_size;
    uint64_t    available_size;
    std::size_t file_count;

    [[nodiscard]] auto usage_percent() const -> double;
};

class storage_manager {
public:
    explicit storage_manager(const storage_config& config);

    // File operations
    [[nodiscard]] auto create_file(
        const std::string& filename,
        uint64_t size
    ) -> Result<std::filesystem::path>;

    [[nodiscard]] auto delete_file(const std::string& filename) -> Result<void>;

    [[nodiscard]] auto get_file_path(const std::string& filename)
        -> Result<std::filesystem::path>;

    [[nodiscard]] auto file_exists(const std::string& filename) const -> bool;

    // File listing
    [[nodiscard]] auto list_files(
        const std::string& pattern = "*",
        std::size_t offset = 0,
        std::size_t limit = 1000
    ) -> Result<std::vector<file_info>>;

    // Validation
    [[nodiscard]] auto validate_filename(const std::string& filename) -> Result<void>;
    [[nodiscard]] auto can_store_file(uint64_t size) -> Result<void>;

    // Statistics
    [[nodiscard]] auto get_stats() const -> storage_stats;

    // Temp file management
    [[nodiscard]] auto create_temp_file(const transfer_id& id)
        -> Result<std::filesystem::path>;
    [[nodiscard]] auto finalize_temp_file(
        const transfer_id& id,
        const std::string& final_name
    ) -> Result<std::filesystem::path>;
    void cleanup_temp_file(const transfer_id& id);

private:
    storage_config              config_;
    std::filesystem::path       temp_dir_;

    mutable std::shared_mutex   mutex_;
    uint64_t                    current_usage_{0};
    std::size_t                 file_count_{0};

    // Path traversal prevention
    [[nodiscard]] auto safe_path(const std::string& filename) const
        -> Result<std::filesystem::path>;

    // Quota tracking
    void update_usage(int64_t delta);
    void recalculate_usage();
};

// Filename validator utility
class filename_validator {
public:
    [[nodiscard]] static auto validate(const std::string& filename) -> Result<void>;

private:
    // Invalid patterns
    static constexpr std::array<std::string_view, 4> invalid_patterns = {
        "..", "/", "\\", "\0"
    };

    // Invalid characters
    static constexpr std::string_view invalid_chars = "<>:\"|?*";
};

} // namespace kcenon::file_transfer
```

---

### 3.4 chunk_manager Component

#### 3.4.1 Responsibility
Splits files into chunks for sending and reassembles chunks into files on receiving.

#### 3.4.2 SRS Traceability
| SRS Requirement | Design Element |
|-----------------|----------------|
| SRS-CHUNK-001 | chunk_splitter class |
| SRS-CHUNK-002 | chunk_assembler class |
| SRS-CHUNK-003 | CRC32 checksum calculation |
| SRS-CHUNK-004 | SHA-256 file hash calculation |

#### 3.4.3 Class Design

```cpp
namespace kcenon::file_transfer {

// Configuration for chunk operations
struct chunk_config {
    std::size_t chunk_size     = 256 * 1024;  // 256KB default
    std::size_t min_chunk_size = 64 * 1024;   // 64KB minimum
    std::size_t max_chunk_size = 1024 * 1024; // 1MB maximum

    [[nodiscard]] auto validate() const -> Result<void>;
};

// Chunk splitter - splits files into chunks
class chunk_splitter {
public:
    explicit chunk_splitter(const chunk_config& config);

    // Iterator-style interface for memory efficiency
    class chunk_iterator {
    public:
        [[nodiscard]] auto has_next() const -> bool;
        [[nodiscard]] auto next() -> Result<chunk>;
        [[nodiscard]] auto current_index() const -> uint64_t;
        [[nodiscard]] auto total_chunks() const -> uint64_t;

    private:
        friend class chunk_splitter;
        std::ifstream           file_;
        chunk_config            config_;
        uint64_t               current_index_;
        uint64_t               total_chunks_;
        transfer_id            transfer_id_;
        std::vector<std::byte> buffer_;
    };

    // Create iterator for file
    [[nodiscard]] auto split(
        const std::filesystem::path& file_path,
        const transfer_id& id
    ) -> Result<chunk_iterator>;

    // Calculate file metadata
    [[nodiscard]] auto calculate_metadata(
        const std::filesystem::path& file_path
    ) -> Result<file_metadata>;

private:
    chunk_config config_;

    [[nodiscard]] auto calculate_crc32(std::span<const std::byte> data) const -> uint32_t;
};

// Chunk assembler - reassembles chunks into files
class chunk_assembler {
public:
    explicit chunk_assembler(const std::filesystem::path& output_dir);

    // Process incoming chunk
    [[nodiscard]] auto process_chunk(const chunk& c) -> Result<void>;

    // Check if file is complete
    [[nodiscard]] auto is_complete(const transfer_id& id) const -> bool;

    // Get missing chunk indices
    [[nodiscard]] auto get_missing_chunks(const transfer_id& id) const
        -> std::vector<uint64_t>;

    // Finalize file (verify SHA-256)
    [[nodiscard]] auto finalize(
        const transfer_id& id,
        const std::string& expected_hash
    ) -> Result<std::filesystem::path>;

    // Get assembly progress
    [[nodiscard]] auto get_progress(const transfer_id& id) const
        -> std::optional<assembly_progress>;

private:
    struct assembly_context {
        std::filesystem::path   temp_file_path;
        std::ofstream          file;
        uint64_t               total_chunks;
        std::vector<bool>      received_chunks;  // Bitmap
        uint64_t               bytes_written;
        std::mutex             mutex;
    };

    std::filesystem::path                                   output_dir_;
    std::unordered_map<transfer_id, assembly_context>       contexts_;
    std::shared_mutex                                        contexts_mutex_;

    [[nodiscard]] auto verify_crc32(const chunk& c) const -> bool;
};

// Checksum utilities
class checksum {
public:
    // CRC32 for chunk integrity
    [[nodiscard]] static auto crc32(std::span<const std::byte> data) -> uint32_t;
    [[nodiscard]] static auto verify_crc32(
        std::span<const std::byte> data,
        uint32_t expected
    ) -> bool;

    // SHA-256 for file integrity
    [[nodiscard]] static auto sha256_file(const std::filesystem::path& path)
        -> Result<std::string>;
    [[nodiscard]] static auto verify_sha256(
        const std::filesystem::path& path,
        const std::string& expected
    ) -> bool;
};

} // namespace kcenon::file_transfer
```

---

### 3.5 compression_engine Component

#### 3.5.1 Responsibility
Provides LZ4 compression and decompression with adaptive detection.

#### 3.5.2 SRS Traceability
| SRS Requirement | Design Element |
|-----------------|----------------|
| SRS-COMP-001 | lz4_engine::compress() |
| SRS-COMP-002 | lz4_engine::decompress() |
| SRS-COMP-003 | adaptive_compression::is_compressible() |
| SRS-COMP-004 | compression_mode enum |
| SRS-COMP-005 | compression_statistics struct |

#### 3.5.3 Class Design

```cpp
namespace kcenon::file_transfer {

// Compression modes
enum class compression_mode {
    disabled,   // Never compress
    enabled,    // Always compress
    adaptive    // Auto-detect compressibility (default)
};

// Compression levels
enum class compression_level {
    fast,             // LZ4 standard (~400 MB/s)
    high_compression  // LZ4-HC (~50 MB/s, better ratio)
};

// LZ4 compression engine
class lz4_engine {
public:
    [[nodiscard]] static auto compress(
        std::span<const std::byte> input,
        std::span<std::byte> output
    ) -> Result<std::size_t>;

    [[nodiscard]] static auto compress_hc(
        std::span<const std::byte> input,
        std::span<std::byte> output,
        int level = 9
    ) -> Result<std::size_t>;

    [[nodiscard]] static auto decompress(
        std::span<const std::byte> compressed,
        std::span<std::byte> output,
        std::size_t original_size
    ) -> Result<std::size_t>;

    [[nodiscard]] static auto max_compressed_size(std::size_t input_size)
        -> std::size_t;
};

// Adaptive compression detection
class adaptive_compression {
public:
    [[nodiscard]] static auto is_compressible(
        std::span<const std::byte> data,
        double threshold = 0.9
    ) -> bool;

    [[nodiscard]] static auto is_likely_compressible(
        const std::filesystem::path& file
    ) -> bool;

private:
    static constexpr std::array<std::string_view, 10> compressed_extensions = {
        ".zip", ".gz", ".tar.gz", ".tgz", ".bz2",
        ".jpg", ".jpeg", ".png", ".mp4", ".mp3"
    };
};

// Compression statistics
struct compression_statistics {
    std::atomic<uint64_t> total_raw_bytes{0};
    std::atomic<uint64_t> total_compressed_bytes{0};
    std::atomic<uint64_t> chunks_compressed{0};
    std::atomic<uint64_t> chunks_skipped{0};
    std::atomic<uint64_t> compression_time_us{0};
    std::atomic<uint64_t> decompression_time_us{0};

    [[nodiscard]] auto compression_ratio() const -> double;
    [[nodiscard]] auto compression_speed_mbps() const -> double;
    [[nodiscard]] auto decompression_speed_mbps() const -> double;
};

} // namespace kcenon::file_transfer
```

---

### 3.6 Pipeline Components

#### 3.6.1 Responsibility
Implements multi-stage parallel processing for maximum throughput.

#### 3.6.2 SRS Traceability
| SRS Requirement | Design Element |
|-----------------|----------------|
| SRS-PIPE-001 | upload_pipeline class |
| SRS-PIPE-002 | download_pipeline class |
| SRS-PIPE-003 | Bounded queue implementation |
| SRS-PIPE-004 | pipeline_statistics struct |

#### 3.6.3 Class Design

```cpp
namespace kcenon::file_transfer {

// Pipeline stage types
enum class pipeline_stage : uint8_t {
    io_read,        // File read operations
    chunk_process,  // Chunk assembly/disassembly
    compression,    // LZ4 compress/decompress
    network,        // Network send/receive
    io_write        // File write operations
};

// Pipeline configuration
struct pipeline_config {
    std::size_t io_read_workers      = 2;
    std::size_t chunk_workers        = 2;
    std::size_t compression_workers  = 4;
    std::size_t network_workers      = 2;
    std::size_t io_write_workers     = 2;

    std::size_t read_queue_size      = 16;
    std::size_t compress_queue_size  = 32;
    std::size_t send_queue_size      = 64;
    std::size_t decompress_queue_size = 32;
    std::size_t write_queue_size     = 16;

    [[nodiscard]] static auto auto_detect() -> pipeline_config;
};

// Bounded queue for backpressure
template<typename T>
class bounded_queue {
public:
    explicit bounded_queue(std::size_t max_size);

    void push(T item);
    [[nodiscard]] auto pop() -> T;
    [[nodiscard]] auto try_push(T item) -> bool;
    [[nodiscard]] auto try_pop() -> std::optional<T>;

    [[nodiscard]] auto size() const -> std::size_t;
    [[nodiscard]] auto empty() const -> bool;
    [[nodiscard]] auto full() const -> bool;

private:
    std::queue<T>           queue_;
    std::size_t             max_size_;
    mutable std::mutex      mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
};

// Pipeline statistics
struct pipeline_statistics {
    struct stage_stats {
        std::atomic<uint64_t>    jobs_processed{0};
        std::atomic<uint64_t>    bytes_processed{0};
        std::atomic<uint64_t>    total_latency_us{0};
        std::atomic<std::size_t> current_queue_depth{0};
        std::atomic<std::size_t> max_queue_depth{0};

        [[nodiscard]] auto avg_latency_us() const -> double;
        [[nodiscard]] auto throughput_mbps() const -> double;
    };

    stage_stats io_read_stats;
    stage_stats chunk_stats;
    stage_stats compression_stats;
    stage_stats network_stats;
    stage_stats io_write_stats;

    [[nodiscard]] auto bottleneck_stage() const -> pipeline_stage;
};

} // namespace kcenon::file_transfer
```

---

### 3.7 Transport Components

#### 3.7.1 Responsibility
Abstracts network transport protocols (TCP, QUIC).

#### 3.7.2 SRS Traceability
| SRS Requirement | Design Element |
|-----------------|----------------|
| SRS-TRANS-001 | transport_interface class |
| SRS-TRANS-002 | tcp_transport class |

#### 3.7.3 Class Design

```cpp
namespace kcenon::file_transfer {

enum class transport_type {
    tcp,    // TCP + TLS 1.3 (default)
    quic    // QUIC (Phase 2)
};

class transport_interface {
public:
    virtual ~transport_interface() = default;

    [[nodiscard]] virtual auto connect(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto disconnect() -> Result<void> = 0;
    [[nodiscard]] virtual auto is_connected() const -> bool = 0;

    [[nodiscard]] virtual auto send(std::span<const std::byte> data) -> Result<void> = 0;
    [[nodiscard]] virtual auto receive(std::span<std::byte> buffer) -> Result<std::size_t> = 0;

    [[nodiscard]] virtual auto listen(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto accept() -> Result<std::unique_ptr<transport_interface>> = 0;
};

struct tcp_transport_config {
    bool        enable_tls      = true;
    bool        tcp_nodelay     = true;
    std::size_t send_buffer     = 256 * 1024;
    std::size_t recv_buffer     = 256 * 1024;
    duration    connect_timeout = std::chrono::seconds(10);
    duration    read_timeout    = std::chrono::seconds(30);
};

class tcp_transport : public transport_interface {
public:
    explicit tcp_transport(const tcp_transport_config& config = {});

    [[nodiscard]] auto connect(const endpoint& ep) -> Result<void> override;
    [[nodiscard]] auto disconnect() -> Result<void> override;
    [[nodiscard]] auto is_connected() const -> bool override;

    [[nodiscard]] auto send(std::span<const std::byte> data) -> Result<void> override;
    [[nodiscard]] auto receive(std::span<std::byte> buffer) -> Result<std::size_t> override;

    [[nodiscard]] auto listen(const endpoint& ep) -> Result<void> override;
    [[nodiscard]] auto accept() -> Result<std::unique_ptr<transport_interface>> override;

private:
    tcp_transport_config config_;
    std::unique_ptr<network::tcp_socket> socket_;
    std::unique_ptr<network::tls_context> tls_context_;
    std::atomic<bool> connected_{false};
};

class transport_factory {
public:
    [[nodiscard]] static auto create(transport_type type)
        -> std::unique_ptr<transport_interface>;
};

} // namespace kcenon::file_transfer
```

---

## 4. Data Design

### 4.1 Data Structures

#### 4.1.1 Chunk Data Structure

```cpp
enum class chunk_flags : uint8_t {
    none            = 0x00,
    first_chunk     = 0x01,
    last_chunk      = 0x02,
    compressed      = 0x04,
    encrypted       = 0x08
};

struct chunk_header {
    transfer_id     transfer_id;        // 16 bytes (UUID)
    uint64_t        file_index;         // 8 bytes
    uint64_t        chunk_index;        // 8 bytes
    uint64_t        chunk_offset;       // 8 bytes
    uint32_t        original_size;      // 4 bytes
    uint32_t        compressed_size;    // 4 bytes
    uint32_t        checksum;           // 4 bytes (CRC32)
    chunk_flags     flags;              // 1 byte
    uint8_t         reserved[3];        // 3 bytes padding
    // Total: 56 bytes
};

struct chunk {
    chunk_header            header;
    std::vector<std::byte>  data;
};
```

#### 4.1.2 Transfer Data Structures

```cpp
// Transfer direction
enum class transfer_direction {
    upload,     // Client → Server
    download    // Server → Client
};

// Transfer identifier
struct transfer_id {
    std::array<uint8_t, 16> bytes;

    [[nodiscard]] static auto generate() -> transfer_id;
    [[nodiscard]] auto to_string() const -> std::string;
};

// File metadata
struct file_metadata {
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    std::filesystem::perms  permissions;
    std::chrono::system_clock::time_point modified_time;
    bool                    compressible_hint;
};

// Upload request (Client → Server)
struct upload_request {
    transfer_id         id;
    std::string         remote_name;
    uint64_t            file_size;
    std::string         sha256_hash;
    compression_mode    compression;
    bool                overwrite;
};

// Download request (Client → Server)
struct download_request {
    transfer_id         id;
    std::string         remote_name;
};

// File info (for listing)
struct file_info {
    std::string         name;
    uint64_t            size;
    std::string         sha256_hash;
    std::chrono::system_clock::time_point modified_time;
};

// Transfer progress
struct transfer_progress {
    transfer_id         id;
    transfer_direction  direction;
    uint64_t            bytes_transferred;
    uint64_t            bytes_on_wire;
    uint64_t            total_bytes;
    double              transfer_rate;
    double              compression_ratio;
    duration            elapsed_time;
    duration            estimated_remaining;
    transfer_state_enum state;
};

// Transfer result
struct transfer_result {
    transfer_id             id;
    transfer_direction      direction;
    std::filesystem::path   local_path;
    std::string             remote_name;
    uint64_t                bytes_transferred;
    uint64_t                bytes_on_wire;
    bool                    verified;
    std::optional<error>    error;
    duration                elapsed_time;
};
```

### 4.2 Data Flow

#### 4.2.1 Upload Data Flow (Client → Server)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              CLIENT SIDE                                     │
└─────────────────────────────────────────────────────────────────────────────┘
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  Local File │────▶│   Chunk     │────▶│ Compressed  │────▶│   Network   │
│             │     │   Buffer    │     │   Chunk     │     │   Send      │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
                           │                   │                   │
                           ▼                   ▼                   ▼
                    CRC32 calc         LZ4 compress         TCP/TLS send

                                        │
                                        │ Network
                                        ▼

┌─────────────────────────────────────────────────────────────────────────────┐
│                              SERVER SIDE                                     │
└─────────────────────────────────────────────────────────────────────────────┘
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   Network   │────▶│Decompressed │────▶│   Verify    │────▶│  Storage    │
│   Receive   │     │   Chunk     │     │   CRC32     │     │   Write     │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
```

#### 4.2.2 Download Data Flow (Server → Client)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              SERVER SIDE                                     │
└─────────────────────────────────────────────────────────────────────────────┘
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  Storage    │────▶│   Chunk     │────▶│ Compressed  │────▶│   Network   │
│   Read      │     │   Buffer    │     │   Chunk     │     │   Send      │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘

                                        │
                                        │ Network
                                        ▼

┌─────────────────────────────────────────────────────────────────────────────┐
│                              CLIENT SIDE                                     │
└─────────────────────────────────────────────────────────────────────────────┘
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   Network   │────▶│Decompressed │────▶│   Verify    │────▶│  Local File │
│   Receive   │     │   Chunk     │     │   CRC32     │     │   Write     │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
```

---

## 5. Interface Design

### 5.1 Protocol Interface

#### 5.1.1 Message Format

```
┌────────────────────────────────────────────────────────────────┐
│                      Protocol Frame                             │
├────────────────────────────────────────────────────────────────┤
│ Message Type (1 byte)                                          │
├────────────────────────────────────────────────────────────────┤
│ Payload Length (4 bytes, big-endian)                           │
├────────────────────────────────────────────────────────────────┤
│ Payload (variable length)                                       │
└────────────────────────────────────────────────────────────────┘

Total frame overhead: 5 bytes
```

#### 5.1.2 Message Types

```cpp
enum class message_type : uint8_t {
    // Session management
    handshake_request   = 0x01,
    handshake_response  = 0x02,

    // Upload operations
    upload_request      = 0x10,
    upload_accept       = 0x11,
    upload_reject       = 0x12,

    // Data transfer
    chunk_data          = 0x20,
    chunk_ack           = 0x21,
    chunk_nack          = 0x22,

    // Resume
    resume_request      = 0x30,
    resume_response     = 0x31,

    // Completion
    transfer_complete   = 0x40,
    transfer_verify     = 0x41,

    // Download operations
    download_request    = 0x50,
    download_accept     = 0x51,
    download_reject     = 0x52,

    // List operations
    list_request        = 0x60,
    list_response       = 0x61,

    // Control
    keepalive           = 0xF0,
    error               = 0xFF
};
```

### 5.2 Error Codes

Following SRS Section 6.4:

```cpp
namespace kcenon::file_transfer::error {

// Transfer errors (-700 to -719)
constexpr int transfer_init_failed      = -700;
constexpr int transfer_cancelled        = -701;
constexpr int transfer_timeout          = -702;
constexpr int upload_rejected           = -703;
constexpr int download_rejected         = -704;
constexpr int connection_refused        = -705;
constexpr int connection_lost           = -706;
constexpr int server_busy               = -707;

// Chunk errors (-720 to -739)
constexpr int chunk_checksum_error      = -720;
constexpr int chunk_sequence_error      = -721;
constexpr int chunk_size_error          = -722;
constexpr int file_hash_mismatch        = -723;

// File I/O errors (-740 to -759)
constexpr int file_read_error           = -740;
constexpr int file_write_error          = -741;
constexpr int file_permission_error     = -742;
constexpr int file_not_found            = -743;
constexpr int file_already_exists       = -744;
constexpr int storage_full              = -745;
constexpr int file_not_found_on_server  = -746;
constexpr int access_denied             = -747;
constexpr int invalid_filename          = -748;

// Resume errors (-760 to -779)
constexpr int resume_state_invalid      = -760;
constexpr int resume_file_changed       = -761;

// Compression errors (-780 to -789)
constexpr int compression_failed        = -780;
constexpr int decompression_failed      = -781;
constexpr int compression_buffer_error  = -782;

// Configuration errors (-790 to -799)
constexpr int config_invalid            = -790;

[[nodiscard]] auto error_message(int code) -> std::string_view;

} // namespace kcenon::file_transfer::error
```

---

## 6. Algorithm Design

### 6.1 Adaptive Compression Algorithm

**SRS Trace**: SRS-COMP-003

```cpp
bool adaptive_compression::is_compressible(
    std::span<const std::byte> data,
    double threshold
) {
    const auto sample_size = std::min(data.size(), std::size_t{1024});
    auto sample = data.first(sample_size);

    auto max_size = lz4_engine::max_compressed_size(sample_size);
    std::vector<std::byte> compressed_buffer(max_size);

    auto result = lz4_engine::compress(sample, compressed_buffer);
    if (!result) {
        return false;
    }

    auto compressed_size = result.value();
    return static_cast<double>(compressed_size) <
           static_cast<double>(sample_size) * threshold;
}
```

### 6.2 Auto-Reconnect Algorithm

**SRS Trace**: SRS-CLIENT-002

```cpp
void auto_reconnect_handler::attempt_reconnect() {
    std::size_t attempt = 0;
    auto delay = policy_.initial_delay;

    while (attempt < policy_.max_attempts && should_reconnect_) {
        ++attempt;

        // Wait with exponential backoff
        std::this_thread::sleep_for(delay);

        // Try to reconnect
        auto result = transport_->connect(server_endpoint_);
        if (result) {
            // Success - notify and restore transfers
            notify_reconnected();
            resume_active_transfers();
            return;
        }

        // Calculate next delay with backoff
        delay = std::min(
            std::chrono::duration_cast<duration>(delay * policy_.backoff_multiplier),
            policy_.max_delay
        );
    }

    // Max attempts exceeded
    notify_reconnect_failed();
}
```

### 6.3 Path Traversal Prevention Algorithm

**SRS Trace**: SRS-STORAGE-002

```cpp
Result<std::filesystem::path> storage_manager::safe_path(
    const std::string& filename
) const {
    // Step 1: Check for path traversal patterns
    if (filename.find("..") != std::string::npos) {
        return error::invalid_filename;
    }

    // Step 2: Check for absolute path
    std::filesystem::path requested_path(filename);
    if (requested_path.is_absolute()) {
        return error::invalid_filename;
    }

    // Step 3: Check for path separators
    if (filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos) {
        return error::invalid_filename;
    }

    // Step 4: Construct and canonicalize
    auto full_path = config_.storage_dir / filename;
    auto canonical = std::filesystem::weakly_canonical(full_path);
    auto canonical_base = std::filesystem::weakly_canonical(config_.storage_dir);

    // Step 5: Verify path is under storage directory
    auto relative = canonical.lexically_relative(canonical_base);
    if (relative.empty() || *relative.begin() == "..") {
        return error::invalid_filename;
    }

    return canonical;
}
```

---

## 7. Security Design

### 7.1 TLS Configuration

**SRS Trace**: SEC-001

```cpp
struct tls_config {
    static constexpr int min_version = TLS1_3_VERSION;

    static constexpr std::array<const char*, 3> cipher_suites = {
        "TLS_AES_256_GCM_SHA384",
        "TLS_CHACHA20_POLY1305_SHA256",
        "TLS_AES_128_GCM_SHA256"
    };

    enum class verify_mode {
        none,
        peer,
        fail_if_no_cert
    };

    verify_mode verification = verify_mode::peer;
    std::string cert_file;
    std::string key_file;
    std::string ca_file;
};
```

### 7.2 Storage Isolation

**SRS Trace**: SEC-005

The storage_manager ensures all file operations are confined within the configured storage directory through:

1. Filename validation (no path separators, no "..")
2. Path canonicalization
3. Relative path verification
4. Symbolic link resolution and validation

---

## 8. Traceability Matrix

### 8.1 SRS to Design Traceability

| SRS ID | SRS Description | Design Component | Design Element |
|--------|-----------------|------------------|----------------|
| SRS-SERVER-001 | Server Initialization | file_transfer_server | builder, storage_manager |
| SRS-SERVER-002 | Server Start | file_transfer_server | start() method |
| SRS-SERVER-003 | Server Stop | file_transfer_server | stop() method |
| SRS-SERVER-004 | Upload Request Handling | file_transfer_server | on_upload_request() |
| SRS-SERVER-005 | Download Request Handling | file_transfer_server | on_download_request() |
| SRS-SERVER-006 | List Request Handling | file_transfer_server | list_files handling |
| SRS-CLIENT-001 | Server Connection | file_transfer_client | connect() method |
| SRS-CLIENT-002 | Auto Reconnect | auto_reconnect_handler | attempt_reconnect() |
| SRS-CLIENT-003 | File Upload | file_transfer_client | upload_file() method |
| SRS-CLIENT-004 | File Download | file_transfer_client | download_file() method |
| SRS-CLIENT-005 | File Listing | file_transfer_client | list_files() method |
| SRS-CLIENT-006 | Batch Upload | file_transfer_client | upload_files() method |
| SRS-CLIENT-007 | Batch Download | file_transfer_client | download_files() method |
| SRS-STORAGE-001 | Storage Quota | storage_manager | can_store_file() |
| SRS-STORAGE-002 | Filename Validation | filename_validator | validate() |
| SRS-CHUNK-001 | File Splitting | chunk_splitter | split() method |
| SRS-CHUNK-002 | File Assembly | chunk_assembler | process_chunk() |
| SRS-CHUNK-003 | Chunk Checksum | checksum | crc32() method |
| SRS-CHUNK-004 | File Hash | checksum | sha256_file() method |
| SRS-COMP-001 | LZ4 Compression | lz4_engine | compress() method |
| SRS-COMP-002 | LZ4 Decompression | lz4_engine | decompress() method |
| SRS-COMP-003 | Adaptive Detection | adaptive_compression | is_compressible() |
| SRS-PIPE-001 | Upload Pipeline | upload_pipeline | Pipeline stages |
| SRS-PIPE-002 | Download Pipeline | download_pipeline | Pipeline stages |
| SRS-PIPE-003 | Backpressure | bounded_queue | push()/pop() blocking |
| SRS-RESUME-001 | State Persistence | resume_handler | save_state() |
| SRS-RESUME-002 | Transfer Resume | resume_handler | load_state() |
| SRS-PROGRESS-001 | Progress Callbacks | progress_tracker | on_progress() |
| SRS-TRANS-001 | Transport Abstraction | transport_interface | Abstract class |
| SRS-TRANS-002 | TCP Transport | tcp_transport | Implementation class |

---

## Appendix A: Revision History

| Version | Date | Author | Description |
|---------|------|--------|-------------|
| 1.0.0 | 2025-12-11 | kcenon@naver.com | Initial SDS creation (P2P model) |
| 0.2.0 | 2025-12-11 | kcenon@naver.com | Complete rewrite for Client-Server architecture |

---

*End of Document*
