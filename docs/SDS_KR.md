# 파일 전송 시스템 - 소프트웨어 설계 명세서

## 문서 정보

| 항목 | 내용 |
|------|------|
| **프로젝트명** | file_trans_system |
| **문서 유형** | 소프트웨어 설계 명세서 (SDS) |
| **버전** | 1.0.0 |
| **상태** | 초안 |
| **작성일** | 2025-12-11 |
| **작성자** | kcenon@naver.com |
| **관련 문서** | SRS_KR.md v1.1.0, PRD_KR.md v1.0.0 |

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
- 시스템 아키텍처 및 컴포넌트 설계
- 데이터 구조 및 데이터 흐름
- 인터페이스 명세
- 알고리즘 설명
- 오류 처리 설계
- SRS 요구사항과의 추적성

### 1.3 설계 개요

file_trans_system은 모듈화된 계층형 아키텍처로 설계되었습니다:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          API 계층                                        │
│   file_sender  │  file_receiver  │  transfer_manager                    │
├─────────────────────────────────────────────────────────────────────────┤
│                        핵심 계층                                         │
│   sender_pipeline  │  receiver_pipeline  │  progress_tracker            │
├─────────────────────────────────────────────────────────────────────────┤
│                       서비스 계층                                        │
│   chunk_manager  │  compression_engine  │  resume_handler               │
├─────────────────────────────────────────────────────────────────────────┤
│                       전송 계층                                          │
│   transport_interface  │  tcp_transport  │  quic_transport (Phase 2)    │
├─────────────────────────────────────────────────────────────────────────┤
│                      인프라스트럭처 계층                                  │
│   common_system  │  thread_system  │  network_system  │  container_system│
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 아키텍처 설계

### 2.1 아키텍처 스타일

시스템은 **파이프라인 아키텍처**와 **계층형 아키텍처**를 결합하여 사용합니다:

| 스타일 | 적용 영역 | 근거 |
|--------|-----------|------|
| **파이프라인** | 데이터 처리 (읽기→압축→전송) | 병렬 단계를 통한 처리량 최대화 |
| **계층형** | 시스템 조직 | 관심사 분리, 테스트 용이성 |
| **전략** | 전송, 압축 | 플러그인 가능한 구현 |
| **옵저버** | 진행 알림 | 분리된 이벤트 처리 |

### 2.2 컴포넌트 아키텍처

#### 2.2.1 컴포넌트 다이어그램

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                              file_trans_system                                │
│                                                                               │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────────┐  │
│  │   file_sender   │  │  file_receiver  │  │     transfer_manager        │  │
│  │                 │  │                 │  │                             │  │
│  │  +send_file()   │  │  +start()       │  │  +get_status()              │  │
│  │  +send_files()  │  │  +stop()        │  │  +list_transfers()          │  │
│  │  +cancel()      │  │  +on_request()  │  │  +get_statistics()          │  │
│  │  +pause()       │  │  +on_progress() │  │  +set_bandwidth_limit()     │  │
│  │  +resume()      │  │  +on_complete() │  │  +set_compression()         │  │
│  └────────┬────────┘  └────────┬────────┘  └─────────────┬───────────────┘  │
│           │                    │                          │                  │
│           └────────────────────┼──────────────────────────┘                  │
│                                ▼                                             │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                          파이프라인 계층                              │   │
│  │  ┌──────────────────────┐    ┌──────────────────────┐                │   │
│  │  │   sender_pipeline    │    │  receiver_pipeline   │                │   │
│  │  │                      │    │                      │                │   │
│  │  │ 읽기→청크→압축→전송  │    │ 수신→해제→조립→쓰기  │                │   │
│  │  └──────────┬───────────┘    └──────────┬───────────┘                │   │
│  │             │                           │                             │   │
│  └─────────────┼───────────────────────────┼─────────────────────────────┘   │
│                ▼                           ▼                                 │
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
| **빌더** | file_sender::builder, file_receiver::builder | API 사용성 |
| **전략** | transport_interface 구현 | SRS-TRANS-001 |
| **옵저버** | 진행 콜백, 완료 이벤트 | SRS-PROGRESS-001 |
| **파이프라인** | sender_pipeline, receiver_pipeline | SRS-PIPE-001, SRS-PIPE-002 |
| **팩토리** | create_transport() | SRS-TRANS-001 |
| **상태** | 전송 상태 머신 | SRS-PROGRESS-002 |
| **커맨드** | 파이프라인 작업 | SRS-PIPE-001 |

---

## 3. 컴포넌트 설계

### 3.1 file_sender 컴포넌트

#### 3.1.1 책임
원격 엔드포인트로 파일을 전송하기 위한 공개 API를 제공합니다.

#### 3.1.2 SRS 추적성
| SRS 요구사항 | 설계 요소 |
|--------------|-----------|
| SRS-CORE-001 | send_file() 메서드 |
| SRS-CORE-003 | send_files() 메서드 |
| SRS-PROGRESS-001 | on_progress() 콜백 |

#### 3.1.3 클래스 설계

```cpp
namespace kcenon::file_transfer {

class file_sender {
public:
    class builder {
    public:
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_compression(compression_mode mode);
        builder& with_compression_level(compression_level level);
        builder& with_chunk_size(std::size_t size);
        builder& with_bandwidth_limit(std::size_t bytes_per_second);
        builder& with_transport(transport_type type);
        [[nodiscard]] auto build() -> Result<file_sender>;

    private:
        pipeline_config         pipeline_config_;
        compression_mode        compression_mode_   = compression_mode::adaptive;
        compression_level       compression_level_  = compression_level::fast;
        std::size_t            chunk_size_         = 256 * 1024;
        std::optional<size_t>  bandwidth_limit_;
        transport_type         transport_type_     = transport_type::tcp;
    };

    // 공개 인터페이스
    [[nodiscard]] auto send_file(
        const std::filesystem::path& file_path,
        const endpoint& destination,
        const transfer_options& options = {}
    ) -> Result<transfer_handle>;

    [[nodiscard]] auto send_files(
        std::span<const std::filesystem::path> files,
        const endpoint& destination,
        const transfer_options& options = {}
    ) -> Result<batch_transfer_handle>;

    [[nodiscard]] auto cancel(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto pause(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto resume(const transfer_id& id) -> Result<void>;

    void on_progress(std::function<void(const transfer_progress&)> callback);

private:
    // 내부 컴포넌트
    std::unique_ptr<sender_pipeline>        pipeline_;
    std::unique_ptr<transport_interface>    transport_;
    std::unique_ptr<progress_tracker>       progress_tracker_;
    std::unique_ptr<resume_handler>         resume_handler_;

    // 설정
    file_sender_config                      config_;

    // 활성 전송
    std::unordered_map<transfer_id, transfer_context> active_transfers_;
    std::mutex                              transfers_mutex_;

    // 콜백
    std::function<void(const transfer_progress&)> progress_callback_;
};

} // namespace kcenon::file_transfer
```

