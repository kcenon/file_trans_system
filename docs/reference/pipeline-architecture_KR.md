# 파이프라인 아키텍처 가이드

**file_trans_system**의 다단계 파이프라인 아키텍처에 대한 상세 가이드입니다.

**버전:** 0.2.0
**아키텍처:** 클라이언트-서버 모델

---

## 개요

file_trans_system은 **파이프라인 아키텍처**를 사용하여 다양한 스테이지의 병렬 처리를 통해 처리량을 극대화합니다. 서버와 클라이언트 모두 업로드와 다운로드 작업을 동시에 지원하는 양방향 파이프라인을 활용합니다.

### 핵심 개념

- **업로드 파이프라인**: 클라이언트가 파일 읽기 → 압축 → 전송 → 서버가 수신 → 압축 해제 → 쓰기
- **다운로드 파이프라인**: 서버가 파일 읽기 → 압축 → 전송 → 클라이언트가 수신 → 압축 해제 → 쓰기
- **양방향**: 서버와 클라이언트 모두 여러 개의 동시 업로드/다운로드 스트림 처리 가능
- **백프레셔**: 제한된 큐가 어떤 부하 조건에서도 메모리 오버플로우를 방지

---

## 아키텍처 다이어그램

### 업로드 파이프라인 (클라이언트 → 서버)

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                      업로드 파이프라인 (클라이언트 측)                            │
│                                                                                  │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐  │
│  │  파일 읽기   │───▶│    청크     │───▶│     LZ4      │───▶│   네트워크   │  │
│  │   스테이지   │    │    조립     │    │    압축      │    │    전송      │  │
│  │  (io_read)   │    │(chunk_process)│   │(compression) │    │  (network)   │  │
│  └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘  │
│        │                   │                   │                   │            │
│        ▼                   ▼                   ▼                   ▼            │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │                    typed_thread_pool<pipeline_stage>                      │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │  │
│  │  │  IO      │  │  IO      │  │  Compute │  │  Compute │  │ Network  │   │  │
│  │  │ Worker 1 │  │ Worker 2 │  │ Worker 1 │  │ Worker 2 │  │ Worker 1 │   │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                  │
│  스테이지 큐 (백프레셔):                                                         │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐               │
│  │ read_queue │─▶│chunk_queue │─▶│ comp_queue │─▶│ send_queue │───────────┐   │
│  │   (16)     │  │   (16)     │  │   (32)     │  │   (64)     │           │   │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘           │   │
└───────────────────────────────────────────────────────────────────────────│───┘
                                                                            │
                                       ════════════════════════════════════╪════
                                                    네트워크               │
                                       ════════════════════════════════════╪════
                                                                            │
┌───────────────────────────────────────────────────────────────────────────│───┐
│                      업로드 파이프라인 (서버 측)                          │   │
│                                                                           │   │
│  스테이지 큐 (백프레셔):                                                  │   │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐          │   │
│  │ recv_queue │◀─│decomp_queue│◀─│assem_queue │◀─│write_queue │◀─────────┘   │
│  │   (64)     │  │   (32)     │  │   (16)     │  │   (16)     │               │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘               │
│        │                   │                   │                   │          │
│        ▼                   ▼                   ▼                   ▼          │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐│
│  │   네트워크   │───▶│     LZ4      │───▶│    청크     │───▶│  파일 쓰기   ││
│  │    수신      │    │  압축 해제   │    │    조립     │    │   스테이지   ││
│  │  (network)   │    │(compression) │    │(chunk_process)│   │  (io_write)  ││
│  └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘│
│                                                                               │
│                           서버 스토리지                                       │
│  ┌─────────────────────────────────────────────────────────────────────────┐ │
│  │  /storage/files/uploaded_file.dat                                        │ │
│  └─────────────────────────────────────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────────────────────────────┘
```

### 다운로드 파이프라인 (서버 → 클라이언트)

```
┌───────────────────────────────────────────────────────────────────────────────┐
│                       다운로드 파이프라인 (서버 측)                            │
│                                                                                │
│                           서버 스토리지                                        │
│  ┌─────────────────────────────────────────────────────────────────────────┐  │
│  │  /storage/files/requested_file.dat                                       │  │
│  └─────────────────────────────────────────────────────────────────────────┘  │
│                                                                                │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐ │
│  │  파일 읽기   │───▶│    청크     │───▶│     LZ4      │───▶│   네트워크   │ │
│  │   스테이지   │    │    조립     │    │    압축      │    │    전송      │ │
│  │  (io_read)   │    │(chunk_process)│   │(compression) │    │  (network)   │ │
│  └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘ │
│        │                   │                   │                   │           │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐    │           │
│  │ read_queue │─▶│chunk_queue │─▶│ comp_queue │─▶│ send_queue │────┼──────┐    │
│  │   (16)     │  │   (16)     │  │   (32)     │  │   (64)     │    │      │    │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘    │      │    │
└────────────────────────────────────────────────────────────────────│──────│────┘
                                                                     │      │
                                       ══════════════════════════════╪══════╪════
                                                    네트워크          │      │
                                       ══════════════════════════════╪══════╪════
                                                                     │      │
