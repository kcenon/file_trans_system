# 설정 가이드

**file_trans_system** 라이브러리의 완전한 설정 레퍼런스입니다.

## 목차

1. [빠른 시작](#빠른-시작)
2. [송신자 설정](#송신자-설정)
3. [수신자 설정](#수신자-설정)
4. [파이프라인 설정](#파이프라인-설정)
5. [압축 설정](#압축-설정)
6. [전송 설정](#전송-설정)
7. [성능 튜닝](#성능-튜닝)

---

## 빠른 시작

### 최소 설정

```cpp
// 기본값으로 송신자
auto sender = file_sender::builder().build();

// 출력 디렉토리와 함께 수신자
auto receiver = file_receiver::builder()
    .with_output_directory("/downloads")
    .build();
```

### 권장 설정

```cpp
// 송신자 - 대부분의 사용 사례에 최적화
auto sender = file_sender::builder()
    .with_compression(compression_mode::adaptive)
    .with_chunk_size(256 * 1024)  // 256KB
    .with_pipeline_config(pipeline_config::auto_detect())
    .build();

// 수신자 - 대부분의 사용 사례에 최적화
auto receiver = file_receiver::builder()
    .with_output_directory("/downloads")
    .with_pipeline_config(pipeline_config::auto_detect())
    .build();
```

---

## 송신자 설정

### Builder 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|-----|------|-------|------|
| `with_pipeline_config` | `pipeline_config` | 자동 감지 | 파이프라인 워커 및 큐 설정 |
| `with_compression` | `compression_mode` | `adaptive` | 압축 모드 |
| `with_compression_level` | `compression_level` | `fast` | LZ4 압축 수준 |
| `with_chunk_size` | `std::size_t` | 256KB | 청크 크기 (64KB-1MB) |
| `with_bandwidth_limit` | `std::size_t` | 0 (무제한) | 대역폭 제한 (바이트/초) |
| `with_transport` | `transport_type` | `tcp` | 전송 프로토콜 |

### 예제

#### 고처리량 설정

```cpp
auto sender = file_sender::builder()
    .with_compression(compression_mode::enabled)
    .with_compression_level(compression_level::fast)
    .with_chunk_size(512 * 1024)  // 처리량 향상을 위한 512KB
    .with_pipeline_config(pipeline_config{
        .io_read_workers = 4,
        .compression_workers = 8,
        .network_workers = 4,
        .send_queue_size = 128
    })
    .build();
```

#### 저메모리 설정

```cpp
auto sender = file_sender::builder()
    .with_chunk_size(64 * 1024)   // 최소 64KB
    .with_pipeline_config(pipeline_config{
        .io_read_workers = 1,
        .compression_workers = 2,
        .network_workers = 1,
        .read_queue_size = 4,
        .compress_queue_size = 8,
        .send_queue_size = 16
    })
    .build();
```

#### 대역폭 제한 설정

```cpp
auto sender = file_sender::builder()
    .with_bandwidth_limit(10 * 1024 * 1024)  // 10 MB/s
    .with_compression(compression_mode::enabled)  // 실효 처리량 극대화
    .build();
```

---

## 수신자 설정

### Builder 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|-----|------|-------|------|
| `with_pipeline_config` | `pipeline_config` | 자동 감지 | 파이프라인 설정 |
| `with_output_directory` | `std::filesystem::path` | 필수 | 출력 디렉토리 |
| `with_bandwidth_limit` | `std::size_t` | 0 (무제한) | 대역폭 제한 |
| `with_transport` | `transport_type` | `tcp` | 전송 프로토콜 |

### 예제

#### 표준 설정

```cpp
auto receiver = file_receiver::builder()
    .with_output_directory("/var/data/incoming")
    .with_pipeline_config(pipeline_config::auto_detect())
    .build();
```

#### 대용량 설정

```cpp
auto receiver = file_receiver::builder()
    .with_output_directory("/fast-ssd/incoming")
    .with_pipeline_config(pipeline_config{
        .network_workers = 4,
        .compression_workers = 8,
        .io_write_workers = 4,
        .write_queue_size = 32
    })
    .build();
```

---

## 파이프라인 설정

### 설정 구조체

```cpp
struct pipeline_config {
    // 스테이지별 워커 수
    std::size_t io_read_workers      = 2;   // 파일 읽기
    std::size_t chunk_workers        = 2;   // 청크 처리
    std::size_t compression_workers  = 4;   // LZ4 압축/해제
    std::size_t network_workers      = 2;   // 네트워크 I/O
    std::size_t io_write_workers     = 2;   // 파일 쓰기

    // 큐 크기 (백프레셔 제어)
    std::size_t read_queue_size      = 16;  // 대기 중인 읽기 청크
    std::size_t compress_queue_size  = 32;  // 대기 중인 압축
    std::size_t send_queue_size      = 64;  // 대기 중인 네트워크 전송
    std::size_t decompress_queue_size = 32; // 대기 중인 해제
    std::size_t write_queue_size     = 16;  // 대기 중인 파일 쓰기
};
```

### 자동 감지

```cpp
// 하드웨어 기반 자동 설정
auto config = pipeline_config::auto_detect();
```

자동 감지가 고려하는 항목:
- CPU 코어 수
- 가용 메모리
- 스토리지 타입 (SSD vs HDD 감지)

### 스테이지별 튜닝

#### I/O 읽기 스테이지

| 워커 수 | 사용 사례 |
|--------|----------|
| 1 | 단일 HDD |
| 2 | 단일 SSD (기본값) |
| 4 | NVMe SSD 또는 RAID |

```cpp
// NVMe 스토리지용
config.io_read_workers = 4;
config.read_queue_size = 32;
```

#### 압축 스테이지

| 워커 수 | 사용 사례 |
|--------|----------|
| 2 | 듀얼 코어 CPU |
| 4 | 쿼드 코어 CPU (기본값) |
| 8+ | 고코어 서버 |

```cpp
// 16코어 서버용
config.compression_workers = 12;
config.compress_queue_size = 64;
```

#### 네트워크 스테이지

| 워커 수 | 사용 사례 |
|--------|----------|
| 1 | 단일 연결 |
| 2 | 표준 사용 (기본값) |
| 4 | 고대역폭 네트워크 |

```cpp
// 10Gbps 네트워크용
config.network_workers = 4;
config.send_queue_size = 128;
```

### 메모리 계산

큐별 메모리 사용량:
```
큐 메모리 = queue_size × chunk_size
```

총 파이프라인 메모리 (송신자):
```
read_queue_size × chunk_size
+ compress_queue_size × chunk_size
+ send_queue_size × chunk_size
```

예제 (기본 설정, 256KB 청크):
```
16 × 256KB + 32 × 256KB + 64 × 256KB
= 4MB + 8MB + 16MB = 28MB
```

---

## 압축 설정

### 압축 모드

| 모드 | 설명 | 적합한 용도 |
|------|------|-----------|
| `disabled` | 압축 없음 | 이미 압축된 파일 (ZIP, 미디어) |
| `enabled` | 항상 압축 | 텍스트, 로그, 소스 코드 |
| `adaptive` | 자동 감지 | 혼합 콘텐츠 (기본값) |

### 압축 수준

| 수준 | 속도 | 비율 | 적합한 용도 |
|------|-----|------|-----------|
| `fast` | ~400 MB/s | ~2.1:1 | 대부분의 사용 사례 |
| `high_compression` | ~50 MB/s | ~2.7:1 | 보관용, 대역폭 제한 환경 |

### 적응형 압축 임계값

적응형 모드는 샘플이 >= 10% 감소를 달성하면 압축합니다.

```cpp
// 내부 로직
bool should_compress = compressed_sample_size < original_sample_size * 0.9;
```

### 파일 타입 휴리스틱

| 파일 확장자 | 기본 동작 |
|-----------|----------|
| `.txt`, `.log`, `.json`, `.xml` | 압축 |
| `.cpp`, `.h`, `.py`, `.java` | 압축 |
| `.csv`, `.html`, `.css`, `.js` | 압축 |
| `.zip`, `.gz`, `.tar.gz`, `.bz2` | 건너뜀 |
| `.jpg`, `.png`, `.mp4`, `.mp3` | 건너뜀 |
| `.exe`, `.dll`, `.so` | 테스트 (적응형) |

### 전송별 옵션

```cpp
// 특정 전송에 대해 전역 설정 재정의
transfer_options opts{
    .compression = compression_mode::enabled,
    .level = compression_level::high_compression
};

sender->send_file(path, endpoint, opts);
```

---

## 전송 설정

### TCP 전송 (기본값)

```cpp
struct tcp_transport_config {
    bool        enable_tls      = true;        // TLS 1.3
    bool        tcp_nodelay     = true;        // Nagle 알고리즘 비활성화
    std::size_t send_buffer     = 256 * 1024;  // 256KB
    std::size_t recv_buffer     = 256 * 1024;  // 256KB
    duration    connect_timeout = 10s;
    duration    read_timeout    = 30s;
};
```

#### TCP 튜닝 예제

```cpp
// 고지연 네트워크 (WAN)
tcp_transport_config config{
    .send_buffer = 1024 * 1024,   // 1MB
    .recv_buffer = 1024 * 1024,   // 1MB
    .connect_timeout = 30s,
    .read_timeout = 60s
};
```

```cpp
// 저지연 네트워크 (LAN)
tcp_transport_config config{
    .tcp_nodelay = true,          // 지연 최소화
    .send_buffer = 128 * 1024,    // 128KB
    .recv_buffer = 128 * 1024,    // 128KB
    .connect_timeout = 5s,
    .read_timeout = 15s
};
```

### QUIC 전송 (Phase 2)

```cpp
struct quic_transport_config {
    bool        enable_0rtt         = true;             // 빠른 재연결
    std::size_t max_streams         = 100;              // 동시 스트림
    std::size_t initial_window      = 10 * 1024 * 1024; // 10MB
    duration    idle_timeout        = 30s;
    bool        enable_migration    = true;             // IP 변경 지원
};
```

#### QUIC 사용 시기

| 조건 | 권장사항 |
|------|---------|
| 안정적인 네트워크 (LAN) | TCP |
| 높은 패킷 손실 (>0.5%) | QUIC |
| 모바일 네트워크 | QUIC |
| 다중 동시 전송 | QUIC |
| 방화벽 제한 (UDP 차단) | TCP |

---

## 성능 튜닝

### 처리량 최적화

#### 1. 청크 크기 증가

큰 청크는 청크당 오버헤드를 줄입니다.

```cpp
.with_chunk_size(512 * 1024)  // 256KB 대신 512KB
```

#### 2. 압축 워커 확장

압축이 종종 병목입니다.

```cpp
// CPU 집약적 워크로드용
config.compression_workers = std::thread::hardware_concurrency() - 2;
```

#### 3. 큐 크기 증가

더 많은 큐 깊이는 병렬성을 높입니다.

```cpp
config.send_queue_size = 128;  // 더 많은 인플라이트 청크
```

### 메모리 최적화

#### 1. 큐 크기 축소

```cpp
pipeline_config config{
    .read_queue_size = 4,
    .compress_queue_size = 8,
    .send_queue_size = 16
};
```

#### 2. 작은 청크 사용

```cpp
.with_chunk_size(64 * 1024)  // 64KB 최소
```

### 지연 최적화

#### 1. TCP_NODELAY 활성화

```cpp
tcp_transport_config config{
    .tcp_nodelay = true  // 기본값
};
```

#### 2. 큐 크기 축소

작은 큐는 새 데이터의 빠른 처리를 의미합니다.

```cpp
config.send_queue_size = 16;
```

### 네트워크 최적화

#### 고대역폭 네트워크

```cpp
// 10Gbps 네트워크
pipeline_config config{
    .network_workers = 4,
    .send_queue_size = 256
};

tcp_transport_config tcp_config{
    .send_buffer = 2 * 1024 * 1024,  // 2MB
    .recv_buffer = 2 * 1024 * 1024
};
```

#### 고지연 네트워크

```cpp
// 100ms 지연 WAN
pipeline_config config{
    .send_queue_size = 256  // 많은 인플라이트 청크
};

tcp_transport_config tcp_config{
    .send_buffer = 4 * 1024 * 1024,  // 4MB
    .read_timeout = 60s
};
```

---

## 설정 모범 사례

### 1. 자동 감지로 시작

```cpp
auto config = pipeline_config::auto_detect();
```

### 2. 튜닝 전 측정

```cpp
// 파이프라인 통계 가져오기
auto stats = sender->get_pipeline_stats();
auto bottleneck = stats.bottleneck_stage();

// 병목 스테이지 튜닝
```

### 3. 큐 깊이 모니터링

```cpp
auto depths = sender->get_queue_depths();
if (depths.compress_queue > config.compress_queue_size * 0.9) {
    // 압축이 병목 - 워커 추가
}
```

### 4. 설정 변경 테스트

```cpp
// 변경 전후 벤치마크
auto start = std::chrono::steady_clock::now();
sender->send_file(test_file, endpoint);
auto duration = std::chrono::steady_clock::now() - start;
```

---

## 설정 검증

설정은 빌드 시 검증됩니다:

```cpp
auto sender = file_sender::builder()
    .with_chunk_size(32 * 1024)  // 오류: 최소값 미만
    .build();

if (!sender) {
    // 오류 코드: -791 (config_chunk_size_error)
    std::cerr << sender.error().message() << "\n";
}
```

### 검증 규칙

| 매개변수 | 제약 |
|---------|------|
| chunk_size | 64KB <= size <= 1MB |
| *_workers | >= 1 |
| *_queue_size | >= 1 |
| bandwidth_limit | >= 0 (0 = 무제한) |

---

*최종 업데이트: 2025-12-11*