#### 3.1.4 시퀀스 다이어그램: send_file()

```
┌──────────┐ ┌──────────────┐ ┌─────────────┐ ┌────────────┐ ┌─────────────┐ ┌───────────┐
│  클라이언트 │ │ file_sender  │ │chunk_manager│ │compression │ │  pipeline   │ │ transport │
└────┬─────┘ └──────┬───────┘ └──────┬──────┘ └─────┬──────┘ └──────┬──────┘ └─────┬─────┘
     │              │                │              │               │              │
     │ send_file()  │                │              │               │              │
     │─────────────>│                │              │               │              │
     │              │                │              │               │              │
     │              │ validate_file()│              │               │              │
     │              │───────────────>│              │               │              │
     │              │                │              │               │              │
     │              │ calc_sha256()  │              │               │              │
     │              │───────────────>│              │               │              │
     │              │                │              │               │              │
     │              │       connect()│              │               │              │
     │              │────────────────────────────────────────────────────────────>│
     │              │                │              │               │              │
     │              │send_transfer_request()        │               │              │
     │              │────────────────────────────────────────────────────────────>│
     │              │                │              │               │              │
     │              │      submit_file_to_pipeline()│               │              │
     │              │──────────────────────────────────────────────>│              │
     │              │                │              │               │              │
     │              │                │   [파이프라인 처리]          │              │
     │              │                │              │               │              │
     │              │                │  read_chunk()│               │              │
     │              │                │<─────────────┼───────────────│              │
     │              │                │              │               │              │
     │              │                │   compress() │               │              │
     │              │                │─────────────>│               │              │
     │              │                │              │               │              │
     │              │                │              │     send()    │              │
     │              │                │              │───────────────┼─────────────>│
     │              │                │              │               │              │
     │  progress()  │                │              │               │              │
     │<─────────────│                │              │               │              │
     │              │                │              │               │              │
     │              │                │  [모든 청크에 대해 반복]     │              │
     │              │                │              │               │              │
     │              │ verify_sha256()│              │               │              │
     │              │────────────────────────────────────────────────────────────>│
     │              │                │              │               │              │
     │   Result     │                │              │               │              │
     │<─────────────│                │              │               │              │
     │              │                │              │               │              │
```

---

### 3.2 file_receiver 컴포넌트

#### 3.2.1 책임
수신되는 파일 전송을 수신 대기하고 받은 데이터를 디스크에 기록합니다.

#### 3.2.2 SRS 추적성
| SRS 요구사항 | 설계 요소 |
|--------------|-----------|
| SRS-CORE-002 | 수신 처리 로직 |
| SRS-CHUNK-002 | chunk_assembler 통합 |
| SRS-PROGRESS-001 | on_progress() 콜백 |

#### 3.2.3 클래스 설계

```cpp
namespace kcenon::file_transfer {

class file_receiver {
public:
    class builder {
    public:
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_output_directory(const std::filesystem::path& dir);
        builder& with_bandwidth_limit(std::size_t bytes_per_second);
        builder& with_transport(transport_type type);
        [[nodiscard]] auto build() -> Result<file_receiver>;

    private:
        pipeline_config             pipeline_config_;
        std::filesystem::path       output_dir_;
        std::optional<std::size_t>  bandwidth_limit_;
        transport_type              transport_type_ = transport_type::tcp;
    };

    // 생명주기
    [[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;
    [[nodiscard]] auto stop() -> Result<void>;

    // 설정
    void set_output_directory(const std::filesystem::path& dir);

    // 콜백
    void on_transfer_request(std::function<bool(const transfer_request&)> callback);
    void on_progress(std::function<void(const transfer_progress&)> callback);
    void on_complete(std::function<void(const transfer_result&)> callback);

private:
    // 상태
    enum class receiver_state { stopped, starting, running, stopping };
    std::atomic<receiver_state>             state_{receiver_state::stopped};

    // 컴포넌트
    std::unique_ptr<receiver_pipeline>      pipeline_;
    std::unique_ptr<transport_interface>    transport_;
    std::unique_ptr<progress_tracker>       progress_tracker_;
    std::unique_ptr<resume_handler>         resume_handler_;

    // 설정
    file_receiver_config                    config_;
    std::filesystem::path                   output_dir_;

    // 활성 전송
    std::unordered_map<transfer_id, receive_context> active_transfers_;
    std::shared_mutex                       transfers_mutex_;

    // 콜백
    std::function<bool(const transfer_request&)>    request_callback_;
    std::function<void(const transfer_progress&)>   progress_callback_;
    std::function<void(const transfer_result&)>     complete_callback_;

    // 내부 메서드
    void handle_incoming_connection(connection_ptr conn);
    void process_transfer_request(const transfer_request& req, connection_ptr conn);
    void handle_chunk(const chunk& c, receive_context& ctx);
};

} // namespace kcenon::file_transfer
```

---

### 3.3 chunk_manager 컴포넌트

#### 3.3.1 책임
전송을 위해 파일을 청크로 분할하고, 수신 시 청크를 파일로 재조립합니다.

#### 3.3.2 SRS 추적성
| SRS 요구사항 | 설계 요소 |
|--------------|-----------|
| SRS-CHUNK-001 | chunk_splitter 클래스 |
| SRS-CHUNK-002 | chunk_assembler 클래스 |
| SRS-CHUNK-003 | CRC32 체크섬 계산 |
| SRS-CHUNK-004 | SHA-256 파일 해시 계산 |

