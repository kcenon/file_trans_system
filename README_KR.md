> 상태: 2026-09 보관 처리. 추가 유지보수 없이 참고용으로 보존합니다.
> 아래 현황은 로컬 검증 범위이며, 저장소의 유지보수 상태를 변경하지 않습니다.

# file_trans_system

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/std/the-standard)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-0.2.0-green.svg)](https://github.com/kcenon/file_trans_system)

클라이언트-서버 아키텍처, LZ4 압축, 재개 기능, 멀티 스테이지 파이프라인 처리를 갖춘 C++20 파일 전송 라이브러리입니다.

## 주요 기능

- **실제 TCP 전송**: 공개 API를 통한 업로드·다운로드의 무결성 검증과 파일 목록 조회.
- **압축 및 무결성**: 적응형 LZ4, 청크 CRC32, 파일 SHA-256.
- **암호화**: 설정한 공유 비밀키로 AES-256-GCM 청크 암호화. TLS 1.3 요구사항 충족을 의미하지 않습니다.
- **복구**: 연결 해제 후 자동 재연결과 명시적으로 활성화하는 영속 체크포인트.
- **배치 및 흐름 제어**: 완료 기반 배치 스케줄링, 대역폭 제한, 쿼터, 파이프라인 메모리 예산.
- **측정된 한계**: 로컬 1 GB 왕복 전송 통과. 100클라이언트 다운로드는 통과했지만 업로드는 서버 수신 예산을 초과했습니다.
- **전송 계층 상태**: TCP 통합 완료. QUIC·0-RTT·연결 마이그레이션은 의존성 문제로 검증이 막혀 있습니다.
- **클라우드 상태**: S3는 로컬 MinIO 검증, GCS 서명은 독립 벡터 검증을 통과했습니다. 그 외 실제 클라우드 작업은 미검증입니다.

[요구사항별 현황과 남은 작업](docs/IMPLEMENTATION_STATUS.md) ·
[실행한 테스트](docs/validation/REGRESSION_GATES.md) ·
[성능 측정](docs/validation/PUBLIC_API_BENCHMARKS.md) ·
[Issues #175–#191 implementation report](docs/validation/ISSUES_175_191.md)

## 빠른 시작

다음 코드는 컴파일되는 예제 소스에서 동기화됩니다. 두 전송 모두 최종 성공과 무결성 검증 후 완료를 출력합니다.

### 파일 전송 서버

<!-- server:start -->
~~~cpp
#include <kcenon/file_transfer/file_transfer.h>
#include <iostream>

using namespace kcenon::file_transfer;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: quick_start_server STORAGE_DIRECTORY PORT\n";
        return 2;
    }
    try {
        const auto port = std::stoul(argv[2]);
        if (!port || port > 65535) return 2;
        auto built = file_transfer_server::builder()
            .with_storage_directory(argv[1])
            .with_max_connections(100)
            .build();
        if (!built) {
            std::cerr << built.error().message << '\n';
            return 1;
        }
        auto server = std::move(built.value());
        server.on_transfer_complete([](const transfer_result& result) {
            if (result.success)
                std::cout << "Verified transfer: " << result.filename << '\n';
            else
                std::cerr << "Transfer failed: " << result.error_message << '\n';
        });
        auto started = server.start({"127.0.0.1", static_cast<uint16_t>(port)});
        if (!started) {
            std::cerr << started.error().message << '\n';
            return 1;
        }
        std::cout << "READY; press Enter to stop." << std::endl;
        std::string line;
        std::getline(std::cin, line);
        auto stopped = server.stop();
        if (!stopped) std::cerr << stopped.error().message << '\n';
        return stopped ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
~~~
<!-- server:end -->

### 파일 전송 클라이언트

<!-- client:start -->
~~~cpp
#include <kcenon/file_transfer/file_transfer.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace kcenon::file_transfer;
using namespace std::chrono_literals;

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: quick_start_client PORT SOURCE_FILE DOWNLOAD_PATH\n";
        return 2;
    }
    try {
        const auto port = std::stoul(argv[1]);
        if (!port || port > 65535) return 2;
        auto built = file_transfer_client::builder()
            .with_compression(compression_mode::adaptive)
            .with_auto_reconnect(true, reconnect_policy{})
            .with_connect_timeout(5s)
            .with_transfer_timeout(30s)
            .build();
        if (!built) throw std::runtime_error(built.error().message);
        auto client = std::move(built.value());
        auto connected = client.connect({"127.0.0.1", static_cast<uint16_t>(port)});
        if (!connected) throw std::runtime_error(connected.error().message);

        auto wait_verified = [](result<transfer_handle> pending) {
            if (!pending) throw std::runtime_error(pending.error().message);
            auto finished = pending.value().wait_for(120s);
            if (!finished) throw std::runtime_error(finished.error().message);
            if (!finished.value().success)
                throw std::runtime_error(finished.value().error_message.value_or("Transfer failed"));
            return finished.value().bytes_transferred;
        };
        const auto uploaded = wait_verified(client.upload_file(argv[2], "quick-start.bin"));
        const auto downloaded = wait_verified(client.download_file("quick-start.bin", argv[3]));
        auto disconnected = client.disconnect();
        if (!disconnected) throw std::runtime_error(disconnected.error().message);
        std::cout << "Verified upload: " << uploaded
                  << " bytes; verified download: " << downloaded << " bytes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
~~~
<!-- client:end -->

~~~sh
# Terminal 1 (keep stdin open)
./build/bin/quick_start_server ./storage 8080
# Terminal 2: use an existing input file and a new output path
./build/bin/quick_start_client 8080 ./input.bin ./download.bin
~~~

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
                    │  on_transfer_complete()           │
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

실제 파이프라인 작업은 마지막 콜백까지 공통 버퍼 예산을 유지합니다. 호출자가 보관한 복사본과 라이브러리의 고정 메모리는 이 예산에 포함되지 않습니다.

파이프라인 버퍼 예약과 프로세스 RSS는 다른 지표입니다. 서버 런타임의 별도 8 MiB 수신 큐는 포화 시 거절하며, 100 업로드 측정에서 이 한계가 확인되었습니다. [예산 범위](docs/protocol/PIPELINE_CAPACITY.md).

## 성능 목표

아래 수치는 PRD 목표이며 달성 선언이 아닙니다. 1 GB 루프백 측정은 업로드 19.27 MB/s, 다운로드 74.39 MB/s입니다. 전용 LAN 검증은 미실시이며, 100 동시 업로드는 수신 예산 초과로 실패했습니다.
[Raw data, environment, variance and memory measurements](docs/validation/PUBLIC_API_BENCHMARKS.md).

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
    .build();
```

### 클라이언트 설정

```cpp
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::adaptive)   // 기본값
    .with_chunk_size(256 * 1024)                   // 256KB (기본값)
    .with_auto_reconnect(true)                     // 자동 재연결 활성화
    .with_auto_reconnect(true, reconnect_policy{})
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
    .io_workers = 2,       // 파일 읽기 병렬성
    .compression_workers = 4,    // LZ4 압축 스레드
    .network_workers = 2,        // 네트워크 송수신
    .queue_size = 64        // 백프레셔 버퍼
};

