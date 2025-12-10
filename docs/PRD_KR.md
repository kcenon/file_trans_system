# 파일 전송 시스템 - 제품 요구사항 명세서

## 문서 정보

| 항목 | 내용 |
|------|------|
| **프로젝트명** | file_trans_system |
| **버전** | 1.0.0 |
| **상태** | 초안 |
| **작성일** | 2025-12-11 |
| **작성자** | kcenon@naver.com |

---

## 1. 개요

### 1.1 목적

**file_trans_system**은 다수의 파일을 신뢰성 있게 송수신하고, 청크 기반 스트리밍 전송 기능을 제공하는 고성능 프로덕션급 C++20 파일 전송 라이브러리입니다. 기존 에코시스템(common_system, thread_system, logger_system, monitoring_system, container_system, network_system)과 원활하게 통합되어 엔터프라이즈급 파일 전송 기능을 제공합니다.

### 1.2 목표

1. **다중 파일 전송**: 단일 세션에서 여러 파일의 동시 전송 지원
2. **청크 기반 스트리밍**: 설정 가능한 청크 분할을 통한 대용량 파일 전송 지원
3. **실시간 LZ4 압축**: 청크 단위 압축/해제를 통한 실질적 전송량 증가
4. **신뢰성**: 체크섬, 재개 기능, 오류 복구를 통한 데이터 무결성 보장
5. **성능**: 비동기 I/O와 스레드 풀을 활용한 높은 처리량 달성
6. **가시성**: 모니터링 및 로깅 시스템과의 완전한 통합
7. **보안**: TLS/SSL을 통한 암호화 전송 지원

### 1.3 성공 지표

| 지표 | 목표값 |
|------|--------|
| 처리량 (1GB 파일, LAN) | ≥ 500 MB/s |
| 처리량 (1GB 파일, WAN) | ≥ 100 MB/s (네트워크 제한) |
| 압축을 통한 실질 처리량 | 압축 가능 데이터에서 2-4배 향상 |
| LZ4 압축 속도 | 코어당 ≥ 400 MB/s |
| LZ4 해제 속도 | 코어당 ≥ 1.5 GB/s |
| 압축률 (텍스트/로그) | 일반적으로 2:1 ~ 4:1 |
| 메모리 사용량 | 기본 < 50 MB |
| 재개 정확도 | 100% (체크섬 검증) |
| 동시 전송 | ≥ 100개 파일 동시 전송 |

---

## 2. 문제 정의

### 2.1 현재 과제

1. **대용량 파일 처리**: 가용 메모리보다 큰 파일 전송 시 스트리밍 필요
2. **네트워크 불안정성**: 중단된 전송은 전체 파일 재전송 없이 재개되어야 함
3. **대역폭 제한**: 네트워크 대역폭이 병목인 경우가 많음; 압축으로 실질 처리량 증가 가능
4. **다중 파일 조정**: 배치 전송 시 파일별 진행 상황 추적 및 오류 처리 필요
5. **리소스 관리**: 메모리, 디스크 I/O, 네트워크 대역폭의 효율적 사용
6. **크로스 플랫폼 지원**: Linux, macOS, Windows에서 일관된 동작

### 2.2 사용 사례

| 사용 사례 | 설명 |
|----------|------|
| **UC-01** | 두 엔드포인트 간 단일 대용량 파일(>10GB) 전송 |
| **UC-02** | 배치 작업으로 여러 소형 파일 전송 |
| **UC-03** | 마지막 성공 청크부터 중단된 전송 재개 |
| **UC-04** | 상세 지표를 통한 전송 진행 상황 모니터링 |
| **UC-05** | 신뢰할 수 없는 네트워크에서의 보안 파일 전송 |
| **UC-06** | 대역폭 조절이 가능한 우선순위 전송 큐 |
| **UC-07** | 압축 가능 파일(로그, 텍스트, JSON) 실시간 압축 전송 |
| **UC-08** | 이미 압축된 파일(ZIP, 미디어)은 이중 압축 오버헤드 없이 전송 |

---

## 3. 시스템 아키텍처

### 3.1 상위 수준 아키텍처

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           file_trans_system                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌────────────────────┐ │
│  │   송신      │  │   수신      │  │   전송      │  │      진행          │ │
│  │   엔진      │  │   엔진      │  │   관리자    │  │      추적기        │ │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └─────────┬──────────┘ │
│         │                │                │                    │            │
│  ┌──────▼────────────────▼────────────────▼────────────────────▼──────────┐ │
│  │                         청크 관리자                                     │ │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐ │ │
│  │  │ 분할기   │  │ 조립기   │  │ 체크섬   │  │  재개    │  │   LZ4    │ │ │
│  │  │          │  │          │  │          │  │ 핸들러   │  │  압축기  │ │ │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘ │ │
│  └─────────────────────────────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────────────────────┤
│                            통합 계층                                         │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────┐│
│  │ common   │ │ thread   │ │ logger   │ │monitoring│ │ network  │ │ LZ4  ││
│  │ _system  │ │ _system  │ │ _system  │ │ _system  │ │ _system  │ │ lib  ││
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────┘│
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 데이터 파이프라인 아키텍처

typed_thread_pool을 활용하여 각 처리 단계를 병렬로 실행하는 파이프라인 아키텍처를 구현합니다.