┌────────────────────────────────────────────────────────────────────│──────│────┐
│                       다운로드 파이프라인 (클라이언트 측)           │      │    │
│                                                                    │      │    │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐   │      │    │
│  │ recv_queue │◀─│decomp_queue│◀─│assem_queue │◀─│write_queue │◀──┴──────┘    │
│  │   (64)     │  │   (32)     │  │   (16)     │  │   (16)     │               │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘               │
│        │                   │                   │                   │          │
│        ▼                   ▼                   ▼                   ▼          │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐│
│  │   네트워크   │───▶│     LZ4      │───▶│    청크     │───▶│  파일 쓰기   ││
│  │    수신      │    │  압축 해제   │    │    조립     │    │   스테이지   ││
│  │  (network)   │    │(compression) │    │(chunk_process)│   │  (io_write)  ││
│  └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘│
│                                                                               │
│                           클라이언트 로컬                                     │
│  ┌─────────────────────────────────────────────────────────────────────────┐ │
│  │  /downloads/requested_file.dat                                           │ │
│  └─────────────────────────────────────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────────────────────────────┘
```

---

## 파이프라인 스테이지

### 스테이지 타입

```cpp
enum class pipeline_stage : uint8_t {
    io_read,        // 파일 읽기 작업 (I/O 바운드)
    chunk_process,  // 청크 조립/분해 (CPU 경량)
    compression,    // LZ4 압축/해제 (CPU 바운드)
    network,        // 네트워크 송/수신 (I/O 바운드)
    io_write        // 파일 쓰기 작업 (I/O 바운드)
};
```

### 스테이지 특성

| 스테이지 | 타입 | 기본 워커 | 병목 요인 |
|---------|------|----------|----------|
| `io_read` | I/O 바운드 | 2 | 스토리지 속도 |
| `chunk_process` | CPU 경량 | 2 | 최소 |
| `compression` | CPU 바운드 | 4 | CPU 코어 |
| `network` | I/O 바운드 | 2 | 네트워크 대역폭 |
| `io_write` | I/O 바운드 | 2 | 스토리지 속도 |

### 역할별 파이프라인 방향

| 역할 | 작업 | 활성 스테이지 |
|-----|------|--------------|
| **클라이언트** | 업로드 | io_read → chunk → compress → network |
| **클라이언트** | 다운로드 | network → decompress → chunk → io_write |
| **서버** | 업로드 수신 | network → decompress → chunk → io_write |
| **서버** | 다운로드 전송 | io_read → chunk → compress → network |

---

## 파이프라인 처리 방식

### 업로드 흐름 (클라이언트 → 서버)

#### 클라이언트 측 (소스)

```
1. 파일 읽기 스테이지
   - 로컬 파일에서 chunk_size 바이트 읽기
   - 메타데이터(오프셋, 인덱스)와 함께 청크 생성
   - read_queue에 인큐

2. 청크 조립 스테이지
   - read_queue에서 디큐
   - CRC32 체크섬 계산
   - 청크 플래그 설정 (first/last)
   - compress_queue에 인큐

3. 압축 스테이지
   - compress_queue에서 디큐
   - 적응형 압축 검사 적용
   - LZ4로 압축 (유익한 경우)
   - compressed 플래그 설정
   - send_queue에 인큐

