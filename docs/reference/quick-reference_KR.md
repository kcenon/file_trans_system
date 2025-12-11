# 빠른 참조 카드

**file_trans_system**의 일반적인 작업을 위한 빠른 참조입니다.

**버전:** 0.2.0
**아키텍처:** 클라이언트-서버 모델

---

## 헤더 포함

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;
```

---

## 서버 작업

### 서버 생성

```cpp
// 최소
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .build();

// 옵션과 함께
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_max_connections(100)
    .with_max_file_size(10ULL * 1024 * 1024 * 1024)  // 10GB
    .with_storage_quota(1ULL * 1024 * 1024 * 1024 * 1024)  // 1TB
    .build();
```

### 시작/중지

```cpp
server->start(endpoint{"0.0.0.0", 19000});
// ... 실행 중 ...
server->stop();
```

### 서버 콜백

```cpp
// 업로드 검증
server->on_upload_request([](const upload_request& req) {
    return req.file_size < 1e9;  // 1GB 미만 수락
});

// 다운로드 검증
server->on_download_request([](const download_request& req) {
    return true;  // 모든 요청 허용
});

// 연결 이벤트
server->on_client_connected([](const client_info& info) {
    std::cout << "연결됨: " << info.address << "\n";
});

// 전송 이벤트
server->on_upload_complete([](const transfer_result& result) {
    std::cout << "업로드됨: " << result.filename << "\n";
});
```

### 서버 통계

```cpp
auto stats = server->get_statistics();
std::cout << "연결 수: " << stats.active_connections << "\n";
std::cout << "업로드 속도: " << stats.upload_throughput_mbps << " MB/s\n";
```

---

## 클라이언트 작업

### 클라이언트 생성

```cpp
// 최소
auto client = file_transfer_client::builder().build();

// 옵션과 함께
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::adaptive)
    .with_chunk_size(256 * 1024)
    .with_auto_reconnect(true)
    .build();
```

### 연결/연결 해제

```cpp
auto result = client->connect(endpoint{"192.168.1.100", 19000});
if (result) {
    std::cout << "서버에 연결됨\n";
}
// ... 작업 ...
client->disconnect();
```

### 파일 업로드

```cpp
auto result = client->upload_file("/local/file.dat", "file.dat");

if (result) {
    std::cout << "업로드 ID: " << result->id.to_string() << "\n";
} else {
    std::cerr << "오류: " << result.error().message() << "\n";
}
```

### 다중 파일 업로드

```cpp
std::vector<upload_entry> files = {
    {"/local/file1.dat", "file1.dat"},
    {"/local/file2.dat", "file2.dat"}
};

auto result = client->upload_files(files);
```

### 파일 다운로드

```cpp
auto result = client->download_file("remote.dat", "/local/remote.dat");

if (result) {
    std::cout << "다운로드됨: " << result->output_path << "\n";
}
```

### 파일 목록 조회

```cpp
auto result = client->list_files();

if (result) {
    for (const auto& file : *result) {
        std::cout << file.name << " (" << file.size << " 바이트)\n";
    }
}

// 패턴 사용
auto result = client->list_files("*.pdf");
```

### 진행 상황 콜백

```cpp
client->on_progress([](const transfer_progress& p) {
    double percent = 100.0 * p.bytes_transferred / p.total_bytes;
    std::cout << percent << "% - " << p.transfer_rate / 1e6 << " MB/s\n";
});
```

### 전송 제어

```cpp
client->pause(transfer_id);
client->resume(transfer_id);
client->cancel(transfer_id);
```

---

## 재연결

### 자동 재연결 활성화

```cpp
auto client = file_transfer_client::builder()
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy::exponential_backoff())
    .build();
```

### 재연결 정책

| 정책 | 초기 지연 | 최대 지연 | 시도 횟수 |
|------|----------|----------|-----------|
| `fast()` | 100ms | 5s | 5 |
| `exponential_backoff()` | 1s | 30s | 10 |
| `aggressive()` | 500ms | 60s | 20 |
| `persistent()` | 1s | 5분 | 무제한 |

### 사용자 정의 정책

```cpp
reconnect_policy policy{
    .initial_delay = 500ms,
    .max_delay = 30s,
    .multiplier = 1.5,
    .max_attempts = 15
};
```

### 재연결 콜백

```cpp
client->on_disconnected([](disconnect_reason reason) {
    std::cout << "연결 끊김: " << to_string(reason) << "\n";
});

client->on_reconnecting([](int attempt, duration delay) {
    std::cout << "재시도 " << attempt << " - " << delay.count() << "ms 후\n";
});

client->on_reconnected([]() {
    std::cout << "재연결됨!\n";
});
```

---

## 설정

### 압축 모드

| 모드 | 설명 |
|------|------|
| `compression_mode::disabled` | 압축 없음 |
| `compression_mode::enabled` | 항상 압축 |
| `compression_mode::adaptive` | 자동 감지 (기본값) |

### 압축 수준

| 수준 | 속도 | 비율 |
|------|-----|------|
| `compression_level::fast` | ~400 MB/s | ~2.1:1 |
| `compression_level::high_compression` | ~50 MB/s | ~2.7:1 |

### 파이프라인 설정

```cpp
pipeline_config config{
    .io_read_workers = 2,
    .compression_workers = 4,
    .network_workers = 2,
    .send_queue_size = 64,
    .recv_queue_size = 64
};