#### 3.2.1 송신자 파이프라인

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           송신자 파이프라인                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐              │
│  │  파일    │    │   청크   │    │   LZ4    │    │  네트워크 │              │
│  │  읽기    │───▶│   구성   │───▶│   압축   │───▶│   전송   │              │
│  └──────────┘    └──────────┘    └──────────┘    └──────────┘              │
│       │               │               │               │                     │
│       ▼               ▼               ▼               ▼                     │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐              │
│  │ I/O 바운드 │  │ CPU 경량  │   │ CPU 집약  │   │ I/O 바운드 │              │
│  │ 2 워커    │  │ 2 워커    │   │ 4 워커    │   │ 2 워커    │              │
│  └──────────┘    └──────────┘    └──────────┘    └──────────┘              │
│                                                                              │
│  typed_thread_pool<pipeline_stage>                                          │
│  ├── io_read:      파일 I/O 작업                                            │
│  ├── chunk_process: 청크 분할/헤더 생성                                      │
│  ├── compression:  LZ4 압축 (CPU 집약)                                       │
│  └── network:      네트워크 송신                                             │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

#### 3.2.2 수신자 파이프라인

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           수신자 파이프라인                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐              │
│  │ 네트워크 │    │   LZ4    │    │   청크   │    │  파일    │              │
│  │   수신   │───▶│   해제   │───▶│   조립   │───▶│  쓰기    │              │
│  └──────────┘    └──────────┘    └──────────┘    └──────────┘              │
│       │               │               │               │                     │
│       ▼               ▼               ▼               ▼                     │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐              │
│  │ I/O 바운드 │  │ CPU 집약  │   │ CPU 경량  │   │ I/O 바운드 │              │
│  │ 2 워커    │  │ 4 워커    │   │ 2 워커    │   │ 2 워커    │              │
│  └──────────┘    └──────────┘    └──────────┘    └──────────┘              │
│                                                                              │
│  typed_thread_pool<pipeline_stage>                                          │
│  ├── network:      네트워크 수신                                             │
│  ├── compression:  LZ4 해제 (CPU 집약)                                       │
│  ├── chunk_process: 청크 조립/검증                                           │
│  └── io_write:     파일 I/O 작업                                            │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

#### 3.2.3 파이프라인 단계 타입

```cpp
// 파이프라인 단계 정의 (typed_thread_pool과 함께 사용)
enum class pipeline_stage : uint8_t {
    io_read,        // 파일 읽기 작업 (I/O 바운드)
    chunk_process,  // 청크 조립/분할 (CPU 경량)
    compression,    // LZ4 압축/해제 (CPU 바운드)
    network,        // 네트워크 송/수신 (I/O 바운드)
    io_write        // 파일 쓰기 작업 (I/O 바운드)
};

// 파이프라인 설정
struct pipeline_config {
    // 단계별 워커 수
    std::size_t io_read_workers      = 2;   // I/O 읽기 워커
    std::size_t chunk_workers        = 2;   // 청크 처리 워커
    std::size_t compression_workers  = 4;   // 압축/해제 워커 (CPU 집약)
    std::size_t network_workers      = 2;   // 네트워크 워커
    std::size_t io_write_workers     = 2;   // I/O 쓰기 워커

    // 백프레셔 제어를 위한 큐 크기
    std::size_t read_queue_size      = 16;  // 읽기 → 청크
    std::size_t compress_queue_size  = 32;  // 청크 → 압축
    std::size_t send_queue_size      = 64;  // 압축 → 전송
    std::size_t receive_queue_size   = 64;  // 수신 → 해제
    std::size_t decompress_queue_size = 32; // 해제 → 조립
    std::size_t write_queue_size     = 16;  // 조립 → 쓰기
};
```

### 3.3 컴포넌트 설명

#### 3.3.1 송신 엔진 (Sender Engine)
- 디스크에서 파일을 읽고 전송을 위한 청크 준비
- 전송 전 각 청크에 LZ4 압축 적용 (활성화된 경우)
- 우선순위 지원이 가능한 송신 큐 관리
- 흐름 제어 및 백프레셔 처리

#### 3.3.2 수신 엔진 (Receiver Engine)
- 수신 청크를 받아 무결성 검증
- LZ4 압축된 청크 해제 (압축 플래그가 설정된 경우)
- 올바른 순서로 청크를 디스크에 기록
- 순서가 뒤바뀐 청크 재조립 처리

#### 3.3.3 전송 관리자 (Transfer Manager)
- 여러 동시 전송 조정
- 전송 생명주기 관리 (초기화 → 전송 → 검증 → 완료)
- 송수신 작업을 위한 통합 API 제공
- 전송별 압축 설정 제어

#### 3.3.4 청크 관리자 (Chunk Manager)
- **분할기**: 파일을 설정 가능한 청크로 분할 (기본값: 64KB - 1MB)
- **조립기**: 수신된 청크로부터 파일 재구성
- **체크섬**: 청크/파일 무결성 계산 및 검증 (CRC32, SHA-256)
- **재개 핸들러**: 재개 기능을 위한 전송 상태 추적
- **LZ4 압축기**: 실시간 청크 단위 압축/해제

#### 3.3.5 진행 추적기 (Progress Tracker)
- 실시간 전송 진행 상황 모니터링
- 정확한 진행률을 위해 원본 및 압축 바이트 모두 추적
- 압축률 및 실질 처리량 보고
- 지표 내보내기를 위한 monitoring_system 통합
- UI/CLI 진행 표시를 위한 이벤트 콜백

---

## 4. 기능 요구사항

### 4.1 핵심 기능

#### FR-01: 단일 파일 전송
| ID | FR-01 |
|----|-------|
| **설명** | 송신자에서 수신자로 단일 파일 전송 |
| **우선순위** | P0 (필수) |
| **인수 기준** | 체크섬으로 100% 무결성 검증된 파일 전송 |

#### FR-02: 다중 파일 배치 전송
| ID | FR-02 |
|----|-------|
| **설명** | 단일 작업에서 여러 파일 전송 |
| **우선순위** | P0 (필수) |
| **인수 기준** | 모든 파일 전송, 개별 파일 상태 추적 |

