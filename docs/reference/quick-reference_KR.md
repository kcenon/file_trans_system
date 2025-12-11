# 빠른 참조 카드

**file_trans_system**의 일반적인 작업을 위한 빠른 참조입니다.

---

## 헤더 포함

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;
```

---

## 송신자 작업

### 송신자 생성

```cpp
// 최소
auto sender = file_sender::builder().build();

// 옵션과 함께
auto sender = file_sender::builder()
    .with_compression(compression_mode::adaptive)
    .with_chunk_size(256 * 1024)
    .with_bandwidth_limit(10 * 1024 * 1024)  // 10 MB/s
    .build();
```

### 파일 전송

```cpp
auto result = sender->send_file(
    "/path/to/file.dat",
    endpoint{"192.168.1.100", 19000}
);

if (result) {
    std::cout << "전송 ID: " << result->id.to_string() << "\n";
} else {
    std::cerr << "오류: " << result.error().message() << "\n";
}
```

### 다중 파일 전송

```cpp
std::vector<std::filesystem::path> files = {
    "/path/to/file1.dat",
    "/path/to/file2.dat"
};

auto result = sender->send_files(files, endpoint{"192.168.1.100", 19000});
```

### 진행 상황 콜백

```cpp
sender->on_progress([](const transfer_progress& p) {
    double percent = 100.0 * p.bytes_transferred / p.total_bytes;
    std::cout << percent << "% - " << p.transfer_rate / 1e6 << " MB/s\n";
});
```

### 전송 제어

```cpp
sender->pause(transfer_id);
sender->resume(transfer_id);
sender->cancel(transfer_id);
```

---

## 수신자 작업

### 수신자 생성

```cpp
auto receiver = file_receiver::builder()
    .with_output_directory("/downloads")
    .build();
```

### 시작/중지

```cpp
receiver->start(endpoint{"0.0.0.0", 19000});
// ... 수신 중 ...
receiver->stop();
```

### 콜백

```cpp
// 전송 수락/거부
receiver->on_transfer_request([](const transfer_request& req) {
    return req.files[0].file_size < 1e9;  // 1GB 미만 수락
});

// 진행 상황 업데이트
receiver->on_progress([](const transfer_progress& p) {
    std::cout << p.bytes_transferred << "/" << p.total_bytes << "\n";
});

// 완료
receiver->on_complete([](const transfer_result& result) {
    if (result.verified) {
        std::cout << "수신됨: " << result.output_path << "\n";
    }
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
    .send_queue_size = 64
};

// 또는 자동 감지
auto config = pipeline_config::auto_detect();
```

### 전송 옵션

```cpp
transfer_options opts{
    .compression = compression_mode::enabled,
    .level = compression_level::fast,
    .chunk_size = 512 * 1024,
    .verify_checksum = true,
    .bandwidth_limit = 10 * 1024 * 1024
};

sender->send_file(path, endpoint, opts);
```

---

## 통계

### 전송 통계

```cpp
auto stats = manager->get_statistics();
std::cout << "총 전송량: " << stats.total_bytes_transferred << "\n";
std::cout << "활성 전송: " << stats.active_transfer_count << "\n";
```

### 압축 통계

```cpp
auto stats = manager->get_compression_stats();
std::cout << "비율: " << stats.compression_ratio() << ":1\n";
std::cout << "속도: " << stats.compression_speed_mbps() << " MB/s\n";
```

### 파이프라인 통계

```cpp
auto stats = sender->get_pipeline_stats();
std::cout << "병목: " << stage_name(stats.bottleneck_stage()) << "\n";
```

---

## 오류 처리

### 결과 확인

```cpp
auto result = sender->send_file(path, endpoint);
if (!result) {
    switch (result.error().code()) {
        case error::file_not_found:
            // 누락 파일 처리
            break;
        case error::transfer_timeout:
            // 타임아웃 처리
            break;
        default:
            std::cerr << result.error().message() << "\n";
    }
}
```

### 일반 오류 코드

| 코드 | 이름 | 설명 |
|------|------|------|
| -700 | `transfer_init_failed` | 연결 실패 |
| -702 | `transfer_timeout` | 전송 시간 초과 |
| -720 | `chunk_checksum_error` | 데이터 손상 |
| -723 | `file_hash_mismatch` | 파일 검증 실패 |
| -743 | `file_not_found` | 소스 파일 없음 |
| -781 | `decompression_failed` | LZ4 해제 오류 |

---

## 청크 설정

### 청크 크기 제한

| 제한 | 값 |
|------|---|
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
|------|-----|
| LAN 처리량 | >= 500 MB/s |
| WAN 처리량 | >= 100 MB/s |
| LZ4 압축 | >= 400 MB/s |
| LZ4 해제 | >= 1.5 GB/s |
| 메모리 기준 | < 50 MB |
| 동시 전송 | >= 100 |

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

## 전송 상태

```
pending → initializing → transferring → verifying → completed
                 ↓              ↓
             failed ←──────────┘
                 ↑
           cancelled
```

---

## 메모리 추정

### 송신자 메모리

```
(read_queue + compress_queue + send_queue) × chunk_size
= (16 + 32 + 64) × 256KB
= 28 MB
```

### 수신자 메모리

```
(recv_queue + decompress_queue + write_queue) × chunk_size
= (64 + 32 + 16) × 256KB
= 28 MB
```

---

## 의존성

| 시스템 | 필수 |
|--------|-----|
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
    class file_sender;
    class file_receiver;
    class transfer_manager;

    enum class compression_mode;
    enum class compression_level;
    enum class transport_type;

    struct transfer_options;
    struct transfer_progress;
    struct transfer_result;
    struct pipeline_config;
    struct compression_statistics;
}
```

---

*file_trans_system v1.0.0 | 최종 업데이트: 2025-12-11*
