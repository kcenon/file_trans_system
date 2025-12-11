# 파일 전송 시스템 - 소프트웨어 설계 명세서

## 문서 정보

| 항목 | 내용 |
|------|------|
| **프로젝트명** | file_trans_system |
| **문서 유형** | 소프트웨어 설계 명세서 (SDS) |
| **버전** | 0.2.0 |
| **상태** | 승인됨 |
| **작성일** | 2025-12-11 |
| **작성자** | kcenon@naver.com |
| **관련 문서** | SRS_KR.md v0.2.0, PRD_KR.md v0.2.0 |

---

## 1. 소개

### 1.1 목적

본 소프트웨어 설계 명세서(SDS)는 **file_trans_system** 라이브러리의 상세 설계를 기술합니다. SRS에서 정의된 소프트웨어 요구사항을 구체적인 구현 가능한 설계로 변환합니다. 이 문서는 개발자를 위한 청사진 역할을 하며 요구사항에서 설계 요소로의 추적성을 제공합니다.

본 문서의 대상 독자:
- 시스템을 구현하는 소프트웨어 개발자
- 설계를 검토하는 시스템 아키텍트
- 테스트 범위를 이해하는 QA 엔지니어
- 시스템 구조를 이해하는 유지보수 담당자

### 1.2 범위

본 문서의 범위:
- 클라이언트-서버 시스템 아키텍처 및 컴포넌트 설계
- 서버 측 저장소 관리 및 클라이언트 처리
- 클라이언트 측 업로드/다운로드 작업
- 데이터 구조 및 데이터 흐름
- 인터페이스 명세
- 알고리즘 설명
- 오류 처리 설계
- SRS 요구사항과의 추적성

### 1.3 설계 개요

file_trans_system은 **클라이언트-서버 아키텍처**를 사용합니다:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                               서버 계층                                       │
│   ┌──────────────────────────────────────────────────────────────────────┐  │
│   │                      file_transfer_server                             │  │
│   │  ┌───────────────┐ ┌──────────────────┐ ┌─────────────────────────┐  │  │
│   │  │ Storage       │ │ Connection       │ │ Server Pipeline         │  │  │
│   │  │ Manager       │ │ Manager          │ │ (업로드/다운로드)       │  │  │
│   │  └───────────────┘ └──────────────────┘ └─────────────────────────┘  │  │
│   └──────────────────────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────────────────┤
│                              클라이언트 계층                                  │
│   ┌──────────────────────────────────────────────────────────────────────┐  │
│   │                      file_transfer_client                             │  │
│   │  ┌───────────────┐ ┌──────────────────┐ ┌─────────────────────────┐  │  │
│   │  │ Connection    │ │ Upload/Download  │ │ Client Pipeline         │  │  │
│   │  │ Handler       │ │ Manager          │ │ (송신/수신)             │  │  │
│   │  └───────────────┘ └──────────────────┘ └─────────────────────────┘  │  │
│   └──────────────────────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────────────────┤
│                             서비스 계층                                       │
│   chunk_manager  │  compression_engine  │  resume_handler  │  checksum      │
├─────────────────────────────────────────────────────────────────────────────┤
│                            전송 계층                                          │
│   transport_interface  │  tcp_transport  │  quic_transport (Phase 2)        │
├─────────────────────────────────────────────────────────────────────────────┤
│                          인프라스트럭처 계층                                   │
│   common_system  │  thread_system  │  network_system  │  container_system   │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 아키텍처 설계

### 2.1 아키텍처 스타일

시스템은 **클라이언트-서버 아키텍처**와 **파이프라인 아키텍처**를 결합하여 사용합니다:

| 스타일 | 적용 영역 | 근거 |
|--------|-----------|------|
| **클라이언트-서버** | 시스템 토폴로지 | 중앙 저장소, 다중 클라이언트 |
| **파이프라인** | 데이터 처리 (읽기→압축→전송) | 병렬 단계를 통한 처리량 최대화 |
| **계층형** | 시스템 조직 | 관심사 분리, 테스트 용이성 |
| **전략** | 전송, 압축 | 플러그인 가능한 구현 |
| **옵저버** | 진행 알림 | 분리된 이벤트 처리 |

### 2.2 컴포넌트 아키텍처