// 또는 자동 감지 사용
auto detected_config = pipeline_config::auto_detect();
```

## 프로토콜

이 라이브러리는 파일 전송에 최적화된 커스텀 경량 바이너리 프로토콜을 사용합니다:

- 길이 제한과 체크섬을 검증하는 FTS1 프레임. 오버헤드는 메시지·암호화 메타데이터에 따라 달라집니다.
- TCP에 TLS가 기본 적용되지 않습니다. 공유키 암호화의 [보호 범위](docs/protocol/ENCRYPTION.md)를 확인하십시오.
- 영속 체크포인트와 부분 파일 검증을 사용하는 [재개 동작](docs/protocol/RESUME.md).

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

Use the pinned compatible dependencies for this implementation branch:
~~~sh
python3 tools/bootstrap_dependencies.py --jobs 4
python3 tools/ci_build.py --build build --examples
python3 tools/sync_readme_snippets.py
python3 tools/check_quick_start.py --build build --artifacts build/quick-start-results
~~~
Linux requires CMake, Ninja, a C++20 compiler, OpenSSL, LZ4 and libcurl development
packages. macOS requires the corresponding Homebrew tools/libraries and
CMAKE_PREFIX_PATH set to the Homebrew prefix. Local validation used Apple Clang
21; CI configures Clang 18 on Ubuntu and Apple Clang on macOS. Windows is unverified.


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

[Public TCP measurements and runnable harness](docs/validation/PUBLIC_API_BENCHMARKS.md). 아래 로컬 분할/조립 테스트는 구성요소 마이크로벤치마크이며 네트워크 처리량 근거가 아닙니다.

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
| **Throughput** | 로컬 파일 분할/조립 마이크로벤치마크 | LAN 근거 아님 |
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

### 고급 예제
- `resume_transfer.cpp` - 전송 일시정지/재개 및 중단 처리
- `auto_reconnect.cpp` - 자동 재연결 구성 및 콜백 처리
- `custom_pipeline.cpp` - 파이프라인 커스터마이징 (청크 크기, 압축 설정)
- `bandwidth_throttling.cpp` - 업로드/다운로드 대역폭 제한
- `server_callbacks.cpp` - 서버측 요청 검증 및 접근 제어
- `quota_management.cpp` - 스토리지 쿼터 구성 및 모니터링

## 오류 처리

초기 result의 실패와 핸들의 최종 전송 실패를 모두 처리해야 합니다. 빠른 시작의 wait_verified 함수는 두 경우를 구분하고 실패 시 성공을 출력하지 않습니다. error().message는 필드이며 함수가 아닙니다. 재개는 [체크포인트 API](docs/protocol/RESUME.md)를 사용합니다.

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

연결 해제 후 재시도 정책은 실제 공개 API로 설정합니다. 초기 연결 실패는 호출자가 처리해야 하며, 전송 재개에는 체크포인트 설정이 필요합니다.

~~~cpp
auto built = file_transfer_client::builder()
    .with_auto_reconnect(true, reconnect_policy{})
    .build();
if (!built) throw std::runtime_error(built.error().message);
auto client = std::move(built.value());
client.on_connection_state_changed([](connection_state state) {
    std::cout << "Connection state: " << static_cast<int>(state) << "\n";
});
~~~

[Reconnect behavior and limits](docs/protocol/RECONNECTION.md).

## 라이선스

이 프로젝트는 BSD 3-Clause 라이선스 하에 배포됩니다. 자세한 내용은 [LICENSE](LICENSE) 파일을 참조하세요.

## 기여

기여를 환영합니다! PR을 제출하기 전에 기여 가이드라인을 읽어주세요.

## 로드맵

PRD의 7단계를 기준으로 합니다. 상태와 검증 범위는 [구현 현황](docs/IMPLEMENTATION_STATUS.md)에 기록되어 있으며, 전체 단계 완료를 의미하지 않습니다.

<!-- roadmap:start -->
| PRD phase | Requirements | Current status and evidence | Remaining acceptance |
|---|---|---|---|
| 1. Core infrastructure | FR-08, FR-11, FR-14, FR-17/18 | Validated (scoped): codec, CRC/SHA, LZ4 and bounded pipeline [#175–#178, #188](docs/protocol/PIPELINE_CAPACITY.md) | NFR-03/07/08 absolute performance unmeasured [#190](https://github.com/kcenon/file_trans_system/issues/190) |
| 2. Server foundation | FR-01/03/05/07/16 | Validated (scoped): real storage, requests, listing, quotas [#175–#179](docs/validation/REGRESSION_GATES.md) | Broad platform/service qualification; retention policies have component evidence only |
| 3. Client foundation | FR-02/03/05/07, NFR-13 | Validated (scoped): TCP transfers, failure handling and reconnect [#176–#179, #185](docs/protocol/RECONNECTION.md) | Optional authentication and TLS 1.3 NFR-16/17 remain open; reconnect does not imply resume |
| 4. Compression and resume | FR-09/11/14/15 | Validated (scoped): plain/encrypted process crash recovery and compression [#183, #186](docs/protocol/RESUME.md) | Subsecond recovery only measured on a small loopback fixture; large-prefix validation remains linear |
| 5. Advanced features | FR-04/06/10/12/13/16/18 | Validated (scoped): batch, progress, throttling, quotas and admission [#187–#188](docs/protocol/BATCH_TRANSFERS.md) | “Without degradation” and 5% bandwidth tolerance need qualification; 100-client uploads and downloads now all pass [#190](docs/validation/PUBLIC_API_BENCHMARKS.md) |
| 6. Integration and polish | PRD §10, NFR-01–10/22 | Validated (scoped): public monitoring/quota, logger, cloud/hybrid persistence and compiled examples [#189–#191](docs/validation/FOLLOWUP_176_190.md) | Coverage 71.98% <80%; full macOS TSAN fails on reproduced errno runtime reports; leak freedom, Windows and dedicated LAN unverified |
| 7. QUIC (optional) | PRD §9 phase 7, NFR-16 | Component: public transport selection and repaired QUIC timeout cleanup [#184](docs/protocol/TRANSPORTS.md) | QUIC handshake/key installation and certificate/UDP routing remain blocked in pinned dependency; 0-RTT/migration unvalidated |
<!-- roadmap:end -->
