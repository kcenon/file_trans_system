# 의존성 요구사항 가이드

**file_trans_system** 라이브러리의 필수 의존성 요구사항입니다.

**버전:** 0.3.0
**아키텍처:** 클라이언트-서버 모델

---

## 목차

1. [개요](#개요)
2. [필수 의존성](#필수-의존성)
3. [common_system](#common_system)
4. [thread_system](#thread_system)
5. [logger_system](#logger_system)
6. [container_system](#container_system)
7. [network_system](#network_system)
8. [통합 가이드](#통합-가이드)
9. [빌드 설정](#빌드-설정)

---

## 개요

### 왜 이 의존성들이 필수인가

**file_trans_system**은 고성능 파일 전송을 위한 필수 서비스를 제공하는 모듈형 인프라 위에 구축되었습니다. **5개의 핵심 시스템 모두 필수**이며 함께 사용해야 합니다.

```
┌─────────────────────────────────────────────────────────────────┐
│                      file_trans_system                          │
│                 (file_transfer_server/client)                   │
└───────────────────────────┬─────────────────────────────────────┘
                            │
        ┌───────────────────┼───────────────────┐
        │                   │                   │
        ▼                   ▼                   ▼
┌───────────────┐   ┌───────────────┐   ┌───────────────┐
│ network_system│   │ thread_system │   │ logger_system │
└───────┬───────┘   └───────┬───────┘   └───────┬───────┘
        │                   │                   │
        └───────────────────┼───────────────────┘
                            │
                            ▼
                   ┌───────────────┐
                   │container_system│
                   └───────┬───────┘
                            │
                            ▼
                   ┌───────────────┐
                   │ common_system │
                   └───────────────┘
```

### 의존성 계층 구조

| 레벨 | 시스템 | 의존 대상 |
|------|--------|-----------|
| 0 | common_system | (없음 - 기반) |
| 1 | container_system | common_system |
| 2 | thread_system | common_system, container_system |
| 2 | logger_system | common_system, container_system |
| 3 | network_system | common_system, container_system, thread_system |
| 4 | **file_trans_system** | **위의 모든 시스템** |

---

## 필수 의존성

### 요약 표

| 시스템 | 용도 | 필수 | 대체 가능 |
|--------|------|------|-----------|
| **common_system** | 기본 유틸리티, 오류 처리, 타입 | **예** | 아니오 |
| **thread_system** | 스레드 풀, 동시성, 비동기 작업 | **예** | 아니오 |
| **logger_system** | 구조화된 로깅, 진단 | **예** | 아니오 |
| **container_system** | 고성능 컨테이너, 버퍼 | **예** | 아니오 |
| **network_system** | TCP/QUIC 전송, 연결 관리 | **예** | 아니오 |

> **중요**: 이 의존성들은 **선택 사항이 아닙니다**. file_trans_system은 5개 시스템 모두가 올바르게 통합되지 않으면 컴파일되지 않거나 정상적으로 작동하지 않습니다.

### 왜 대체가 불가능한가?

1. **긴밀한 통합**: 내부 API가 특정 컨테이너 타입과 스레딩 기본 요소에 의존
2. **성능 보장**: 5단계 파이프라인이 특정 스레드 풀 동작을 요구
3. **오류 전파**: 오류 코드와 결과 타입이 common_system에 정의됨
4. **메모리 관리**: 버퍼 풀링이 container_system에서 처리됨
5. **프로토콜 구현**: 네트워크 프레이밍이 network_system 기본 요소 위에 구축됨

---

## common_system

### 용도

모든 다른 시스템에서 사용되는 기본 유틸리티를 제공합니다.

### 필수 컴포넌트

| 컴포넌트 | file_trans_system에서의 사용 |
|----------|------------------------------|
| `result<T, E>` | 모든 API 반환 타입 |
| `error_code` | 오류 처리 및 전파 |
| `expected<T>` | 선택적 값 처리 |
| `span<T>` | 제로카피 데이터 뷰 |
| `byte_buffer` | 원시 바이트 조작 |
| `uuid` | 전송 ID, 클라이언트 ID |
| `endpoint` | 서버/클라이언트 주소 |
| `duration`, `time_point` | 타임아웃, 스케줄링 |

### 코드 예제

```cpp
#include <kcenon/common/result.h>
#include <kcenon/common/error_code.h>
#include <kcenon/common/endpoint.h>

using namespace kcenon::common;

// 모든 file_transfer API는 result<T, error>를 반환
result<transfer_id, error> upload_result = client->upload_file(path, name);

if (!upload_result) {
    error_code code = upload_result.error().code();
    std::string msg = upload_result.error().message();
    // 오류 처리
}

// 연결을 위한 엔드포인트
endpoint server_addr{"192.168.1.100", 19000};
client->connect(server_addr);
```

### 필수인 이유

- **result<T, E>**: 모든 공개 API가 오류 처리에 이것을 사용
- **error_code**: 모든 오류 코드(-700 ~ -799)가 여기에 정의됨
- **endpoint**: 서버 바인딩과 클라이언트 연결에 필수
- **uuid**: 전송 추적과 클라이언트 식별에 사용

---

## thread_system

### 용도

5단계 파이프라인을 위한 고성능 스레드 풀과 비동기 기본 요소를 제공합니다.

### 필수 컴포넌트

| 컴포넌트 | file_trans_system에서의 사용 |
|----------|------------------------------|
| `thread_pool` | 각 파이프라인 단계의 워커 스레드 |
| `priority_thread_pool` | 우선순위 기반 작업 스케줄링 |
| `async_task<T>` | 비동기 작업 결과 |
| `semaphore` | 백프레셔 제어 |
| `mutex`, `condition_variable` | 동기화 |
| `atomic_queue` | 락프리 단계 간 큐 |

### 코드 예제

```cpp
#include <kcenon/thread/thread_pool.h>
#include <kcenon/thread/async_task.h>

using namespace kcenon::thread;

// 파이프라인 설정은 내부적으로 thread_pool 사용
pipeline_config config{
    .io_read_workers = 2,       // 파일 I/O용 스레드
    .compression_workers = 4,   // LZ4 압축용 스레드
    .network_workers = 2,       // 네트워크 송수신용 스레드
    .send_queue_size = 64,      // 제한된 큐 크기
    .recv_queue_size = 64
};

// 비동기 작업은 async_task<T> 반환
async_task<transfer_result> task = client->upload_file_async(path, name);
transfer_result result = task.get();  // 완료될 때까지 블록
```

### 필수인 이유

- **5단계 파이프라인**: 각 단계가 전용 스레드 풀 워커에서 실행
- **백프레셔**: 제한된 큐로 메모리 고갈 방지
- **동시 전송**: 다중 동시 업로드/다운로드
- **비동기 API**: 논블로킹 작업이 async_task 사용

---

## logger_system

### 용도

디버깅, 모니터링, 진단을 위한 구조화된 로깅을 제공합니다.

### 필수 컴포넌트

| 컴포넌트 | file_trans_system에서의 사용 |
|----------|------------------------------|
| `logger` | 메인 로깅 인터페이스 |
| `log_level` | DEBUG, INFO, WARN, ERROR, FATAL |
| `log_context` | 구조화된 컨텍스트 (transfer_id, client_id) |
| `log_sink` | 출력 대상 (파일, 콘솔, 네트워크) |
| `performance_logger` | 처리량 및 지연 시간 메트릭 |

### 코드 예제

```cpp
#include <kcenon/logger/logger.h>

using namespace kcenon::logger;

// logger는 file_trans_system에서 자동으로 사용됨
// 디버깅을 위한 로그 레벨 설정
logger::set_level(log_level::debug);

// 영구 로그를 위한 파일 싱크 추가
logger::add_sink(file_sink{"/var/log/file_transfer.log"});

// 컨텍스트가 있는 구조화된 로깅
logger::info("전송 시작됨", {
    {"transfer_id", transfer.id.to_string()},
    {"file_name", transfer.filename},
    {"file_size", transfer.size}
});
```

### 로그 형식

```
2025-12-11T10:30:45.123Z [INFO] [file_trans] Transfer started
  transfer_id=550e8400-e29b-41d4-a716-446655440000
  file_name=data.zip
  file_size=1073741824
  client_addr=192.168.1.50:45678
```

### 필수인 이유

- **디버깅**: 내부 작업이 광범위하게 로깅
- **오류 진단**: 오류 컨텍스트가 로그 상관관계 ID 포함
- **성능 모니터링**: 처리량 메트릭이 로깅됨
- **감사 추적**: 업로드/다운로드 작업이 로깅됨

---

## container_system

### 용도

파일 전송 작업에 최적화된 고성능 컨테이너를 제공합니다.

### 필수 컴포넌트

| 컴포넌트 | file_trans_system에서의 사용 |
|----------|------------------------------|
| `chunk_buffer` | 고정 크기 청크 저장소 |
| `buffer_pool` | 사전 할당된 버퍼 관리 |
| `ring_buffer` | 락프리 프로듀서-컨슈머 큐 |
| `flat_map` | 빠른 키-값 검색 |
| `small_vector` | 스택 할당 소형 벡터 |
| `object_pool` | 재사용 가능한 객체 할당 |

### 코드 예제

```cpp
#include <kcenon/container/chunk_buffer.h>
#include <kcenon/container/buffer_pool.h>

using namespace kcenon::container;

// 청크 버퍼는 내부적으로 파일 데이터에 사용
// 버퍼 풀은 할당 오버헤드 감소
buffer_pool<chunk_buffer> pool{
    .chunk_size = 256 * 1024,  // 256KB 청크
    .initial_count = 128,       // 128개 버퍼 사전 할당
    .max_count = 1024          // 최대 1024개 버퍼
};

// 풀에서 버퍼 획득 (핫 패스에서 제로 할당)
auto buffer = pool.acquire();
// ... 버퍼 사용 ...
pool.release(std::move(buffer));
```

### 필수인 이유

- **제로카피**: chunk_buffer가 제로카피 데이터 흐름 지원
- **메모리 효율성**: buffer_pool이 할당 오버헤드 제거
- **락프리 큐**: ring_buffer로 단계 간 통신
- **빠른 검색**: flat_map으로 클라이언트와 전송 관리

---

## network_system

### 용도

TCP 및 QUIC 프로토콜을 위한 전송 계층 추상화를 제공합니다.

### 필수 컴포넌트

| 컴포넌트 | file_trans_system에서의 사용 |
|----------|------------------------------|
| `tcp_server` | 서버 측 TCP 리스너 |
| `tcp_client` | 클라이언트 측 TCP 연결 |
| `connection` | 추상 연결 인터페이스 |
| `frame_codec` | 메시지 프레이밍 및 파싱 |
| `connection_pool` | 연결 재사용 |
| `ssl_context` | TLS 암호화 (선택 사항) |

### 코드 예제

```cpp
#include <kcenon/network/tcp_server.h>
#include <kcenon/network/tcp_client.h>

using namespace kcenon::network;

// file_transfer_server는 내부적으로 tcp_server 사용
// file_transfer_client는 내부적으로 tcp_client 사용

// 전송 프로토콜 선택
auto client = file_transfer_client::builder()
    .with_transport(transport_type::tcp)   // 기본값
    // .with_transport(transport_type::quic)  // Phase 2
    .build();

// 연결 이벤트는 network_system에서 전파
client->on_disconnected([](disconnect_reason reason) {
    // reason은 network_system에서 옴
});
```

### 프로토콜 스택

```
┌─────────────────────────────────────┐
│        file_trans_system            │
│        (애플리케이션 프로토콜)        │
├─────────────────────────────────────┤
│          frame_codec                │
│       (길이 프리픽스 프레이밍)        │
├─────────────────────────────────────┤
│     tcp_client / tcp_server         │
│            (전송)                    │
├─────────────────────────────────────┤
│          TCP / QUIC                 │
│          (운영체제)                  │
└─────────────────────────────────────┘
```

### 필수인 이유

- **전송 추상화**: TCP/QUIC를 위한 통합 인터페이스
- **연결 관리**: 재연결, 풀링, 킵얼라이브
- **프레임 코덱**: 와이어 프로토콜 구현
- **이벤트 시스템**: 연결 상태 변경 알림

---

## 통합 가이드

### 포함 순서

컴파일 문제를 피하기 위해 의존성 순서로 헤더를 포함하세요:

```cpp
// 1. common_system (기반)
#include <kcenon/common/result.h>
#include <kcenon/common/error_code.h>
#include <kcenon/common/endpoint.h>

// 2. container_system
#include <kcenon/container/buffer_pool.h>

// 3. thread_system
#include <kcenon/thread/thread_pool.h>

// 4. logger_system
#include <kcenon/logger/logger.h>

// 5. network_system
#include <kcenon/network/tcp_client.h>

// 6. file_trans_system (최상위)
#include <kcenon/file_transfer/file_transfer.h>
```

### 초기화 순서

시스템은 의존성 순서로 초기화해야 합니다:

```cpp
int main() {
    // 1. 진단을 위해 logger 먼저 초기화
    kcenon::logger::initialize({
        .level = log_level::info,
        .sinks = {console_sink{}, file_sink{"/var/log/app.log"}}
    });

    // 2. 스레드 풀 초기화
    kcenon::thread::initialize({
        .default_pool_size = std::thread::hardware_concurrency()
    });

    // 3. 네트워크 초기화 (커스텀 설정 사용 시)
    kcenon::network::initialize({
        .tcp_nodelay = true,
        .keep_alive = true
    });

    // 4. 이제 file_trans_system 사용 가능
    auto server = file_transfer_server::builder()
        .with_storage_directory("/data")
        .build();

    server->start(endpoint{"0.0.0.0", 19000});

    // ... 애플리케이션 코드 ...

    // 역순으로 종료
    server->stop();
    kcenon::network::shutdown();
    kcenon::thread::shutdown();
    kcenon::logger::shutdown();
}
```

### 통합 헤더 사용

편의를 위해 모든 의존성을 포함하는 통합 헤더를 사용하세요:

```cpp
#include <kcenon/file_transfer/file_transfer.h>

// 이 헤더는 자동으로 다음을 포함:
// - common_system 필수 요소
// - thread_system (pipeline_config용)
// - logger_system (로그 설정용)
// - container_system (버퍼 타입용)
// - network_system (전송 타입용)

using namespace kcenon::file_transfer;
```

---

## 빌드 설정

### CMake 통합

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_app)

# 모든 필수 패키지 찾기
find_package(kcenon_common REQUIRED)
find_package(kcenon_container REQUIRED)
find_package(kcenon_thread REQUIRED)
find_package(kcenon_logger REQUIRED)
find_package(kcenon_network REQUIRED)
find_package(kcenon_file_transfer REQUIRED)

add_executable(my_app main.cpp)

# 모든 의존성 링크 (순서 중요!)
target_link_libraries(my_app PRIVATE
    kcenon::common
    kcenon::container
    kcenon::thread
    kcenon::logger
    kcenon::network
    kcenon::file_transfer
)
```

### vcpkg 통합

```json
{
  "name": "my-app",
  "dependencies": [
    "kcenon-common",
    "kcenon-container",
    "kcenon-thread",
    "kcenon-logger",
    "kcenon-network",
    "kcenon-file-transfer"
  ]
}
```

### Conan 통합

```txt
[requires]
kcenon-common/0.3.0
kcenon-container/0.3.0
kcenon-thread/0.3.0
kcenon-logger/0.3.0
kcenon-network/0.3.0
kcenon-file-transfer/0.3.0

[generators]
cmake
```

---

## 버전 호환성

### 최소 버전

| 시스템 | 최소 버전 | 권장 |
|--------|----------|------|
| common_system | 0.3.0 | 0.3.0 |
| container_system | 0.3.0 | 0.3.0 |
| thread_system | 0.3.0 | 0.3.0 |
| logger_system | 0.3.0 | 0.3.0 |
| network_system | 0.3.0 | 0.3.0 |
| file_trans_system | 0.3.0 | 0.3.0 |

### ABI 호환성

모든 시스템은 시맨틱 버전을 따릅니다. 메이저 버전 내에서:
- **마이너 버전 변경**: ABI 호환, 새 기능
- **패치 버전 변경**: ABI 호환, 버그 수정만

> **경고**: 이 시스템들의 다른 메이저 버전을 혼합하는 것은 **지원되지 않으며** 정의되지 않은 동작을 초래합니다.

---

## 문제 해결

### 일반적인 빌드 오류

| 오류 | 원인 | 해결책 |
|------|------|--------|
| `undefined reference to kcenon::common::*` | common_system 링크 안됨 | target_link_libraries에 `kcenon::common` 추가 |
| `cannot find <kcenon/thread/thread_pool.h>` | thread_system을 찾을 수 없음 | `find_package(kcenon_thread REQUIRED)` 실행 |
| `ABI mismatch detected` | 버전 불일치 | 모든 시스템이 같은 메이저 버전 사용 확인 |
| `logger not initialized` | 초기화 누락 | `kcenon::logger::initialize()` 먼저 호출 |

### 런타임 오류

| 오류 | 원인 | 해결책 |
|------|------|--------|
| `thread_pool exhausted` | 워커 부족 | `pipeline_config` 워커 수 증가 |
| `buffer_pool exhausted` | 버퍼 부족 | `buffer_pool::max_count` 증가 |
| `connection refused` | 네트워크 미초기화 | `kcenon::network::initialize()` 호출 |

---

## 참고 문서

- [API 레퍼런스](api-reference_KR.md) - 완전한 API 문서
- [파이프라인 아키텍처](pipeline-architecture_KR.md) - 5단계 파이프라인 상세
- [설정 가이드](configuration_KR.md) - 모든 설정 옵션
- [빠른 참조](quick-reference_KR.md) - 일반적인 사용 패턴

---

*최종 업데이트: 2025-12-24*
*버전: 0.3.0*