#### 3.3.3 클래스 설계

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

    // 대용량 파일을 위한 스트리밍 SHA-256
    class sha256_stream {
    public:
        void update(std::span<const std::byte> data);
        [[nodiscard]] auto finalize() -> std::string;

    private:
        // 구현 세부사항 (예: OpenSSL 컨텍스트)
    };
};

} // namespace kcenon::file_transfer
```

---

### 3.4 compression_engine 컴포넌트

#### 3.4.1 책임
적응형 감지 기능이 포함된 LZ4 압축 및 해제를 제공합니다.

#### 3.4.2 SRS 추적성
| SRS 요구사항 | 설계 요소 |
|--------------|-----------|
| SRS-COMP-001 | lz4_engine::compress() |
| SRS-COMP-002 | lz4_engine::decompress() |
| SRS-COMP-003 | adaptive_compression::is_compressible() |
| SRS-COMP-004 | compression_mode 열거형 |
| SRS-COMP-005 | compression_statistics 구조체 |

#### 3.4.3 클래스 설계

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
    // 표준 LZ4 압축
    [[nodiscard]] static auto compress(
        std::span<const std::byte> input,
        std::span<std::byte> output
    ) -> Result<std::size_t>;

    // 높은 압축률을 위한 LZ4-HC
    [[nodiscard]] static auto compress_hc(
        std::span<const std::byte> input,
        std::span<std::byte> output,
        int level = 9  // 1-12
    ) -> Result<std::size_t>;

    // 해제 (두 모드 모두 동일)
    [[nodiscard]] static auto decompress(
        std::span<const std::byte> compressed,
        std::span<std::byte> output,
        std::size_t original_size
    ) -> Result<std::size_t>;

    // 버퍼 할당을 위한 최대 압축 크기 계산
    [[nodiscard]] static auto max_compressed_size(std::size_t input_size)
        -> std::size_t;
};

// 적응형 압축 감지
class adaptive_compression {
public:
    // 샘플을 사용한 빠른 압축 가능 여부 검사
    [[nodiscard]] static auto is_compressible(
        std::span<const std::byte> data,
        double threshold = 0.9  // 원본의 90% 미만이면 압축
    ) -> bool;

    // 파일 확장자로 압축 가능 여부 확인 (휴리스틱)
    [[nodiscard]] static auto is_likely_compressible(
        const std::filesystem::path& file
    ) -> bool;

private:
    // 이미 압축된 확장자
    static constexpr std::array<std::string_view, 10> compressed_extensions = {
        ".zip", ".gz", ".tar.gz", ".tgz", ".bz2",
        ".jpg", ".jpeg", ".png", ".mp4", ".mp3"
    };
};

// 통계 기능이 포함된 청크 압축기
class chunk_compressor {
public:
    explicit chunk_compressor(
        compression_mode mode = compression_mode::adaptive,
        compression_level level = compression_level::fast
    );

    // 청크 압축
    [[nodiscard]] auto compress(const chunk& input) -> Result<chunk>;

    // 청크 해제
    [[nodiscard]] auto decompress(const chunk& input) -> Result<chunk>;

    // 압축 통계 가져오기
    [[nodiscard]] auto get_statistics() const -> compression_statistics;

    // 통계 초기화
    void reset_statistics();

private:
    compression_mode    mode_;
    compression_level   level_;

    // 통계 (스레드 안전)
    mutable std::mutex  stats_mutex_;
    compression_statistics statistics_;

    // 압축용 내부 버퍼
    std::vector<std::byte> compression_buffer_;
};

// 압축 통계
struct compression_statistics {
    std::atomic<uint64_t> total_raw_bytes{0};
    std::atomic<uint64_t> total_compressed_bytes{0};
    std::atomic<uint64_t> chunks_compressed{0};
    std::atomic<uint64_t> chunks_skipped{0};
    std::atomic<uint64_t> compression_time_us{0};
    std::atomic<uint64_t> decompression_time_us{0};

    // 계산된 지표
    [[nodiscard]] auto compression_ratio() const -> double;
    [[nodiscard]] auto compression_speed_mbps() const -> double;
    [[nodiscard]] auto decompression_speed_mbps() const -> double;
};

} // namespace kcenon::file_transfer
```

---

### 3.5 파이프라인 컴포넌트

#### 3.5.1 책임
최대 처리량을 위한 다단계 병렬 처리를 구현합니다.

#### 3.5.2 SRS 추적성
| SRS 요구사항 | 설계 요소 |
|--------------|-----------|
| SRS-PIPE-001 | sender_pipeline 클래스 |
| SRS-PIPE-002 | receiver_pipeline 클래스 |
| SRS-PIPE-003 | 제한된 큐 구현 |
| SRS-PIPE-004 | pipeline_statistics 구조체 |

#### 3.5.3 클래스 설계

