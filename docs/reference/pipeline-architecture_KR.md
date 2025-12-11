# 파이프라인 아키텍처 가이드

**file_trans_system**의 다단계 파이프라인 아키텍처에 대한 상세 가이드입니다.

## 개요

file_trans_system은 **파이프라인 아키텍처**를 사용하여 다양한 스테이지의 병렬 처리를 통해 처리량을 극대화합니다. 이 설계는 I/O 바운드 및 CPU 바운드 작업이 서로 차단하지 않고 동시에 실행될 수 있게 합니다.

---

## 아키텍처 다이어그램

### 송신자 파이프라인

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           송신자 파이프라인                                   │
│                                                                              │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐│
│  │  파일 읽기   │───▶│    청크     │───▶│     LZ4      │───▶│   네트워크   ││
│  │    스테이지   │    │   조립      │    │    압축      │    │    전송      ││
│  │  (io_read)   │    │(chunk_process)│   │(compression) │    │  (network)   ││
│  └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘│
│        │                   │                   │                   │          │
│        ▼                   ▼                   ▼                   ▼          │
│  ┌──────────────────────────────────────────────────────────────────────────┐│
│  │                    typed_thread_pool<pipeline_stage>                      ││
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   ││
│  │  │  IO      │  │  IO      │  │  Compute │  │  Compute │  │ Network  │   ││
│  │  │ Worker 1 │  │ Worker 2 │  │ Worker 1 │  │ Worker 2 │  │ Worker 1 │   ││
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘   ││
│  └──────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  스테이지 큐 (백프레셔):                                                      │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐            │
│  │ read_queue │─▶│chunk_queue │─▶│ comp_queue │─▶│ send_queue │            │
│  │   (16)     │  │   (16)     │  │   (32)     │  │   (64)     │            │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘            │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 수신자 파이프라인

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           수신자 파이프라인                                   │
│                                                                              │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐│
│  │   네트워크   │───▶│     LZ4      │───▶│    청크     │───▶│  파일 쓰기   ││
│  │    수신      │    │  압축 해제   │    │   조립      │    │    스테이지   ││
│  │  (network)   │    │(compression) │    │(chunk_process)│   │  (io_write)  ││
│  └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘│
│        │                   │                   │                   │          │
│        ▼                   ▼                   ▼                   ▼          │
│  ┌──────────────────────────────────────────────────────────────────────────┐│
│  │                    typed_thread_pool<pipeline_stage>                      ││
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   ││
│  │  │ Network  │  │  Compute │  │  Compute │  │  IO      │  │  IO      │   ││
│  │  │ Worker 1 │  │ Worker 1 │  │ Worker 2 │  │ Worker 1 │  │ Worker 2 │   ││
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘   ││
│  └──────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  스테이지 큐 (백프레셔):                                                      │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐            │
│  │ recv_queue │─▶│decomp_queue│─▶│assem_queue │─▶│write_queue │            │
│  │   (64)     │  │   (32)     │  │   (16)     │  │   (16)     │            │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘            │
└─────────────────────────────────────────────────────────────────────────────┘
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

---

## 파이프라인 처리 방식

### 데이터 흐름 (송신자)

```
1. 파일 읽기 스테이지
   - 파일에서 chunk_size 바이트 읽기
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
   - 전송 계층으로 전송
   - ACK 대기
```

### 데이터 흐름 (수신자)

```
1. 네트워크 수신 스테이지
   - 전송 계층에서 수신
   - 청크 헤더 파싱
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
   - 올바른 오프셋에 파일 쓰기
   - 진행 상황 업데이트
```

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

#### 느린 네트워크 (네트워크 스테이지 병목)

```
read_queue:     [■■■■■■■■■■■■■■■■] FULL
compress_queue: [■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■] FULL
send_queue:     [■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■] FULL

→ 파일 읽기 스테이지가 BLOCKS
→ 메모리 사용량이 제한됨
→ 메모리 폭주 방지
```

#### 느린 압축 (압축 스테이지 병목)

```
read_queue:     [■■■■■■■■■■■■■■■■] FULL
compress_queue: [■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■] 처리 중...
send_queue:     [■■■□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□□] 대기 중

→ 네트워크 스테이지가 여유 있음
→ 압축 워커 추가 필요
```

### 메모리 경계

메모리 사용량은 다음에 의해 제한됩니다:

```
max_memory = Σ (queue_size × chunk_size) 모든 큐에 대해
```