#### 2.2.1 고수준 컴포넌트 다이어그램

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
│  │  │  Manager    │ │   Manager       │ │ (업로드/다운로드/목록)      │   │  │
│  │  └──────┬──────┘ └────────┬────────┘ └──────────────┬──────────────┘   │  │
│  │         │                 │                          │                  │  │
│  │         └─────────────────┼──────────────────────────┘                  │  │
│  │                           ▼                                             │  │
│  │  ┌──────────────────────────────────────────────────────────────────┐  │  │
│  │  │                    Server Pipeline                                │  │  │
│  │  │  [업로드]  수신→압축해제→검증→쓰기                               │  │  │
│  │  │  [다운로드] 읽기→압축→전송                                        │  │  │
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
│  │  │ Manager     │ │ Handler         │ │ (업로드/다운로드)           │   │  │
│  │  └──────┬──────┘ └────────┬────────┘ └──────────────┬──────────────┘   │  │
│  │         │                 │                          │                  │  │
│  │         └─────────────────┼──────────────────────────┘                  │  │
│  │                           ▼                                             │  │
│  │  ┌──────────────────────────────────────────────────────────────────┐  │  │
│  │  │                    Client Pipeline                                │  │  │
│  │  │  [업로드]   읽기→압축→전송                                        │  │  │
│  │  │  [다운로드] 수신→압축해제→쓰기                                    │  │  │
│  │  └──────────────────────────────────────────────────────────────────┘  │  │
│  └────────────────────────────────────────────────────────────────────────┘  │
│                                                                               │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                         서비스 계층                                   │   │
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
│  │                        전송 계층                                      │   │
│  │  ┌────────────────────┐  ┌──────────────┐  ┌────────────────────┐   │   │
│  │  │transport_interface │  │tcp_transport │  │ quic_transport     │   │   │
│  │  │   <<인터페이스>>   │◄─┤              │  │  (Phase 2)         │   │   │
│  │  │                    │  └──────────────┘  └────────────────────┘   │   │
│  │  └────────────────────┘                                              │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 2.3 사용된 설계 패턴

| 패턴 | 용도 | SRS 추적 |
|------|------|----------|
| **빌더** | file_transfer_server::builder, file_transfer_client::builder | API 사용성 |
| **전략** | transport_interface 구현 | SRS-TRANS-001 |
| **옵저버** | 진행 콜백, 완료 이벤트 | SRS-PROGRESS-001 |
| **파이프라인** | upload_pipeline, download_pipeline | SRS-PIPE-001, SRS-PIPE-002 |
| **팩토리** | create_transport() | SRS-TRANS-001 |
| **상태** | 전송 상태 머신, 연결 상태 | SRS-PROGRESS-002 |
| **커맨드** | 파이프라인 작업 | SRS-PIPE-001 |

---

## 3. 컴포넌트 설계

### 3.1 file_transfer_server 컴포넌트

#### 3.1.1 책임
파일 저장소를 관리하고, 클라이언트 연결을 수락하며, 업로드/다운로드/목록 요청을 처리합니다.

#### 3.1.2 SRS 추적성
| SRS 요구사항 | 설계 요소 |
|--------------|-----------|
| SRS-SERVER-001 | 서버 초기화 및 저장소 설정 |
| SRS-SERVER-002 | start() 메서드 |
| SRS-SERVER-003 | stop() 메서드 |
| SRS-SERVER-004 | on_upload_request() 콜백 |
| SRS-SERVER-005 | on_download_request() 콜백 |
| SRS-SERVER-006 | list_files 처리 |
| SRS-STORAGE-001 | storage_manager 컴포넌트 |
| SRS-STORAGE-002 | filename_validator 컴포넌트 |

#### 3.1.3 클래스 설계

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

    // 생명주기
    [[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;
    [[nodiscard]] auto stop() -> Result<void>;

    // 요청 콜백
    void on_upload_request(std::function<bool(const upload_request&)> callback);
    void on_download_request(std::function<bool(const download_request&)> callback);

    // 이벤트 콜백
    void on_client_connected(std::function<void(const client_info&)> callback);
    void on_client_disconnected(std::function<void(const client_info&)> callback);
    void on_transfer_complete(std::function<void(const transfer_result&)> callback);

    // 진행 상황 모니터링
    void on_progress(std::function<void(const transfer_progress&)> callback);

    // 통계
    [[nodiscard]] auto get_statistics() -> server_statistics;
    [[nodiscard]] auto get_storage_stats() -> storage_stats;
    [[nodiscard]] auto list_active_transfers() -> std::vector<transfer_info>;

private:
    // 상태
    enum class server_state { stopped, starting, running, stopping };
    std::atomic<server_state>           state_{server_state::stopped};

    // 컴포넌트
    std::unique_ptr<storage_manager>    storage_manager_;
    std::unique_ptr<connection_manager> connection_manager_;
    std::unique_ptr<server_pipeline>    pipeline_;
    std::unique_ptr<transport_interface> transport_;
    std::unique_ptr<resume_handler>     resume_handler_;

    // 설정
    server_config                       config_;

    // 활성 클라이언트 및 전송
    std::unordered_map<client_id, client_context>     clients_;
    std::unordered_map<transfer_id, transfer_context> transfers_;
    std::shared_mutex                   clients_mutex_;
    std::shared_mutex                   transfers_mutex_;

    // 콜백
    std::function<bool(const upload_request&)>    upload_callback_;
    std::function<bool(const download_request&)>  download_callback_;
    std::function<void(const client_info&)>       connect_callback_;
    std::function<void(const client_info&)>       disconnect_callback_;
    std::function<void(const transfer_result&)>   complete_callback_;
    std::function<void(const transfer_progress&)> progress_callback_;

    // 내부 메서드
    void accept_connections();
    void handle_client(client_context& ctx);
    void process_upload_request(const upload_request& req, client_context& ctx);
    void process_download_request(const download_request& req, client_context& ctx);
    void process_list_request(const list_request& req, client_context& ctx);
};

} // namespace kcenon::file_transfer
```

#### 3.1.4 시퀀스 다이어그램: 업로드 요청 처리

```
┌──────────┐ ┌──────────────┐ ┌──────────────┐ ┌────────────┐ ┌─────────────┐
│ 클라이언트│ │    서버      │ │   Storage    │ │  Pipeline  │ │  Callbacks  │
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
     │              │                │               │   수락/거부    │
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
     │              │                │  압축 해제    │               │
     │              │                │<──────────────│               │
     │              │                │               │               │
     │              │                │  청크 쓰기    │               │
     │              │                │<──────────────│               │
     │              │                │               │               │
     │   CHUNK_ACK  │                │               │               │
     │<─────────────│                │               │               │
     │              │                │               │               │
     │     ...      │    [모든 청크에 대해 반복]    │               │
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