```cpp
namespace kcenon::file_transfer {

// typed_thread_pool을 위한 파이프라인 단계 유형
enum class pipeline_stage : uint8_t {
    io_read,        // 파일 읽기 작업 (I/O 바운드)
    chunk_process,  // 청크 조립/분해 (CPU 경량)
    compression,    // LZ4 압축/해제 (CPU 바운드)
    network,        // 네트워크 송신/수신 (I/O 바운드)
    io_write        // 파일 쓰기 작업 (I/O 바운드)
};

// 파이프라인 설정
struct pipeline_config {
    // 단계별 워커 수
    std::size_t io_read_workers      = 2;
    std::size_t chunk_workers        = 2;
    std::size_t compression_workers  = 4;  // CPU 바운드에 더 많이
    std::size_t network_workers      = 2;
    std::size_t io_write_workers     = 2;

    // 백프레셔를 위한 큐 크기
    std::size_t read_queue_size      = 16;
    std::size_t compress_queue_size  = 32;
    std::size_t send_queue_size      = 64;
    std::size_t decompress_queue_size = 32;
    std::size_t write_queue_size     = 16;

    // 하드웨어 기반 자동 감지
    [[nodiscard]] static auto auto_detect() -> pipeline_config;
};

// 파이프라인 작업 기본 클래스
template<pipeline_stage Stage>
class pipeline_job : public thread::typed_job_t<pipeline_stage> {
public:
    explicit pipeline_job(const std::string& name)
        : typed_job_t<pipeline_stage>(Stage, name) {}

    virtual void execute() = 0;

protected:
    // 지표
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point end_time_;
};

// 송신자 파이프라인
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

    // 처리를 위해 파일 제출
    [[nodiscard]] auto submit(
        const std::filesystem::path& file,
        const transfer_id& id,
        const transfer_options& options
    ) -> Result<void>;

    // 통계 가져오기
    [[nodiscard]] auto get_stats() const -> pipeline_statistics;
    [[nodiscard]] auto get_queue_depths() const -> queue_depth_info;

private:
    // 스레드 풀 (thread_system에서)
    std::unique_ptr<thread::typed_thread_pool<pipeline_stage>> thread_pool_;

    // 백프레셔가 있는 단계 간 큐
    bounded_queue<read_result>      read_queue_;
    bounded_queue<chunk>            chunk_queue_;
    bounded_queue<chunk>            compress_queue_;
    bounded_queue<chunk>            send_queue_;

    // 공유 컴포넌트
    std::shared_ptr<chunk_compressor>    compressor_;
    std::shared_ptr<transport_interface> transport_;

    // 설정
    pipeline_config config_;

    // 상태
    std::atomic<bool> running_{false};

    // 통계
    pipeline_statistics stats_;

    // 작업 구현
    class read_job;
    class chunk_job;
    class compress_job;
    class send_job;
};

// 수신자 파이프라인
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

    // 처리를 위해 수신된 청크 제출
    [[nodiscard]] auto submit_chunk(chunk c) -> Result<void>;

    // 통계 가져오기
    [[nodiscard]] auto get_stats() const -> pipeline_statistics;
    [[nodiscard]] auto get_queue_depths() const -> queue_depth_info;

private:
    // 스레드 풀
    std::unique_ptr<thread::typed_thread_pool<pipeline_stage>> thread_pool_;

    // 단계 간 큐
    bounded_queue<chunk>            recv_queue_;
    bounded_queue<chunk>            decompress_queue_;
    bounded_queue<chunk>            assemble_queue_;
    bounded_queue<chunk>            write_queue_;

    // 공유 컴포넌트
    std::shared_ptr<chunk_compressor>  compressor_;
    std::shared_ptr<chunk_assembler>   assembler_;

    // 설정
    pipeline_config config_;

    // 상태
    std::atomic<bool> running_{false};

    // 통계
    pipeline_statistics stats_;

    // 작업 구현
    class receive_job;
    class decompress_job;
    class assemble_job;
    class write_job;
};

// 백프레셔를 위한 제한된 큐
template<typename T>
class bounded_queue {
public:
    explicit bounded_queue(std::size_t max_size);

    // 공간이 생길 때까지 차단
    void push(T item);

    // 항목이 생길 때까지 차단
    [[nodiscard]] auto pop() -> T;

    // 비차단 변형
    [[nodiscard]] auto try_push(T item) -> bool;
    [[nodiscard]] auto try_pop() -> std::optional<T>;

    // 상태 조회
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

    // 병목 단계 식별
    [[nodiscard]] auto bottleneck_stage() const -> pipeline_stage;
};

// 큐 깊이 정보
struct queue_depth_info {
    std::size_t read_queue;
    std::size_t chunk_queue;
    std::size_t compress_queue;
    std::size_t send_queue;
    std::size_t recv_queue;
    std::size_t decompress_queue;
    std::size_t assemble_queue;
    std::size_t write_queue;
};

} // namespace kcenon::file_transfer
```

---

### 3.6 전송 컴포넌트

#### 3.6.1 책임
네트워크 전송 프로토콜(TCP, QUIC)을 추상화합니다.

#### 3.6.2 SRS 추적성
| SRS 요구사항 | 설계 요소 |
|--------------|-----------|
| SRS-TRANS-001 | transport_interface 클래스 |
| SRS-TRANS-002 | tcp_transport 클래스 |
| SRS-TRANS-003 | quic_transport 클래스 (Phase 2) |
| SRS-TRANS-004 | transport_factory의 폴백 로직 |

#### 3.6.3 클래스 설계

```cpp
namespace kcenon::file_transfer {

// 전송 유형
enum class transport_type {
    tcp,    // TCP + TLS 1.3 (기본값)
    quic    // QUIC (Phase 2)
};

// 추상 전송 인터페이스
class transport_interface {
public:
    virtual ~transport_interface() = default;

    // 연결 관리
    [[nodiscard]] virtual auto connect(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto disconnect() -> Result<void> = 0;
    [[nodiscard]] virtual auto is_connected() const -> bool = 0;

    // 데이터 전송
    [[nodiscard]] virtual auto send(std::span<const std::byte> data) -> Result<void> = 0;
    [[nodiscard]] virtual auto receive(std::span<std::byte> buffer) -> Result<std::size_t> = 0;

    // QUIC 전용 (TCP에서는 no-op)
    [[nodiscard]] virtual auto create_stream() -> Result<stream_id> {
        return stream_id{0};
    }
    [[nodiscard]] virtual auto close_stream(stream_id) -> Result<void> {
        return {};
    }

    // 서버 측
    [[nodiscard]] virtual auto listen(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto accept() -> Result<std::unique_ptr<transport_interface>> = 0;
};

// TCP 전송 설정
struct tcp_transport_config {
    bool        enable_tls      = true;
    bool        tcp_nodelay     = true;
    std::size_t send_buffer     = 256 * 1024;
    std::size_t recv_buffer     = 256 * 1024;
    duration    connect_timeout = std::chrono::seconds(10);
    duration    read_timeout    = std::chrono::seconds(30);
};

// TCP 전송 구현
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

    // 내부적으로 network_system 사용
    std::unique_ptr<network::tcp_socket> socket_;
    std::unique_ptr<network::tls_context> tls_context_;

    std::atomic<bool> connected_{false};
};

// QUIC 전송 설정 (Phase 2)
struct quic_transport_config {
    bool        enable_0rtt         = true;
    std::size_t max_streams         = 100;
    std::size_t initial_window      = 10 * 1024 * 1024;
    duration    idle_timeout        = std::chrono::seconds(30);
    bool        enable_migration    = true;
};

// QUIC 전송 구현 (Phase 2)
class quic_transport : public transport_interface {
public:
    explicit quic_transport(const quic_transport_config& config = {});

    [[nodiscard]] auto connect(const endpoint& ep) -> Result<void> override;
    [[nodiscard]] auto disconnect() -> Result<void> override;
    [[nodiscard]] auto is_connected() const -> bool override;

    [[nodiscard]] auto send(std::span<const std::byte> data) -> Result<void> override;
    [[nodiscard]] auto receive(std::span<std::byte> buffer) -> Result<std::size_t> override;

    [[nodiscard]] auto create_stream() -> Result<stream_id> override;
    [[nodiscard]] auto close_stream(stream_id id) -> Result<void> override;

    [[nodiscard]] auto listen(const endpoint& ep) -> Result<void> override;
    [[nodiscard]] auto accept() -> Result<std::unique_ptr<transport_interface>> override;

private:
    quic_transport_config config_;

    // network_system QUIC 지원 사용
    std::unique_ptr<network::quic_connection> connection_;
    std::unordered_map<stream_id, network::quic_stream> streams_;
};

// 폴백 지원이 있는 전송 팩토리
class transport_factory {
public:
    [[nodiscard]] static auto create(transport_type type)
        -> std::unique_ptr<transport_interface>;

    // 자동 폴백으로 생성 (QUIC -> TCP)
    [[nodiscard]] static auto create_with_fallback(
        const endpoint& ep,
        transport_type preferred = transport_type::quic
    ) -> Result<std::unique_ptr<transport_interface>>;
};

} // namespace kcenon::file_transfer
```