#### FR-03: 청크 기반 전송
| ID | FR-03 |
|----|-------|
| **설명** | 스트리밍 전송을 위해 파일을 청크로 분할 |
| **우선순위** | P0 (필수) |
| **인수 기준** | 설정 가능한 청크 크기, 올바른 재조립 |

#### FR-04: 전송 재개
| ID | FR-04 |
|----|-------|
| **설명** | 마지막 성공 청크부터 중단된 전송 재개 |
| **우선순위** | P1 (높음) |
| **인수 기준** | 1초 이내 재개, 데이터 손실 없음 |

#### FR-05: 진행 상황 모니터링
| ID | FR-05 |
|----|-------|
| **설명** | 콜백을 통한 실시간 진행 상황 추적 |
| **우선순위** | P1 (높음) |
| **인수 기준** | 설정 가능한 간격으로 진행 상황 업데이트 |

#### FR-06: 무결성 검증
| ID | FR-06 |
|----|-------|
| **설명** | 체크섬을 사용한 데이터 무결성 검증 |
| **우선순위** | P0 (필수) |
| **인수 기준** | 청크당 CRC32, 파일당 SHA-256 |

#### FR-07: 동시 전송
| ID | FR-07 |
|----|-------|
| **설명** | 다수의 동시 파일 전송 지원 |
| **우선순위** | P1 (높음) |
| **인수 기준** | 성능 저하 없이 100개 이상 동시 전송 |

#### FR-08: 대역폭 조절
| ID | FR-08 |
|----|-------|
| **설명** | 연결별/전체 전송 대역폭 제한 |
| **우선순위** | P2 (중간) |
| **인수 기준** | 설정된 제한의 5% 이내 대역폭 |

#### FR-09: 실시간 LZ4 압축
| ID | FR-09 |
|----|-------|
| **설명** | 청크 단위 LZ4 압축/해제를 통한 실질 처리량 증가 |
| **우선순위** | P0 (필수) |
| **인수 기준** | 압축 속도 ≥400 MB/s, 해제 ≥1.5 GB/s, 사용자에게 투명하게 동작 |

#### FR-10: 적응형 압축
| ID | FR-10 |
|----|-------|
| **설명** | 압축 불가능한 데이터에 대해 자동으로 압축 건너뛰기 |
| **우선순위** | P1 (높음) |
| **인수 기준** | 1KB 샘플 내에서 압축 불가능 청크 감지, CPU 낭비 방지 |

#### FR-11: 압축 통계
| ID | FR-11 |
|----|-------|
| **설명** | 압축률 및 실질 처리량 보고 |
| **우선순위** | P2 (중간) |
| **인수 기준** | 전송별 및 전체 압축 지표 사용 가능 |

#### FR-12: 파이프라인 기반 처리
| ID | FR-12 |
|----|-------|
| **설명** | typed_thread_pool을 사용한 멀티스테이지 파이프라인 처리 |
| **우선순위** | P0 (필수) |
| **인수 기준** | 파일읽기→청크구성→압축→전송, 수신→해제→조립→파일쓰기 파이프라인 동작 |

#### FR-13: 파이프라인 백프레셔
| ID | FR-13 |
|----|-------|
| **설명** | 단계 간 바운디드 큐를 통한 백프레셔 제어 |
| **우선순위** | P1 (높음) |
| **인수 기준** | 큐 용량 초과 시 느린 단계 대기, 메모리 무한 증가 방지 |

### 4.2 API 요구사항

#### 4.2.1 송신자 API

```cpp
namespace kcenon::file_transfer {

class file_sender {
public:
    // 설정을 위한 빌더 패턴
    class builder;

    // 단일 파일 전송
    [[nodiscard]] auto send_file(
        const std::filesystem::path& file_path,
        const endpoint& destination,
        const transfer_options& options = {}
    ) -> Result<transfer_handle>;

    // 다중 파일 전송
    [[nodiscard]] auto send_files(
        std::span<const std::filesystem::path> files,
        const endpoint& destination,
        const transfer_options& options = {}
    ) -> Result<batch_transfer_handle>;

    // 전송 취소
    [[nodiscard]] auto cancel(const transfer_id& id) -> Result<void>;

    // 일시정지/재개
    [[nodiscard]] auto pause(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto resume(const transfer_id& id) -> Result<void>;

    // 진행 상황 콜백
    void on_progress(std::function<void(const transfer_progress&)> callback);
};

} // namespace kcenon::file_transfer
```

#### 4.2.2 수신자 API

```cpp
namespace kcenon::file_transfer {

class file_receiver {
public:
    class builder;

    // 수신 전송 대기 시작
    [[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;

    // 수신 중지
    [[nodiscard]] auto stop() -> Result<void>;

    // 출력 디렉토리 설정
    void set_output_directory(const std::filesystem::path& dir);

    // 수락/거부 콜백
    void on_transfer_request(
        std::function<bool(const transfer_request&)> callback
    );

    // 진행 상황 콜백
    void on_progress(std::function<void(const transfer_progress&)> callback);

    // 완료 콜백
    void on_complete(std::function<void(const transfer_result&)> callback);
};

} // namespace kcenon::file_transfer
```

#### 4.2.3 전송 관리자 API

```cpp
namespace kcenon::file_transfer {

class transfer_manager {
public:
    class builder;

    // 전송 상태 조회
    [[nodiscard]] auto get_status(const transfer_id& id)
        -> Result<transfer_status>;

    // 활성 전송 목록
    [[nodiscard]] auto list_transfers()
        -> Result<std::vector<transfer_info>>;

    // 통계 조회
    [[nodiscard]] auto get_statistics() -> transfer_statistics;

    // 압축 통계 조회
    [[nodiscard]] auto get_compression_stats() -> compression_statistics;

    // 전역 대역폭 제한 설정
    void set_bandwidth_limit(std::size_t bytes_per_second);

    // 동시 전송 제한 설정
    void set_max_concurrent_transfers(std::size_t max_count);

    // 기본 압축 모드 설정
    void set_default_compression(compression_mode mode);
};

} // namespace kcenon::file_transfer
```

