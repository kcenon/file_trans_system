# file_trans_system

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/std/the-standard)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-1.0.0-green.svg)](https://github.com/kcenon/file_trans_system)

압축, 재개 기능, 멀티 스테이지 파이프라인 아키텍처를 갖춘 고성능 프로덕션급 C++20 파일 전송 라이브러리입니다.

## 주요 기능

- **고성능**: 멀티 스테이지 파이프라인 처리로 LAN 환경 ≥500 MB/s 처리량
- **LZ4 압축**: ~400 MB/s 속도와 ~2.1:1 압축률의 적응형 압축
- **재개 지원**: 중단 시 체크포인트 기반 자동 전송 재개
- **다중 파일 배치**: 단일 세션으로 여러 파일 전송
- **진행 상황 추적**: 통계를 포함한 실시간 진행 상황 콜백
- **무결성 검증**: 청크별 CRC32 + 파일별 SHA-256 검증
- **동시 전송**: ≥100개의 동시 전송 지원
- **낮은 메모리 사용량**: 제한된 메모리 사용량 (~32MB per direction)

## 빠른 시작

### 기본 송신자

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

int main() {
    // 적응형 압축을 사용하는 송신자 생성
    auto sender = file_sender::builder()
        .with_compression(compression_mode::adaptive)
        .with_chunk_size(256 * 1024)  // 256KB 청크
        .build();

    if (!sender) {
        std::cerr << "송신자 생성 실패\n";
        return 1;
    }

    // 진행 상황 콜백 등록
    sender->on_progress([](const transfer_progress& p) {
        double percent = 100.0 * p.bytes_transferred / p.total_bytes;
        std::cout << percent << "% - " << p.transfer_rate / 1e6 << " MB/s\n";
    });

    // 파일 전송
    auto result = sender->send_file(
        "/path/to/file.dat",
        endpoint{"192.168.1.100", 19000}
    );

    if (result) {
        std::cout << "전송 완료: " << result->id.to_string() << "\n";
    } else {
        std::cerr << "전송 실패: " << result.error().message() << "\n";
    }
}
```

### 기본 수신자

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

int main() {
    // 수신자 생성
    auto receiver = file_receiver::builder()
        .with_output_directory("/downloads")
        .build();

    if (!receiver) {
        std::cerr << "수신자 생성 실패\n";
        return 1;
    }

    // 10GB 미만 전송만 수락
    receiver->on_transfer_request([](const transfer_request& req) {
        uint64_t total = 0;
        for (const auto& file : req.files) total += file.file_size;
        return total < 10ULL * 1024 * 1024 * 1024;
    });

    // 완료 처리
    receiver->on_complete([](const transfer_result& result) {
        if (result.verified) {
            std::cout << "수신 완료: " << result.output_path << "\n";
        }
    });

    // 수신 대기 시작
    receiver->start(endpoint{"0.0.0.0", 19000});

    // 신호 대기...
    std::this_thread::sleep_for(std::chrono::hours(24));

    receiver->stop();
}
```

## 아키텍처

file_trans_system은 최대 처리량을 위해 **멀티 스테이지 파이프라인 아키텍처**를 사용합니다:

```
┌────────────────────────────────────────────────────────────────────────┐
│                         송신자 파이프라인                                │
│                                                                         │
│  파일 읽기 ──▶  청크     ──▶   LZ4      ──▶  네트워크                   │
│   스테이지      조립         압축          전송                          │
│  (io_read)   (chunk_process) (compression)   (network)                 │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │              typed_thread_pool<pipeline_stage>                  │   │
│  │   [IO 워커] [연산 워커] [네트워크 워커]                            │   │
│  └─────────────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────────┘
```

각 스테이지는 백프레셔를 제공하는 제한된 큐와 함께 독립적으로 실행되어 다음을 보장합니다:
- **메모리 제한**: 파일 크기에 관계없이 고정된 최대 메모리 사용량
- **병렬성**: I/O와 CPU 작업이 동시에 실행
- **처리량**: 각 스테이지가 최대 속도로 실행

## 성능 목표

| 지표 | 목표 |
|-----|------|
| LAN 처리량 (1GB 단일 파일) | ≥ 500 MB/s |
| WAN 처리량 | ≥ 100 MB/s (네트워크 제한) |
| LZ4 압축 속도 | ≥ 400 MB/s |
| LZ4 압축 해제 속도 | ≥ 1.5 GB/s |
| 기본 메모리 | < 50 MB |
| 동시 전송 | ≥ 100 |

## 설정

### 압축 모드

| 모드 | 설명 | 사용 사례 |
|-----|------|----------|
| `disabled` | 압축 없음 | 이미 압축된 파일 (비디오, 이미지) |
| `enabled` | 항상 압축 | 텍스트 파일, 로그, 문서 |
| `adaptive` | 자동 감지 (기본값) | 혼합 워크로드 |

### 파이프라인 설정

```cpp
pipeline_config config{
    .io_read_workers = 2,       // 파일 읽기 병렬성
    .compression_workers = 4,    // LZ4 압축 스레드
    .network_workers = 2,        // 네트워크 송수신
    .send_queue_size = 64        // 백프레셔 버퍼
};

// 또는 자동 감지 사용
auto config = pipeline_config::auto_detect();
```

## 프로토콜

이 라이브러리는 파일 전송에 최적화된 커스텀 경량 바이너리 프로토콜을 사용합니다:

- 청크당 **54바이트** 오버헤드 (HTTP ~800바이트 대비)
- 기본 **TLS 1.3** 암호화
- **효율적인 재개**: 비트맵 기반 누락 청크 추적

## 의존성

file_trans_system은 kcenon 에코시스템 라이브러리를 기반으로 구축되었습니다:

| 시스템 | 필수 | 용도 |
|-------|-----|-----|
| common_system | 예 | Result<T>, 오류 처리, 시간 유틸리티 |
| thread_system | 예 | 파이프라인 병렬 처리를 위한 `typed_thread_pool<pipeline_stage>` |
| **network_system** | 예 | **TCP/TLS 1.3 전송 계층 (Phase 1), QUIC 전송 (Phase 2)** |
| container_system | 예 | 백프레셔를 위한 제한된 큐 |
| LZ4 | 예 | 압축 라이브러리 (v1.9.0+) |
| logger_system | 선택 | 구조화된 로깅 |
| monitoring_system | 선택 | 메트릭 내보내기 |

> **참고**: 전송 계층은 **network_system**을 사용하여 구현되며, TCP와 QUIC 전송을 모두 제공합니다. 별도의 외부 전송 라이브러리가 필요하지 않습니다.

## 문서

| 문서 | 설명 |
|-----|------|
| [빠른 참조](docs/reference/quick-reference.md) | 일반 작업 치트 시트 |
| [API 참조](docs/reference/api-reference.md) | 완전한 API 문서 |
| [프로토콜 사양](docs/reference/protocol-spec.md) | 와이어 프로토콜 상세 |
| [파이프라인 아키텍처](docs/reference/pipeline-architecture.md) | 파이프라인 설계 가이드 |
| [설정 가이드](docs/reference/configuration.md) | 튜닝 옵션 |
| [오류 코드](docs/reference/error-codes.md) | 오류 코드 참조 |
| [LZ4 압축](docs/reference/lz4-compression.md) | 압축 상세 |

### 설계 문서

| 문서 | 설명 |
|-----|------|
| [PRD](docs/PRD_KR.md) | 제품 요구사항 문서 |
| [SRS](docs/SRS_KR.md) | 소프트웨어 요구사항 사양 |
| [SDS](docs/SDS_KR.md) | 소프트웨어 설계 사양 |
| [검증](docs/Verification_KR.md) | 검증 계획 |
| [확인](docs/Validation_KR.md) | 확인 계획 |

## 빌드

### 요구사항

- C++20 호환 컴파일러:
  - GCC 11+
  - Clang 14+
  - MSVC 19.29+
- CMake 3.20+
- LZ4 라이브러리 (v1.9.0+)

### 빌드 명령어

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

### CMake 통합

```cmake
find_package(file_trans_system REQUIRED)
target_link_libraries(your_target PRIVATE kcenon::file_transfer)
```

## 예제

다음은 [examples/](examples/) 디렉토리에서 확인할 수 있습니다:

- `simple_sender.cpp` - 기본 파일 전송
- `simple_receiver.cpp` - 기본 파일 수신
- `batch_transfer.cpp` - 다중 파일 배치 전송
- `resume_transfer.cpp` - 전송 재개 처리
- `custom_pipeline.cpp` - 파이프라인 설정 튜닝

## 오류 처리

모든 작업은 명시적 오류 처리를 위해 `Result<T>`를 반환합니다:

```cpp
auto result = sender->send_file(path, endpoint);
if (!result) {
    auto code = result.error().code();
    if (error::is_retryable(code)) {
        // 지수 백오프로 재시도
        sender->resume(transfer_id);
    } else {
        std::cerr << "영구 오류: " << result.error().message() << "\n";
    }
}
```

주요 오류 코드:

| 코드 | 이름 | 설명 |
|-----|-----|-----|
| -700 | `transfer_init_failed` | 연결 실패 |
| -702 | `transfer_timeout` | 전송 시간 초과 |
| -720 | `chunk_checksum_error` | 데이터 손상 감지 |
| -743 | `file_not_found` | 원본 파일을 찾을 수 없음 |

## 라이선스

이 프로젝트는 BSD 3-Clause 라이선스 하에 배포됩니다. 자세한 내용은 [LICENSE](LICENSE) 파일을 참조하세요.

## 기여

기여를 환영합니다! PR을 제출하기 전에 기여 가이드라인을 읽어주세요.

## 로드맵

- [ ] **Phase 1**: LZ4 압축을 포함한 핵심 TCP 전송
- [ ] **Phase 2**: QUIC 전송 지원
- [ ] **Phase 3**: 암호화 레이어 (AES-256-GCM)
- [ ] **Phase 4**: 클라우드 스토리지 통합

---

*file_trans_system v1.0.0 | 고성능 파일 전송 라이브러리*
