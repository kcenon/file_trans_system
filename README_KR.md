# file_trans_system

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/std/the-standard)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-0.2.0-green.svg)](https://github.com/kcenon/file_trans_system)

클라이언트-서버 아키텍처, LZ4 압축, 재개 기능, 멀티 스테이지 파이프라인 처리를 갖춘 고성능 프로덕션급 C++20 파일 전송 라이브러리입니다.

## 주요 기능

- **클라이언트-서버 아키텍처**: 다중 클라이언트 연결을 지원하는 중앙 서버
- **양방향 전송**: 서버로 파일 업로드, 서버에서 파일 다운로드
- **고성능**: 멀티 스테이지 파이프라인 처리로 LAN 환경 ≥500 MB/s 처리량
- **LZ4 압축**: ~400 MB/s 속도와 ~2.1:1 압축률의 적응형 압축
- **재개 지원**: 중단 시 체크포인트 기반 자동 전송 재개
- **자동 재연결**: 지수 백오프 정책을 통한 자동 재연결
- **파일 관리**: 서버 저장소의 파일 목록 조회, 업로드, 다운로드
- **진행 상황 추적**: 통계를 포함한 실시간 진행 상황 콜백
- **무결성 검증**: 청크별 CRC32 + 파일별 SHA-256 검증
- **동시 연결**: ≥100개의 동시 클라이언트 연결 지원
- **낮은 메모리 사용량**: 제한된 메모리 사용량 (~32MB per direction)

## 빠른 시작

### 파일 전송 서버

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

int main() {
    // 저장소 디렉토리를 가진 서버 생성
    auto server = file_transfer_server::builder()
        .with_storage_directory("/data/files")
        .with_max_connections(100)
        .with_max_file_size(10ULL * 1024 * 1024 * 1024)  // 10GB
        .build();

    if (!server) {
        std::cerr << "서버 생성 실패: " << server.error().message() << "\n";
        return 1;
    }

    // 5GB 미만 업로드만 수락
    server->on_upload_request([](const upload_request& req) {
        return req.file_size < 5ULL * 1024 * 1024 * 1024;
    });

    // 모든 다운로드 허용
    server->on_download_request([](const download_request& req) {
        return true;
    });

    // 업로드 완료 처리
    server->on_upload_complete([](const transfer_result& result) {
        if (result.verified) {
            std::cout << "수신 완료: " << result.filename << "\n";
        }
    });

    // 서버 시작
    server->start(endpoint{"0.0.0.0", 19000});

    std::cout << "서버가 19000 포트에서 대기 중...\n";
    std::this_thread::sleep_for(std::chrono::hours(24));

    server->stop();
}
```

### 파일 전송 클라이언트

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

int main() {
    // 자동 재연결을 사용하는 클라이언트 생성
    auto client = file_transfer_client::builder()
        .with_compression(compression_mode::adaptive)
        .with_auto_reconnect(true)
        .with_reconnect_policy(reconnect_policy{
            .initial_delay = std::chrono::seconds(1),
            .max_delay = std::chrono::seconds(30),
            .multiplier = 2.0,
            .max_attempts = 10
        })
        .build();

    if (!client) {
        std::cerr << "클라이언트 생성 실패: " << client.error().message() << "\n";
        return 1;
    }

    // 서버에 연결
    auto connect_result = client->connect(endpoint{"192.168.1.100", 19000});
    if (!connect_result) {
        std::cerr << "연결 실패: " << connect_result.error().message() << "\n";
        return 1;
    }

    // 진행 상황 콜백 등록
    client->on_progress([](const transfer_progress& p) {
        double percent = 100.0 * p.bytes_transferred / p.total_bytes;
        std::cout << p.direction << ": " << percent << "% - "
                  << p.transfer_rate / 1e6 << " MB/s\n";
    });

    // 파일 업로드
    auto upload_result = client->upload_file("/local/data.zip", "data.zip");
    if (upload_result) {
        std::cout << "업로드 완료: " << upload_result->id.to_string() << "\n";
    }

    // 파일 다운로드
    auto download_result = client->download_file("report.pdf", "/local/report.pdf");
    if (download_result) {
        std::cout << "다운로드 완료: " << download_result->output_path << "\n";
    }

    // 서버 파일 목록 조회
    auto files = client->list_files();
    if (files) {
        for (const auto& file : *files) {
            std::cout << file.filename << " (" << file.file_size << " 바이트)\n";
        }
    }

    client->disconnect();
}
```

## 아키텍처

file_trans_system은 **클라이언트-서버 아키텍처**와 **멀티 스테이지 파이프라인 처리**를 사용합니다:

```
                    ┌─────────────────────────────────┐
                    │    file_transfer_server         │
                    │                                 │
                    │  ┌───────────────────────────┐  │
                    │  │    저장소: /data/files     │  │
                    │  └───────────────────────────┘  │
                    │                                 │
                    │  on_upload_request()            │
                    │  on_download_request()          │
                    │  on_upload_complete()           │
                    │  on_download_complete()         │
                    └────────────┬────────────────────┘
                                 │
         ┌───────────────────────┼───────────────────────┐
         │                       │                       │
         ▼                       ▼                       ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   클라이언트 A   │    │   클라이언트 B   │    │   클라이언트 C   │
│  upload_file()  │    │ download_file() │    │  list_files()   │
│                 │    │                 │    │                 │
│    자동 재연결   │    │    자동 재연결   │    │    자동 재연결   │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

### 파이프라인 아키텍처

각 전송은 최대 처리량을 위해 멀티 스테이지 파이프라인을 사용합니다:

```
┌────────────────────────────────────────────────────────────────────────┐
│                     업로드 파이프라인 (클라이언트)                        │
│                                                                        │
│  파일 읽기 ──▶  청크     ──▶   LZ4      ──▶  네트워크                   │
│   스테이지      조립         압축          전송                          │
│  (io_read)   (chunk_process) (compression)   (network)                │
│                                                                        │
│  ┌──────────────────────────────────────────────────────────────────┐ │
│  │              typed_thread_pool<pipeline_stage>                   │ │
│  │   [IO 워커] [연산 워커] [네트워크 워커]                             │ │
│  └──────────────────────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────────────┐
│                    다운로드 파이프라인 (클라이언트)                       │
│                                                                        │
│  네트워크  ──▶   LZ4       ──▶  청크     ──▶  파일 쓰기                 │
│   수신        압축 해제        재조립        스테이지                    │
│ (network)    (compression)  (chunk_process)  (io_write)               │
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
| 동시 클라이언트 연결 | ≥ 100 |

## 설정

### 서버 설정

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")         // 필수
    .with_max_connections(100)                     // 기본값: 100
    .with_max_file_size(10ULL * 1024 * 1024 * 1024) // 기본값: 10GB
    .with_storage_quota(1ULL * 1024 * 1024 * 1024 * 1024) // 1TB
    .with_pipeline_config(pipeline_config::auto_detect())
    .build();
```

### 클라이언트 설정

```cpp
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::adaptive)   // 기본값
    .with_chunk_size(256 * 1024)                   // 256KB (기본값)
    .with_auto_reconnect(true)                     // 자동 재연결 활성화
    .with_reconnect_policy(reconnect_policy{
        .initial_delay = 1s,
        .max_delay = 30s,
        .multiplier = 2.0,
        .max_attempts = 10
    })
    .with_pipeline_config(pipeline_config::auto_detect())
    .build();