4. 네트워크 전송 스테이지
   - send_queue에서 디큐
   - 청크 헤더 + 데이터 직렬화
   - 서버로 전송
   - CHUNK_ACK 대기
```

#### 서버 측 (목적지)

```
1. 네트워크 수신 스테이지
   - 클라이언트 연결에서 수신
   - 청크 헤더 파싱
   - 클라이언트 세션 검증
   - decompress_queue에 인큐

2. 압축 해제 스테이지
   - decompress_queue에서 디큐
   - compressed 플래그 설정 시 압축 해제
   - CRC32 검증
   - assemble_queue에 인큐

3. 청크 조립 스테이지
   - assemble_queue에서 디큐
   - 순서가 맞지 않는 청크 처리
   - 수신된 청크 추적
   - 준비된 청크를 write_queue에 인큐

4. 파일 쓰기 스테이지
   - write_queue에서 디큐
   - 스토리지의 올바른 오프셋에 쓰기
   - 전송 진행 상황 업데이트
   - 완료 시: SHA-256 검증
```

### 다운로드 흐름 (서버 → 클라이언트)

다운로드 흐름은 서버가 스토리지에서 읽고 클라이언트가 로컬 파일시스템에 쓰는 반대 방향입니다.

---

## 서버 파이프라인 관리

### 다중 동시 연결

```
┌─────────────────────────────────────────────────────────────────┐
│                           서버                                   │
│                                                                  │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐ │
│  │   연결 1        │  │   연결 2        │  │   연결 N        │ │
│  │   (업로드)      │  │   (다운로드)    │  │   (업로드)      │ │
│  │   파이프라인    │  │   파이프라인    │  │   파이프라인    │ │
│  └────────┬────────┘  └────────┬────────┘  └────────┬────────┘ │
│           │                    │                    │           │
│           ▼                    ▼                    ▼           │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              공유 스레드 풀                               │  │
│  │  ┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐       │  │
│  │  │ Net 1 │ │ Net 2 │ │ Comp 1│ │ Comp 2│ │ IO 1  │ ...   │  │
│  │  └───────┘ └───────┘ └───────┘ └───────┘ └───────┘       │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              스토리지 관리자                              │  │
│  │  /storage/files/                                          │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### 연결 격리

각 클라이언트 연결은 독립적인 항목을 가집니다:
- 업로드/다운로드 진행 상황 추적
- 청크 재조립 상태
- 전송 체크포인트

공유 리소스:
- 스레드 풀 워커
- 스토리지 관리자
- 압축 컨텍스트 풀

---

## 백프레셔 메커니즘

### 백프레셔 작동 방식

제한된 큐가 자연스러운 흐름 제어를 생성합니다:

```cpp
template<typename T>
class bounded_queue {
    std::size_t max_size_;
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;

public:
    void push(T item) {
        std::unique_lock lock(mutex_);
        // 큐가 가득 차면 BLOCK
        not_full_.wait(lock, [this] {
            return queue_.size() < max_size_;
        });
        queue_.push(std::move(item));
        not_empty_.notify_one();
    }

    T pop() {
        std::unique_lock lock(mutex_);
        // 큐가 비어있으면 BLOCK
        not_empty_.wait(lock, [this] {
            return !queue_.empty();
        });
        T item = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return item;
    }
};
```

### 백프레셔 시나리오

#### 느린 네트워크 (업로드 병목)

```
클라이언트:
read_queue:     [■■■■■■■■■■■■■■■■] FULL
compress_queue: [■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■] FULL
send_queue:     [■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■] FULL

→ 파일 읽기 스테이지가 BLOCKS
→ 메모리 사용량이 제한됨
→ 메모리 폭주 방지
```

#### 느린 스토리지 (다운로드 병목)

```
클라이언트:
recv_queue:     [■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■] FULL
decompress_queue: [■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■] FULL
write_queue:    [■■■■■■■■■■■■■■■■] 천천히 처리 중...

→ 네트워크 수신 스테이지가 BLOCKS
→ 서버가 알아채고 전송 속도 감소
→ 시스템 전체 흐름 제어 달성
```