### 3.2 file_transfer_client 컴포넌트

#### 3.2.1 책임
서버에 연결하고, 파일을 업로드/다운로드하며, 자동 재연결을 처리합니다.

#### 3.2.2 SRS 추적성
| SRS 요구사항 | 설계 요소 |
|--------------|-----------|
| SRS-CLIENT-001 | connect() 메서드 |
| SRS-CLIENT-002 | auto_reconnect_handler |
| SRS-CLIENT-003 | upload_file() 메서드 |
| SRS-CLIENT-004 | download_file() 메서드 |
| SRS-CLIENT-005 | list_files() 메서드 |
| SRS-CLIENT-006 | upload_files() 메서드 |
| SRS-CLIENT-007 | download_files() 메서드 |
| SRS-PROGRESS-001 | on_progress() 콜백 |

#### 3.2.3 클래스 설계

```cpp
namespace kcenon::file_transfer {

// 재연결 정책
struct reconnect_policy {
    std::size_t max_attempts        = 5;
    duration    initial_delay       = std::chrono::seconds(1);
    duration    max_delay           = std::chrono::seconds(30);
    double      backoff_multiplier  = 2.0;
};

// 연결 상태
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

    // 연결
    [[nodiscard]] auto connect(const endpoint& server_addr) -> Result<void>;
    [[nodiscard]] auto disconnect() -> Result<void>;
    [[nodiscard]] auto is_connected() const -> bool;

    // 단일 파일 작업
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

    // 배치 작업
    [[nodiscard]] auto upload_files(
        std::span<const upload_entry> files,
        const upload_options& options = {}
    ) -> Result<batch_transfer_handle>;

    [[nodiscard]] auto download_files(
        std::span<const download_entry> files,
        const download_options& options = {}
    ) -> Result<batch_transfer_handle>;

    // 파일 목록 조회
    [[nodiscard]] auto list_files(
        const list_options& options = {}
    ) -> Result<std::vector<file_info>>;

    // 전송 제어
    [[nodiscard]] auto cancel(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto pause(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto resume(const transfer_id& id) -> Result<void>;

    // 콜백
    void on_progress(std::function<void(const transfer_progress&)> callback);
    void on_complete(std::function<void(const transfer_result&)> callback);
    void on_connection_state_changed(std::function<void(connection_state)> callback);

    // 통계
    [[nodiscard]] auto get_statistics() -> client_statistics;
    [[nodiscard]] auto get_compression_stats() -> compression_statistics;

private:
    // 상태
    std::atomic<connection_state>       connection_state_{connection_state::disconnected};

    // 컴포넌트
    std::unique_ptr<client_pipeline>    pipeline_;
    std::unique_ptr<transport_interface> transport_;
    std::unique_ptr<auto_reconnect_handler> reconnect_handler_;
    std::unique_ptr<resume_handler>     resume_handler_;
    std::unique_ptr<chunk_compressor>   compressor_;

    // 설정
    client_config                       config_;
    endpoint                            server_endpoint_;

    // 활성 전송
    std::unordered_map<transfer_id, transfer_context> active_transfers_;
    std::mutex                          transfers_mutex_;

    // 콜백
    std::function<void(const transfer_progress&)> progress_callback_;
    std::function<void(const transfer_result&)>   complete_callback_;
    std::function<void(connection_state)>         state_callback_;

    // 내부 메서드
    void handle_connection_loss();
    void attempt_reconnect();
    void send_upload_request(const upload_request& req);
    void send_download_request(const download_request& req);
    void process_download_chunk(const chunk& c, transfer_context& ctx);
};

} // namespace kcenon::file_transfer
```