---

### 3.7 재개 핸들러 컴포넌트

#### 3.7.1 책임
재개 기능을 위한 전송 상태 영속성을 관리합니다.

#### 3.7.2 SRS 추적성
| SRS 요구사항 | 설계 요소 |
|--------------|-----------|
| SRS-RESUME-001 | transfer_state 구조체, 상태 영속화 |
| SRS-RESUME-002 | resume() 메서드 로직 |

#### 3.7.3 클래스 설계

```cpp
namespace kcenon::file_transfer {

// 영속화를 위한 전송 상태
struct transfer_state {
    transfer_id                             id;
    std::string                             file_path;
    uint64_t                                file_size;
    std::string                             sha256_hash;
    uint64_t                                chunks_completed;
    uint64_t                                chunks_total;
    std::vector<bool>                       chunk_bitmap;  // true = 수신됨
    compression_mode                        compression;
    std::chrono::system_clock::time_point   created_at;
    std::chrono::system_clock::time_point   last_update;

    // 직렬화
    [[nodiscard]] auto serialize() const -> std::vector<std::byte>;
    [[nodiscard]] static auto deserialize(std::span<const std::byte> data)
        -> Result<transfer_state>;
};

// 재개 핸들러
class resume_handler {
public:
    explicit resume_handler(const std::filesystem::path& state_dir);

    // 전송 상태 저장
    [[nodiscard]] auto save_state(const transfer_state& state) -> Result<void>;

    // 전송 상태 로드
    [[nodiscard]] auto load_state(const transfer_id& id)
        -> Result<transfer_state>;

    // 전송 상태 삭제 (완료 시)
    [[nodiscard]] auto delete_state(const transfer_id& id) -> Result<void>;

    // 재개 가능한 전송 목록
    [[nodiscard]] auto list_resumable() -> Result<std::vector<transfer_id>>;

    // 청크 완료 업데이트
    [[nodiscard]] auto mark_chunk_complete(
        const transfer_id& id,
        uint64_t chunk_index
    ) -> Result<void>;

    // 재개를 위한 누락된 청크 가져오기
    [[nodiscard]] auto get_missing_chunks(const transfer_id& id)
        -> Result<std::vector<uint64_t>>;

    // 상태 무결성 검증
    [[nodiscard]] auto validate_state(const transfer_state& state)
        -> Result<void>;

private:
    std::filesystem::path state_dir_;

    // 상태 파일 경로
    [[nodiscard]] auto state_file_path(const transfer_id& id) const
        -> std::filesystem::path;

    // 원자적 상태 업데이트
    [[nodiscard]] auto atomic_write(
        const std::filesystem::path& path,
        std::span<const std::byte> data
    ) -> Result<void>;
};

} // namespace kcenon::file_transfer
```

---

## 4. 데이터 설계

### 4.1 데이터 구조

#### 4.1.1 청크 데이터 구조

```cpp
// 청크 플래그
enum class chunk_flags : uint8_t {
    none            = 0x00,
    first_chunk     = 0x01,     // 파일의 첫 번째 청크
    last_chunk      = 0x02,     // 파일의 마지막 청크
    compressed      = 0x04,     // LZ4 압축됨
    encrypted       = 0x08      // TLS용 예약
};

// 비트 연산 활성화
constexpr chunk_flags operator|(chunk_flags a, chunk_flags b);
constexpr chunk_flags operator&(chunk_flags a, chunk_flags b);
constexpr bool has_flag(chunk_flags flags, chunk_flags test);

// 청크 헤더 (와이어 프로토콜용 고정 크기)
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

    // 직렬화
    [[nodiscard]] auto to_bytes() const -> std::array<std::byte, 56>;
    [[nodiscard]] static auto from_bytes(std::span<const std::byte, 56> data)
        -> chunk_header;
};

// 완전한 청크
struct chunk {
    chunk_header            header;
    std::vector<std::byte>  data;

    // 검증
    [[nodiscard]] auto is_valid() const -> bool;
    [[nodiscard]] auto verify_checksum() const -> bool;
};
```

#### 4.1.2 전송 데이터 구조

```cpp
// 전송 식별자 (UUID)
struct transfer_id {
    std::array<uint8_t, 16> bytes;

    [[nodiscard]] static auto generate() -> transfer_id;
    [[nodiscard]] auto to_string() const -> std::string;
    [[nodiscard]] static auto from_string(std::string_view str)
        -> Result<transfer_id>;

    auto operator<=>(const transfer_id&) const = default;
};

// 파일 메타데이터
struct file_metadata {
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    std::filesystem::perms  permissions;
    std::chrono::system_clock::time_point modified_time;
    bool                    compressible_hint;

    [[nodiscard]] auto serialize() const -> std::vector<std::byte>;
    [[nodiscard]] static auto deserialize(std::span<const std::byte> data)
        -> Result<file_metadata>;
};

// 전송 옵션
struct transfer_options {
    compression_mode            compression     = compression_mode::adaptive;
    compression_level           level           = compression_level::fast;
    std::size_t                 chunk_size      = 256 * 1024;
    bool                        verify_checksum = true;
    std::optional<std::size_t>  bandwidth_limit;
    std::optional<int>          priority;
};

// 전송 요청
struct transfer_request {
    transfer_id                     id;
    std::vector<file_metadata>      files;
    transfer_options                options;

    [[nodiscard]] auto serialize() const -> std::vector<std::byte>;
    [[nodiscard]] static auto deserialize(std::span<const std::byte> data)
        -> Result<transfer_request>;
};

// 전송 상태 열거형
enum class transfer_state_enum {
    pending,        // 시작 대기
    initializing,   // 연결 설정 중
    transferring,   // 활성 전송 중
    verifying,      // 무결성 검증 중
    completed,      // 성공적으로 완료
    failed,         // 전송 실패
    cancelled       // 사용자 취소
};

// 전송 진행 상황
struct transfer_progress {
    transfer_id         id;
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

// 전송 결과
struct transfer_result {
    transfer_id             id;
    std::filesystem::path   output_path;
    uint64_t                bytes_transferred;
    uint64_t                bytes_on_wire;
    bool                    verified;
    std::optional<error>    error;
    duration                elapsed_time;
    compression_statistics  compression_stats;
};
```