### 메모리 경계

메모리 사용량은 다음에 의해 제한됩니다:

```
max_memory = Σ (queue_size × chunk_size) 모든 큐에 대해
```

**클라이언트 메모리** (기본 설정, 256KB 청크):
```
업로드:   (16 + 16 + 32 + 64) × 256KB = 32MB
다운로드: (64 + 32 + 16 + 16) × 256KB = 32MB
양방향:   최대 64MB
```

**서버 메모리** (연결당):
```
업로드 수신:   (64 + 32 + 16 + 16) × 256KB = 32MB
다운로드 전송: (16 + 16 + 32 + 64) × 256KB = 32MB
연결당:        최대 ~64MB
```

---

## typed_thread_pool 통합

### 스테이지 기반 작업 라우팅

파이프라인은 thread_system의 `typed_thread_pool`을 사용하여 스테이지별 워커 풀을 구성합니다:

```cpp
// 스테이지 타입으로 타입화된 스레드 풀 생성
auto pool = std::make_unique<thread::typed_thread_pool<pipeline_stage>>();

// 스테이지별 워커 설정
pool->add_workers(pipeline_stage::io_read, config.io_read_workers);
pool->add_workers(pipeline_stage::compression, config.compression_workers);
pool->add_workers(pipeline_stage::network, config.network_workers);
pool->add_workers(pipeline_stage::io_write, config.io_write_workers);
```

### 작업 구현

```cpp
template<pipeline_stage Stage>
class pipeline_job : public thread::typed_job_t<pipeline_stage> {
public:
    explicit pipeline_job(const std::string& name)
        : typed_job_t<pipeline_stage>(Stage, name) {}

    virtual void execute() = 0;
};

// 업로드 압축 작업 (클라이언트 측)
class upload_compress_job : public pipeline_job<pipeline_stage::compression> {
public:
    upload_compress_job(chunk c, bounded_queue<chunk>& output_queue, transfer_id tid)
        : pipeline_job("upload_compress_job")
        , chunk_(std::move(c))
        , output_queue_(output_queue)
        , transfer_id_(tid)
    {}

    void execute() override {
        auto compressed = compressor_.compress(chunk_);
        output_queue_.push(std::move(compressed));
    }

private:
    chunk chunk_;
    bounded_queue<chunk>& output_queue_;
    transfer_id transfer_id_;
};

// 다운로드 쓰기 작업 (클라이언트 측)
class download_write_job : public pipeline_job<pipeline_stage::io_write> {
public:
    download_write_job(chunk c, std::filesystem::path output_path, file_writer& writer)
        : pipeline_job("download_write_job")
        , chunk_(std::move(c))
        , output_path_(std::move(output_path))
        , writer_(writer)
    {}

    void execute() override {
        writer_.write_at(chunk_.header.offset, chunk_.data);
    }

private:
    chunk chunk_;
    std::filesystem::path output_path_;
    file_writer& writer_;
};
```

---

## 파이프라인 통계

### 통계 구조체

```cpp
struct pipeline_statistics {
    struct stage_stats {
        std::atomic<uint64_t> jobs_processed{0};
        std::atomic<uint64_t> bytes_processed{0};
        std::atomic<uint64_t> total_latency_us{0};
        std::atomic<std::size_t> current_queue_depth{0};
        std::atomic<std::size_t> max_queue_depth{0};

        [[nodiscard]] auto avg_latency_us() const -> double {
            return jobs_processed > 0
                ? static_cast<double>(total_latency_us) / jobs_processed
                : 0.0;
        }

        [[nodiscard]] auto throughput_mbps() const -> double;
    };

    stage_stats io_read_stats;
    stage_stats chunk_stats;
    stage_stats compression_stats;
    stage_stats network_stats;
    stage_stats io_write_stats;

    // 방향별 통계
    uint64_t total_uploaded_bytes{0};
    uint64_t total_downloaded_bytes{0};

    [[nodiscard]] auto bottleneck_stage() const -> pipeline_stage;
};
```

### 모니터링 예제

