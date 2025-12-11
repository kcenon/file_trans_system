# 설정 가이드

**file_trans_system** 라이브러리의 완전한 설정 레퍼런스입니다.

**버전:** 0.2.0
**아키텍처:** 클라이언트-서버 모델

---

## 목차

1. [빠른 시작](#빠른-시작)
2. [서버 설정](#서버-설정)
3. [클라이언트 설정](#클라이언트-설정)
4. [저장소 설정](#저장소-설정)
5. [재연결 설정](#재연결-설정)
6. [파이프라인 설정](#파이프라인-설정)
7. [압축 설정](#압축-설정)
8. [전송 설정](#전송-설정)
9. [보안 설정](#보안-설정)
10. [성능 튜닝](#성능-튜닝)

---

## 빠른 시작

### 최소 설정

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

// 저장소 디렉토리가 있는 서버
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .build();

// 기본값으로 클라이언트
auto client = file_transfer_client::builder().build();
```

### 권장 설정

```cpp
// 서버 - 프로덕션 최적화
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_max_connections(100)
    .with_max_file_size(10ULL * 1024 * 1024 * 1024)  // 10GB
    .with_storage_quota(1ULL * 1024 * 1024 * 1024 * 1024)  // 1TB
    .with_pipeline_config(pipeline_config::auto_detect())
    .build();

// 클라이언트 - 프로덕션 최적화
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::adaptive)
    .with_chunk_size(256 * 1024)  // 256KB
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy::exponential_backoff())
    .with_pipeline_config(pipeline_config::auto_detect())
    .build();
```

---

## 서버 설정

### Builder 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|-----|------|-------|------|
| `with_storage_directory` | `std::filesystem::path` | **필수** | 파일 저장소 루트 디렉토리 |
| `with_max_connections` | `std::size_t` | 100 | 최대 동시 클라이언트 연결 |
| `with_max_file_size` | `uint64_t` | 10GB | 허용된 최대 파일 크기 |
| `with_storage_quota` | `uint64_t` | 0 (무제한) | 총 저장소 할당량 |
| `with_pipeline_config` | `pipeline_config` | 자동 감지 | 파이프라인 워커 설정 |
| `with_transport` | `transport_type` | `tcp` | 전송 프로토콜 |
| `with_connection_timeout` | `duration` | 30s | 연결 유휴 타임아웃 |
| `with_request_timeout` | `duration` | 10s | 요청 응답 타임아웃 |

### 서버 예제

#### 기본 서버

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/var/data/files")
    .build();

server->start(endpoint{"0.0.0.0", 19000});
```

#### 고용량 서버

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/fast-nvme/storage")
    .with_max_connections(500)
    .with_max_file_size(50ULL * 1024 * 1024 * 1024)  // 50GB
    .with_storage_quota(10ULL * 1024 * 1024 * 1024 * 1024)  // 10TB
    .with_pipeline_config(pipeline_config{
        .network_workers = 8,
        .compression_workers = 16,
        .io_write_workers = 8,
        .recv_queue_size = 256,
        .write_queue_size = 64
    })
    .build();
```

#### 제한된 서버

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/uploads")
    .with_max_connections(10)
    .with_max_file_size(100 * 1024 * 1024)  // 100MB 제한
    .with_storage_quota(10ULL * 1024 * 1024 * 1024)  // 총 10GB
    .with_connection_timeout(5min)
    .build();

// 사용자 정의 검증 콜백
server->on_upload_request([](const upload_request& req) {
    // 특정 확장자만 허용
    auto ext = std::filesystem::path(req.filename).extension();
    return ext == ".pdf" || ext == ".doc" || ext == ".txt";
});

server->on_download_request([](const download_request& req) {
    // 모든 다운로드 허용
    return true;
});
```

### 서버 콜백

```cpp
// 업로드 요청 검증
server->on_upload_request([](const upload_request& req) -> bool {
    // 파일 크기 검증
    if (req.file_size > 1e9) return false;  // 1GB 초과 거부

    // 파일명 검증
    if (req.filename.find("..") != std::string::npos) return false;

    return true;
});

// 다운로드 요청 검증
server->on_download_request([](const download_request& req) -> bool {
    // 접근 권한 확인 (사용자 정의 로직)
    return has_access(req.client_id, req.filename);
});

// 연결 이벤트
server->on_client_connected([](const client_info& info) {
    log_info("클라이언트 연결됨: {}", info.address);
});

server->on_client_disconnected([](const client_info& info, disconnect_reason reason) {
    log_info("클라이언트 연결 끊김: {} ({})", info.address, to_string(reason));
});

// 전송 이벤트
server->on_upload_complete([](const transfer_result& result) {
    log_info("업로드 완료: {} ({} 바이트)", result.filename, result.file_size);
});

server->on_download_complete([](const transfer_result& result) {
    log_info("다운로드 완료: {} → {}", result.filename, result.client_address);
});
```

---

## 클라이언트 설정

### Builder 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|-----|------|-------|------|
| `with_pipeline_config` | `pipeline_config` | 자동 감지 | 파이프라인 설정 |
| `with_compression` | `compression_mode` | `adaptive` | 압축 모드 |
| `with_compression_level` | `compression_level` | `fast` | LZ4 압축 수준 |
| `with_chunk_size` | `std::size_t` | 256KB | 청크 크기 (64KB-1MB) |
| `with_bandwidth_limit` | `std::size_t` | 0 (무제한) | 대역폭 제한 (바이트/초) |
| `with_transport` | `transport_type` | `tcp` | 전송 프로토콜 |
| `with_auto_reconnect` | `bool` | true | 자동 재연결 활성화 |
| `with_reconnect_policy` | `reconnect_policy` | exponential | 재연결 전략 |
| `with_connect_timeout` | `duration` | 10s | 연결 타임아웃 |
| `with_request_timeout` | `duration` | 30s | 요청 타임아웃 |

### 클라이언트 예제

#### 기본 클라이언트

```cpp
auto client = file_transfer_client::builder().build();

auto result = client->connect(endpoint{"192.168.1.100", 19000});
if (result) {
    client->upload_file("/local/report.pdf", "report.pdf");
}
```

#### 고처리량 클라이언트

```cpp
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::enabled)
    .with_compression_level(compression_level::fast)
    .with_chunk_size(512 * 1024)  // 512KB 청크
    .with_pipeline_config(pipeline_config{
        .io_read_workers = 4,
        .compression_workers = 8,
        .network_workers = 4,
        .send_queue_size = 128
    })
    .build();
```

#### 저메모리 클라이언트

```cpp
auto client = file_transfer_client::builder()
    .with_chunk_size(64 * 1024)  // 최소 64KB
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

#### 대역폭 제한 클라이언트

```cpp
auto client = file_transfer_client::builder()
    .with_bandwidth_limit(10 * 1024 * 1024)  // 10 MB/s
    .with_compression(compression_mode::enabled)  // 실효 처리량 극대화
    .build();
```

#### 모바일/불안정 네트워크 클라이언트

```cpp
auto client = file_transfer_client::builder()
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy{
        .initial_delay = std::chrono::milliseconds(500),
        .max_delay = std::chrono::seconds(30),
        .multiplier = 1.5,
        .max_attempts = 20
    })
    .with_chunk_size(64 * 1024)  // 빠른 복구를 위해 작은 청크
    .build();
```

### 클라이언트 콜백

```cpp
// 연결 이벤트
client->on_connected([](const server_info& info) {
    log_info("서버에 연결됨: {}", info.address);
});

client->on_disconnected([](disconnect_reason reason) {
    log_warning("연결 끊김: {}", to_string(reason));
});

client->on_reconnecting([](int attempt, duration delay) {
    log_info("재연결 중 (시도 {}, {}ms 대기)", attempt, delay.count());
});

client->on_reconnected([]() {
    log_info("재연결 성공");
});

// 전송 진행 상황
client->on_progress([](const transfer_progress& p) {
    double percent = 100.0 * p.bytes_transferred / p.total_bytes;
    log_info("{}% - {} MB/s", percent, p.transfer_rate / 1e6);
});
```

---

## 저장소 설정

서버 측 저장소 관리 설정입니다.

### 저장소 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|-----|------|-------|------|
| `with_storage_directory` | `path` | **필수** | 루트 저장소 경로 |
| `with_storage_quota` | `uint64_t` | 0 | 총 할당량 (0 = 무제한) |
| `with_max_file_size` | `uint64_t` | 10GB | 최대 단일 파일 크기 |
| `with_temp_directory` | `path` | storage/temp | 임시 업로드 디렉토리 |
| `with_filename_validator` | `function` | 기본값 | 사용자 정의 파일명 검증 |

### 저장소 구조

```
storage_directory/
├── files/           # 완료된 업로드
├── temp/            # 진행 중인 업로드
└── metadata/        # 파일 메타데이터 (선택)
```

### 사용자 정의 파일명 검증

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_filename_validator([](const std::string& filename) -> Result<void> {
        // 길이 확인
        if (filename.length() > 255) {
            return Error{error::invalid_filename, "파일명이 너무 김"};
        }

        // 잘못된 문자 확인
        static const std::string invalid = "<>:\"/\\|?*";
        if (filename.find_first_of(invalid) != std::string::npos) {
            return Error{error::invalid_filename, "파일명에 잘못된 문자 포함"};
        }

        // 경로 순회 확인
        if (filename.find("..") != std::string::npos) {
            return Error{error::invalid_filename, "경로 순회 허용 안됨"};
        }

        return {};
    })
    .build();
```

### 할당량 관리

```cpp
// 저장소 상태 확인
auto status = server->get_storage_status();
log_info("저장소: {} / {} 바이트 사용 ({:.1f}%)",
    status.used_bytes,
    status.quota_bytes,
    100.0 * status.used_bytes / status.quota_bytes);

// 저장소 이벤트 모니터링
server->on_storage_warning([](const storage_warning& warning) {
    if (warning.percent_used > 90) {
        log_warning("저장소 거의 가득 참: {}%", warning.percent_used);
    }
});

server->on_storage_full([]() {
    log_error("저장소 할당량 초과");
    // 정리 또는 알림 트리거
});
```

---

## 재연결 설정

클라이언트 자동 재연결 설정입니다.

### 재연결 정책 구조체

```cpp
struct reconnect_policy {
    duration    initial_delay = 1s;      // 첫 번째 재시도 지연
    duration    max_delay     = 30s;     // 최대 지연 상한
    double      multiplier    = 2.0;     // 지수 백오프 계수
    std::size_t max_attempts  = 10;      // 최대 재시도 횟수 (0 = 무한)
    bool        jitter        = true;    // 랜덤 지터 추가
};
```

### 사전 정의 정책

```cpp
// 빠른 재연결 (LAN)
auto policy = reconnect_policy::fast();
// initial_delay=100ms, max_delay=5s, multiplier=1.5, max_attempts=5

// 표준 재연결 (기본값)
auto policy = reconnect_policy::exponential_backoff();
// initial_delay=1s, max_delay=30s, multiplier=2.0, max_attempts=10

// 적극적 재연결 (모바일)
auto policy = reconnect_policy::aggressive();
// initial_delay=500ms, max_delay=60s, multiplier=1.5, max_attempts=20

// 영구 재연결 (필수 애플리케이션)
auto policy = reconnect_policy::persistent();
// initial_delay=1s, max_delay=5분, multiplier=2.0, max_attempts=0 (무한)
```

### 사용자 정의 정책 예제

```cpp
// 실시간 애플리케이션용 매우 적극적 재연결
auto client = file_transfer_client::builder()
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy{
        .initial_delay = std::chrono::milliseconds(100),
        .max_delay = std::chrono::seconds(2),
        .multiplier = 1.2,
        .max_attempts = 50,
        .jitter = true
    })
    .build();

// 배치 처리용 보수적 재연결
auto client = file_transfer_client::builder()
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy{
        .initial_delay = std::chrono::seconds(5),
        .max_delay = std::chrono::minutes(10),
        .multiplier = 2.5,
        .max_attempts = 5,
        .jitter = false
    })
    .build();
```

### 백오프 계산

```
delay(n) = min(initial_delay × multiplier^n, max_delay)

지터 포함:
delay(n) = delay(n) × (0.5 + random(0.0, 1.0))
```

예제 시퀀스 (기본 정책):
```
시도 1: 1초
시도 2: 2초
시도 3: 4초
시도 4: 8초
시도 5: 16초
시도 6: 30초 (상한)
시도 7: 30초 (상한)
...
```

---

## 파이프라인 설정

서버와 클라이언트 데이터 파이프라인의 공유 설정입니다.

### 설정 구조체

```cpp
struct pipeline_config {
    // 스테이지별 워커 수
    std::size_t io_read_workers      = 2;   // 파일 읽기 (업로드 소스)
    std::size_t chunk_workers        = 2;   // 청크 처리
    std::size_t compression_workers  = 4;   // LZ4 압축/해제
    std::size_t network_workers      = 2;   // 네트워크 I/O
    std::size_t io_write_workers     = 2;   // 파일 쓰기 (다운로드 대상)

    // 큐 크기 (백프레셔 제어)
    std::size_t read_queue_size      = 16;  // 대기 중인 읽기 청크
    std::size_t compress_queue_size  = 32;  // 대기 중인 압축
    std::size_t send_queue_size      = 64;  // 대기 중인 네트워크 전송
    std::size_t recv_queue_size      = 64;  // 대기 중인 네트워크 수신
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
- 네트워크 인터페이스 속도

### 파이프라인 방향

#### 업로드 파이프라인 (클라이언트 → 서버)

```
[클라이언트]                              [서버]
읽기 → 청크 → 압축 → 전송 ─────────────→ 수신 → 해제 → 쓰기
```

관련 워커:
- **클라이언트**: io_read_workers, compression_workers (압축), network_workers
- **서버**: network_workers, compression_workers (해제), io_write_workers

#### 다운로드 파이프라인 (서버 → 클라이언트)

```
[서버]                                    [클라이언트]
읽기 → 청크 → 압축 → 전송 ─────────────→ 수신 → 해제 → 쓰기
```

관련 워커:
- **서버**: io_read_workers, compression_workers (압축), network_workers
- **클라이언트**: network_workers, compression_workers (해제), io_write_workers

### 스테이지별 튜닝

#### I/O 스테이지

| 워커 수 | 사용 사례 |
|--------|----------|
| 1 | 단일 HDD |
| 2 | 단일 SSD (기본값) |
| 4 | NVMe SSD 또는 RAID |
| 8 | 고성능 스토리지 어레이 |

```cpp
// NVMe 스토리지용
config.io_read_workers = 4;
config.io_write_workers = 4;
config.read_queue_size = 32;
config.write_queue_size = 32;
```

#### 압축 스테이지

| 워커 수 | 사용 사례 |
|--------|----------|
| 2 | 듀얼 코어 CPU |
| 4 | 쿼드 코어 CPU (기본값) |
| 8 | 8코어 CPU |
| 16+ | 고코어 서버 |

```cpp
// 32코어 서버용
config.compression_workers = 24;
config.compress_queue_size = 128;
config.decompress_queue_size = 128;
```

#### 네트워크 스테이지

| 워커 수 | 사용 사례 |
|--------|----------|
| 1 | 단일 연결 |
| 2 | 표준 사용 (기본값) |
| 4 | 고대역폭 네트워크 |
| 8 | 10Gbps+ 네트워크 |

```cpp
// 10Gbps 네트워크용
config.network_workers = 8;
config.send_queue_size = 256;
config.recv_queue_size = 256;
```

### 메모리 계산

큐별 메모리 사용량:
```
큐 메모리 = queue_size × chunk_size
```

**업로드 메모리 (클라이언트)**:
```
read_queue_size × chunk_size
+ compress_queue_size × chunk_size
+ send_queue_size × chunk_size
```

**다운로드 메모리 (클라이언트)**:
```
recv_queue_size × chunk_size
+ decompress_queue_size × chunk_size
+ write_queue_size × chunk_size
```

**예제** (기본 설정, 256KB 청크):
```
업로드:  16 × 256KB + 32 × 256KB + 64 × 256KB = 28MB
다운로드: 64 × 256KB + 32 × 256KB + 16 × 256KB = 28MB
총합: 56MB (양방향)
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
// 특정 업로드에 대해 전역 설정 재정의
upload_options opts{
    .compression = compression_mode::enabled,
    .level = compression_level::high_compression
};

client->upload_file(local_path, remote_name, opts);

// 특정 다운로드에 대해 재정의
download_options opts{
    .verify_checksum = true,
    .overwrite = true
};

client->download_file(remote_name, local_path, opts);
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
    duration    write_timeout   = 30s;
    duration    keepalive       = 30s;         // TCP keepalive 간격
};
```

#### TCP 예제

```cpp
// 고지연 네트워크 (WAN)
auto client = file_transfer_client::builder()
    .with_transport_config(tcp_transport_config{
        .send_buffer = 1024 * 1024,   // 1MB
        .recv_buffer = 1024 * 1024,   // 1MB
        .connect_timeout = 30s,
        .read_timeout = 60s,
        .write_timeout = 60s
    })
    .build();
```

```cpp
// 저지연 네트워크 (LAN)
auto client = file_transfer_client::builder()
    .with_transport_config(tcp_transport_config{
        .tcp_nodelay = true,          // 지연 최소화
        .send_buffer = 128 * 1024,    // 128KB
        .recv_buffer = 128 * 1024,    // 128KB
        .connect_timeout = 5s,
        .read_timeout = 15s
    })
    .build();
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

## 보안 설정

### TLS 설정

```cpp
struct tls_config {
    std::filesystem::path certificate_path;    // 서버 인증서
    std::filesystem::path private_key_path;    // 서버 개인키
    std::filesystem::path ca_certificate_path; // CA 인증서 (선택)
    bool                  verify_peer = true;  // 클라이언트 인증서 확인
    tls_version           min_version = tls_version::tls_1_3;
};

// TLS가 있는 서버
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_tls(tls_config{
        .certificate_path = "/etc/ssl/server.crt",
        .private_key_path = "/etc/ssl/server.key",
        .ca_certificate_path = "/etc/ssl/ca.crt",
        .verify_peer = false  // 클라이언트 인증서 불필요
    })
    .build();
```

### 클라이언트 인증

```cpp
// 인증서 인증이 있는 클라이언트
auto client = file_transfer_client::builder()
    .with_tls(tls_config{
        .certificate_path = "/etc/ssl/client.crt",
        .private_key_path = "/etc/ssl/client.key",
        .ca_certificate_path = "/etc/ssl/ca.crt"
    })
    .build();
```

### 접근 제어

```cpp
// 서버 측 접근 제어
server->on_upload_request([&auth_service](const upload_request& req) {
    // 클라이언트 토큰 검증
    if (!auth_service.validate_token(req.auth_token)) {
        return false;
    }

    // 업로드 권한 확인
    return auth_service.can_upload(req.client_id, req.filename);
});

server->on_download_request([&auth_service](const download_request& req) {
    // 클라이언트 토큰 검증
    if (!auth_service.validate_token(req.auth_token)) {
        return false;
    }

    // 다운로드 권한 확인
    return auth_service.can_download(req.client_id, req.filename);
});
```

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
config.recv_queue_size = 128;
```

### 메모리 최적화

#### 1. 큐 크기 축소

```cpp
pipeline_config config{
    .read_queue_size = 4,
    .compress_queue_size = 8,
    .send_queue_size = 16,
    .recv_queue_size = 16,
    .decompress_queue_size = 8,
    .write_queue_size = 4
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
// 10Gbps 네트워크 (서버)
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_pipeline_config(pipeline_config{
        .network_workers = 8,
        .send_queue_size = 256,
        .recv_queue_size = 256
    })
    .with_transport_config(tcp_transport_config{
        .send_buffer = 2 * 1024 * 1024,  // 2MB
        .recv_buffer = 2 * 1024 * 1024
    })
    .build();
```

#### 고지연 네트워크

```cpp
// 100ms 지연 WAN (클라이언트)
auto client = file_transfer_client::builder()
    .with_pipeline_config(pipeline_config{
        .send_queue_size = 256,  // 많은 인플라이트 청크
        .recv_queue_size = 256
    })
    .with_transport_config(tcp_transport_config{
        .send_buffer = 4 * 1024 * 1024,  // 4MB
        .read_timeout = 60s
    })
    .build();
```

### 동시 전송

```cpp
// 많은 동시 클라이언트에 최적화된 서버
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_max_connections(500)
    .with_pipeline_config(pipeline_config{
        .network_workers = 16,      // 많은 연결 처리
        .compression_workers = 32,  // 병렬 압축
        .io_write_workers = 8       // 병렬 I/O
    })
    .build();
```

---

## 설정 모범 사례

### 1. 자동 감지로 시작

```cpp
auto config = pipeline_config::auto_detect();
```

### 2. 튜닝 전 측정

```cpp
// 서버 통계
auto stats = server->get_statistics();
log_info("활성 연결: {}", stats.active_connections);
log_info("업로드 처리량: {} MB/s", stats.upload_throughput_mbps);
log_info("다운로드 처리량: {} MB/s", stats.download_throughput_mbps);

// 클라이언트 통계
auto stats = client->get_statistics();
log_info("업로드 속도: {} MB/s", stats.current_upload_rate_mbps);
log_info("다운로드 속도: {} MB/s", stats.current_download_rate_mbps);
```

### 3. 파이프라인 병목 모니터링

```cpp
// 파이프라인 통계 가져오기
auto pipeline_stats = server->get_pipeline_stats();
auto bottleneck = pipeline_stats.bottleneck_stage();

log_info("병목 스테이지: {}", stage_name(bottleneck));

// 병목 스테이지 튜닝
if (bottleneck == pipeline_stage::compression) {
    config.compression_workers *= 2;
}
```

### 4. 큐 깊이 모니터링

```cpp
auto depths = server->get_queue_depths();
if (depths.compress_queue > config.compress_queue_size * 0.9) {
    log_warning("압축 큐 용량 근접 - 워커 추가 고려");
}
```

### 5. 설정 변경 테스트

```cpp
// 업로드 벤치마크
auto start = std::chrono::steady_clock::now();
auto result = client->upload_file(test_file, "benchmark.dat");
auto duration = std::chrono::steady_clock::now() - start;

if (result) {
    auto file_size = std::filesystem::file_size(test_file);
    auto throughput = file_size / duration.count();
    log_info("업로드 처리량: {} MB/s", throughput / 1e6);
}
```

---

## 설정 검증

설정은 빌드 시 검증됩니다:

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("")  // 오류: 빈 경로
    .build();

if (!server) {
    // 오류 코드: -790 (config_storage_dir_error)
    std::cerr << server.error().message() << "\n";
}

auto client = file_transfer_client::builder()
    .with_chunk_size(32 * 1024)  // 오류: 최소값 미만
    .build();

if (!client) {
    // 오류 코드: -791 (config_chunk_size_error)
    std::cerr << client.error().message() << "\n";
}
```

### 검증 규칙

| 매개변수 | 제약 |
|---------|------|
| storage_directory | 비어 있지 않음, 쓰기 가능 |
| max_connections | >= 1 |
| max_file_size | >= 1KB |
| chunk_size | 64KB <= size <= 1MB |
| *_workers | >= 1 |
| *_queue_size | >= 1 |
| bandwidth_limit | >= 0 (0 = 무제한) |
| reconnect max_attempts | >= 0 (0 = 무한) |
| initial_delay | > 0 |
| multiplier | > 1.0 |

---

*버전: 0.2.0*
*최종 업데이트: 2025-12-11*