### 4.2 데이터 흐름

#### 4.2.1 송신자 데이터 흐름

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  디스크의   │────▶│   청크      │────▶│   압축된    │────▶│   네트워크  │
│    파일     │     │   버퍼      │     │    청크     │     │    버퍼     │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
                           │                   │                   │
                           ▼                   ▼                   ▼
                    ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
                    │ CRC32 계산  │     │ LZ4 압축    │     │ TCP/QUIC    │
                    │             │     │ (모드 활성화│     │   전송      │
                    │             │     │  시)        │     │             │
                    └─────────────┘     └─────────────┘     └─────────────┘

데이터 크기 (256KB 청크):
- 원본 청크:        262,144 바이트
- CRC32 후:         262,144 바이트 + 4 바이트 체크섬
- 압축 후:          ~130,000 바이트 (텍스트의 경우 일반적, ~50% 감소)
- 와이어 형식:      56 바이트 헤더 + 압축 데이터
```

#### 4.2.2 수신자 데이터 흐름

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   네트워크  │────▶│   해제된    │────▶│   재조립    │────▶│  디스크의   │
│    버퍼     │     │    청크     │     │    버퍼     │     │    파일     │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
       │                   │                   │                   │
       ▼                   ▼                   ▼                   ▼
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│ 헤더 파싱   │     │LZ4 해제     │     │ CRC32 검증  │     │ SHA256      │
│             │     │(압축 플래그 │     │             │     │ 검증        │
│             │     │ 설정 시)    │     │             │     │             │
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

    // 전송 제어
    transfer_request    = 0x10,
    transfer_accept     = 0x11,
    transfer_reject     = 0x12,
    transfer_cancel     = 0x13,

    // 데이터 전송
    chunk_data          = 0x20,
    chunk_ack           = 0x21,
    chunk_nack          = 0x22,  // 재전송 요청

    // 재개
    resume_request      = 0x30,
    resume_response     = 0x31,

    // 완료
    transfer_complete   = 0x40,
    transfer_verify     = 0x41,

    // 제어
    keepalive           = 0xF0,
    error               = 0xFF
};
```

#### 5.1.3 프로토콜 상태 머신

```
                    ┌─────────────────┐
                    │    연결 끊김    │
                    └────────┬────────┘
                             │ connect()
                             ▼
                    ┌─────────────────┐
                    │    연결 중      │
                    └────────┬────────┘
                             │ 핸드셰이크 완료
                             ▼
                    ┌─────────────────┐
          ┌────────│    연결됨       │────────┐
          │        └────────┬────────┘        │
          │                 │ transfer_request│
          │                 ▼                 │
          │        ┌─────────────────┐        │
          │ 오류   │    전송 중      │ 취소   │
          │        └────────┬────────┘        │
          │                 │ 완료            │
          │                 ▼                 │
          │        ┌─────────────────┐        │
          │        │    검증 중      │        │
          │        └────────┬────────┘        │
          │                 │ 검증됨          │
          │                 ▼                 │
          │        ┌─────────────────┐        │
          └───────▶│    완료됨       │◀───────┘
                   └─────────────────┘
                             │ disconnect()
                             ▼
                   ┌─────────────────┐
                   │   연결 끊김     │
                   └─────────────────┘
```

### 5.2 오류 코드

SRS 섹션 6.4에 따라 오류 코드는 **-700 ~ -799** 범위입니다:

```cpp
namespace kcenon::file_transfer::error {

// 전송 오류 (-700 ~ -719)
constexpr int transfer_init_failed      = -700;
constexpr int transfer_cancelled        = -701;
constexpr int transfer_timeout          = -702;
constexpr int transfer_rejected         = -703;
constexpr int transfer_already_exists   = -704;
constexpr int transfer_not_found        = -705;

// 청크 오류 (-720 ~ -739)
constexpr int chunk_checksum_error      = -720;
constexpr int chunk_sequence_error      = -721;
constexpr int chunk_size_error          = -722;
constexpr int file_hash_mismatch        = -723;
constexpr int chunk_timeout             = -724;
constexpr int chunk_duplicate           = -725;

// 파일 I/O 오류 (-740 ~ -759)
constexpr int file_read_error           = -740;
constexpr int file_write_error          = -741;
constexpr int file_permission_error     = -742;
constexpr int file_not_found            = -743;
constexpr int disk_full                 = -744;
constexpr int invalid_path              = -745;

// 재개 오류 (-760 ~ -779)
constexpr int resume_state_invalid      = -760;
constexpr int resume_file_changed       = -761;
constexpr int resume_state_corrupted    = -762;
constexpr int resume_not_supported      = -763;

// 압축 오류 (-780 ~ -789)
constexpr int compression_failed        = -780;
constexpr int decompression_failed      = -781;
constexpr int compression_buffer_error  = -782;
constexpr int invalid_compression_data  = -783;

// 설정 오류 (-790 ~ -799)
constexpr int config_invalid            = -790;
constexpr int config_chunk_size_error   = -791;
constexpr int config_transport_error    = -792;

// 도우미 함수
[[nodiscard]] auto error_message(int code) -> std::string_view;

} // namespace kcenon::file_transfer::error
```

---

## 6. 알고리즘 설계

### 6.1 적응형 압축 알고리즘

**SRS 추적**: SRS-COMP-003

```cpp
// 알고리즘: 데이터 청크의 압축 가치 여부 판단
bool adaptive_compression::is_compressible(
    std::span<const std::byte> data,
    double threshold
) {
    // 단계 1: 처음 1KB 샘플링 (또는 작은 청크의 경우 더 적게)
    const auto sample_size = std::min(data.size(), std::size_t{1024});
    auto sample = data.first(sample_size);

    // 단계 2: 압축 버퍼 할당
    auto max_size = lz4_engine::max_compressed_size(sample_size);
    std::vector<std::byte> compressed_buffer(max_size);

    // 단계 3: 샘플 압축 시도
    auto result = lz4_engine::compress(sample, compressed_buffer);
    if (!result) {
        // 압축 실패, 압축 불가로 가정
        return false;
    }

    auto compressed_size = result.value();

    // 단계 4: 크기 비교
    // 최소 (1-threshold) 감소가 있을 때만 압축
    // threshold=0.9인 경우, 압축된 크기가 원본의 90% 미만이면 압축
    return static_cast<double>(compressed_size) <
           static_cast<double>(sample_size) * threshold;
}
```