```cpp
// 서버 통계
auto server_stats = server->get_pipeline_stats();

std::cout << "서버 파이프라인 성능:\n";
std::cout << "  네트워크 수신: " << server_stats.network_stats.throughput_mbps() << " MB/s\n";
std::cout << "  압축 해제:     " << server_stats.compression_stats.throughput_mbps() << " MB/s\n";
std::cout << "  파일 쓰기:     " << server_stats.io_write_stats.throughput_mbps() << " MB/s\n";
std::cout << "  총 업로드:     " << server_stats.total_uploaded_bytes / 1e9 << " GB\n";
std::cout << "  총 다운로드:   " << server_stats.total_downloaded_bytes / 1e9 << " GB\n";

// 클라이언트 통계
auto client_stats = client->get_pipeline_stats();

std::cout << "클라이언트 파이프라인 성능:\n";
std::cout << "  파일 읽기:     " << client_stats.io_read_stats.throughput_mbps() << " MB/s\n";
std::cout << "  압축:          " << client_stats.compression_stats.throughput_mbps() << " MB/s\n";
std::cout << "  네트워크 전송: " << client_stats.network_stats.throughput_mbps() << " MB/s\n";

// 병목 식별
auto bottleneck = client_stats.bottleneck_stage();
std::cout << "병목: " << stage_name(bottleneck) << "\n";
```

---

## 튜닝 가이드라인

### 병목 기반 튜닝

| 병목 | 증상 | 해결책 |
|------|------|--------|
| `io_read` | read_queue가 자주 비어있음, 높은 읽기 지연 | 더 빠른 스토리지, 더 많은 읽기 워커 |
| `compression` | compress_queue가 가득 참, CPU 100% | 더 많은 압축 워커, 또는 압축 비활성화 |
| `network` | send_queue가 가득 참, 네트워크 포화 | 더 높은 대역폭, 또는 압축 증가 |
| `io_write` | write_queue가 가득 참 | 더 빠른 스토리지, 더 많은 쓰기 워커 |

### 권장 설정

#### 균형 설정 (기본값)

```cpp
pipeline_config config{
    .io_read_workers = 2,
    .chunk_workers = 2,
    .compression_workers = 4,
    .network_workers = 2,
    .io_write_workers = 2,

    .read_queue_size = 16,
    .compress_queue_size = 32,
    .send_queue_size = 64,
    .recv_queue_size = 64,
    .decompress_queue_size = 32,
    .write_queue_size = 16
};
```

#### 고처리량 서버

```cpp
pipeline_config config{
    .io_read_workers = 4,
    .chunk_workers = 4,
    .compression_workers = 16,
    .network_workers = 8,
    .io_write_workers = 4,

    .send_queue_size = 128,
    .recv_queue_size = 128
};
```

#### 고처리량 클라이언트

```cpp
pipeline_config config{
    .io_read_workers = 4,
    .compression_workers = std::thread::hardware_concurrency(),
    .network_workers = 4,

    .compress_queue_size = 64,
    .send_queue_size = 128
};
```

#### 메모리 제한 클라이언트

```cpp
pipeline_config config{
    .io_read_workers = 1,
    .chunk_workers = 1,
    .compression_workers = 2,
    .network_workers = 1,
    .io_write_workers = 1,

    .read_queue_size = 4,
    .compress_queue_size = 8,
    .send_queue_size = 16,
    .recv_queue_size = 16,
    .decompress_queue_size = 8,
    .write_queue_size = 4
};
// 256KB 청크로 총 메모리: ~14MB
```

---

## 파이프라인 오류 처리

### 스테이지 오류 복구

각 스테이지는 독립적으로 오류를 처리합니다:

```cpp
void upload_compress_job::execute() {
    auto result = compressor_.compress(chunk_);

    if (!result) {
        // 압축 실패 - 비압축으로 전달
        chunk_.header.flags &= ~chunk_flags::compressed;
        output_queue_.push(std::move(chunk_));
        return;
    }

    output_queue_.push(std::move(result.value()));
}

void download_write_job::execute() {
    auto result = writer_.write_at(chunk_.header.offset, chunk_.data);

    if (!result) {
        // 쓰기 실패 - 전송 관리자에 오류 보고
        transfer_manager_.report_error(transfer_id_, result.error());
        return;
    }
}
```

### 파이프라인 종료