#### 4.2.4 압축 API

```cpp
namespace kcenon::file_transfer {

// 압축 모드
enum class compression_mode {
    disabled,       // 압축 비활성화
    enabled,        // 항상 압축
    adaptive        // 압축 가능 여부 자동 감지 (기본값)
};

// 압축 레벨 (속도 vs 압축률 트레이드오프)
enum class compression_level {
    fast,           // LZ4 기본 - 가장 빠름
    high_compression // LZ4-HC - 더 높은 압축률, 느림
};

// 압축 설정이 포함된 전송 옵션
struct transfer_options {
    compression_mode    compression     = compression_mode::adaptive;
    compression_level   level           = compression_level::fast;
    std::size_t         chunk_size      = 256 * 1024;  // 256KB 기본값
    bool                verify_checksum = true;
    std::optional<std::size_t> bandwidth_limit;
};

// 압축 통계
struct compression_statistics {
    uint64_t    total_raw_bytes;
    uint64_t    total_compressed_bytes;
    double      compression_ratio;          // 원본 / 압축
    double      compression_speed_mbps;     // MB/s
    double      decompression_speed_mbps;   // MB/s
    uint64_t    chunks_compressed;
    uint64_t    chunks_skipped;             // 압축 불가능 청크
};

// 청크 단위 압축 인터페이스
class chunk_compressor {
public:
    // 청크 압축, 압축 불가능시 원본 반환
    [[nodiscard]] auto compress(
        std::span<const std::byte> input
    ) -> Result<compressed_chunk>;

    // 청크 해제
    [[nodiscard]] auto decompress(
        std::span<const std::byte> compressed,
        std::size_t original_size
    ) -> Result<std::vector<std::byte>>;

    // 데이터 압축 가치 여부 확인 (빠른 샘플 테스트)
    [[nodiscard]] auto is_compressible(
        std::span<const std::byte> sample
    ) -> bool;
};

} // namespace kcenon::file_transfer
```

#### 4.2.5 파이프라인 API

```cpp
namespace kcenon::file_transfer {

// typed_thread_pool 기반 송신 파이프라인
class sender_pipeline {
public:
    class builder;

    // 파이프라인 시작
    [[nodiscard]] auto start() -> Result<void>;

    // 파이프라인 중지 (진행 중인 작업 완료 대기)
    [[nodiscard]] auto stop() -> Result<void>;

    // 파일을 파이프라인에 제출
    [[nodiscard]] auto submit(
        const std::filesystem::path& file,
        const endpoint& destination,
        const transfer_options& options = {}
    ) -> Result<transfer_handle>;

    // 현재 파이프라인 통계 조회
    [[nodiscard]] auto get_statistics() const -> pipeline_statistics;

    // 각 단계별 큐 깊이 조회 (모니터링/디버깅용)
    [[nodiscard]] auto get_queue_depths() const -> queue_depth_info;
};

// typed_thread_pool 기반 수신 파이프라인
class receiver_pipeline {
public:
    class builder;

    // 파이프라인 시작 (지정 엔드포인트에서 수신 대기)
    [[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;

    // 파이프라인 중지
    [[nodiscard]] auto stop() -> Result<void>;

    // 출력 디렉토리 설정
    void set_output_directory(const std::filesystem::path& dir);

    // 현재 파이프라인 통계 조회
    [[nodiscard]] auto get_statistics() const -> pipeline_statistics;

    // 각 단계별 큐 깊이 조회
    [[nodiscard]] auto get_queue_depths() const -> queue_depth_info;
};

// 파이프라인 통계
struct pipeline_statistics {
    // 단계별 처리 항목 수
    uint64_t items_read;
    uint64_t items_chunked;
    uint64_t items_compressed;
    uint64_t items_sent;
    uint64_t items_received;
    uint64_t items_decompressed;
    uint64_t items_assembled;
    uint64_t items_written;

    // 단계별 처리 시간 (평균, 마이크로초)
    uint64_t avg_read_time_us;
    uint64_t avg_chunk_time_us;
    uint64_t avg_compress_time_us;
    uint64_t avg_send_time_us;
    uint64_t avg_receive_time_us;
    uint64_t avg_decompress_time_us;
    uint64_t avg_assemble_time_us;
    uint64_t avg_write_time_us;

    // 병목 단계 식별
    pipeline_stage bottleneck_stage;
};

// 큐 깊이 정보 (백프레셔 모니터링용)
struct queue_depth_info {
    std::size_t read_queue_depth;       // 읽기 → 청크
    std::size_t compress_queue_depth;   // 청크 → 압축
    std::size_t send_queue_depth;       // 압축 → 전송
    std::size_t receive_queue_depth;    // 수신 → 해제
    std::size_t decompress_queue_depth; // 해제 → 조립
    std::size_t write_queue_depth;      // 조립 → 쓰기

    // 큐가 꽉 찬 비율 (0.0 ~ 1.0)
    double read_queue_fill_ratio;
    double compress_queue_fill_ratio;
    double send_queue_fill_ratio;
    double receive_queue_fill_ratio;
    double decompress_queue_fill_ratio;
    double write_queue_fill_ratio;
};

} // namespace kcenon::file_transfer
```

### 4.3 데이터 구조

#### 4.3.1 청크 구조

