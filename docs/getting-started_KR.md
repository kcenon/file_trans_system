# 시작 가이드

C++20 프로젝트에서 **file_trans_system**을 사용하기 위한 단계별 가이드입니다.

## 목차

1. [설치](#설치)
2. [빠른 시작](#빠른-시작)
3. [기본 예제](#기본-예제)
4. [설정 옵션](#설정-옵션)
5. [오류 처리](#오류-처리)
6. [진행 상황 모니터링](#진행-상황-모니터링)
7. [고급 사용법](#고급-사용법)
8. [문제 해결](#문제-해결)

---

## 설치

### 요구 사항

- **컴파일러**: C++20 호환
  - GCC 11+
  - Clang 14+
  - MSVC 19.29+
- **빌드 시스템**: CMake 3.20+
- **외부 의존성**: LZ4 (v1.9.0+)

### 생태계 의존성

file_trans_system은 **kcenon 생태계** 라이브러리를 기반으로 구축됩니다:

| 라이브러리 | 용도 |
|-----------|------|
| common_system | Result<T>, 오류 처리, 시간 유틸리티 |
| thread_system | 파이프라인 병렬성을 위한 `typed_thread_pool` |
| **network_system** | **TCP/TLS 1.3 및 QUIC 전송 계층** |
| container_system | 백프레셔를 위한 제한된 큐 |

> **중요**: 전송 계층은 프로덕션 수준의 TCP 및 QUIC 구현을 제공하는 **network_system**을 사용하여 구현됩니다. Boost.Asio나 libuv 같은 외부 네트워크 라이브러리가 필요하지 않습니다.

### CMake 통합

#### 옵션 1: FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
    file_trans_system
    GIT_REPOSITORY https://github.com/kcenon/file_trans_system.git
    GIT_TAG v1.0.0
)

FetchContent_MakeAvailable(file_trans_system)

target_link_libraries(your_target PRIVATE kcenon::file_transfer)
```

#### 옵션 2: find_package

```cmake
find_package(file_trans_system REQUIRED)
target_link_libraries(your_target PRIVATE kcenon::file_transfer)
```

#### 옵션 3: 하위 디렉토리

```cmake
add_subdirectory(external/file_trans_system)
target_link_libraries(your_target PRIVATE kcenon::file_transfer)
```

### 헤더 포함

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;
```

---

## 빠른 시작

### 5분 예제

**송신자 (send_file.cpp):**

```cpp
#include <kcenon/file_transfer/file_transfer.h>
#include <iostream>

using namespace kcenon::file_transfer;

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "사용법: send_file <파일> <호스트> <포트>\n";
        return 1;
    }

    // 송신자 생성
    auto sender = file_sender::builder().build();
    if (!sender) {
        std::cerr << "송신자 생성 실패: " << sender.error().message() << "\n";
        return 1;
    }

    // 진행 상황 표시
    sender->on_progress([](const transfer_progress& p) {
        double pct = 100.0 * p.bytes_transferred / p.total_bytes;
        std::cout << "\r진행률: " << std::fixed << std::setprecision(1)
                  << pct << "% (" << p.transfer_rate / 1e6 << " MB/s)" << std::flush;
    });

    // 파일 전송
    auto result = sender->send_file(
        argv[1],
        endpoint{argv[2], static_cast<uint16_t>(std::stoi(argv[3]))}
    );

    if (result) {
        std::cout << "\n전송 완료!\n";
        return 0;
    } else {
        std::cerr << "\n전송 실패: " << result.error().message() << "\n";
        return 1;
    }
}
```

**수신자 (receive_file.cpp):**

```cpp
#include <kcenon/file_transfer/file_transfer.h>
#include <iostream>
#include <csignal>

using namespace kcenon::file_transfer;

std::atomic<bool> running{true};

void signal_handler(int) { running = false; }

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "사용법: receive_file <포트> <출력_디렉토리>\n";
        return 1;
    }

    std::signal(SIGINT, signal_handler);

    // 수신자 생성
    auto receiver = file_receiver::builder()
        .with_output_directory(argv[2])
        .build();

    if (!receiver) {
        std::cerr << "수신자 생성 실패: " << receiver.error().message() << "\n";
        return 1;
    }

    // 모든 전송 수락
    receiver->on_transfer_request([](const transfer_request&) {
        return true;
    });

    // 완료 처리
    receiver->on_complete([](const transfer_result& r) {
        if (r.verified) {
            std::cout << "수신됨: " << r.output_path << "\n";
        } else {
            std::cerr << "실패: " << r.error->message << "\n";
        }
    });

    // 수신 대기 시작
    auto port = static_cast<uint16_t>(std::stoi(argv[1]));
    receiver->start(endpoint{"0.0.0.0", port});

    std::cout << "포트 " << port << "에서 수신 대기 중...\n";

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    receiver->stop();
    return 0;
}
```

**사용:**

```bash
# 터미널 1 - 수신자 시작
./receive_file 19000 /downloads

# 터미널 2 - 파일 전송
./send_file /path/to/file.zip 127.0.0.1 19000
```

---

## 기본 예제

### 예제 1: 압축과 함께 전송

```cpp
auto sender = file_sender::builder()
    .with_compression(compression_mode::enabled)  // 항상 압축
    .with_compression_level(compression_level::fast)  // 빠른 압축
    .build();

auto result = sender->send_file(
    "/path/to/logs.txt",
    endpoint{"192.168.1.100", 19000}
);
```

### 예제 2: 대역폭 제한과 함께 대용량 파일 전송

```cpp
auto sender = file_sender::builder()
    .with_chunk_size(512 * 1024)  // 512KB 청크
    .with_bandwidth_limit(10 * 1024 * 1024)  // 10 MB/s 제한
    .build();

auto result = sender->send_file(
    "/path/to/large_video.mp4",
    endpoint{"192.168.1.100", 19000}
);
```

### 예제 3: 다중 파일 전송

```cpp
std::vector<std::filesystem::path> files = {
    "/path/to/file1.txt",
    "/path/to/file2.txt",
    "/path/to/file3.txt"
};

auto sender = file_sender::builder()
    .with_compression(compression_mode::adaptive)
    .build();

auto result = sender->send_files(
    files,
    endpoint{"192.168.1.100", 19000}
);

if (result) {
    std::cout << "배치 전송 ID: " << result->id.to_string() << "\n";
    std::cout << "전송된 파일 수: " << result->file_count << "\n";
}
```

### 예제 4: 크기 제한과 함께 수신

```cpp
auto receiver = file_receiver::builder()
    .with_output_directory("/downloads")
    .build();

// 1GB 이하 파일만 수락
receiver->on_transfer_request([](const transfer_request& req) {
    for (const auto& file : req.files) {
        if (file.file_size > 1ULL * 1024 * 1024 * 1024) {
            return false;  // 거부
        }
    }
    return true;  // 수락
});

receiver->start(endpoint{"0.0.0.0", 19000});
```

### 예제 5: 파일 형식 필터와 함께 수신

```cpp
receiver->on_transfer_request([](const transfer_request& req) {
    static const std::set<std::string> allowed_extensions = {
        ".txt", ".pdf", ".doc", ".docx", ".xls", ".xlsx"
    };

    for (const auto& file : req.files) {
        std::filesystem::path p(file.filename);
        if (allowed_extensions.find(p.extension()) == allowed_extensions.end()) {
            return false;  // 알 수 없는 파일 형식 거부
        }
    }
    return true;
});
```

---

## 설정 옵션

### 송신자 설정

| 옵션 | 메서드 | 기본값 | 설명 |
|-----|--------|--------|------|
| 압축 모드 | `with_compression()` | `adaptive` | disabled, enabled, adaptive |
| 압축 수준 | `with_compression_level()` | `fast` | fast, high_compression |
| 청크 크기 | `with_chunk_size()` | 256KB | 64KB - 1MB |
| 대역폭 제한 | `with_bandwidth_limit()` | 0 (무제한) | 바이트/초 |
| 전송 방식 | `with_transport()` | `tcp` | tcp, quic (Phase 2) |

```cpp
auto sender = file_sender::builder()
    .with_compression(compression_mode::adaptive)
    .with_compression_level(compression_level::fast)
    .with_chunk_size(256 * 1024)
    .with_bandwidth_limit(0)
    .with_transport(transport_type::tcp)
    .build();
```

### 수신자 설정

| 옵션 | 메서드 | 기본값 | 설명 |
|-----|--------|--------|------|
| 출력 디렉토리 | `with_output_directory()` | 현재 디렉토리 | 파일 저장 위치 |
| 대역폭 제한 | `with_bandwidth_limit()` | 0 (무제한) | 바이트/초 |
| 전송 방식 | `with_transport()` | `tcp` | tcp, quic (Phase 2) |

```cpp
auto receiver = file_receiver::builder()
    .with_output_directory("/downloads")
    .with_bandwidth_limit(50 * 1024 * 1024)  // 50 MB/s
    .build();
```

### 파이프라인 설정

고급 튜닝용:

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

auto sender = file_sender::builder()
    .with_pipeline_config(config)
    .build();
```

또는 자동 감지 사용:

```cpp
auto sender = file_sender::builder()
    .with_pipeline_config(pipeline_config::auto_detect())
    .build();
```

---

## 오류 처리

### Result 패턴

모든 연산은 `Result<T>`를 반환합니다:

```cpp
auto result = sender->send_file(path, endpoint);

if (result) {
    // 성공 - 값에 접근
    std::cout << "전송 ID: " << result->id.to_string() << "\n";
} else {
    // 실패 - 오류에 접근
    auto code = result.error().code();
    auto message = result.error().message();
    std::cerr << "오류 " << code << ": " << message << "\n";
}
```

### 재시도 가능한 오류

```cpp
auto result = sender->send_file(path, endpoint);

if (!result) {
    if (error::is_retryable(result.error().code())) {
        // 네트워크 오류 - 재시도 가능
        std::cout << "재시도 중...\n";
        result = sender->send_file(path, endpoint);
    } else {
        // 영구 오류 - 재시도 불가
        std::cerr << "치명적 오류: " << result.error().message() << "\n";
    }
}
```

### 일반적인 오류 코드

| 코드 | 이름 | 설명 | 조치 |
|------|------|------|------|
| -700 | `transfer_init_failed` | 연결 실패 | 네트워크 확인, 재시도 |
| -702 | `transfer_timeout` | 작업 시간 초과 | 재시도 또는 재개 |
| -703 | `transfer_rejected` | 수신자가 거부 | 요청 확인 |
| -720 | `chunk_checksum_error` | 데이터 손상 | 자동 재시도 |
| -723 | `file_hash_mismatch` | 파일 검증 실패 | 재전송 |
| -743 | `file_not_found` | 소스 파일 없음 | 경로 확인 |
| -744 | `disk_full` | 디스크 공간 부족 | 공간 확보 |

### 오류 카테고리

```cpp
auto code = result.error().code();

if (error::is_transfer_error(code)) {
    // 네트워크 또는 연결 문제
} else if (error::is_chunk_error(code)) {
    // 데이터 무결성 문제
} else if (error::is_file_error(code)) {
    // 파일 시스템 문제
} else if (error::is_resume_error(code)) {
    // 재개 작업 문제
} else if (error::is_compression_error(code)) {
    // 압축/해제 문제
} else if (error::is_config_error(code)) {
    // 설정 문제
}
```

---

## 진행 상황 모니터링

### 진행 상황 콜백

```cpp
sender->on_progress([](const transfer_progress& p) {
    // 백분율 계산
    double percent = 100.0 * p.bytes_transferred / p.total_bytes;

    // MB/s 단위 전송 속도
    double rate_mbps = p.transfer_rate / (1024 * 1024);

    // 예상 남은 시간
    auto remaining = p.estimated_remaining;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(remaining);

    // 압축 비율
    double ratio = p.compression_ratio;

    std::cout << std::fixed << std::setprecision(1)
              << percent << "% 완료, "
              << rate_mbps << " MB/s, "
              << seconds.count() << "초 남음, "
              << ratio << ":1 압축\n";
});
```

### 진행 상황 정보

| 필드 | 타입 | 설명 |
|------|------|------|
| `id` | `transfer_id` | 고유 전송 식별자 |
| `bytes_transferred` | `uint64_t` | 전송된 원시 바이트 |
| `bytes_on_wire` | `uint64_t` | 실제 전송 바이트 (압축됨) |
| `total_bytes` | `uint64_t` | 총 파일 크기 |
| `transfer_rate` | `double` | 현재 속도 (바이트/초) |
| `effective_rate` | `double` | 압축 포함 효과적 속도 |
| `compression_ratio` | `double` | 압축 비율 |
| `elapsed_time` | `duration` | 시작 이후 경과 시간 |
| `estimated_remaining` | `duration` | 예상 완료 시간 |
| `state` | `transfer_state_enum` | 현재 상태 |

### 전송 통계

```cpp
// 집계 통계 가져오기
auto stats = manager->get_statistics();
std::cout << "총 전송량: " << stats.total_bytes_transferred << "\n";
std::cout << "활성 전송: " << stats.active_transfer_count << "\n";
std::cout << "평균 속도: " << stats.average_transfer_rate / 1e6 << " MB/s\n";

// 압축 통계 가져오기
auto comp_stats = manager->get_compression_stats();
std::cout << "압축 비율: " << comp_stats.compression_ratio() << ":1\n";
std::cout << "압축 속도: " << comp_stats.compression_speed_mbps() << " MB/s\n";
std::cout << "압축된 청크: " << comp_stats.chunks_compressed << "\n";
std::cout << "건너뛴 청크: " << comp_stats.chunks_skipped << "\n";
```

---

## 고급 사용법

### 전송 제어

```cpp
// 전송 시작
auto handle = sender->send_file(path, endpoint);
auto transfer_id = handle->id;

// 전송 일시 중지
sender->pause(transfer_id);

// 전송 재개
sender->resume(transfer_id);

// 전송 취소
sender->cancel(transfer_id);
```

### 연결 끊김 후 전송 재개

```cpp
// 원래 전송
auto handle = sender->send_file(path, endpoint);
auto transfer_id = handle->id;

// ... 연결 끊김 ...

// 전송 재개 (자동으로 체크포인트 사용)
auto resume_result = sender->resume(transfer_id);
if (resume_result) {
    std::cout << resume_result->bytes_already_transferred
              << " 바이트부터 재개\n";
}
```

### 사용자 정의 전송 옵션

```cpp
transfer_options opts{
    .compression = compression_mode::enabled,
    .level = compression_level::high_compression,
    .chunk_size = 512 * 1024,
    .verify_checksum = true,
    .bandwidth_limit = 10 * 1024 * 1024,
    .priority = 1  // 높은 우선순위
};

auto result = sender->send_file(path, endpoint, opts);
```

### 파이프라인 통계

```cpp
auto pipeline_stats = sender->get_pipeline_stats();

// 병목 지점 찾기
auto bottleneck = pipeline_stats.bottleneck_stage();
std::cout << "병목: " << stage_name(bottleneck) << "\n";

// 스테이지 처리량
std::cout << "IO 읽기: " << pipeline_stats.io_read_stats.throughput_mbps() << " MB/s\n";
std::cout << "압축: " << pipeline_stats.compression_stats.throughput_mbps() << " MB/s\n";
std::cout << "네트워크: " << pipeline_stats.network_stats.throughput_mbps() << " MB/s\n";

// 큐 깊이
auto depths = sender->get_queue_depths();
std::cout << "읽기 큐: " << depths.read_queue << "/" << depths.read_queue_max << "\n";
std::cout << "전송 큐: " << depths.send_queue << "/" << depths.send_queue_max << "\n";
```

---

## 문제 해결

### 일반적인 문제

#### "Connection refused" 오류

**원인**: 수신자가 실행 중이지 않거나 포트가 잘못됨.

**해결:**
```bash
# 수신자가 수신 대기 중인지 확인
netstat -an | grep 19000

# 송신자보다 수신자를 먼저 시작
./receive_file 19000 /downloads &
./send_file file.txt localhost 19000
```

#### 느린 전송 속도

**원인**: 파이프라인 또는 네트워크의 병목.

**해결:**
```cpp
// 병목 지점 확인
auto stats = sender->get_pipeline_stats();
std::cout << "병목: " << stage_name(stats.bottleneck_stage()) << "\n";

// 압축 병목인 경우, 워커 추가
pipeline_config config = pipeline_config::auto_detect();
config.compression_workers = 8;

// 네트워크 병목인 경우, 대역폭 확인
// IO 병목인 경우, 더 빠른 스토리지 고려
```

#### "File hash mismatch" 오류

**원인**: 전송 중 파일 변경 또는 데이터 손상.

**해결:**
```cpp
// 전송 중 소스 파일 수정하지 않기
// 또는 스트리밍 사용 사례의 경우 검증 비활성화
transfer_options opts{
    .verify_checksum = false  // 소스가 변경될 수 있는 경우 비활성화
};
```

#### 높은 메모리 사용량

**원인**: 큐 크기가 너무 큼.

**해결:**
```cpp
pipeline_config config{
    .read_queue_size = 4,    // 기본값 16에서 축소
    .compress_queue_size = 8, // 기본값 32에서 축소
    .send_queue_size = 16    // 기본값 64에서 축소
};
// 메모리: ~7MB vs 기본값 ~32MB
```

### 디버그 로깅

상세 로깅 활성화:

```cpp
#include <kcenon/logger/logger.h>

// 로그 수준을 debug로 설정
logger::set_level(log_level::debug);

// 이제 file_trans_system이 상세 로그 출력
```

### 성능 튜닝

| 시나리오 | 권장 설정 |
|---------|----------|
| **텍스트 파일** | `compression_mode::enabled`, `compression_level::fast` |
| **비디오/이미지** | `compression_mode::disabled` |
| **혼합 워크로드** | `compression_mode::adaptive` |
| **저대역폭** | 큰 청크 크기, `high_compression` 수준 |
| **고대역폭** | 작은 청크 크기, 더 많은 워커 |
| **저메모리** | 작은 큐 크기 |

---

## 다음 단계

- 완전한 API 문서는 [API 레퍼런스](reference/api-reference_KR.md) 참조
- 고급 튜닝은 [파이프라인 아키텍처](reference/pipeline-architecture_KR.md) 탐색
- 포괄적인 오류 처리는 [오류 코드](reference/error-codes_KR.md) 확인
- 와이어 프로토콜 세부사항은 [프로토콜 명세](reference/protocol-spec_KR.md) 검토

---

*최종 업데이트: 2025-12-11*