**복잡도**: O(sample_size) - 고정 샘플 크기에 대해 상수 시간
**지연시간**: < 100μs (SRS 요구사항 PERF-011에 따라)

### 6.2 청크 조립 알고리즘

**SRS 추적**: SRS-CHUNK-002

```cpp
// 알고리즘: 순서가 뒤바뀔 수 있는 청크로부터 파일 재조립
Result<void> chunk_assembler::process_chunk(const chunk& c) {
    std::unique_lock lock(contexts_mutex_);

    // 단계 1: 조립 컨텍스트 가져오기 또는 생성
    auto& ctx = get_or_create_context(c.header.transfer_id);
    std::lock_guard ctx_lock(ctx.mutex);

    // 단계 2: CRC32 체크섬 검증
    if (!verify_crc32(c)) {
        return error::chunk_checksum_error;
    }

    // 단계 3: 중복 확인
    if (ctx.received_chunks[c.header.chunk_index]) {
        // 중복 청크 - 무시 (멱등 연산)
        return {};
    }

    // 단계 4: 필요시 해제
    std::span<const std::byte> data_to_write = c.data;
    std::vector<std::byte> decompressed;

    if (has_flag(c.header.flags, chunk_flags::compressed)) {
        decompressed.resize(c.header.original_size);
        auto result = lz4_engine::decompress(
            c.data, decompressed, c.header.original_size);
        if (!result) {
            return error::decompression_failed;
        }
        data_to_write = decompressed;
    }

    // 단계 5: 올바른 오프셋에 파일 쓰기
    ctx.file.seekp(c.header.chunk_offset);
    ctx.file.write(
        reinterpret_cast<const char*>(data_to_write.data()),
        data_to_write.size()
    );

    // 단계 6: 청크 수신됨으로 표시
    ctx.received_chunks[c.header.chunk_index] = true;
    ctx.bytes_written += data_to_write.size();

    return {};
}
```

### 6.3 파이프라인 백프레셔 알고리즘

**SRS 추적**: SRS-PIPE-003

```cpp
// 백프레셔가 있는 제한된 큐
template<typename T>
void bounded_queue<T>::push(T item) {
    std::unique_lock lock(mutex_);

    // 공간이 생길 때까지 차단 (백프레셔)
    not_full_.wait(lock, [this] {
        return queue_.size() < max_size_;
    });

    queue_.push(std::move(item));

    lock.unlock();
    not_empty_.notify_one();
}

template<typename T>
T bounded_queue<T>::pop() {
    std::unique_lock lock(mutex_);

    // 항목이 생길 때까지 차단
    not_empty_.wait(lock, [this] {
        return !queue_.empty();
    });

    T item = std::move(queue_.front());
    queue_.pop();

    lock.unlock();
    not_full_.notify_one();

    return item;
}
```

**메모리 제한**: max_queue_size × chunk_size
**예시**: 16개 큐 항목 × 256KB = 큐당 4MB

### 6.4 전송 재개 알고리즘

**SRS 추적**: SRS-RESUME-002

```cpp
// 알고리즘: 중단된 전송 재개
Result<void> resume_transfer(
    const transfer_id& id,
    const endpoint& destination
) {
    // 단계 1: 영속화된 상태 로드
    auto state_result = resume_handler_->load_state(id);
    if (!state_result) {
        return state_result.error();
    }
    auto state = state_result.value();

    // 단계 2: 소스 파일이 여전히 존재하고 변경되지 않았는지 검증
    if (!std::filesystem::exists(state.file_path)) {
        return error::file_not_found;
    }

    auto current_hash = checksum::sha256_file(state.file_path);
    if (!current_hash || current_hash.value() != state.sha256_hash) {
        return error::resume_file_changed;
    }

    // 단계 3: 연결 및 재개 요청 전송
    auto connect_result = transport_->connect(destination);
    if (!connect_result) {
        return connect_result.error();
    }

    // 단계 4: 청크 비트맵과 함께 재개 요청 전송
    resume_request req{
        .transfer_id = id,
        .chunk_bitmap = state.chunk_bitmap
    };
    send_message(message_type::resume_request, req.serialize());

    // 단계 5: 누락된 청크와 함께 재개 응답 수신
    auto response = receive_resume_response();
    if (!response) {
        return response.error();
    }

    // 단계 6: 누락된 청크만 전송
    auto splitter = chunk_splitter(config_.chunk_config);
    auto iterator = splitter.split(state.file_path, id);

    while (iterator.has_next()) {
        auto chunk = iterator.next();
        if (!chunk) {
            return chunk.error();
        }

        // 이미 수신된 청크 건너뛰기
        if (state.chunk_bitmap[chunk->header.chunk_index]) {
            continue;
        }

        // 파이프라인을 통해 청크 전송
        pipeline_->submit_chunk(std::move(chunk.value()));
    }

    return {};
}
```

---

## 7. 보안 설계

### 7.1 TLS 설정

**SRS 추적**: SEC-001

```cpp
// TLS 1.3 설정
struct tls_config {
    // 최소 TLS 버전
    static constexpr int min_version = TLS1_3_VERSION;

    // 암호 스위트 (TLS 1.3 전용)
    static constexpr std::array<const char*, 3> cipher_suites = {
        "TLS_AES_256_GCM_SHA384",
        "TLS_CHACHA20_POLY1305_SHA256",
        "TLS_AES_128_GCM_SHA256"
    };

    // 인증서 검증 모드
    enum class verify_mode {
        none,           // 검증 없음 (테스트 전용)
        peer,           // 피어 인증서 검증
        fail_if_no_cert // 피어 인증서 필수 및 검증
    };

    verify_mode verification = verify_mode::peer;
    std::string cert_file;
    std::string key_file;
    std::string ca_file;
};
```

### 7.2 경로 순회 방지

**SRS 추적**: SEC-003