// 또는 자동 감지
auto config = pipeline_config::auto_detect();
```

### 업로드 옵션

```cpp
upload_options opts{
    .compression = compression_mode::enabled,
    .level = compression_level::fast,
    .verify_checksum = true
};

client->upload_file(local, remote, opts);
```

### 다운로드 옵션

```cpp
download_options opts{
    .verify_checksum = true,
    .overwrite = false
};

client->download_file(remote, local, opts);
```

---

## 오류 처리

### 결과 확인

```cpp
auto result = client->upload_file(path, name);
if (!result) {
    switch (result.error().code()) {
        case error::connection_failed:
            // 연결 오류 처리
            break;
        case error::storage_full:
            // 서버 저장소 부족 처리
            break;
        case error::file_not_found:
            // 파일 없음 처리
            break;
        default:
            std::cerr << result.error().message() << "\n";
    }
}
```

### 일반 오류 코드

| 코드 | 이름 | 설명 |
|------|------|------|
| -700 | `connection_failed` | 서버 연결 실패 |
| -703 | `connection_lost` | 연결 끊김 |
| -704 | `reconnect_failed` | 자동 재연결 실패 |
| -710 | `transfer_init_failed` | 전송 설정 실패 |
| -712 | `transfer_timeout` | 전송 시간 초과 |
| -713 | `upload_rejected` | 서버가 업로드 거부 |
| -714 | `download_rejected` | 서버가 다운로드 거부 |
| -720 | `chunk_checksum_error` | 데이터 손상 |
| -744 | `file_already_exists` | 서버에 파일 이미 존재 |
| -745 | `storage_full` | 서버 저장소 할당량 초과 |
| -746 | `file_not_found_on_server` | 서버에 파일 없음 |
| -750 | `file_not_found` | 로컬 파일 없음 |

---

## 청크 설정

### 청크 크기 제한

| 제한 | 값 |
|------|-----|
| 최소 | 64 KB |
| 기본값 | 256 KB |
| 최대 | 1 MB |

### 청크 수 계산

```cpp
uint64_t file_size = 1024 * 1024 * 1024;  // 1 GB
uint64_t chunk_size = 256 * 1024;          // 256 KB
uint64_t num_chunks = (file_size + chunk_size - 1) / chunk_size;
// num_chunks = 4096
```

---

## 성능 목표

| 지표 | 목표 |
|------|------|
| LAN 처리량 | >= 500 MB/s |
| WAN 처리량 | >= 100 MB/s |
| LZ4 압축 | >= 400 MB/s |
| LZ4 해제 | >= 1.5 GB/s |
| 메모리 기준 | < 50 MB |
| 동시 클라이언트 | >= 100 |

---

## 전송 타입

```cpp
// TCP (기본값)
.with_transport(transport_type::tcp)

// QUIC (Phase 2)
.with_transport(transport_type::quic)
```

### QUIC 사용 시기

- 높은 패킷 손실 (>0.5%)
- 모바일 네트워크
- 빈번한 IP 변경
- 다중 동시 전송

---

## 연결 상태

### 클라이언트 연결

```
disconnected → connecting → connected → disconnected
                   ↓             ↓
             reconnecting ←──────┘
                   ↓
            reconnect_failed
```

### 전송 상태

```
pending → initializing → transferring → verifying → completed
                ↓              ↓
            failed ←───────────┘
                ↑
          cancelled
```

---

## 메모리 추정

### 클라이언트 메모리 (업로드)

```
(read_queue + compress_queue + send_queue) × chunk_size
= (16 + 32 + 64) × 256KB
= 28 MB
```

### 클라이언트 메모리 (다운로드)

```
(recv_queue + decompress_queue + write_queue) × chunk_size
= (64 + 32 + 16) × 256KB
= 28 MB
```

### 서버 메모리 (연결당)

```
~56 MB (양방향 파이프라인 버퍼)
```

---

## 의존성

| 시스템 | 필수 |
|--------|------|
| common_system | 예 |
| thread_system | 예 |
| network_system | 예 |
| container_system | 예 |
| LZ4 | 예 |
| logger_system | 선택 |
| monitoring_system | 선택 |

---

## 네임스페이스

```cpp
namespace kcenon::file_transfer {
    // 핵심 클래스
    class file_transfer_server;
    class file_transfer_client;

    // 열거형
    enum class compression_mode;
    enum class compression_level;
    enum class transport_type;
    enum class transfer_state;
    enum class disconnect_reason;

    // 서버 타입
    struct upload_request;
    struct download_request;
    struct client_info;
    struct storage_status;

    // 클라이언트 타입
    struct reconnect_policy;
    struct server_info;

    // 공유 타입
    struct transfer_progress;
    struct transfer_result;
    struct file_info;
    struct pipeline_config;
    struct compression_statistics;

    // 옵션
    struct upload_options;
    struct download_options;
}
```

---

*file_trans_system v0.2.0 | 최종 업데이트: 2025-12-11*