```cpp
// 청크 플래그
enum class chunk_flags : uint8_t {
    none            = 0x00,
    first_chunk     = 0x01,     // 파일의 첫 번째 청크
    last_chunk      = 0x02,     // 파일의 마지막 청크
    compressed      = 0x04,     // LZ4 압축됨
    encrypted       = 0x08      // TLS 암호화 (예약)
};

struct chunk_header {
    transfer_id     transfer_id;        // 고유 전송 식별자
    uint64_t        file_index;         // 배치 전송에서의 파일 인덱스
    uint64_t        chunk_index;        // 청크 순서 번호
    uint64_t        chunk_offset;       // 원본 파일 내 바이트 오프셋
    uint32_t        original_size;      // 원본 (비압축) 데이터 크기
    uint32_t        compressed_size;    // 압축 크기 (비압축시 원본과 동일)
    uint32_t        checksum;           // 원본 (비압축) 데이터의 CRC32
    chunk_flags     flags;              // 압축 플래그를 포함한 청크 플래그
};

struct chunk {
    chunk_header            header;
    std::vector<std::byte>  data;       // 압축 가능
};

struct compressed_chunk {
    std::vector<std::byte>  data;
    uint32_t                original_size;
    bool                    is_compressed;  // 압축 건너뛴 경우 false
};
```

#### 4.3.2 전송 메타데이터

```cpp
struct file_metadata {
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    std::filesystem::perms  permissions;
    std::chrono::system_clock::time_point modified_time;
    bool                    compressible_hint;  // 파일 확장자 기반 힌트
};

struct transfer_request {
    transfer_id                     id;
    std::vector<file_metadata>      files;
    transfer_options                options;
};

struct transfer_progress {
    transfer_id     id;
    uint64_t        bytes_transferred;      // 원본 바이트 (비압축)
    uint64_t        bytes_on_wire;          // 실제 전송 바이트 (압축)
    uint64_t        total_bytes;            // 총 원본 바이트
    double          transfer_rate;          // 원본 바이트/초
    double          effective_rate;         // 압축 고려한 실질 속도
    double          compression_ratio;      // 현재 압축률
    duration        elapsed_time;
    duration        estimated_remaining;
    transfer_state  state;
};
```

---

## 5. 비기능 요구사항

### 5.1 성능

| 요구사항 | 목표값 | 측정 방법 |
|----------|--------|-----------|
| **NFR-01** 처리량 | ≥500 MB/s (LAN) | 1GB 파일 전송 시간 |
| **NFR-02** 지연시간 | <10ms 청크 처리 | 종단간 청크 지연시간 |
| **NFR-03** 메모리 | <50MB 기본 | 유휴 시 RSS |
| **NFR-04** 메모리 (전송 중) | 1GB 전송당 <100MB | 전송 중 RSS |
| **NFR-05** CPU 사용률 | 코어당 <30% | 지속 전송 중 |
| **NFR-06** LZ4 압축 | ≥400 MB/s | 압축 처리량 |
| **NFR-07** LZ4 해제 | ≥1.5 GB/s | 해제 처리량 |
| **NFR-08** 압축률 | 텍스트에서 2:1 ~ 4:1 | 일반 압축 가능 데이터 |
| **NFR-09** 적응형 감지 | <100μs | 압축 가능 여부 확인 시간 |

### 5.2 신뢰성

| 요구사항 | 목표값 |
|----------|--------|
| **NFR-10** 데이터 무결성 | 100% (SHA-256 검증) |
| **NFR-11** 재개 정확도 | 100% 성공적 재개 |
| **NFR-12** 오류 복구 | 지수 백오프를 통한 자동 재시도 |
| **NFR-13** 우아한 성능 저하 | 부하 시 처리량 감소 |
| **NFR-14** 압축 폴백 | 해제 오류 시 원활한 폴백 |

### 5.3 보안

| 요구사항 | 설명 |
|----------|------|
| **NFR-15** 암호화 | 네트워크 전송에 TLS 1.3 |
| **NFR-16** 인증 | 선택적 인증서 기반 인증 |
| **NFR-17** 경로 순회 | 디렉토리 탈출 공격 방지 |
| **NFR-18** 리소스 제한 | 최대 파일 크기, 전송 횟수 제한 |

### 5.4 호환성

| 요구사항 | 설명 |
|----------|------|
| **NFR-19** C++ 표준 | C++20 이상 |
| **NFR-20** 플랫폼 | Linux, macOS, Windows |
| **NFR-21** 컴파일러 | GCC 11+, Clang 14+, MSVC 19.29+ |
| **NFR-22** LZ4 라이브러리 | LZ4 1.9.0+ (BSD 라이선스) |

---

## 6. 통합 요구사항

### 6.1 시스템 의존성

| 시스템 | 용도 | 필수 여부 |
|--------|------|----------|
| **common_system** | Result<T>, 인터페이스, 오류 코드 | 예 |
| **thread_system** | 비동기 작업 실행, 스레드 풀 | 예 |
| **network_system** | TCP/TLS (1단계) 및 QUIC (2단계) 전송 | 예 |
| **container_system** | 청크 직렬화 | 예 |
| **LZ4** | 실시간 압축/해제 | 예 |
| **logger_system** | 진단 로깅 | 선택 |
| **monitoring_system** | 지표 및 추적 | 선택 |

> **참고**: network_system이 TCP와 QUIC 전송을 모두 제공하므로 별도의 외부 QUIC 라이브러리는 필요하지 않습니다.

### 6.2 전송 프로토콜 설계

#### 6.2.1 프로토콜 선택 근거

**HTTP는 본 시스템에서 명시적으로 제외됩니다.** 그 이유는 다음과 같습니다:
- 스트리밍 파일 전송에 불필요한 추상화 계층
- 높은 헤더 오버헤드 (요청당 ~800 바이트)가 고빈도 청크 전송에 부적합
- 비상태(stateless) 설계가 연결 기반 재개 기능과 충돌
- HTTP 청크 인코딩 의미론이 우리의 청크 기반 전송 모델과 상이

**지원 전송 프로토콜:**