정상 종료는 모든 인플라이트 데이터가 처리되도록 보장합니다.

> **참고:** 파이프라인 소멸자는 순환 참조로 인한 메모리 누수를 방지하기 위해 항상 모든 작업 큐를 정리합니다. 작업(job)은 `shared_ptr<pipeline_context>`를 보유하고, 이는 다시 `shared_ptr<thread_pool>`을 보유합니다. 스레드 풀 종료 전에 큐를 정리하면 이 참조 순환이 끊어집니다.

```cpp
// 클라이언트 종료
auto client::disconnect(bool wait_for_completion) -> Result<void> {
    if (wait_for_completion) {
        // 활성 업로드 완료 대기
        upload_pipeline_->drain();

        // 활성 다운로드 완료 대기
        download_pipeline_->drain();
    }

    // 연결 닫기
    connection_->close();

    // 파이프라인 중지
    upload_pipeline_->stop();
    download_pipeline_->stop();

    return {};
}

// 서버 종료
auto server::stop(bool wait_for_completion) -> Result<void> {
    if (wait_for_completion) {
        // 모든 클라이언트 전송 완료 대기
        for (auto& [client_id, connection] : connections_) {
            connection.pipeline->drain();
        }
    }

    // 새 연결 수락 중지
    acceptor_->stop();

    // 모든 연결 닫기
    for (auto& [client_id, connection] : connections_) {
        connection.close();
    }

    // 스레드 풀 중지
    thread_pool_->stop();

    return {};
}
```

---

## 고급 주제

### 연결 인식 파이프라인

서버 파이프라인은 연결별 상태를 추적합니다:

```cpp
struct connection_pipeline {
    connection_id client_id;

    // 업로드 상태 (클라이언트 → 서버)
    std::map<transfer_id, upload_state> active_uploads;

    // 다운로드 상태 (서버 → 클라이언트)
    std::map<transfer_id, download_state> active_downloads;

    // 공유 큐
    bounded_queue<chunk> recv_queue;
    bounded_queue<chunk> send_queue;

    // 연결별 통계
    pipeline_statistics stats;
};
```

### 파이프라인 메트릭 통합

monitoring_system으로 메트릭 내보내기:

```cpp
class server_pipeline_metrics_exporter {
public:
    void export_to_monitoring() {
        auto stats = server_->get_pipeline_stats();

        // 서버 전체 메트릭
        monitoring::gauge("server.pipeline.network.recv_throughput_mbps",
            stats.network_stats.throughput_mbps());
        monitoring::gauge("server.pipeline.compression.throughput_mbps",
            stats.compression_stats.throughput_mbps());
        monitoring::gauge("server.pipeline.io_write.throughput_mbps",
            stats.io_write_stats.throughput_mbps());

        // 전송 메트릭
        monitoring::counter("server.total_uploaded_bytes",
            stats.total_uploaded_bytes);
        monitoring::counter("server.total_downloaded_bytes",
            stats.total_downloaded_bytes);

        // 연결 메트릭
        monitoring::gauge("server.active_connections",
            server_->get_statistics().active_connections);
    }
};
```

---

## 모범 사례

### 1. auto_detect()로 시작

```cpp
auto config = pipeline_config::auto_detect();
```

### 2. 튜닝 전 모니터링

```cpp
// 기본 설정으로 실행하고 관찰
auto stats = client->get_pipeline_stats();
auto bottleneck = stats.bottleneck_stage();
// 변경하기 전에 실제 병목 식별
```

### 3. 한 번에 하나의 매개변수 튜닝

```cpp
// 압축이 병목인 경우:
config.compression_workers *= 2;
// 추가 변경 전에 재측정
```

### 4. 양방향 모두 고려

```cpp
// 클라이언트는 업로드와 다운로드 모두에 리소스 필요
// 업로드 최적화가 다운로드에 영향을 주지 않도록 주의
```

### 5. 대표적인 워크로드로 테스트

```cpp
// 텍스트 파일은 바이너리와 다른 특성
// 실제로 전송할 파일 타입으로 테스트
// 업로드와 다운로드 시나리오 모두 테스트
```

---

*버전: 0.2.0*
*최종 업데이트: 2025-12-11*
