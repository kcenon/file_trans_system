# API 레퍼런스

**file_trans_system** 라이브러리의 완전한 API 문서입니다.

**버전**: 2.0.0
**최종 업데이트**: 2025-12-11

## 목차

1. [핵심 클래스](#핵심-클래스)
   - [file_transfer_server](#file_transfer_server)
   - [file_transfer_client](#file_transfer_client)
   - [transfer_manager](#transfer_manager)
2. [데이터 타입](#데이터-타입)
   - [열거형](#열거형)
   - [구조체](#구조체)
3. [청크 관리](#청크-관리)
4. [압축](#압축)
5. [파이프라인](#파이프라인)
6. [전송](#전송)

---

## 핵심 클래스

### file_transfer_server

파일 저장소 및 클라이언트 연결을 관리하는 중앙 서버 클래스입니다.

#### Builder 패턴

```cpp
namespace kcenon::file_transfer {

class file_transfer_server {
public:
    class builder {
    public:
        // 업로드된 파일의 저장소 디렉토리 설정
        builder& with_storage_directory(const std::filesystem::path& dir);

        // 최대 동시 클라이언트 연결 수 설정
        builder& with_max_connections(std::size_t max_count);

        // 업로드 허용 최대 파일 크기 설정
        builder& with_max_file_size(uint64_t max_size);

        // 총 저장소 할당량 설정
        builder& with_storage_quota(uint64_t quota);

        // 파이프라인 워커 수 및 큐 크기 설정
        builder& with_pipeline_config(const pipeline_config& config);

        // 전송 타입 설정 (tcp, quic)
        builder& with_transport(transport_type type);

        // 서버 인스턴스 빌드
        [[nodiscard]] auto build() -> Result<file_transfer_server>;
    };
};

}
```

#### 메서드

##### start() / stop()

서버 시작 및 중지.

```cpp
[[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;
[[nodiscard]] auto stop() -> Result<void>;
```

**예제:**
```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_max_connections(100)
    .with_max_file_size(10ULL * 1024 * 1024 * 1024)  // 10GB
    .with_storage_quota(1ULL * 1024 * 1024 * 1024 * 1024)  // 1TB
    .build();

if (server) {
    auto result = server->start(endpoint{"0.0.0.0", 19000});
    if (result) {
        std::cout << "서버가 포트 19000에서 시작됨\n";
    }

    // ... 서버 실행 중 ...

    server->stop();
}
```

##### list_stored_files()

저장소 디렉토리의 파일 목록 조회.

```cpp
[[nodiscard]] auto list_stored_files(
    const list_options& options = {}
) -> Result<std::vector<file_info>>;
```

**매개변수:**
| 매개변수 | 타입 | 설명 |
|----------|------|------|
| `options` | `list_options` | 필터링, 페이지네이션, 정렬 옵션 |

**반환값:** `Result<std::vector<file_info>>` - 메타데이터가 포함된 파일 목록

##### delete_file()

저장소에서 파일 삭제.

```cpp
[[nodiscard]] auto delete_file(const std::string& filename) -> Result<void>;
```

##### get_statistics()

서버 통계 조회.

```cpp
[[nodiscard]] auto get_statistics() -> server_statistics;
[[nodiscard]] auto get_storage_stats() -> storage_statistics;
```

#### 콜백

##### on_upload_request()

업로드 요청 수락 또는 거부 콜백 등록.

```cpp
void on_upload_request(std::function<bool(const upload_request&)> callback);
```

**예제:**
```cpp
server->on_upload_request([](const upload_request& req) {
    // 1GB보다 큰 파일 거부
    if (req.file_size > 1ULL * 1024 * 1024 * 1024) {
        return false;
    }

    // 특정 파일 유형 거부
    if (req.filename.ends_with(".exe")) {
        return false;
    }

    return true;
});
```

##### on_download_request()

다운로드 요청 수락 또는 거부 콜백 등록.

```cpp
void on_download_request(std::function<bool(const download_request&)> callback);
```

**예제:**
```cpp
server->on_download_request([](const download_request& req) {
    // 모든 다운로드 허용
    return true;
});
```

##### on_transfer_complete()

전송 완료 이벤트 콜백 등록.

```cpp
void on_transfer_complete(std::function<void(const transfer_result&)> callback);
```

##### on_client_connected() / on_client_disconnected()

클라이언트 연결 이벤트 콜백 등록.

```cpp
void on_client_connected(std::function<void(const session_info&)> callback);
void on_client_disconnected(std::function<void(const session_info&, disconnect_reason)> callback);
```

---

### file_transfer_client

서버에 연결하고 파일을 전송하는 클라이언트 클래스입니다.

#### Builder 패턴

```cpp
class file_transfer_client {
public:
    class builder {
    public:
        // 압축 모드 설정 (disabled, enabled, adaptive)
        builder& with_compression(compression_mode mode);

        // 압축 수준 설정 (fast, high_compression)
        builder& with_compression_level(compression_level level);

        // 청크 크기 설정 (64KB - 1MB, 기본값: 256KB)
        builder& with_chunk_size(std::size_t size);

        // 자동 재연결 활성화
        builder& with_auto_reconnect(bool enable);

        // 재연결 정책 설정
        builder& with_reconnect_policy(const reconnect_policy& policy);

        // 대역폭 제한 설정 (바이트/초, 0 = 무제한)
        builder& with_bandwidth_limit(std::size_t bytes_per_second);

        // 파이프라인 설정
        builder& with_pipeline_config(const pipeline_config& config);

        // 전송 타입 설정 (tcp, quic)
        builder& with_transport(transport_type type);

        // 클라이언트 인스턴스 빌드
        [[nodiscard]] auto build() -> Result<file_transfer_client>;
    };
};
```

#### 연결 메서드

##### connect() / disconnect()

서버에 연결 및 연결 해제.

```cpp
[[nodiscard]] auto connect(const endpoint& server_addr) -> Result<void>;
[[nodiscard]] auto disconnect() -> Result<void>;
[[nodiscard]] auto is_connected() const -> bool;
```

**예제:**
```cpp
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::adaptive)
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy{
        .initial_delay = std::chrono::seconds(1),
        .max_delay = std::chrono::seconds(60),
        .multiplier = 2.0,
        .max_attempts = 10
    })
    .build();

if (client) {
    auto result = client->connect(endpoint{"192.168.1.100", 19000});
    if (result) {
        std::cout << "서버에 연결됨\n";
    }
}
```

#### 전송 메서드

##### upload_file()

서버에 파일 업로드.

```cpp
[[nodiscard]] auto upload_file(
    const std::filesystem::path& local_path,
    const std::string& remote_name,
    const upload_options& options = {}
) -> Result<transfer_handle>;
```

**매개변수:**
| 매개변수 | 타입 | 설명 |
|----------|------|------|
| `local_path` | `std::filesystem::path` | 업로드할 로컬 파일 경로 |
| `remote_name` | `std::string` | 서버에 저장할 파일 이름 |
| `options` | `upload_options` | 선택적 업로드 설정 |

**반환값:** `Result<transfer_handle>` - 전송 추적을 위한 핸들

**예제:**
```cpp
auto handle = client->upload_file(
    "/local/data/report.pdf",
    "report.pdf",
    upload_options{
        .compression = compression_mode::enabled,
        .overwrite_existing = true
    }
);

if (handle) {
    std::cout << "업로드 시작: " << handle->id.to_string() << "\n";
}
```

##### download_file()

서버에서 파일 다운로드.

```cpp
[[nodiscard]] auto download_file(
    const std::string& remote_name,
    const std::filesystem::path& local_path,
    const download_options& options = {}
) -> Result<transfer_handle>;
```

**매개변수:**
| 매개변수 | 타입 | 설명 |
|----------|------|------|
| `remote_name` | `std::string` | 서버의 파일 이름 |
| `local_path` | `std::filesystem::path` | 로컬에 저장할 경로 |
| `options` | `download_options` | 선택적 다운로드 설정 |

**반환값:** `Result<transfer_handle>` - 전송 추적을 위한 핸들

**예제:**
```cpp
auto handle = client->download_file(
    "report.pdf",
    "/local/downloads/report.pdf"
);

if (handle) {
    std::cout << "다운로드 시작: " << handle->id.to_string() << "\n";
}
```

##### list_files()

서버에서 사용 가능한 파일 목록 조회.

```cpp
[[nodiscard]] auto list_files(
    const list_options& options = {}
) -> Result<std::vector<file_info>>;
```

**매개변수:**
| 매개변수 | 타입 | 설명 |
|----------|------|------|
| `options` | `list_options` | 필터링, 페이지네이션, 정렬 옵션 |

**반환값:** `Result<std::vector<file_info>>` - 메타데이터가 포함된 파일 목록

**예제:**
```cpp
auto result = client->list_files(list_options{
    .pattern = "*.pdf",
    .offset = 0,
    .limit = 100,
    .sort_by = sort_field::modified_time,
    .sort_order = sort_order::descending
});

if (result) {
    for (const auto& file : *result) {
        std::cout << file.filename << " - "
                  << file.file_size << " 바이트\n";
    }
}
```

##### upload_files()

여러 파일을 배치 작업으로 업로드.

```cpp
[[nodiscard]] auto upload_files(
    std::span<const file_upload_entry> files,
    const upload_options& options = {}
) -> Result<batch_transfer_handle>;
```

#### 전송 제어

##### cancel() / pause() / resume()

활성 전송 제어.

```cpp
[[nodiscard]] auto cancel(const transfer_id& id) -> Result<void>;
[[nodiscard]] auto pause(const transfer_id& id) -> Result<void>;
[[nodiscard]] auto resume(const transfer_id& id) -> Result<void>;
```

##### get_transfer_status()

전송 상태 조회.

```cpp
[[nodiscard]] auto get_transfer_status(const transfer_id& id) -> Result<transfer_status>;
```

#### 콜백

##### on_progress()

진행 상황 업데이트 콜백 등록.

```cpp
void on_progress(std::function<void(const transfer_progress&)> callback);
```

**예제:**
```cpp
client->on_progress([](const transfer_progress& p) {
    double percent = 100.0 * p.bytes_transferred / p.total_bytes;
    std::cout << std::fixed << std::setprecision(1)
              << percent << "% - "
              << p.transfer_rate / (1024*1024) << " MB/s"
              << " (압축: " << p.compression_ratio << ":1)\n";
});
```

##### on_complete()

전송 완료 콜백 등록.

```cpp
void on_complete(std::function<void(const transfer_result&)> callback);
```

##### on_error()

전송 오류 콜백 등록.

```cpp
void on_error(std::function<void(const transfer_id&, const error&)> callback);
```

##### on_disconnected()

연결 끊김 이벤트 콜백 등록.

```cpp
void on_disconnected(std::function<void(disconnect_reason)> callback);
```

---

### transfer_manager

여러 동시 전송을 관리하고 통계를 제공합니다.

#### Builder 패턴

```cpp
class transfer_manager {
public:
    class builder {
    public:
        builder& with_max_concurrent(std::size_t max_count);
        builder& with_default_compression(compression_mode mode);
        builder& with_global_bandwidth_limit(std::size_t bytes_per_second);
        [[nodiscard]] auto build() -> Result<transfer_manager>;
    };
};
```

#### 메서드

##### get_status()

특정 전송의 상태 조회.

```cpp
[[nodiscard]] auto get_status(const transfer_id& id) -> Result<transfer_status>;
```

##### list_transfers()

모든 활성 전송 목록 조회.

```cpp
[[nodiscard]] auto list_transfers() -> Result<std::vector<transfer_info>>;
```

##### get_statistics()

집계된 전송 통계 조회.

```cpp
[[nodiscard]] auto get_statistics() -> transfer_statistics;
[[nodiscard]] auto get_compression_stats() -> compression_statistics;
```

##### 설정

```cpp
void set_bandwidth_limit(std::size_t bytes_per_second);
void set_max_concurrent_transfers(std::size_t max_count);
void set_default_compression(compression_mode mode);
```

---

## 데이터 타입

### 열거형

#### compression_mode

```cpp
enum class compression_mode {
    disabled,   // 압축 없음
    enabled,    // 항상 압축
    adaptive    // 자동 감지 (기본값)
};
```

#### compression_level

```cpp
enum class compression_level {
    fast,             // LZ4 표준 (~400 MB/s)
    high_compression  // LZ4-HC (~50 MB/s, 더 높은 비율)
};
```

#### transport_type

```cpp
enum class transport_type {
    tcp,    // TCP + TLS 1.3 (기본값)
    quic    // QUIC (Phase 2)
};
```

#### transfer_state_enum

```cpp
enum class transfer_state_enum {
    pending,        // 시작 대기 중
    requested,      // 요청 전송됨, 응답 대기 중
    transferring,   // 활성 전송 중
    verifying,      // 무결성 검증 중
    completed,      // 성공적으로 완료됨
    failed,         // 전송 실패
    cancelled,      // 사용자 취소
    rejected        // 서버가 요청 거부
};
```

#### transfer_direction

```cpp
enum class transfer_direction {
    upload,     // 클라이언트에서 서버로
    download    // 서버에서 클라이언트로
};
```

#### connection_state

```cpp
enum class connection_state {
    disconnected,   // 연결되지 않음
    connecting,     // 연결 진행 중
    connected,      // 연결됨, 준비 완료
    reconnecting,   // 자동 재연결 진행 중
    failed          // 연결 영구 실패
};
```

#### sort_field

```cpp
enum class sort_field {
    name,           // 파일명으로 정렬
    size,           // 파일 크기로 정렬
    modified_time,  // 수정 시간으로 정렬
    created_time    // 생성 시간으로 정렬
};
```

#### sort_order

```cpp
enum class sort_order {
    ascending,
    descending
};
```

#### chunk_flags

```cpp
enum class chunk_flags : uint8_t {
    none            = 0x00,
    first_chunk     = 0x01,
    last_chunk      = 0x02,
    compressed      = 0x04,
    encrypted       = 0x08
};
```

#### pipeline_stage

```cpp
enum class pipeline_stage : uint8_t {
    io_read,        // 파일 읽기 작업
    chunk_process,  // 청크 조립/분해
    compression,    // LZ4 압축/해제
    network,        // 네트워크 송수신
    io_write        // 파일 쓰기 작업
};
```

---

### 구조체

#### endpoint

```cpp
struct endpoint {
    std::string address;
    uint16_t    port;
};
```

#### transfer_id

```cpp
struct transfer_id {
    std::array<uint8_t, 16> bytes;  // UUID

    [[nodiscard]] static auto generate() -> transfer_id;
    [[nodiscard]] auto to_string() const -> std::string;
    [[nodiscard]] static auto from_string(std::string_view str) -> Result<transfer_id>;
};
```

#### file_info

```cpp
struct file_info {
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    std::chrono::system_clock::time_point created_time;
    std::chrono::system_clock::time_point modified_time;
};
```

#### upload_request

```cpp
struct upload_request {
    transfer_id             id;
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    session_info            client;
    compression_mode        compression;
};
```

#### download_request

```cpp
struct download_request {
    transfer_id             id;
    std::string             filename;
    session_info            client;
    compression_mode        compression;
};
```

#### upload_options

```cpp
struct upload_options {
    compression_mode            compression     = compression_mode::adaptive;
    compression_level           level           = compression_level::fast;
    bool                        overwrite_existing = false;
    bool                        verify_checksum = true;
    std::optional<std::size_t>  bandwidth_limit;
    std::optional<int>          priority;
};
```

#### download_options

```cpp
struct download_options {
    compression_mode            compression     = compression_mode::adaptive;
    bool                        verify_checksum = true;
    std::optional<std::size_t>  bandwidth_limit;
    std::optional<int>          priority;
};
```

#### list_options

```cpp
struct list_options {
    std::string     pattern     = "*";      // Glob 패턴
    std::size_t     offset      = 0;        // 페이지네이션 오프셋
    std::size_t     limit       = 1000;     // 반환할 최대 항목 수
    sort_field      sort_by     = sort_field::name;
    sort_order      order       = sort_order::ascending;
};
```

#### reconnect_policy

```cpp
struct reconnect_policy {
    std::chrono::milliseconds   initial_delay   = std::chrono::seconds(1);
    std::chrono::milliseconds   max_delay       = std::chrono::seconds(60);
    double                      multiplier      = 2.0;
    uint32_t                    max_attempts    = 10;
    bool                        resume_transfers = true;
};
```

#### session_info

```cpp
struct session_info {
    session_id                  id;
    endpoint                    remote_address;
    std::chrono::system_clock::time_point connected_at;
    uint64_t                    bytes_uploaded;
    uint64_t                    bytes_downloaded;
    std::size_t                 active_transfers;
};
```

#### transfer_handle

```cpp
struct transfer_handle {
    transfer_id             id;
    transfer_direction      direction;

    // 완료까지 블로킹 대기
    [[nodiscard]] auto wait() -> Result<transfer_result>;

    // 논블로킹 상태 확인
    [[nodiscard]] auto get_status() -> transfer_status;
};
```

#### transfer_progress

```cpp
struct transfer_progress {
    transfer_id         id;
    transfer_direction  direction;
    std::string         filename;
    uint64_t            bytes_transferred;      // 원본 바이트
    uint64_t            bytes_on_wire;          // 압축된 바이트
    uint64_t            total_bytes;
    double              transfer_rate;          // 바이트/초
    double              effective_rate;         // 압축 포함
    double              compression_ratio;
    duration            elapsed_time;
    duration            estimated_remaining;
    transfer_state_enum state;
    std::optional<std::string> error_message;
};
```

#### transfer_result

```cpp
struct transfer_result {
    transfer_id             id;
    transfer_direction      direction;
    std::string             filename;
    std::filesystem::path   local_path;         // 다운로드용
    std::string             stored_path;        // 업로드용 (서버상)
    uint64_t                bytes_transferred;
    uint64_t                bytes_on_wire;
    bool                    verified;           // SHA-256 일치
    std::optional<error>    error;
    duration                elapsed_time;
    compression_statistics  compression_stats;
};
```

#### server_statistics

```cpp
struct server_statistics {
    std::size_t             active_connections;
    std::size_t             total_connections;
    std::size_t             active_uploads;
    std::size_t             active_downloads;
    uint64_t                total_bytes_received;
    uint64_t                total_bytes_sent;
    std::size_t             files_stored;
    uint64_t                storage_used;
    uint64_t                storage_available;
    duration                uptime;
};
```

#### storage_statistics

```cpp
struct storage_statistics {
    std::size_t             file_count;
    uint64_t                total_size;
    uint64_t                quota;
    uint64_t                available;
    double                  usage_percent;
};
```

#### pipeline_config

```cpp
struct pipeline_config {
    // 스테이지별 워커 수
    std::size_t io_read_workers      = 2;
    std::size_t chunk_workers        = 2;
    std::size_t compression_workers  = 4;
    std::size_t network_workers      = 2;
    std::size_t io_write_workers     = 2;

    // 큐 크기 (백프레셔)
    std::size_t read_queue_size      = 16;
    std::size_t compress_queue_size  = 32;
    std::size_t send_queue_size      = 64;
    std::size_t decompress_queue_size = 32;
    std::size_t write_queue_size     = 16;

    [[nodiscard]] static auto auto_detect() -> pipeline_config;
};
```

#### compression_statistics

```cpp
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
```

---

## 청크 관리

### chunk_splitter

스트리밍 전송을 위해 파일을 청크로 분할합니다.

```cpp
class chunk_splitter {
public:
    explicit chunk_splitter(const chunk_config& config);

    [[nodiscard]] auto split(
        const std::filesystem::path& file_path,
        const transfer_id& id
    ) -> Result<chunk_iterator>;

    [[nodiscard]] auto calculate_metadata(
        const std::filesystem::path& file_path
    ) -> Result<file_metadata>;
};
```

### chunk_assembler

수신된 청크를 파일로 재조립합니다.

```cpp
class chunk_assembler {
public:
    explicit chunk_assembler(const std::filesystem::path& output_dir);

    [[nodiscard]] auto process_chunk(const chunk& c) -> Result<void>;
    [[nodiscard]] auto is_complete(const transfer_id& id) const -> bool;
    [[nodiscard]] auto get_missing_chunks(const transfer_id& id) const
        -> std::vector<uint64_t>;
    [[nodiscard]] auto finalize(
        const transfer_id& id,
        const std::string& expected_hash
    ) -> Result<std::filesystem::path>;
};
```

### checksum

무결성 검증 유틸리티입니다.

```cpp
class checksum {
public:
    // 청크용 CRC32
    [[nodiscard]] static auto crc32(std::span<const std::byte> data) -> uint32_t;
    [[nodiscard]] static auto verify_crc32(
        std::span<const std::byte> data,
        uint32_t expected
    ) -> bool;

    // 파일용 SHA-256
    [[nodiscard]] static auto sha256_file(const std::filesystem::path& path)
        -> Result<std::string>;
    [[nodiscard]] static auto verify_sha256(
        const std::filesystem::path& path,
        const std::string& expected
    ) -> bool;
};
```

---

## 압축

### lz4_engine

저수준 LZ4 압축 인터페이스입니다.

```cpp
class lz4_engine {
public:
    // 표준 LZ4 압축 (~400 MB/s)
    [[nodiscard]] static auto compress(
        std::span<const std::byte> input,
        std::span<std::byte> output
    ) -> Result<std::size_t>;

    // LZ4-HC 압축 (~50 MB/s, 더 높은 비율)
    [[nodiscard]] static auto compress_hc(
        std::span<const std::byte> input,
        std::span<std::byte> output,
        int level = 9
    ) -> Result<std::size_t>;

    // 압축 해제 (~1.5 GB/s)
    [[nodiscard]] static auto decompress(
        std::span<const std::byte> compressed,
        std::span<std::byte> output,
        std::size_t original_size
    ) -> Result<std::size_t>;

    // 버퍼 크기 계산
    [[nodiscard]] static auto max_compressed_size(std::size_t input_size)
        -> std::size_t;
};
```

### adaptive_compression

자동 압축 가능성 감지입니다.

```cpp
class adaptive_compression {
public:
    // 샘플 기반 압축 가능성 확인 (<100us)
    [[nodiscard]] static auto is_compressible(
        std::span<const std::byte> data,
        double threshold = 0.9
    ) -> bool;

    // 파일 확장자 휴리스틱
    [[nodiscard]] static auto is_likely_compressible(
        const std::filesystem::path& file
    ) -> bool;
};
```

### chunk_compressor

통계가 포함된 고수준 청크 압축입니다.

```cpp
class chunk_compressor {
public:
    explicit chunk_compressor(
        compression_mode mode = compression_mode::adaptive,
        compression_level level = compression_level::fast
    );

    [[nodiscard]] auto compress(const chunk& input) -> Result<chunk>;
    [[nodiscard]] auto decompress(const chunk& input) -> Result<chunk>;
    [[nodiscard]] auto get_statistics() const -> compression_statistics;
    void reset_statistics();
};
```

---

## 파이프라인

### server_pipeline

서버 측 처리를 위한 다단계 파이프라인입니다.

```cpp
class server_pipeline {
public:
    class builder {
    public:
        builder& with_config(const pipeline_config& config);
        builder& with_storage_manager(std::shared_ptr<storage_manager> storage);
        builder& with_compressor(std::shared_ptr<chunk_compressor> compressor);
        [[nodiscard]] auto build() -> Result<server_pipeline>;
    };

    // 업로드 파이프라인 (클라이언트에서 수신, 저장소에 쓰기)
    [[nodiscard]] auto start_upload_pipeline() -> Result<void>;

    // 다운로드 파이프라인 (저장소에서 읽기, 클라이언트에 전송)
    [[nodiscard]] auto start_download_pipeline() -> Result<void>;

    [[nodiscard]] auto stop(bool wait_for_completion = true) -> Result<void>;
    [[nodiscard]] auto get_stats() const -> pipeline_statistics;
};
```

### client_pipeline

클라이언트 측 처리를 위한 다단계 파이프라인입니다.

```cpp
class client_pipeline {
public:
    class builder {
    public:
        builder& with_config(const pipeline_config& config);
        builder& with_compressor(std::shared_ptr<chunk_compressor> compressor);
        builder& with_transport(std::shared_ptr<transport_interface> transport);
        [[nodiscard]] auto build() -> Result<client_pipeline>;
    };

    // 업로드 (서버로 전송)
    [[nodiscard]] auto submit_upload(
        const std::filesystem::path& file,
        const transfer_id& id,
        const upload_options& options
    ) -> Result<void>;

    // 다운로드 (서버에서 수신)
    [[nodiscard]] auto submit_download(
        const std::string& remote_name,
        const std::filesystem::path& local_path,
        const transfer_id& id,
        const download_options& options
    ) -> Result<void>;

    [[nodiscard]] auto start() -> Result<void>;
    [[nodiscard]] auto stop(bool wait_for_completion = true) -> Result<void>;
    [[nodiscard]] auto get_stats() const -> pipeline_statistics;
    [[nodiscard]] auto get_queue_depths() const -> queue_depth_info;
};
```

---

## 전송

### transport_interface

추상 전송 계층 인터페이스입니다.

```cpp
class transport_interface {
public:
    virtual ~transport_interface() = default;

    // 클라이언트 작업
    [[nodiscard]] virtual auto connect(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto disconnect() -> Result<void> = 0;
    [[nodiscard]] virtual auto is_connected() const -> bool = 0;

    // 데이터 전송
    [[nodiscard]] virtual auto send(std::span<const std::byte> data) -> Result<void> = 0;
    [[nodiscard]] virtual auto receive(std::span<std::byte> buffer) -> Result<std::size_t> = 0;

    // 서버 작업
    [[nodiscard]] virtual auto listen(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto accept() -> Result<std::unique_ptr<transport_interface>> = 0;

    // QUIC 전용 (TCP에서는 무작동)
    [[nodiscard]] virtual auto create_stream() -> Result<stream_id>;
    [[nodiscard]] virtual auto close_stream(stream_id) -> Result<void>;
};
```

### 전송 설정

```cpp
struct tcp_transport_config {
    bool        enable_tls      = true;
    bool        tcp_nodelay     = true;
    std::size_t send_buffer     = 256 * 1024;
    std::size_t recv_buffer     = 256 * 1024;
    duration    connect_timeout = 10s;
    duration    read_timeout    = 30s;
};

struct quic_transport_config {
    bool        enable_0rtt         = true;
    std::size_t max_streams         = 100;
    std::size_t initial_window      = 10 * 1024 * 1024;
    duration    idle_timeout        = 30s;
    bool        enable_migration    = true;
};
```

### transport_factory

전송 인스턴스 생성을 위한 팩토리입니다.

```cpp
class transport_factory {
public:
    [[nodiscard]] static auto create(transport_type type)
        -> std::unique_ptr<transport_interface>;

    [[nodiscard]] static auto create_with_fallback(
        const endpoint& ep,
        transport_type preferred = transport_type::quic
    ) -> Result<std::unique_ptr<transport_interface>>;
};
```

---

## 전체 예제

### 서버 예제

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

int main() {
    // 서버 생성 및 설정
    auto server = file_transfer_server::builder()
        .with_storage_directory("/data/files")
        .with_max_connections(100)
        .with_max_file_size(10ULL * 1024 * 1024 * 1024)  // 10GB
        .with_storage_quota(1ULL * 1024 * 1024 * 1024 * 1024)  // 1TB
        .with_pipeline_config(pipeline_config::auto_detect())
        .build();

    if (!server) {
        std::cerr << "서버 생성 실패: " << server.error().message() << "\n";
        return 1;
    }

    // 콜백 설정
    server->on_upload_request([](const upload_request& req) {
        std::cout << "업로드 요청: " << req.filename
                  << " (" << req.file_size << " 바이트)\n";
        return req.file_size < 1ULL * 1024 * 1024 * 1024;  // 1GB 미만 수락
    });

    server->on_download_request([](const download_request& req) {
        std::cout << "다운로드 요청: " << req.filename << "\n";
        return true;
    });

    server->on_transfer_complete([](const transfer_result& result) {
        if (result.verified) {
            std::cout << "전송 완료: " << result.filename << "\n";
        } else {
            std::cerr << "전송 실패: " << result.error->message << "\n";
        }
    });

    // 서버 시작
    auto result = server->start(endpoint{"0.0.0.0", 19000});
    if (!result) {
        std::cerr << "서버 시작 실패: " << result.error().message() << "\n";
        return 1;
    }

    std::cout << "서버가 포트 19000에서 실행 중입니다. Enter를 눌러 중지하세요.\n";
    std::cin.get();

    server->stop();
    return 0;
}
```

### 클라이언트 예제

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

int main() {
    // 클라이언트 생성 및 설정
    auto client = file_transfer_client::builder()
        .with_compression(compression_mode::adaptive)
        .with_auto_reconnect(true)
        .with_reconnect_policy(reconnect_policy{
            .initial_delay = std::chrono::seconds(1),
            .max_delay = std::chrono::seconds(60),
            .multiplier = 2.0,
            .max_attempts = 10
        })
        .build();

    if (!client) {
        std::cerr << "클라이언트 생성 실패: " << client.error().message() << "\n";
        return 1;
    }

    // 콜백 설정
    client->on_progress([](const transfer_progress& p) {
        double percent = 100.0 * p.bytes_transferred / p.total_bytes;
        std::cout << "\r" << p.filename << ": "
                  << std::fixed << std::setprecision(1) << percent << "% - "
                  << p.transfer_rate / (1024*1024) << " MB/s" << std::flush;
    });

    client->on_complete([](const transfer_result& result) {
        std::cout << "\n";
        if (result.verified) {
            std::cout << "전송 완료: " << result.filename
                      << " (압축: " << result.compression_stats.compression_ratio()
                      << ":1)\n";
        } else {
            std::cerr << "전송 실패: " << result.error->message << "\n";
        }
    });

    client->on_disconnected([](disconnect_reason reason) {
        std::cerr << "연결 끊김: " << static_cast<int>(reason) << "\n";
    });

    // 서버에 연결
    auto connect_result = client->connect(endpoint{"192.168.1.100", 19000});
    if (!connect_result) {
        std::cerr << "연결 실패: " << connect_result.error().message() << "\n";
        return 1;
    }

    // 파일 목록 조회
    auto files = client->list_files();
    if (files) {
        std::cout << "서버의 파일:\n";
        for (const auto& file : *files) {
            std::cout << "  " << file.filename << " - " << file.file_size << " 바이트\n";
        }
    }

    // 파일 업로드
    auto upload = client->upload_file("/local/data.zip", "data.zip");
    if (upload) {
        upload->wait();  // 완료 대기
    }

    // 파일 다운로드
    auto download = client->download_file("report.pdf", "/local/report.pdf");
    if (download) {
        download->wait();  // 완료 대기
    }

    client->disconnect();
    return 0;
}
```

---

*최종 업데이트: 2025-12-11*
*버전: 2.0.0*