| 프로토콜 | 단계 | 우선순위 | 사용 사례 |
|----------|------|----------|-----------|
| **TCP + TLS 1.3** | 1단계 | 기본 | 모든 환경, 기본값 |
| **QUIC** | 2단계 | 선택 | 고손실 네트워크, 모바일 |

#### 6.2.2 커스텀 애플리케이션 프로토콜

최소 오버헤드를 위해 TCP/QUIC 위에 경량 커스텀 프로토콜을 사용합니다:

```cpp
// 메시지 유형 (1 바이트)
enum class message_type : uint8_t {
    // 세션 관리
    handshake_request   = 0x01,
    handshake_response  = 0x02,

    // 전송 제어
    transfer_request    = 0x10,
    transfer_accept     = 0x11,
    transfer_reject     = 0x12,
    transfer_cancel     = 0x13,

    // 데이터 전송
    chunk_data          = 0x20,
    chunk_ack           = 0x21,
    chunk_nack          = 0x22,  // 재전송 요청

    // 재개
    resume_request      = 0x30,
    resume_response     = 0x31,

    // 완료
    transfer_complete   = 0x40,
    transfer_verify     = 0x41,

    // 제어
    keepalive           = 0xF0,
    error               = 0xFF
};

// 메시지 프레임 (5 바이트 오버헤드)
struct message_frame {
    message_type    type;           // 1 바이트
    uint32_t        payload_length; // 4 바이트 (빅 엔디안)
    // 페이로드 뒤따름...
};
```

**오버헤드 비교 (1GB 파일, 256KB 청크 = 4,096 청크):**

| 프로토콜 | 청크당 오버헤드 | 총 오버헤드 | 비율 |
|----------|----------------|-------------|------|
| HTTP/1.1 | ~800 바이트 | ~3.2 MB | 0.31% |
| 커스텀/TCP | 54 바이트 | ~221 KB | 0.02% |
| 커스텀/QUIC | ~74 바이트 | ~303 KB | 0.03% |

#### 6.2.3 TCP 전송 (1단계 - 필수)

**장점:**
- 40년 이상의 검증된 신뢰성
- 모든 플랫폼에서 커널 수준 최적화
- network_system에서 이미 지원
- 방화벽 친화적 (TLS와 함께 포트 443)

**설정:**
```cpp
struct tcp_transport_config {
    bool        enable_tls      = true;     // TLS 1.3
    bool        tcp_nodelay     = true;     // Nagle 알고리즘 비활성화
    std::size_t send_buffer     = 256 * 1024;
    std::size_t recv_buffer     = 256 * 1024;
    duration    connect_timeout = 10s;
    duration    read_timeout    = 30s;
};
```

#### 6.2.4 QUIC 전송 (2단계 - 선택)

**장점:**
- 0-RTT 연결 재개
- HOL(Head-of-Line) 블로킹 없음 (스트림 멀티플렉싱)
- 연결 마이그레이션 (IP 변경 시에도 연결 유지)
- 내장 암호화 (TLS 1.3)

**QUIC 사용 시점:**
- 높은 패킷 손실 환경 (>0.5%)
- 잦은 핸드오프가 있는 모바일 네트워크
- 멀티플렉싱이 유리한 다중 파일 전송

**구현**: network_system의 QUIC 라이브러리 사용 (별도 외부 의존성 없음)

```cpp
struct quic_transport_config {
    bool        enable_0rtt         = true;
    std::size_t max_streams         = 100;      // 연결당
    std::size_t initial_window      = 10 * 1024 * 1024;
    duration    idle_timeout        = 30s;
    bool        enable_migration    = true;
};
```

#### 6.2.5 전송 추상화 계층

```cpp
// 전송 구현을 위한 추상 인터페이스
class transport_interface {
public:
    virtual ~transport_interface() = default;

    [[nodiscard]] virtual auto connect(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto disconnect() -> Result<void> = 0;
    [[nodiscard]] virtual auto send(std::span<const std::byte> data) -> Result<void> = 0;
    [[nodiscard]] virtual auto receive(std::span<std::byte> buffer) -> Result<std::size_t> = 0;
    [[nodiscard]] virtual auto is_connected() const -> bool = 0;

    // QUIC 전용 (TCP에서는 no-op)
    [[nodiscard]] virtual auto create_stream() -> Result<stream_id> { return stream_id{0}; }
    [[nodiscard]] virtual auto close_stream(stream_id) -> Result<void> { return {}; }
};

// 전송 인스턴스 생성을 위한 팩토리
[[nodiscard]] auto create_transport(transport_type type) -> std::unique_ptr<transport_interface>;
```

### 6.3 LZ4 라이브러리 통합

```cpp
// LZ4 통합 래퍼
namespace kcenon::file_transfer::compression {

class lz4_engine {
public:
    // 표준 LZ4 압축 (빠름)
    [[nodiscard]] static auto compress(
        std::span<const std::byte> input,
        std::span<std::byte> output
    ) -> Result<std::size_t>;

    // LZ4-HC 압축 (높은 압축률)
    [[nodiscard]] static auto compress_hc(
        std::span<const std::byte> input,
        std::span<std::byte> output,
        int compression_level = 9
    ) -> Result<std::size_t>;

    // 해제 (두 모드 공통)
    [[nodiscard]] static auto decompress(
        std::span<const std::byte> compressed,
        std::span<std::byte> output,
        std::size_t original_size
    ) -> Result<std::size_t>;

    // 버퍼 할당을 위한 최대 압축 크기 계산
    [[nodiscard]] static auto max_compressed_size(
        std::size_t input_size
    ) -> std::size_t;
};

} // namespace kcenon::file_transfer::compression
```

### 6.4 오류 코드 범위