#### 3.2.4 시퀀스 다이어그램: download_file()

```
┌──────────┐ ┌──────────────┐ ┌─────────────┐ ┌────────────┐ ┌─────────────┐
│   앱     │ │  클라이언트  │ │    서버     │ │  Pipeline  │ │    디스크   │
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
     │              │(메타데이터 포함)│              │               │
     │              │<───────────────│              │               │
     │              │                │              │               │
     │              │  CHUNK_DATA    │              │               │
     │              │<───────────────│              │               │
     │              │                │              │               │
     │              │    submit_chunk()             │               │
     │              │──────────────────────────────>│               │
     │              │                │              │               │
     │              │                │   압축 해제  │               │
     │              │                │<─────────────│               │
     │              │                │              │               │
     │              │                │    write()   │               │
     │              │                │──────────────────────────────>│
     │              │                │              │               │
     │  progress()  │                │              │               │
     │<─────────────│                │              │               │
     │              │                │              │               │
     │              │     ...        │  [반복]      │               │
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

### 3.3 storage_manager 컴포넌트

#### 3.3.1 책임
서버 측 파일 저장소, 할당량 적용, 파일 검증을 관리합니다.

#### 3.3.2 SRS 추적성
| SRS 요구사항 | 설계 요소 |
|--------------|-----------|
| SRS-STORAGE-001 | 할당량 관리 메서드 |
| SRS-STORAGE-002 | validate_filename() 메서드 |

#### 3.3.3 클래스 설계

```cpp
namespace kcenon::file_transfer {

// 저장소 설정
struct storage_config {
    std::filesystem::path   storage_dir;
    uint64_t                max_total_size  = 100ULL * 1024 * 1024 * 1024;  // 100GB
    uint64_t                max_file_size   = 10ULL * 1024 * 1024 * 1024;   // 10GB
    uint64_t                reserved_space  = 1ULL * 1024 * 1024 * 1024;    // 1GB
};

// 저장소 통계
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

    // 파일 작업
    [[nodiscard]] auto create_file(
        const std::string& filename,
        uint64_t size
    ) -> Result<std::filesystem::path>;

    [[nodiscard]] auto delete_file(const std::string& filename) -> Result<void>;

    [[nodiscard]] auto get_file_path(const std::string& filename)
        -> Result<std::filesystem::path>;

    [[nodiscard]] auto file_exists(const std::string& filename) const -> bool;

    // 파일 목록 조회
    [[nodiscard]] auto list_files(
        const std::string& pattern = "*",
        std::size_t offset = 0,
        std::size_t limit = 1000
    ) -> Result<std::vector<file_info>>;

    // 검증
    [[nodiscard]] auto validate_filename(const std::string& filename) -> Result<void>;
    [[nodiscard]] auto can_store_file(uint64_t size) -> Result<void>;

    // 통계
    [[nodiscard]] auto get_stats() const -> storage_stats;

    // 임시 파일 관리
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

    // 경로 순회 방지
    [[nodiscard]] auto safe_path(const std::string& filename) const
        -> Result<std::filesystem::path>;

    // 할당량 추적
    void update_usage(int64_t delta);
    void recalculate_usage();
};

// 파일명 검증 유틸리티
class filename_validator {
public:
    [[nodiscard]] static auto validate(const std::string& filename) -> Result<void>;

private:
    // 유효하지 않은 패턴
    static constexpr std::array<std::string_view, 4> invalid_patterns = {
        "..", "/", "\\", "\0"
    };

    // 유효하지 않은 문자
    static constexpr std::string_view invalid_chars = "<>:\"|?*";
};

} // namespace kcenon::file_transfer
```

---

### 3.4 chunk_manager 컴포넌트

#### 3.4.1 책임
전송을 위해 파일을 청크로 분할하고, 수신 시 청크를 파일로 재조립합니다.

#### 3.4.2 SRS 추적성
| SRS 요구사항 | 설계 요소 |
|--------------|-----------|
| SRS-CHUNK-001 | chunk_splitter 클래스 |
| SRS-CHUNK-002 | chunk_assembler 클래스 |
| SRS-CHUNK-003 | CRC32 체크섬 계산 |
| SRS-CHUNK-004 | SHA-256 파일 해시 계산 |

#### 3.4.3 클래스 설계

```cpp
namespace kcenon::file_transfer {

// 청크 작업 설정
struct chunk_config {
    std::size_t chunk_size     = 256 * 1024;  // 기본값 256KB
    std::size_t min_chunk_size = 64 * 1024;   // 최소 64KB
    std::size_t max_chunk_size = 1024 * 1024; // 최대 1MB