```

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

### 메시지 타입

| 코드 | 메시지 | 방향 | 설명 |
|-----|-------|------|------|
| 0x10 | UPLOAD_REQUEST | C→S | 메타데이터 포함 업로드 요청 |
| 0x11 | UPLOAD_ACCEPT | S→C | 업로드 승인 |
| 0x12 | UPLOAD_REJECT | S→C | 업로드 거부 (사유 포함) |
| 0x50 | DOWNLOAD_REQUEST | C→S | 파일 다운로드 요청 |
| 0x51 | DOWNLOAD_ACCEPT | S→C | 다운로드 승인 (메타데이터 포함) |
| 0x52 | DOWNLOAD_REJECT | S→C | 다운로드 거부 |
| 0x60 | LIST_REQUEST | C→S | 파일 목록 요청 |
| 0x61 | LIST_RESPONSE | S→C | 파일 목록 응답 |

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
| [빠른 참조](docs/reference/quick-reference_KR.md) | 일반 작업 치트 시트 |
| [API 참조](docs/reference/api-reference_KR.md) | 완전한 API 문서 |
| [프로토콜 사양](docs/reference/protocol-spec_KR.md) | 와이어 프로토콜 상세 |
| [파이프라인 아키텍처](docs/reference/pipeline-architecture_KR.md) | 파이프라인 설계 가이드 |
| [설정 가이드](docs/reference/configuration_KR.md) | 튜닝 옵션 |
| [오류 코드](docs/reference/error-codes_KR.md) | 오류 코드 참조 |
| [시작하기](docs/reference/getting-started_KR.md) | 단계별 튜토리얼 |
| [시퀀스 다이어그램](docs/reference/sequence-diagrams_KR.md) | 상호작용 흐름 |

### 설계 문서

| 문서 | 설명 |
|-----|------|
| [PRD](docs/PRD_KR.md) | 제품 요구사항 문서 |
| [SRS](docs/SRS_KR.md) | 소프트웨어 요구사항 사양 |
| [SDS](docs/SDS_KR.md) | 소프트웨어 설계 사양 |
| [아키텍처](docs/architecture_KR.md) | 아키텍처 개요 |
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

### Sanitizer 테스트

메모리 문제와 정의되지 않은 동작을 감지하기 위해 sanitizer로 빌드:

```bash
# AddressSanitizer (메모리 오류)
cmake -B build -DFILE_TRANS_ENABLE_ASAN=ON
cmake --build build
ASAN_OPTIONS="abort_on_error=1" ctest --test-dir build