에코시스템 규칙에 따라 file_trans_system은 **-700 ~ -799** 범위의 오류 코드를 예약합니다:

| 범위 | 카테고리 |
|------|----------|
| -700 ~ -719 | 전송 오류 (초기화, 취소, 타임아웃) |
| -720 ~ -739 | 청크 오류 (체크섬, 순서, 크기) |
| -740 ~ -759 | 파일 I/O 오류 (읽기, 쓰기, 권한) |
| -760 ~ -779 | 재개 오류 (상태, 손상) |
| -780 ~ -789 | 압축 오류 (압축, 해제, 무효) |
| -790 ~ -799 | 설정 오류 |

### 6.5 인터페이스 구현

```cpp
// 전송 작업 스케줄링을 위해 IExecutor 구현
class transfer_executor : public common::IExecutor {
    // 내부적으로 thread_system 사용
};

// 선택적 IMonitor 통합
class transfer_monitor : public common::IMonitor {
    // 압축 통계를 포함하여 monitoring_system으로 지표 내보내기
};
```

---

## 7. 디렉토리 구조

```
file_trans_system/
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── PRD.md
│   ├── PRD_KR.md
│   ├── API.md
│   └── ARCHITECTURE.md
├── include/
│   └── kcenon/
│       └── file_transfer/
│           ├── file_transfer.h           # 메인 헤더
│           ├── core/
│           │   ├── file_sender.h
│           │   ├── file_receiver.h
│           │   ├── transfer_manager.h
│           │   ├── chunk_manager.h
│           │   └── error_codes.h
│           ├── chunk/
│           │   ├── chunk.h
│           │   ├── chunk_splitter.h
│           │   ├── chunk_assembler.h
│           │   └── checksum.h
│           ├── compression/
│           │   ├── lz4_engine.h
│           │   ├── chunk_compressor.h
│           │   ├── adaptive_compression.h
│           │   └── compression_stats.h
│           ├── transport/
│           │   ├── transport_interface.h
│           │   ├── transport_factory.h
│           │   ├── tcp_transport.h
│           │   ├── quic_transport.h      # 2단계
│           │   └── protocol_messages.h
│           ├── pipeline/
│           │   ├── pipeline_stage.h
│           │   ├── pipeline_config.h
│           │   ├── sender_pipeline.h
│           │   ├── receiver_pipeline.h
│           │   ├── pipeline_statistics.h
│           │   └── bounded_queue.h
│           ├── resume/
│           │   ├── resume_handler.h
│           │   └── transfer_state.h
│           ├── adapters/
│           │   └── common_adapter.h
│           ├── di/
│           │   └── file_transfer_module.h
│           └── metrics/
│               └── transfer_metrics.h
├── src/
│   ├── core/
│   ├── chunk/
│   ├── compression/
│   ├── transport/
│   ├── pipeline/
│   ├── resume/
│   └── adapters/
├── tests/
│   ├── unit/
│   │   ├── chunk_test.cpp
│   │   ├── compression_test.cpp
│   │   ├── pipeline_test.cpp
│   │   └── ...
│   ├── integration/
│   └── benchmark/
│       ├── compression_benchmark.cpp
│       ├── pipeline_benchmark.cpp
│       └── transfer_benchmark.cpp
└── examples/
    ├── simple_send/
    ├── simple_receive/
    ├── compressed_transfer/
    ├── pipeline_transfer/
    └── batch_transfer/
```

---

## 8. 개발 단계

### 1단계: 핵심 인프라 (2-3주)
- [ ] CMake를 통한 프로젝트 설정
- [ ] LZ4 라이브러리 통합
- [ ] 청크 데이터 구조 및 직렬화
- [ ] 기본 청크 분할기/조립기
- [ ] CRC32 체크섬 구현
- [ ] 핵심 컴포넌트 단위 테스트

### 2단계: LZ4 압축 엔진 (1-2주)
- [ ] LZ4 압축 래퍼
- [ ] 고압축 모드를 위한 LZ4-HC 지원
- [ ] 적응형 압축 감지
- [ ] 압축 통계 추적
- [ ] 압축 단위 테스트 및 벤치마크

### 3단계: 전송 엔진 (2-3주)
- [ ] 전송 추상화 계층 구현
- [ ] TCP 전송 구현 (network_system 활용)
- [ ] 커스텀 프로토콜 메시지 처리
- [ ] 압축 지원 파일 송신자 구현
- [ ] 해제 지원 파일 수신자 구현
- [ ] 기본 전송 관리자
- [ ] 통합 테스트

### 4단계: 신뢰성 기능 (2주)
- [ ] 재개 핸들러 구현
- [ ] 전송 상태 영속화
- [ ] SHA-256 파일 검증
- [ ] 오류 복구 및 재시도 로직
- [ ] 압축 오류 처리

### 5단계: 고급 기능 (2주)
- [ ] 다중 파일 배치 전송
- [ ] 동시 전송 지원
- [ ] 대역폭 조절
- [ ] 압축 지표를 포함한 진행 상황 추적

### 6단계: 통합 및 마무리 (1-2주)
- [ ] logger_system 통합
- [ ] monitoring_system 통합
- [ ] 성능 벤치마크
- [ ] 문서화 및 예제

### 7단계: QUIC 전송 (선택, 2-3주)
- [ ] network_system QUIC 라이브러리 통합
- [ ] QUIC 전송 구현
- [ ] 0-RTT 연결 재개
- [ ] 연결 마이그레이션 지원
- [ ] 멀티스트림 파일 전송
- [ ] QUIC 전용 벤치마크
- [ ] 폴백 메커니즘 (QUIC → TCP)

---

## 9. 위험 및 완화 방안