    [[nodiscard]] auto validate() const -> Result<void>;
};

// 청크 분할기 - 파일을 청크로 분할
class chunk_splitter {
public:
    explicit chunk_splitter(const chunk_config& config);

    // 메모리 효율성을 위한 반복자 스타일 인터페이스
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

    // 파일용 반복자 생성
    [[nodiscard]] auto split(
        const std::filesystem::path& file_path,
        const transfer_id& id
    ) -> Result<chunk_iterator>;

    // 파일 메타데이터 계산
    [[nodiscard]] auto calculate_metadata(
        const std::filesystem::path& file_path
    ) -> Result<file_metadata>;

private:
    chunk_config config_;

    [[nodiscard]] auto calculate_crc32(std::span<const std::byte> data) const -> uint32_t;
};

// 청크 조립기 - 청크를 파일로 재조립
class chunk_assembler {
public:
    explicit chunk_assembler(const std::filesystem::path& output_dir);

    // 수신 청크 처리
    [[nodiscard]] auto process_chunk(const chunk& c) -> Result<void>;

    // 파일 완료 여부 확인
    [[nodiscard]] auto is_complete(const transfer_id& id) const -> bool;

    // 누락된 청크 인덱스 가져오기
    [[nodiscard]] auto get_missing_chunks(const transfer_id& id) const
        -> std::vector<uint64_t>;

    // 파일 완료 (SHA-256 검증)
    [[nodiscard]] auto finalize(
        const transfer_id& id,
        const std::string& expected_hash
    ) -> Result<std::filesystem::path>;

    // 조립 진행 상황 가져오기
    [[nodiscard]] auto get_progress(const transfer_id& id) const
        -> std::optional<assembly_progress>;

private:
    struct assembly_context {
        std::filesystem::path   temp_file_path;
        std::ofstream          file;
        uint64_t               total_chunks;
        std::vector<bool>      received_chunks;  // 비트맵
        uint64_t               bytes_written;
        std::mutex             mutex;
    };

    std::filesystem::path                                   output_dir_;
    std::unordered_map<transfer_id, assembly_context>       contexts_;
    std::shared_mutex                                        contexts_mutex_;

    [[nodiscard]] auto verify_crc32(const chunk& c) const -> bool;
};

// 체크섬 유틸리티
class checksum {
public:
    // 청크 무결성을 위한 CRC32
    [[nodiscard]] static auto crc32(std::span<const std::byte> data) -> uint32_t;
    [[nodiscard]] static auto verify_crc32(
        std::span<const std::byte> data,
        uint32_t expected
    ) -> bool;