```cpp
// 디렉토리 순회 방지를 위한 출력 경로 검증
Result<std::filesystem::path> validate_output_path(
    const std::filesystem::path& base_dir,
    const std::string& filename
) {
    // 단계 1: 경로 순회 시도 확인
    if (filename.find("..") != std::string::npos) {
        return error::invalid_path;
    }

    // 단계 2: 절대 경로 확인
    std::filesystem::path requested_path(filename);
    if (requested_path.is_absolute()) {
        return error::invalid_path;
    }

    // 단계 3: 전체 경로 구성
    auto full_path = base_dir / filename;

    // 단계 4: 정규화하고 base_dir 아래에 있는지 확인
    auto canonical = std::filesystem::weakly_canonical(full_path);
    auto canonical_base = std::filesystem::weakly_canonical(base_dir);

    // 정규화된 경로가 기본 디렉토리로 시작하는지 확인
    auto [base_end, _] = std::mismatch(
        canonical_base.begin(), canonical_base.end(),
        canonical.begin()
    );

    if (base_end != canonical_base.end()) {
        return error::invalid_path;
    }

    return canonical;
}
```

---

## 8. 추적성 매트릭스

### 8.1 SRS에서 설계로 추적성

| SRS ID | SRS 설명 | 설계 컴포넌트 | 설계 요소 |
|--------|----------|---------------|-----------|
| SRS-CORE-001 | 단일 파일 송신 | file_sender | send_file() 메서드 |
| SRS-CORE-002 | 단일 파일 수신 | file_receiver | process_chunk() 메서드 |
| SRS-CORE-003 | 다중 파일 배치 | file_sender | send_files() 메서드 |
| SRS-CHUNK-001 | 파일 분할 | chunk_splitter | split() 메서드 |
| SRS-CHUNK-002 | 파일 조립 | chunk_assembler | process_chunk() 메서드 |
| SRS-CHUNK-003 | 청크 체크섬 | checksum | crc32() 메서드 |
| SRS-CHUNK-004 | 파일 해시 | checksum | sha256_file() 메서드 |
| SRS-COMP-001 | LZ4 압축 | lz4_engine | compress() 메서드 |
| SRS-COMP-002 | LZ4 해제 | lz4_engine | decompress() 메서드 |
| SRS-COMP-003 | 적응형 감지 | adaptive_compression | is_compressible() 메서드 |
| SRS-COMP-004 | 압축 모드 | compression_mode | 열거형 정의 |
| SRS-COMP-005 | 압축 통계 | compression_statistics | 구조체 정의 |
| SRS-PIPE-001 | 송신자 파이프라인 | sender_pipeline | 파이프라인 단계 |
| SRS-PIPE-002 | 수신자 파이프라인 | receiver_pipeline | 파이프라인 단계 |
| SRS-PIPE-003 | 백프레셔 | bounded_queue | push()/pop() 차단 |
| SRS-PIPE-004 | 파이프라인 통계 | pipeline_statistics | 구조체 정의 |
| SRS-RESUME-001 | 상태 영속화 | resume_handler | save_state() 메서드 |
| SRS-RESUME-002 | 전송 재개 | resume_handler | load_state() 메서드 |
| SRS-PROGRESS-001 | 진행 콜백 | progress_tracker | on_progress() 콜백 |
| SRS-PROGRESS-002 | 전송 상태 | transfer_state_enum | 열거형 정의 |
| SRS-CONCURRENT-001 | 다중 전송 | transfer_manager | 활성 전송 맵 |
| SRS-CONCURRENT-002 | 대역폭 조절 | bandwidth_limiter | 토큰 버킷 알고리즘 |
| SRS-TRANS-001 | 전송 추상화 | transport_interface | 추상 클래스 |
| SRS-TRANS-002 | TCP 전송 | tcp_transport | 구현 클래스 |
| SRS-TRANS-003 | QUIC 전송 | quic_transport | 구현 클래스 |
| SRS-TRANS-004 | 프로토콜 폴백 | transport_factory | create_with_fallback() |

### 8.2 설계에서 테스트로 추적성

| 설계 컴포넌트 | 테스트 카테고리 | 테스트 파일 |
|---------------|-----------------|-------------|
| chunk_splitter | 단위 | chunk_splitter_test.cpp |
| chunk_assembler | 단위 | chunk_assembler_test.cpp |
| lz4_engine | 단위 | lz4_engine_test.cpp |
| adaptive_compression | 단위 | adaptive_compression_test.cpp |
| bounded_queue | 단위 | bounded_queue_test.cpp |
| sender_pipeline | 통합 | sender_pipeline_test.cpp |
| receiver_pipeline | 통합 | receiver_pipeline_test.cpp |
| file_sender | 통합 | file_sender_test.cpp |
| file_receiver | 통합 | file_receiver_test.cpp |
| 종단간 전송 | E2E | transfer_e2e_test.cpp |
| 압축 처리량 | 벤치마크 | compression_benchmark.cpp |
| 파이프라인 처리량 | 벤치마크 | pipeline_benchmark.cpp |

---

## 9. 배포 고려사항

### 9.1 빌드 설정

```cmake
# CMakeLists.txt 발췌
cmake_minimum_required(VERSION 3.20)
project(file_trans_system VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 의존성
find_package(lz4 REQUIRED)
find_package(OpenSSL REQUIRED)

# 핵심 라이브러리
add_library(file_trans_system
    src/core/file_sender.cpp
    src/core/file_receiver.cpp
    src/core/transfer_manager.cpp
    src/chunk/chunk_splitter.cpp
    src/chunk/chunk_assembler.cpp
    src/chunk/checksum.cpp
    src/compression/lz4_engine.cpp
    src/compression/adaptive_compression.cpp
    src/compression/chunk_compressor.cpp
    src/pipeline/sender_pipeline.cpp
    src/pipeline/receiver_pipeline.cpp
    src/pipeline/bounded_queue.cpp
    src/transport/tcp_transport.cpp
    src/resume/resume_handler.cpp
)

target_link_libraries(file_trans_system
    PUBLIC
        common_system
        thread_system
        network_system
        container_system
    PRIVATE
        lz4::lz4
        OpenSSL::SSL
        OpenSSL::Crypto
)
```

### 9.2 플랫폼별 참고사항

| 플랫폼 | 고려사항 |
|--------|----------|
| Linux | 비동기 I/O에 io_uring 사용 (커널 5.1+) |
| macOS | 비동기 I/O에 dispatch_io 사용 |
| Windows | 비동기 I/O에 IOCP 사용 |

---

## 부록 A: 개정 이력

| 버전 | 날짜 | 작성자 | 설명 |
|------|------|--------|------|
| 1.0.0 | 2025-12-11 | kcenon@naver.com | 초기 SDS 작성 |

---

*문서 끝*