| 위험 | 영향 | 확률 | 완화 방안 |
|------|------|------|-----------|
| 대용량 파일 메모리 고갈 | 높음 | 중간 | 고정 버퍼 풀을 통한 스트리밍 |
| 네트워크 불안정성 | 중간 | 높음 | 강력한 재시도 및 재개 로직 |
| 청크 순서 복잡성 | 중간 | 중간 | 순서 번호 및 검증 |
| 크로스 플랫폼 파일 권한 | 낮음 | 중간 | 추상 권한 모델 |
| 성능 병목 | 중간 | 중간 | 초기 벤치마킹, 프로파일링 |
| LZ4 압축 오버헤드 | 낮음 | 낮음 | 적응형 압축, 비압축 데이터 건너뛰기 |
| 압축률 변동성 | 낮음 | 중간 | 실제 압축률 보고, 보장하지 않음 |

---

## 10. 성공 기준

### 10.1 기능 완성도
- [ ] 모든 P0 요구사항 구현 및 테스트 완료
- [ ] 모든 P1 요구사항 구현 및 테스트 완료
- [ ] LZ4 압축 완전 통합
- [ ] API 문서화 완료

### 10.2 품질 기준
- [ ] ≥80% 코드 커버리지
- [ ] ThreadSanitizer 경고 없음
- [ ] AddressSanitizer 메모리 누수 없음
- [ ] 모든 통합 테스트 통과

### 10.3 성능 검증
- [ ] 처리량 목표 달성 (벤치마크로 검증)
- [ ] 압축 속도 목표 달성 (≥400 MB/s)
- [ ] 해제 속도 목표 달성 (≥1.5 GB/s)
- [ ] 메모리 목표 달성 (프로파일링으로 검증)
- [ ] 재개 기능 검증 완료

---

## 부록 A: 용어집

| 용어 | 정의 |
|------|------|
| **청크 (Chunk)** | 스트리밍 전송을 위한 고정 크기의 파일 세그먼트 |
| **전송 (Transfer)** | 송수신되는 단일 파일 또는 파일 배치 |
| **재개 (Resume)** | 마지막 성공 지점부터 중단된 전송 계속 |
| **체크섬 (Checksum)** | 데이터 무결성 검증에 사용되는 해시 값 |
| **백프레셔 (Backpressure)** | 버퍼 오버플로우 방지를 위한 흐름 제어 메커니즘 |
| **LZ4** | 속도에 최적화된 빠른 무손실 압축 알고리즘 |
| **적응형 압축 (Adaptive Compression)** | 압축 불가능 데이터의 자동 감지 및 건너뛰기 |
| **압축률 (Compression Ratio)** | 원본 크기를 압축 크기로 나눈 값 (높을수록 좋음) |

---

## 부록 B: LZ4 압축 상세

### B.1 왜 LZ4인가?

| 알고리즘 | 압축 속도 | 해제 속도 | 압축률 |
|----------|-----------|-----------|--------|
| **LZ4** | ~500 MB/s | ~2 GB/s | 2.1:1 |
| LZ4-HC | ~50 MB/s | ~2 GB/s | 2.7:1 |
| zstd | ~400 MB/s | ~1 GB/s | 2.9:1 |
| gzip | ~30 MB/s | ~300 MB/s | 2.7:1 |
| snappy | ~400 MB/s | ~800 MB/s | 1.8:1 |

LZ4는 다음의 최상의 균형을 제공합니다:
- **속도**: 메모리 대역폭에 가까운 압축/해제 속도
- **단순성**: 단일 헤더 라이브러리, 최소 의존성
- **라이선스**: BSD 라이선스 (상업적으로 친화적)
- **성숙도**: 프로덕션에서 검증됨 (Linux 커널, ZFS 등)

### B.2 적응형 압축 전략

```cpp
// 적응형 압축 결정을 위한 의사 코드
bool should_compress(span<byte> chunk) {
    // 청크의 처음 1KB 샘플링
    auto sample = chunk.first(min(1024, chunk.size()));

    // 샘플 압축 시도
    auto compressed = lz4_compress(sample);

    // 최소 10% 감소가 있을 때만 압축
    return compressed.size() < sample.size() * 0.9;
}
```

### B.3 파일 유형 휴리스틱

| 파일 유형 | 압축 여부 | 이유 |
|-----------|-----------|------|
| .txt, .log, .json, .xml | 예 | 높은 압축률의 텍스트 |
| .csv, .html, .css, .js | 예 | 텍스트 기반 포맷 |
| .cpp, .h, .py, .java | 예 | 소스 코드 |
| .zip, .gz, .tar.gz | 아니오 | 이미 압축됨 |
| .jpg, .png, .mp4 | 아니오 | 이미 압축된 미디어 |
| .exe, .dll, .so | 상황에 따름 | 바이너리, 일부 이득 가능 |

---

## 부록 C: 참고 자료

### 내부 문서
- [common_system 문서](../../../common_system/README.md)
- [thread_system 문서](../../../thread_system/README.md)
- [network_system 문서](../../../network_system/README.md) - TCP 및 QUIC 전송 제공
- [container_system 문서](../../../container_system/README.md)

### 압축
- [LZ4 공식 저장소](https://github.com/lz4/lz4)
- [LZ4 프레임 포맷 명세](https://github.com/lz4/lz4/blob/dev/doc/lz4_Frame_format.md)

### 전송 프로토콜
- [RFC 9000 - QUIC: UDP 기반 다중화 보안 전송](https://tools.ietf.org/html/rfc9000)
- [RFC 9001 - TLS를 사용한 QUIC 보안](https://tools.ietf.org/html/rfc9001)
- [RFC 9002 - QUIC 손실 감지 및 혼잡 제어](https://tools.ietf.org/html/rfc9002)

### 기타 참조
- [RFC 7233 - HTTP Range Requests](https://tools.ietf.org/html/rfc7233) (참조용, HTTP는 사용하지 않음)