기본 설정 (256KB 청크):
```
송신자:   (16 + 16 + 32 + 64) × 256KB = 32MB
수신자:   (64 + 32 + 16 + 16) × 256KB = 32MB
총합:     최대 64MB
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

// 구체적인 작업 예제
class compress_job : public pipeline_job<pipeline_stage::compression> {
public:
    compress_job(chunk c, bounded_queue<chunk>& output_queue)
        : pipeline_job("compress_job")
        , chunk_(std::move(c))
        , output_queue_(output_queue)
    {}

    void execute() override {
        auto compressed = compressor_.compress(chunk_);
        output_queue_.push(std::move(compressed));
    }

private:
    chunk chunk_;
    bounded_queue<chunk>& output_queue_;
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

    [[nodiscard]] auto bottleneck_stage() const -> pipeline_stage;
};
```

### 모니터링 예제

```cpp
// 현재 통계 가져오기
auto stats = sender->get_pipeline_stats();

// 스테이지 성능 출력
std::cout << "스테이지 성능:\n";
std::cout << "  IO 읽기:     " << stats.io_read_stats.throughput_mbps() << " MB/s\n";
std::cout << "  압축:        " << stats.compression_stats.throughput_mbps() << " MB/s\n";
std::cout << "  네트워크:    " << stats.network_stats.throughput_mbps() << " MB/s\n";

// 병목 식별
auto bottleneck = stats.bottleneck_stage();
std::cout << "병목: " << stage_name(bottleneck) << "\n";

// 큐 깊이 가져오기
auto depths = sender->get_queue_depths();
std::cout << "큐 깊이:\n";
std::cout << "  읽기:    " << depths.read_queue << "\n";
std::cout << "  압축:    " << depths.compress_queue << "\n";
std::cout << "  전송:    " << depths.send_queue << "\n";
```

### 병목 감지

```cpp
auto bottleneck_stage() const -> pipeline_stage {
    // 처리량 대비 평균 지연 시간이 가장 높은 스테이지가
    // 병목을 나타냄

    double max_bottleneck_score = 0;
    pipeline_stage bottleneck = pipeline_stage::io_read;

    for (const auto& [stage, stats] : stage_stats_) {
        double score = stats.avg_latency_us() / stats.throughput_mbps();
        if (score > max_bottleneck_score) {
            max_bottleneck_score = score;
            bottleneck = stage;
        }
    }

    return bottleneck;
}
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
    .send_queue_size = 64
};
```

#### CPU 최적화 (압축 가능한 데이터용)

```cpp
pipeline_config config{
    .io_read_workers = 2,
    .chunk_workers = 2,
    .compression_workers = std::thread::hardware_concurrency(),
    .network_workers = 2,
    .io_write_workers = 2,

    .compress_queue_size = 64
};
```

#### 네트워크 최적화 (고대역폭 네트워크용)

```cpp
pipeline_config config{
    .io_read_workers = 4,
    .compression_workers = 4,
    .network_workers = 4,

    .send_queue_size = 128
};
```

#### 메모리 제한

```cpp
pipeline_config config{
    .io_read_workers = 1,
    .chunk_workers = 1,
    .compression_workers = 2,
    .network_workers = 1,
    .io_write_workers = 1,

    .read_queue_size = 4,
    .compress_queue_size = 8,
    .send_queue_size = 16
};
// 256KB 청크로 총 메모리: ~7MB
```

---

## 파이프라인 오류 처리

### 스테이지 오류 복구

각 스테이지는 독립적으로 오류를 처리합니다:

```cpp
void compress_job::execute() {
    auto result = compressor_.compress(chunk_);

    if (!result) {
        // 압축 실패 - 비압축으로 전달
        chunk_.header.flags &= ~chunk_flags::compressed;
        output_queue_.push(std::move(chunk_));
        return;
    }

    output_queue_.push(std::move(result.value()));
}
```

### 파이프라인 종료

정상 종료는 모든 인플라이트 데이터가 처리되도록 보장합니다:

```cpp
auto stop(bool wait_for_completion = true) -> Result<void> {
    if (wait_for_completion) {
        // 더 이상 입력 없음 신호
        read_queue_.close();

        // 큐 드레인 대기
        compress_queue_.wait_until_empty();
        send_queue_.wait_until_empty();
    }

    // 스레드 풀 중지
    thread_pool_->stop();

    return {};
}
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
auto stats = sender->get_stats();
// 변경하기 전에 실제 병목 식별
```

### 3. 한 번에 하나의 매개변수 튜닝

```cpp
// 압축이 병목인 경우:
config.compression_workers *= 2;
// 추가 변경 전에 재측정
```

### 4. 전체 시스템 고려

```cpp
// 스토리지, CPU, 네트워크가 시스템을 형성
// 하나를 최적화하면 병목이 다른 곳으로 이동할 수 있음
```

### 5. 대표적인 워크로드로 테스트

```cpp
// 텍스트 파일은 바이너리와 다른 특성
// 실제로 전송할 파일 타입으로 테스트
```

---

*최종 업데이트: 2025-12-11*