    // 파일 무결성을 위한 SHA-256
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

### 3.5 compression_engine 컴포넌트

#### 3.5.1 책임
적응형 감지 기능이 포함된 LZ4 압축 및 압축 해제를 제공합니다.

#### 3.5.2 SRS 추적성
| SRS 요구사항 | 설계 요소 |
|--------------|-----------|
| SRS-COMP-001 | lz4_engine::compress() |
| SRS-COMP-002 | lz4_engine::decompress() |
| SRS-COMP-003 | adaptive_compression::is_compressible() |
| SRS-COMP-004 | compression_mode 열거형 |
| SRS-COMP-005 | compression_statistics 구조체 |

#### 3.5.3 클래스 설계

```cpp
namespace kcenon::file_transfer {

// 압축 모드
enum class compression_mode {
    disabled,   // 압축 안 함
    enabled,    // 항상 압축
    adaptive    // 압축 가능 여부 자동 감지 (기본값)
};

// 압축 레벨
enum class compression_level {
    fast,             // LZ4 표준 (~400 MB/s)
    high_compression  // LZ4-HC (~50 MB/s, 더 높은 압축률)
};

// LZ4 압축 엔진
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

// 적응형 압축 감지
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

// 압축 통계
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

### 3.6 파이프라인 컴포넌트

#### 3.6.1 책임
최대 처리량을 위한 다단계 병렬 처리를 구현합니다.

#### 3.6.2 SRS 추적성
| SRS 요구사항 | 설계 요소 |
|--------------|-----------|
| SRS-PIPE-001 | upload_pipeline 클래스 |
| SRS-PIPE-002 | download_pipeline 클래스 |
| SRS-PIPE-003 | 제한된 큐 구현 |
| SRS-PIPE-004 | pipeline_statistics 구조체 |

#### 3.6.3 클래스 설계

```cpp
namespace kcenon::file_transfer {

// 파이프라인 단계 유형
enum class pipeline_stage : uint8_t {
    io_read,        // 파일 읽기 작업
    chunk_process,  // 청크 조립/분해
    compression,    // LZ4 압축/해제
    network,        // 네트워크 송신/수신
    io_write        // 파일 쓰기 작업
};

// 파이프라인 설정
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

// 백프레셔를 위한 제한된 큐
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

// 파이프라인 통계
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

### 3.7 전송 컴포넌트

#### 3.7.1 책임
네트워크 전송 프로토콜(TCP, QUIC)을 추상화합니다.

#### 3.7.2 SRS 추적성
| SRS 요구사항 | 설계 요소 |
|--------------|-----------|
| SRS-TRANS-001 | transport_interface 클래스 |
| SRS-TRANS-002 | tcp_transport 클래스 |

#### 3.7.3 클래스 설계

```cpp
namespace kcenon::file_transfer {

enum class transport_type {
    tcp,    // TCP + TLS 1.3 (기본값)
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

## 4. 데이터 설계

### 4.1 데이터 구조

#### 4.1.1 청크 데이터 구조

```cpp
enum class chunk_flags : uint8_t {
    none            = 0x00,
    first_chunk     = 0x01,
    last_chunk      = 0x02,
    compressed      = 0x04,
    encrypted       = 0x08
};

struct chunk_header {
    transfer_id     transfer_id;        // 16 바이트 (UUID)
    uint64_t        file_index;         // 8 바이트
    uint64_t        chunk_index;        // 8 바이트
    uint64_t        chunk_offset;       // 8 바이트
    uint32_t        original_size;      // 4 바이트
    uint32_t        compressed_size;    // 4 바이트
    uint32_t        checksum;           // 4 바이트 (CRC32)
    chunk_flags     flags;              // 1 바이트
    uint8_t         reserved[3];        // 3 바이트 패딩
    // 총합: 56 바이트
};

struct chunk {
    chunk_header            header;
    std::vector<std::byte>  data;
};
```

#### 4.1.2 전송 데이터 구조

```cpp
// 전송 방향
enum class transfer_direction {
    upload,     // 클라이언트 → 서버
    download    // 서버 → 클라이언트
};

// 전송 식별자
struct transfer_id {
    std::array<uint8_t, 16> bytes;

    [[nodiscard]] static auto generate() -> transfer_id;
    [[nodiscard]] auto to_string() const -> std::string;
};

// 파일 메타데이터
struct file_metadata {
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    std::filesystem::perms  permissions;
    std::chrono::system_clock::time_point modified_time;
    bool                    compressible_hint;
};

// 업로드 요청 (클라이언트 → 서버)
struct upload_request {
    transfer_id         id;
    std::string         remote_name;
    uint64_t            file_size;
    std::string         sha256_hash;
    compression_mode    compression;
    bool                overwrite;
};

// 다운로드 요청 (클라이언트 → 서버)
struct download_request {
    transfer_id         id;
    std::string         remote_name;
};

// 파일 정보 (목록 조회용)
struct file_info {
    std::string         name;
    uint64_t            size;
    std::string         sha256_hash;
    std::chrono::system_clock::time_point modified_time;
};

// 전송 진행 상황
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

// 전송 결과
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

### 4.2 데이터 흐름

#### 4.2.1 업로드 데이터 흐름 (클라이언트 → 서버)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              클라이언트 측                                    │
└─────────────────────────────────────────────────────────────────────────────┘
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  로컬 파일  │────▶│   청크      │────▶│   압축된    │────▶│   네트워크  │
│             │     │   버퍼      │     │    청크     │     │    전송     │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
                           │                   │                   │
                           ▼                   ▼                   ▼
                    CRC32 계산         LZ4 압축            TCP/TLS 전송