# ThreadSanitizer (데이터 경쟁)
cmake -B build -DFILE_TRANS_ENABLE_TSAN=ON
cmake --build build
TSAN_OPTIONS="abort_on_error=1" ctest --test-dir build

# UndefinedBehaviorSanitizer
cmake -B build -DFILE_TRANS_ENABLE_UBSAN=ON
cmake --build build
UBSAN_OPTIONS="print_stacktrace=1:abort_on_error=1" ctest --test-dir build
```

> **참고**: ASAN과 TSAN은 동시에 활성화할 수 없습니다.

### 벤치마크

성능 벤치마크를 빌드하고 실행합니다:

```bash
# 벤치마크 활성화로 빌드
cmake -B build -DFILE_TRANS_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# 처리량 벤치마크 실행
./build/bin/throughput_benchmarks

# JSON 출력으로 실행
./build/bin/throughput_benchmarks --benchmark_format=json --benchmark_out=results.json

# 특정 벤치마크 실행
./build/bin/throughput_benchmarks --benchmark_filter="BM_SingleFile*"
```

#### 벤치마크 카테고리

| 카테고리 | 설명 | 목표 |
|---------|-----|-----|
| **Throughput** | 파일 분할/조립 처리량 | ≥ 500 MB/s |
| **Chunk Operations** | 청크 스플리터 및 어셈블러 성능 | - |
| **Checksum** | CRC32 및 SHA-256 성능 | - |

### CMake 통합

```cmake
find_package(file_trans_system REQUIRED)
target_link_libraries(your_target PRIVATE kcenon::file_transfer)
```

## 예제

다음은 [examples/](examples/) 디렉토리에서 확인할 수 있습니다:

### 기본 예제
- `simple_server.cpp` - 기본 파일 전송 서버 설정
- `simple_client.cpp` - 기본 파일 전송 클라이언트

### 업로드/다운로드 예제
- `upload_example.cpp` - 진행 콜백, 압축 설정, 오류 처리를 포함한 단일 파일 업로드
- `download_example.cpp` - 해시 검증과 덮어쓰기 정책을 포함한 단일 파일 다운로드
- `batch_upload_example.cpp` - 배치 진행 추적을 포함한 다중 파일 병렬 업로드
- `batch_download_example.cpp` - 패턴 매칭을 포함한 다중 파일 병렬 다운로드

### 고급 예제 (계획됨)
- `resume_transfer.cpp` - 전송 재개 처리
- `custom_pipeline.cpp` - 파이프라인 설정 튜닝
- `auto_reconnect.cpp` - 자동 재연결 데모

## 오류 처리

모든 작업은 명시적 오류 처리를 위해 `Result<T>`를 반환합니다:

```cpp
// 오류 처리를 포함한 업로드
auto result = client->upload_file(path, filename);
if (!result) {
    auto code = result.error().code();
    switch (code) {
        case error::upload_rejected:
            std::cerr << "서버가 업로드를 거부했습니다\n";
            break;
        case error::storage_full:
            std::cerr << "서버 저장소가 가득 찼습니다\n";
            break;
        case error::transfer_timeout:
            // 재시도 가능 - 재개 가능
            client->resume_upload(transfer_id);
            break;
        default:
            std::cerr << "오류: " << result.error().message() << "\n";
    }
}

