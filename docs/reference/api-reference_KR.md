# API 레퍼런스

**file_trans_system** 라이브러리의 완전한 API 문서입니다.

## 목차

1. [핵심 클래스](#핵심-클래스)
   - [file_sender](#file_sender)
   - [file_receiver](#file_receiver)
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

### file_sender

원격 엔드포인트로 파일을 전송하는 기본 클래스입니다.

#### Builder 패턴

```cpp
namespace kcenon::file_transfer {

class file_sender {
public:
    class builder {
    public:
        // 파이프라인 워커 수 및 큐 크기 설정
        builder& with_pipeline_config(const pipeline_config& config);

        // 압축 모드 설정 (disabled, enabled, adaptive)
        builder& with_compression(compression_mode mode);

        // 압축 수준 설정 (fast, high_compression)
        builder& with_compression_level(compression_level level);

        // 청크 크기 설정 (64KB - 1MB, 기본값: 256KB)
        builder& with_chunk_size(std::size_t size);

        // 대역폭 제한 설정 (바이트/초, 0 = 무제한)
        builder& with_bandwidth_limit(std::size_t bytes_per_second);

        // 전송 타입 설정 (tcp, quic)
        builder& with_transport(transport_type type);

        // 송신자 인스턴스 빌드
        [[nodiscard]] auto build() -> Result<file_sender>;
    };
};

}
```

#### 메서드

##### send_file()

원격 엔드포인트로 단일 파일 전송.

```cpp
[[nodiscard]] auto send_file(
    const std::filesystem::path& file_path,
    const endpoint& destination,
    const transfer_options& options = {}
) -> Result<transfer_handle>;
```

**매개변수:**
| 매개변수 | 타입 | 설명 |
|---------|------|------|
| `file_path` | `std::filesystem::path` | 전송할 파일 경로 |
| `destination` | `endpoint` | 원격 엔드포인트 (IP, 포트) |
| `options` | `transfer_options` | 선택적 전송 설정 |

**반환:** `Result<transfer_handle>` - 전송 추적용 핸들

**예제:**
```cpp
auto sender = file_sender::builder()
    .with_compression(compression_mode::adaptive)
    .with_chunk_size(512 * 1024)  // 512KB 청크
    .build();

if (sender) {
    auto handle = sender->send_file(
        "/path/to/large_file.dat",
        endpoint{"192.168.1.100", 19000}
    );

    if (handle) {
        std::cout << "전송 ID: " << handle->id.to_string() << "\n";
    }
}
```

##### send_files()

배치 작업으로 다중 파일 전송.

```cpp
[[nodiscard]] auto send_files(
    std::span<const std::filesystem::path> files,
    const endpoint& destination,
    const transfer_options& options = {}
) -> Result<batch_transfer_handle>;
```

**매개변수:**
| 매개변수 | 타입 | 설명 |
|---------|------|------|
| `files` | `std::span<const path>` | 전송할 파일 경로 목록 |
| `destination` | `endpoint` | 원격 엔드포인트 |
| `options` | `transfer_options` | 선택적 전송 설정 |

**반환:** `Result<batch_transfer_handle>` - 배치 추적용 핸들

##### cancel()

활성 전송 취소.

```cpp
[[nodiscard]] auto cancel(const transfer_id& id) -> Result<void>;
```

##### pause() / resume()

전송 일시 중지 및 재개.

```cpp
[[nodiscard]] auto pause(const transfer_id& id) -> Result<void>;
[[nodiscard]] auto resume(const transfer_id& id) -> Result<void>;
```

##### on_progress()

진행 상황 업데이트를 위한 콜백 등록.

```cpp
void on_progress(std::function<void(const transfer_progress&)> callback);
```

**예제:**
```cpp
sender->on_progress([](const transfer_progress& p) {
    double percent = 100.0 * p.bytes_transferred / p.total_bytes;
    std::cout << std::fixed << std::setprecision(1)
              << percent << "% - "
              << p.transfer_rate / (1024*1024) << " MB/s"
              << " (압축: " << p.compression_ratio << ":1)\n";
});
```

---

### file_receiver

원격 송신자로부터 파일을 수신하는 기본 클래스입니다.

#### Builder 패턴

```cpp
class file_receiver {
public:
    class builder {
    public:
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_output_directory(const std::filesystem::path& dir);
        builder& with_bandwidth_limit(std::size_t bytes_per_second);
        builder& with_transport(transport_type type);
        [[nodiscard]] auto build() -> Result<file_receiver>;
    };
};
```

#### 메서드

##### start() / stop()

수신자 시작 및 중지.

```cpp
[[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;
[[nodiscard]] auto stop() -> Result<void>;
```

**예제:**
```cpp
auto receiver = file_receiver::builder()
    .with_output_directory("/downloads")
    .build();

if (receiver) {
    receiver->start(endpoint{"0.0.0.0", 19000});

    // ... 전송 대기 ...

    receiver->stop();
}
```

##### set_output_directory()

런타임에 출력 디렉토리 변경.

```cpp
void set_output_directory(const std::filesystem::path& dir);
```

##### 콜백

```cpp
// 들어오는 전송 수락 또는 거부
void on_transfer_request(std::function<bool(const transfer_request&)> callback);

// 진행 상황 업데이트
void on_progress(std::function<void(const transfer_progress&)> callback);

// 전송 완료
void on_complete(std::function<void(const transfer_result&)> callback);
```

**예제:**
```cpp
receiver->on_transfer_request([](const transfer_request& req) {
    // 총 크기 확인
    uint64_t total_size = 0;
    for (const auto& file : req.files) {
        total_size += file.file_size;
    }

    // 10GB 미만이면 수락
    return total_size < 10ULL * 1024 * 1024 * 1024;
});

receiver->on_complete([](const transfer_result& result) {
    if (result.verified) {
        std::cout << "수신됨: " << result.output_path << "\n";
        std::cout << "압축 비율: "
                  << result.compression_stats.compression_ratio() << ":1\n";
    } else {
        std::cerr << "전송 실패: " << result.error->message << "\n";
    }
});
```

---

### transfer_manager

다중 동시 전송 관리 및 통계 제공.

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

특정 전송의 상태 가져오기.

```cpp
[[nodiscard]] auto get_status(const transfer_id& id) -> Result<transfer_status>;
```

##### list_transfers()

모든 활성 전송 목록.

```cpp
[[nodiscard]] auto list_transfers() -> Result<std::vector<transfer_info>>;
```

##### get_statistics()

집계 전송 통계 가져오기.

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
    adaptive    // 압축 가능성 자동 감지 (기본값)
};
```

#### compression_level

```cpp
enum class compression_level {
    fast,             // LZ4 표준 (~400 MB/s)
    high_compression  // LZ4-HC (~50 MB/s, 더 나은 비율)
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
    pending,        // 시작 대기
    initializing,   // 연결 설정 중
    transferring,   // 활성 전송
    verifying,      // 무결성 검증
    completed,      // 성공적으로 완료
    failed,         // 전송 실패
    cancelled       // 사용자 취소
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
    network,        // 네트워크 송/수신
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

#### transfer_options

```cpp
struct transfer_options {
    compression_mode            compression     = compression_mode::adaptive;
    compression_level           level           = compression_level::fast;
    std::size_t                 chunk_size      = 256 * 1024;  // 256KB
    bool                        verify_checksum = true;
    std::optional<std::size_t>  bandwidth_limit;
    std::optional<int>          priority;
};
```

#### transfer_progress

```cpp
struct transfer_progress {
    transfer_id         id;
    uint64_t            bytes_transferred;      // 원시 바이트
    uint64_t            bytes_on_wire;          // 압축 바이트
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
    std::filesystem::path   output_path;
    uint64_t                bytes_transferred;
    uint64_t                bytes_on_wire;
    bool                    verified;           // SHA-256 일치
    std::optional<error>    error;
    duration                elapsed_time;
    compression_statistics  compression_stats;
};
```

#### file_metadata

```cpp
struct file_metadata {
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    std::filesystem::perms  permissions;
    std::chrono::system_clock::time_point modified_time;
    bool                    compressible_hint;
};
```

#### chunk_config

```cpp
struct chunk_config {
    std::size_t chunk_size     = 256 * 1024;    // 256KB 기본값
    std::size_t min_chunk_size = 64 * 1024;     // 64KB 최소
    std::size_t max_chunk_size = 1024 * 1024;   // 1MB 최대
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

스트리밍 전송을 위해 파일을 청크로 분할.

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

수신된 청크를 파일로 재조립.

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

무결성 검증 유틸리티.

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

저수준 LZ4 압축 인터페이스.

```cpp
class lz4_engine {
public:
    // 표준 LZ4 압축 (~400 MB/s)
    [[nodiscard]] static auto compress(
        std::span<const std::byte> input,
        std::span<std::byte> output
    ) -> Result<std::size_t>;

    // LZ4-HC 압축 (~50 MB/s, 더 나은 비율)
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

자동 압축 가능성 감지.

```cpp
class adaptive_compression {
public:
    // 샘플 기반 압축 가능성 검사 (<100us)
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

통계와 함께 고수준 청크 압축.

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

### sender_pipeline

다단계 송신자 처리 파이프라인.

```cpp
class sender_pipeline {
public:
    class builder {
    public:
        builder& with_config(const pipeline_config& config);
        builder& with_compressor(std::shared_ptr<chunk_compressor> compressor);
        builder& with_transport(std::shared_ptr<transport_interface> transport);
        [[nodiscard]] auto build() -> Result<sender_pipeline>;
    };

    [[nodiscard]] auto start() -> Result<void>;
    [[nodiscard]] auto stop(bool wait_for_completion = true) -> Result<void>;

    [[nodiscard]] auto submit(
        const std::filesystem::path& file,
        const transfer_id& id,
        const transfer_options& options
    ) -> Result<void>;

    [[nodiscard]] auto get_stats() const -> pipeline_statistics;
    [[nodiscard]] auto get_queue_depths() const -> queue_depth_info;
};
```

### receiver_pipeline

다단계 수신자 처리 파이프라인.

```cpp
class receiver_pipeline {
public:
    class builder {
    public:
        builder& with_config(const pipeline_config& config);
        builder& with_compressor(std::shared_ptr<chunk_compressor> compressor);
        builder& with_assembler(std::shared_ptr<chunk_assembler> assembler);
        [[nodiscard]] auto build() -> Result<receiver_pipeline>;
    };

    [[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;
    [[nodiscard]] auto stop(bool wait_for_completion = true) -> Result<void>;

    [[nodiscard]] auto submit_chunk(chunk c) -> Result<void>;
    [[nodiscard]] auto get_stats() const -> pipeline_statistics;
    [[nodiscard]] auto get_queue_depths() const -> queue_depth_info;
};
```

---

## 전송

### transport_interface

추상 전송 계층 인터페이스.

```cpp
class transport_interface {
public:
    virtual ~transport_interface() = default;

    [[nodiscard]] virtual auto connect(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto disconnect() -> Result<void> = 0;
    [[nodiscard]] virtual auto is_connected() const -> bool = 0;

    [[nodiscard]] virtual auto send(std::span<const std::byte> data) -> Result<void> = 0;
    [[nodiscard]] virtual auto receive(std::span<std::byte> buffer) -> Result<std::size_t> = 0;

    // QUIC 전용 (TCP에서는 no-op)
    [[nodiscard]] virtual auto create_stream() -> Result<stream_id>;
    [[nodiscard]] virtual auto close_stream(stream_id) -> Result<void>;

    [[nodiscard]] virtual auto listen(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto accept() -> Result<std::unique_ptr<transport_interface>> = 0;
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

전송 인스턴스 생성을 위한 팩토리.

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

*최종 업데이트: 2025-12-11*