                                        │
                                        │ 네트워크
                                        ▼

┌─────────────────────────────────────────────────────────────────────────────┐
│                              서버 측                                          │
└─────────────────────────────────────────────────────────────────────────────┘
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   네트워크  │────▶│   압축해제  │────▶│   CRC32     │────▶│   저장소    │
│    수신     │     │    청크     │     │    검증     │     │    쓰기     │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
```

#### 4.2.2 다운로드 데이터 흐름 (서버 → 클라이언트)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              서버 측                                          │
└─────────────────────────────────────────────────────────────────────────────┘
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   저장소    │────▶│   청크      │────▶│   압축된    │────▶│   네트워크  │
│    읽기     │     │   버퍼      │     │    청크     │     │    전송     │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘

                                        │
                                        │ 네트워크
                                        ▼

┌─────────────────────────────────────────────────────────────────────────────┐
│                              클라이언트 측                                    │
└─────────────────────────────────────────────────────────────────────────────┘
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   네트워크  │────▶│   압축해제  │────▶│   CRC32     │────▶│  로컬 파일  │
│    수신     │     │    청크     │     │    검증     │     │    쓰기     │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
```

---

## 5. 인터페이스 설계

### 5.1 프로토콜 인터페이스

#### 5.1.1 메시지 형식

```
┌────────────────────────────────────────────────────────────────┐
│                      프로토콜 프레임                            │
├────────────────────────────────────────────────────────────────┤
│ 메시지 유형 (1 바이트)                                         │
├────────────────────────────────────────────────────────────────┤
│ 페이로드 길이 (4 바이트, 빅 엔디안)                            │
├────────────────────────────────────────────────────────────────┤
│ 페이로드 (가변 길이)                                           │
└────────────────────────────────────────────────────────────────┘

총 프레임 오버헤드: 5 바이트
```

#### 5.1.2 메시지 유형

```cpp
enum class message_type : uint8_t {
    // 세션 관리
    handshake_request   = 0x01,
    handshake_response  = 0x02,

    // 업로드 작업
    upload_request      = 0x10,
    upload_accept       = 0x11,
    upload_reject       = 0x12,

    // 데이터 전송
    chunk_data          = 0x20,
    chunk_ack           = 0x21,
    chunk_nack          = 0x22,

    // 재개
    resume_request      = 0x30,
    resume_response     = 0x31,

    // 완료
    transfer_complete   = 0x40,
    transfer_verify     = 0x41,

    // 다운로드 작업
    download_request    = 0x50,
    download_accept     = 0x51,
    download_reject     = 0x52,

    // 목록 작업
    list_request        = 0x60,
    list_response       = 0x61,

    // 제어
    keepalive           = 0xF0,
    error               = 0xFF
};
```

### 5.2 오류 코드

SRS 섹션 6.4에 따름:

```cpp
namespace kcenon::file_transfer::error {

// 전송 오류 (-700 ~ -719)
constexpr int transfer_init_failed      = -700;
constexpr int transfer_cancelled        = -701;
constexpr int transfer_timeout          = -702;
constexpr int upload_rejected           = -703;
constexpr int download_rejected         = -704;
constexpr int connection_refused        = -705;
constexpr int connection_lost           = -706;
constexpr int server_busy               = -707;

// 청크 오류 (-720 ~ -739)
constexpr int chunk_checksum_error      = -720;
constexpr int chunk_sequence_error      = -721;
constexpr int chunk_size_error          = -722;
constexpr int file_hash_mismatch        = -723;

// 파일 I/O 오류 (-740 ~ -759)
constexpr int file_read_error           = -740;
constexpr int file_write_error          = -741;
constexpr int file_permission_error     = -742;
constexpr int file_not_found            = -743;
constexpr int file_already_exists       = -744;
constexpr int storage_full              = -745;
constexpr int file_not_found_on_server  = -746;
constexpr int access_denied             = -747;
constexpr int invalid_filename          = -748;

// 재개 오류 (-760 ~ -779)
constexpr int resume_state_invalid      = -760;
constexpr int resume_file_changed       = -761;

// 압축 오류 (-780 ~ -789)
constexpr int compression_failed        = -780;
constexpr int decompression_failed      = -781;
constexpr int compression_buffer_error  = -782;

// 설정 오류 (-790 ~ -799)
constexpr int config_invalid            = -790;

[[nodiscard]] auto error_message(int code) -> std::string_view;

} // namespace kcenon::file_transfer::error
```

---

## 6. 알고리즘 설계

### 6.1 적응형 압축 알고리즘

**SRS 추적**: SRS-COMP-003

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

### 6.2 자동 재연결 알고리즘

**SRS 추적**: SRS-CLIENT-002

```cpp
void auto_reconnect_handler::attempt_reconnect() {
    std::size_t attempt = 0;
    auto delay = policy_.initial_delay;

    while (attempt < policy_.max_attempts && should_reconnect_) {
        ++attempt;

        // 지수 백오프로 대기
        std::this_thread::sleep_for(delay);

        // 재연결 시도
        auto result = transport_->connect(server_endpoint_);
        if (result) {
            // 성공 - 알림 및 전송 복원
            notify_reconnected();
            resume_active_transfers();
            return;
        }

        // 백오프로 다음 지연 계산
        delay = std::min(
            std::chrono::duration_cast<duration>(delay * policy_.backoff_multiplier),
            policy_.max_delay
        );
    }

    // 최대 시도 횟수 초과
    notify_reconnect_failed();
}
```

### 6.3 경로 순회 방지 알고리즘

**SRS 추적**: SRS-STORAGE-002

```cpp
Result<std::filesystem::path> storage_manager::safe_path(
    const std::string& filename
) const {
    // 단계 1: 경로 순회 패턴 확인
    if (filename.find("..") != std::string::npos) {
        return error::invalid_filename;
    }

    // 단계 2: 절대 경로 확인
    std::filesystem::path requested_path(filename);
    if (requested_path.is_absolute()) {
        return error::invalid_filename;
    }

    // 단계 3: 경로 구분자 확인
    if (filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos) {
        return error::invalid_filename;
    }

    // 단계 4: 구성 및 정규화
    auto full_path = config_.storage_dir / filename;
    auto canonical = std::filesystem::weakly_canonical(full_path);
    auto canonical_base = std::filesystem::weakly_canonical(config_.storage_dir);

    // 단계 5: 경로가 저장소 디렉토리 아래에 있는지 검증
    auto relative = canonical.lexically_relative(canonical_base);
    if (relative.empty() || *relative.begin() == "..") {
        return error::invalid_filename;
    }

    return canonical;
}
```

---

## 7. 보안 설계

### 7.1 TLS 설정

**SRS 추적**: SEC-001

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

### 7.2 저장소 격리

**SRS 추적**: SEC-005

storage_manager는 다음을 통해 모든 파일 작업이 설정된 저장소 디렉토리 내로 제한되도록 합니다:

1. 파일명 검증 (경로 구분자 없음, ".." 없음)
2. 경로 정규화
3. 상대 경로 검증
4. 심볼릭 링크 해석 및 검증

---

## 8. 추적성 매트릭스

### 8.1 SRS에서 설계로 추적성

| SRS ID | SRS 설명 | 설계 컴포넌트 | 설계 요소 |
|--------|----------|---------------|-----------|
| SRS-SERVER-001 | 서버 초기화 | file_transfer_server | builder, storage_manager |
| SRS-SERVER-002 | 서버 시작 | file_transfer_server | start() 메서드 |
| SRS-SERVER-003 | 서버 중지 | file_transfer_server | stop() 메서드 |
| SRS-SERVER-004 | 업로드 요청 처리 | file_transfer_server | on_upload_request() |
| SRS-SERVER-005 | 다운로드 요청 처리 | file_transfer_server | on_download_request() |
| SRS-SERVER-006 | 목록 요청 처리 | file_transfer_server | list_files 처리 |
| SRS-CLIENT-001 | 서버 연결 | file_transfer_client | connect() 메서드 |
| SRS-CLIENT-002 | 자동 재연결 | auto_reconnect_handler | attempt_reconnect() |
| SRS-CLIENT-003 | 파일 업로드 | file_transfer_client | upload_file() 메서드 |
| SRS-CLIENT-004 | 파일 다운로드 | file_transfer_client | download_file() 메서드 |
| SRS-CLIENT-005 | 파일 목록 조회 | file_transfer_client | list_files() 메서드 |
| SRS-CLIENT-006 | 배치 업로드 | file_transfer_client | upload_files() 메서드 |
| SRS-CLIENT-007 | 배치 다운로드 | file_transfer_client | download_files() 메서드 |
| SRS-STORAGE-001 | 저장소 할당량 | storage_manager | can_store_file() |
| SRS-STORAGE-002 | 파일명 검증 | filename_validator | validate() |
| SRS-CHUNK-001 | 파일 분할 | chunk_splitter | split() 메서드 |
| SRS-CHUNK-002 | 파일 조립 | chunk_assembler | process_chunk() |
| SRS-CHUNK-003 | 청크 체크섬 | checksum | crc32() 메서드 |
| SRS-CHUNK-004 | 파일 해시 | checksum | sha256_file() 메서드 |
| SRS-COMP-001 | LZ4 압축 | lz4_engine | compress() 메서드 |
| SRS-COMP-002 | LZ4 압축 해제 | lz4_engine | decompress() 메서드 |
| SRS-COMP-003 | 적응형 감지 | adaptive_compression | is_compressible() |
| SRS-PIPE-001 | 업로드 파이프라인 | upload_pipeline | 파이프라인 단계 |
| SRS-PIPE-002 | 다운로드 파이프라인 | download_pipeline | 파이프라인 단계 |
| SRS-PIPE-003 | 백프레셔 | bounded_queue | push()/pop() 차단 |
| SRS-RESUME-001 | 상태 영속화 | resume_handler | save_state() |
| SRS-RESUME-002 | 전송 재개 | resume_handler | load_state() |
| SRS-PROGRESS-001 | 진행 콜백 | progress_tracker | on_progress() |
| SRS-TRANS-001 | 전송 추상화 | transport_interface | 추상 클래스 |
| SRS-TRANS-002 | TCP 전송 | tcp_transport | 구현 클래스 |

---

## 부록 A: 개정 이력

| 버전 | 날짜 | 작성자 | 설명 |
|------|------|--------|------|
| 1.0.0 | 2025-12-11 | kcenon@naver.com | 초기 SDS 작성 (P2P 모델) |
| 0.2.0 | 2025-12-11 | kcenon@naver.com | 클라이언트-서버 아키텍처로 전면 재작성 |

---

*문서 끝*