// 오류 처리를 포함한 다운로드
auto download = client->download_file(filename, local_path);
if (!download) {
    if (download.error().code() == error::file_not_found_on_server) {
        std::cerr << "서버에서 파일을 찾을 수 없습니다\n";
    }
}
```

### 주요 오류 코드

| 코드 | 이름 | 설명 |
|-----|-----|-----|
| -700 | `transfer_init_failed` | 연결 실패 |
| -702 | `transfer_timeout` | 전송 시간 초과 |
| -711 | `connection_closed` | 연결이 예기치 않게 닫힘 |
| -712 | `upload_rejected` | 서버가 업로드를 거부함 |
| -720 | `chunk_checksum_error` | 데이터 손상 감지 |
| -743 | `file_not_found` | 로컬 파일을 찾을 수 없음 |
| -745 | `storage_full` | 서버 저장소 할당량 초과 |
| -746 | `file_not_found_on_server` | 서버에 요청한 파일 없음 |
| -747 | `access_denied` | 권한 없음 |
| -748 | `invalid_filename` | 잘못된 파일명 |

## 자동 재연결

클라이언트는 지수 백오프를 사용한 자동 재연결을 지원합니다:

```cpp
auto client = file_transfer_client::builder()
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy{
        .initial_delay = std::chrono::seconds(1),
        .max_delay = std::chrono::seconds(30),
        .multiplier = 2.0,
        .max_attempts = 10
    })
    .build();

// 재연결 콜백 설정
client->on_reconnect([](int attempt, const reconnect_info& info) {
    std::cout << "재연결 중 (시도 " << attempt << ")...\n";
});

// 연결 복구 콜백 설정
client->on_connection_restored([]() {
    std::cout << "연결이 복구되었습니다!\n";
});
```

## 라이선스

이 프로젝트는 BSD 3-Clause 라이선스 하에 배포됩니다. 자세한 내용은 [LICENSE](LICENSE) 파일을 참조하세요.

## 기여

기여를 환영합니다! PR을 제출하기 전에 기여 가이드라인을 읽어주세요.

## 로드맵

- [x] **Phase 1**: TCP 전송 및 LZ4 압축을 포함한 클라이언트-서버 아키텍처
- [ ] **Phase 2**: QUIC 전송 지원
- [ ] **Phase 3**: 암호화 레이어 (AES-256-GCM)
- [ ] **Phase 4**: 클라우드 스토리지 통합

---

*file_trans_system v0.2.0 | 클라이언트-서버 아키텍처를 갖춘 고성능 파일 전송 라이브러리*
