# 파일 전송 시스템 - 제품 요구사항 명세서

## 문서 정보

| 항목 | 내용 |
|------|------|
| **프로젝트명** | file_trans_system |
| **버전** | 2.0.0 |
| **상태** | 초안 |
| **작성일** | 2025-12-11 |
| **작성자** | kcenon@naver.com |

---

## 1. 개요

### 1.1 목적

**file_trans_system**은 중앙 집중식 파일 관리를 위한 **클라이언트-서버 아키텍처**를 구현한 고성능 프로덕션급 C++20 파일 전송 라이브러리입니다. 서버는 파일 저장소를 관리하며, 클라이언트는 서버에 연결하여 파일을 업로드, 다운로드 또는 조회합니다. 기존 에코시스템(common_system, thread_system, logger_system, monitoring_system, container_system, network_system)과 원활하게 통합되어 엔터프라이즈급 파일 전송 기능을 제공합니다.

### 1.2 목표

1. **클라이언트-서버 아키텍처**: 파일 저장소를 가진 중앙 서버, 다중 클라이언트 연결 지원
2. **양방향 전송**: 업로드(클라이언트→서버)와 다운로드(서버→클라이언트) 모두 지원
3. **파일 관리**: 서버 측 파일 목록 조회, 저장소 관리, 접근 제어
4. **실시간 LZ4 압축**: 청크 단위 압축/해제를 통한 실질 처리량 증가
5. **신뢰성**: 체크섬, 재개 기능, 오류 복구를 통한 데이터 무결성 보장
6. **성능**: 비동기 I/O와 스레드 풀을 활용한 높은 처리량 달성
7. **가시성**: 모니터링 및 로깅 시스템과의 완전한 통합
8. **보안**: TLS/SSL을 통한 암호화 전송 지원

### 1.3 성공 지표

| 지표 | 목표값 |
|------|--------|
| 업로드 처리량 (1GB 파일, LAN) | ≥ 500 MB/s |
| 다운로드 처리량 (1GB 파일, LAN) | ≥ 500 MB/s |
| 처리량 (1GB 파일, WAN) | ≥ 100 MB/s (네트워크 제한) |
| 압축을 통한 실질 처리량 | 압축 가능 데이터에서 2-4배 향상 |
| LZ4 압축 속도 | 코어당 ≥ 400 MB/s |
| LZ4 해제 속도 | 코어당 ≥ 1.5 GB/s |
| 압축률 (텍스트/로그) | 일반적으로 2:1 ~ 4:1 |
| 메모리 사용량 | 기본 < 50 MB |
| 재개 정확도 | 100% (체크섬 검증) |
| 동시 연결 | ≥ 100개 동시 클라이언트 |
| 파일 목록 응답 | 10,000개 파일에 대해 < 100ms |

---

## 2. 문제 정의

### 2.1 현재 과제

1. **중앙 집중식 파일 관리**: 파일 저장 및 배포를 위한 중앙 저장소 필요
2. **양방향 전송**: 단일 서버에서 업로드와 다운로드 기능 모두 필요
3. **대용량 파일 처리**: 가용 메모리보다 큰 파일 전송 시 스트리밍 필요
4. **네트워크 불안정성**: 중단된 전송은 전체 파일 재전송 없이 재개되어야 함
5. **대역폭 제한**: 네트워크 대역폭이 병목인 경우가 많음; 압축으로 실질 처리량 증가 가능
6. **다중 클라이언트 조정**: 여러 클라이언트가 서버 리소스에 동시 접근 필요
7. **리소스 관리**: 메모리, 디스크 I/O, 네트워크 대역폭의 효율적 사용
8. **크로스 플랫폼 지원**: Linux, macOS, Windows에서 일관된 동작

### 2.2 사용 사례

| 사용 사례 | 설명 |
|----------|------|
| **UC-01** | 클라이언트가 서버에 단일 파일 업로드 |
| **UC-02** | 클라이언트가 서버에서 단일 파일 다운로드 |
| **UC-03** | 클라이언트가 여러 파일을 배치 작업으로 업로드 |
| **UC-04** | 클라이언트가 서버에서 여러 파일 다운로드 |
| **UC-05** | 클라이언트가 서버에 사용 가능한 파일 목록 조회 |
| **UC-06** | 마지막 성공 청크부터 중단된 업로드/다운로드 재개 |
| **UC-07** | 상세 지표를 통한 전송 진행 상황 모니터링 |
| **UC-08** | 신뢰할 수 없는 네트워크에서의 보안 파일 전송 |
| **UC-09** | 서버가 파일 저장소 관리 (할당량, 보존, 정리) |
| **UC-10** | 압축 가능 파일(로그, 텍스트, JSON) 실시간 압축 전송 |
| **UC-11** | 이미 압축된 파일(ZIP, 미디어)은 이중 압축 오버헤드 없이 전송 |

---

## 3. 시스템 아키텍처

### 3.1 상위 수준 아키텍처

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           file_trans_system                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                        file_transfer_server                            │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌───────────┐  │  │
│  │  │    서버      │  │    저장소    │  │  클라이언트  │  │   전송    │  │  │
│  │  │   핸들러     │  │   매니저     │  │   매니저     │  │  매니저   │  │  │
│  │  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘  └─────┬─────┘  │  │
│  │         │                 │                 │                │        │  │
│  │  ┌──────▼─────────────────▼─────────────────▼────────────────▼──────┐ │  │
│  │  │                         파일 저장소                              │ │  │
│  │  │                       /data/files/                               │ │  │
│  │  └──────────────────────────────────────────────────────────────────┘ │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│                                    ▲                                         │
│                                    │ TCP/TLS 또는 QUIC                       │
│                                    ▼                                         │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                       file_transfer_client                             │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌───────────┐  │  │
│  │  │    업로드    │  │   다운로드   │  │     목록     │  │   진행    │  │  │
│  │  │    엔진      │  │    엔진      │  │    핸들러    │  │   추적기  │  │  │
│  │  └──────────────┘  └──────────────┘  └──────────────┘  └───────────┘  │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│                                                                              │
├─────────────────────────────────────────────────────────────────────────────┤
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                         청크 관리자                                   │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────┐ │   │
│  │  │ 분할기   │  │ 조립기   │  │ 체크섬   │  │  재개    │  │  LZ4   │ │   │
│  │  │          │  │          │  │          │  │ 핸들러   │  │ 압축기 │ │   │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └────────┘ │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────────────────┤
│                            통합 계층                                         │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────┐ │
│  │ common   │ │ thread   │ │ logger   │ │monitoring│ │ network  │ │ LZ4  │ │
│  │ _system  │ │ _system  │ │ _system  │ │ _system  │ │ _system  │ │ lib  │ │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────┘ │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 데이터 흐름

#### 3.2.1 업로드 흐름 (클라이언트 → 서버)

```
┌────────────────────────────────────────────────────────────────────────────┐
│                           업로드 흐름                                        │
│                                                                             │
│  클라이언트                                          서버                    │
│  ┌─────────────────┐                    ┌─────────────────────────────────┐│
│  │ 로컬 파일       │                    │        파일 저장소              ││
│  │ /local/data.zip │                    │      /data/files/data.zip       ││
│  └────────┬────────┘                    └──────────────▲──────────────────┘│
│           │                                            │                    │
│           ▼                                            │                    │
│  ┌─────────────────┐    UPLOAD_REQUEST    ┌───────────┴───────────┐       │
│  │   업로드 엔진   │─────────────────────▶│      서버 핸들러      │       │
│  │                 │◀─────────────────────│                       │       │
│  │  io_read        │    UPLOAD_ACCEPT     │  요청 검증            │       │
│  │  chunk_process  │                      │  저장소 할당량 확인   │       │
│  │  compression    │                      └───────────────────────┘       │
│  │  network_send   │                                                       │
│  └────────┬────────┘                                                       │
│           │                                                                 │
│           │  CHUNK_DATA [0..N]           ┌───────────────────────┐        │
│           │─────────────────────────────▶│  서버 수신            │        │
│           │◀─────────────────────────────│  파이프라인           │        │
│           │  CHUNK_ACK                   │                       │        │
│           │                              │  network_recv         │        │
│           │  TRANSFER_COMPLETE           │  decompression        │        │
│           │─────────────────────────────▶│  chunk_assemble       │        │
│           │◀─────────────────────────────│  io_write             │        │
│           │  TRANSFER_VERIFY             └───────────────────────┘        │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

#### 3.2.2 다운로드 흐름 (서버 → 클라이언트)

```
┌────────────────────────────────────────────────────────────────────────────┐
│                          다운로드 흐름                                       │
│                                                                             │
│  클라이언트                                          서버                    │
│  ┌─────────────────┐                    ┌─────────────────────────────────┐│
│  │ 로컬 파일       │                    │        파일 저장소              ││
│  │/local/report.pdf│                    │    /data/files/report.pdf       ││
│  └────────▲────────┘                    └──────────────┬──────────────────┘│
│           │                                            │                    │
│           │                                            ▼                    │
│  ┌────────┴────────┐   DOWNLOAD_REQUEST   ┌───────────────────────┐       │
│  │  다운로드 엔진  │─────────────────────▶│      서버 핸들러      │       │
│  │                 │◀─────────────────────│                       │       │
│  │  network_recv   │   DOWNLOAD_ACCEPT    │  요청 검증            │       │
│  │  decompression  │   (+ 파일 메타데이터)│  파일 존재 확인       │       │
│  │  chunk_assemble │                      └───────────┬───────────┘       │
│  │  io_write       │                                  │                    │
│  └────────▲────────┘                                  ▼                    │
│           │                              ┌───────────────────────┐        │
│           │  CHUNK_DATA [0..N]           │  서버 전송            │        │
│           │◀─────────────────────────────│  파이프라인           │        │
│           │─────────────────────────────▶│                       │        │
│           │  CHUNK_ACK                   │  io_read              │        │
│           │                              │  chunk_process        │        │
│           │  TRANSFER_COMPLETE           │  compression          │        │
│           │◀─────────────────────────────│  network_send         │        │
│           │─────────────────────────────▶└───────────────────────┘        │
│           │  TRANSFER_VERIFY                                               │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

#### 3.2.3 파일 목록 조회 흐름

```
┌────────────────────────────────────────────────────────────────────────────┐
│                          파일 목록 조회 흐름                                  │
│                                                                             │
│  클라이언트                                          서버                    │
│                                                                             │
│  ┌─────────────────┐    LIST_REQUEST      ┌───────────────────────┐       │
│  │   목록 핸들러   │─────────────────────▶│      서버 핸들러      │       │
│  │                 │                      │                       │       │
│  │  filter: *.pdf  │                      │  저장소 디렉토리 스캔 │       │
│  │  sort: by_size  │                      │  필터 적용            │       │
│  │  limit: 100     │                      │  정렬 적용            │       │
│  │                 │◀─────────────────────│  결과 페이징          │       │
│  │  파일 목록 표시 │    LIST_RESPONSE     │                       │       │
│  └─────────────────┘                      └───────────────────────┘       │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

### 3.3 파이프라인 아키텍처

파일 전송 시스템은 thread_system의 **typed_thread_pool 기반 파이프라인 아키텍처**를 사용하여 서로 다른 파이프라인 단계의 병렬 처리를 통해 처리량을 극대화합니다.

#### 3.3.1 클라이언트 업로드 파이프라인

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      클라이언트 업로드 파이프라인                              │
│                                                                              │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌────────────┐│
│  │  파일 읽기   │───▶│    청크      │───▶│     LZ4      │───▶│  네트워크  ││
│  │    단계      │    │    조립      │    │    압축      │    │    전송    ││
│  └──────────────┘    └──────────────┘    └──────────────┘    └────────────┘│
│        │                   │                   │                   │        │
│        ▼                   ▼                   ▼                   ▼        │
│  ┌──────────────────────────────────────────────────────────────────────┐  │
│  │                    typed_thread_pool<pipeline_stage>                  │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐              │  │
│  │  │  IO (2)  │  │ Chunk(2) │  │Compress(4)│ │Network(2)│              │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘              │  │
│  └──────────────────────────────────────────────────────────────────────┘  │
│                                                                              │
│  단계 큐 (백프레셔 제어):                                                    │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐           │
│  │ read_queue │─▶│chunk_queue │─▶│ comp_queue │─▶│ send_queue │           │
│  │    (16)    │  │    (16)    │  │    (32)    │  │    (64)    │           │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘           │
└─────────────────────────────────────────────────────────────────────────────┘
```

#### 3.3.2 클라이언트 다운로드 파이프라인

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     클라이언트 다운로드 파이프라인                             │
│                                                                              │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌────────────┐│
│  │   네트워크   │───▶│     LZ4      │───▶│    청크      │───▶│ 파일 쓰기  ││
│  │    수신      │    │    해제      │    │    조립      │    │    단계    ││
│  └──────────────┘    └──────────────┘    └──────────────┘    └────────────┘│
│        │                   │                   │                   │        │
│        ▼                   ▼                   ▼                   ▼        │
│  ┌──────────────────────────────────────────────────────────────────────┐  │
│  │                    typed_thread_pool<pipeline_stage>                  │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐              │  │
│  │  │Network(2)│  │Decomp(4) │  │ Chunk(2) │  │  IO (2)  │              │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘              │  │
│  └──────────────────────────────────────────────────────────────────────┘  │
│                                                                              │
│  단계 큐:                                                                    │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐           │
│  │ recv_queue │─▶│decomp_queue│─▶│assem_queue │─▶│write_queue │           │
│  │    (64)    │  │    (32)    │  │    (16)    │  │    (16)    │           │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘           │
└─────────────────────────────────────────────────────────────────────────────┘
```

#### 3.3.3 파이프라인 단계 타입

```cpp
namespace kcenon::file_transfer {

// typed_thread_pool을 위한 파이프라인 단계 타입
enum class pipeline_stage : uint8_t {
    io_read,        // 파일 읽기 작업 (I/O 바운드)
    chunk_process,  // 청크 조립/분할 (CPU 경량)
    compression,    // LZ4 압축/해제 (CPU 바운드)
    network,        // 네트워크 송/수신 (I/O 바운드)
    io_write        // 파일 쓰기 작업 (I/O 바운드)
};

} // namespace kcenon::file_transfer
```

#### 3.3.4 파이프라인 설정

```cpp
// 파이프라인 워커 설정
struct pipeline_config {
    // 단계별 워커 수 (기본값으로 자동 튜닝)
    std::size_t io_read_workers      = 2;   // 디스크 읽기 병렬화
    std::size_t chunk_workers        = 2;   // 청크 처리
    std::size_t compression_workers  = 4;   // LZ4 압축 (CPU 바운드)
    std::size_t network_workers      = 2;   // 네트워크 작업
    std::size_t io_write_workers     = 2;   // 디스크 쓰기 병렬화

    // 큐 크기 (백프레셔 제어)
    std::size_t read_queue_size      = 16;  // 대기 중인 읽기 청크
    std::size_t compress_queue_size  = 32;  // 대기 중인 압축
    std::size_t send_queue_size      = 64;  // 대기 중인 네트워크 전송
    std::size_t decompress_queue_size = 32; // 대기 중인 해제
    std::size_t write_queue_size     = 16;  // 대기 중인 쓰기

    // 하드웨어 기반 자동 튜닝
    static auto auto_detect() -> pipeline_config;
};
```

### 3.4 컴포넌트 설명

#### 3.4.1 file_transfer_server

파일 저장소를 관리하고 클라이언트 요청을 처리하는 중앙 서버 컴포넌트입니다.

**책임:**
- 들어오는 클라이언트 연결 수신 대기
- 클라이언트 요청 인증 및 권한 부여
- 업로드 요청 처리 (클라이언트로부터 파일 수신)
- 다운로드 요청 처리 (클라이언트에게 파일 전송)
- 목록 요청 처리 (사용 가능한 파일 반환)
- 파일 저장소 관리 (할당량, 보존, 정리)
- 활성 전송 및 연결된 클라이언트 추적

**주요 기능:**
- 연결 풀링을 통한 다중 클라이언트 지원
- 설정 가능한 저장소 디렉토리 및 할당량
- 파일 접근 제어 (선택 사항)
- 자동 파일 메타데이터 인덱싱

#### 3.4.2 file_transfer_client

파일 작업을 수행하기 위해 서버에 연결하는 클라이언트 컴포넌트입니다.

**책임:**
- 서버에 연결 및 연결 해제
- 서버에 파일 업로드
- 서버에서 파일 다운로드
- 서버에 사용 가능한 파일 조회
- 전송 진행 상황 추적
- 중단 시 전송 재개 처리

**주요 기능:**
- 지수 백오프를 통한 자동 재연결
- 동시 업로드/다운로드 지원
- 압축 지표를 포함한 진행 상황 콜백
- 전송 일시정지/재개/취소

#### 3.4.3 저장소 매니저 (서버 측)

서버의 파일 저장소를 관리합니다.

**책임:**
- 파일 저장소 구성
- 저장소 할당량 적용
- 파일 보존 정책
- 빠른 조회를 위한 파일 메타데이터 캐싱
- 동시 접근 관리

#### 3.4.4 청크 관리자

청크 수준 작업을 위한 공유 컴포넌트입니다.

**책임:**
- **분할기**: 파일을 설정 가능한 청크로 분할 (기본값: 64KB - 1MB)
- **조립기**: 수신된 청크로부터 파일 재구성
- **체크섬**: 청크/파일 무결성 계산 및 검증 (CRC32, SHA-256)
- **재개 핸들러**: 재개 기능을 위한 전송 상태 추적
- **LZ4 압축기**: 실시간 청크 단위 압축/해제

---

## 4. 기능 요구사항

### 4.1 핵심 기능

#### FR-01: 서버 시작 및 종료
| ID | FR-01 |
|----|-------|
| **설명** | 서버가 설정된 엔드포인트에서 수신 대기를 시작하고 정상적으로 종료 |
| **우선순위** | P0 (필수) |
| **인수 기준** | 서버가 연결을 수락하고, 활성 전송이 있는 상태에서 정상 종료 처리 |

#### FR-02: 클라이언트 연결
| ID | FR-02 |
|----|-------|
| **설명** | 클라이언트가 선택적 인증과 함께 서버에 연결 |
| **우선순위** | P0 (필수) |
| **인수 기준** | 성공적인 핸드셰이크, 연결 상태 관리 |

#### FR-03: 파일 업로드 (단일)
| ID | FR-03 |
|----|-------|
| **설명** | 클라이언트가 서버에 단일 파일 업로드 |
| **우선순위** | P0 (필수) |
| **인수 기준** | 체크섬으로 100% 무결성 검증된 파일 전송 |

#### FR-04: 파일 업로드 (배치)
| ID | FR-04 |
|----|-------|
| **설명** | 클라이언트가 단일 작업으로 여러 파일 업로드 |
| **우선순위** | P1 (높음) |
| **인수 기준** | 모든 파일 전송, 개별 파일 상태 추적 |

#### FR-05: 파일 다운로드 (단일)
| ID | FR-05 |
|----|-------|
| **설명** | 클라이언트가 서버에서 단일 파일 다운로드 |
| **우선순위** | P0 (필수) |
| **인수 기준** | 체크섬으로 100% 무결성 검증된 파일 전송 |

#### FR-06: 파일 다운로드 (배치)
| ID | FR-06 |
|----|-------|
| **설명** | 클라이언트가 단일 작업으로 여러 파일 다운로드 |
| **우선순위** | P1 (높음) |
| **인수 기준** | 모든 파일 전송, 개별 파일 상태 추적 |

#### FR-07: 파일 목록 조회
| ID | FR-07 |
|----|-------|
| **설명** | 클라이언트가 필터링 및 정렬 기능과 함께 서버에 사용 가능한 파일 조회 |
| **우선순위** | P0 (필수) |
| **인수 기준** | 메타데이터가 포함된 파일 목록 반환, 필터/정렬/페이징 지원 |

#### FR-08: 청크 기반 전송
| ID | FR-08 |
|----|-------|
| **설명** | 스트리밍 전송을 위해 파일을 청크로 분할 |
| **우선순위** | P0 (필수) |
| **인수 기준** | 설정 가능한 청크 크기, 올바른 재조립 |

#### FR-09: 전송 재개
| ID | FR-09 |
|----|-------|
| **설명** | 마지막 성공 청크부터 중단된 업로드/다운로드 재개 |
| **우선순위** | P1 (높음) |
| **인수 기준** | 1초 이내 재개, 데이터 손실 없음 |

#### FR-10: 진행 상황 모니터링
| ID | FR-10 |
|----|-------|
| **설명** | 콜백을 통한 실시간 진행 상황 추적 |
| **우선순위** | P1 (높음) |
| **인수 기준** | 설정 가능한 간격으로 진행 상황 업데이트 |

#### FR-11: 무결성 검증
| ID | FR-11 |
|----|-------|
| **설명** | 체크섬을 사용한 데이터 무결성 검증 |
| **우선순위** | P0 (필수) |
| **인수 기준** | 청크당 CRC32, 파일당 SHA-256 |

#### FR-12: 동시 전송
| ID | FR-12 |
|----|-------|
| **설명** | 다수의 동시 파일 전송 지원 |
| **우선순위** | P1 (높음) |
| **인수 기준** | 성능 저하 없이 100개 이상 동시 전송 |

#### FR-13: 대역폭 조절
| ID | FR-13 |
|----|-------|
| **설명** | 연결별/전체 전송 대역폭 제한 |
| **우선순위** | P2 (중간) |
| **인수 기준** | 설정된 제한의 5% 이내 대역폭 |

#### FR-14: 실시간 LZ4 압축
| ID | FR-14 |
|----|-------|
| **설명** | 청크 단위 LZ4 압축/해제 |
| **우선순위** | P0 (필수) |
| **인수 기준** | 압축 속도 ≥400 MB/s, 해제 ≥1.5 GB/s |

#### FR-15: 적응형 압축
| ID | FR-15 |
|----|-------|
| **설명** | 압축 불가능한 데이터에 대해 자동으로 압축 건너뛰기 |
| **우선순위** | P1 (높음) |
| **인수 기준** | 1KB 샘플 내에서 압축 불가능 청크 감지 |

#### FR-16: 저장소 관리
| ID | FR-16 |
|----|-------|
| **설명** | 서버가 할당량 및 보존 정책으로 파일 저장소 관리 |
| **우선순위** | P1 (높음) |
| **인수 기준** | 저장소 제한 적용, 선택적 자동 정리 |

#### FR-17: 파이프라인 기반 처리
| ID | FR-17 |
|----|-------|
| **설명** | 병렬 처리를 위한 멀티스테이지 파이프라인 |
| **우선순위** | P0 (필수) |
| **인수 기준** | 설정 가능한 단계를 가진 업로드/다운로드 파이프라인 |

#### FR-18: 파이프라인 백프레셔
| ID | FR-18 |
|----|-------|
| **설명** | 바운디드 큐를 통한 메모리 고갈 방지 |
| **우선순위** | P1 (높음) |
| **인수 기준** | 설정 가능한 큐 크기, 자동 속도 조절 |

### 4.2 API 요구사항

#### 4.2.1 서버 API

```cpp
namespace kcenon::file_transfer {

class file_transfer_server {
public:
    // 설정을 위한 빌더 패턴
    class builder {
    public:
        builder& with_storage_directory(const std::filesystem::path& dir);
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_max_connections(std::size_t max_conn);
        builder& with_max_file_size(uint64_t max_size);
        builder& with_storage_quota(uint64_t quota_bytes);
        builder& with_allowed_extensions(std::vector<std::string> exts);
        builder& with_transport(transport_type type);
        [[nodiscard]] auto build() -> Result<file_transfer_server>;
    };

    // 생명주기 관리
    [[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;
    [[nodiscard]] auto stop() -> Result<void>;
    [[nodiscard]] auto is_running() const -> bool;

    // 파일 관리
    [[nodiscard]] auto list_files(const list_options& opts = {})
        -> std::vector<file_info>;
    [[nodiscard]] auto delete_file(const std::string& filename) -> Result<void>;
    [[nodiscard]] auto get_file_info(const std::string& filename)
        -> Result<file_info>;
    [[nodiscard]] auto get_storage_usage() -> storage_stats;

    // 요청 콜백
    void on_upload_request(
        std::function<bool(const upload_request&)> callback
    );
    void on_download_request(
        std::function<bool(const download_request&)> callback
    );

    // 이벤트 콜백
    void on_transfer_progress(
        std::function<void(const transfer_progress&)> callback
    );
    void on_transfer_complete(
        std::function<void(const transfer_result&)> callback
    );
    void on_client_connected(
        std::function<void(const client_info&)> callback
    );
    void on_client_disconnected(
        std::function<void(const client_info&)> callback
    );

    // 통계
    [[nodiscard]] auto get_statistics() -> server_statistics;
    [[nodiscard]] auto get_connected_clients() -> std::vector<client_info>;
};

} // namespace kcenon::file_transfer
```

#### 4.2.2 클라이언트 API

```cpp
namespace kcenon::file_transfer {

class file_transfer_client {
public:
    // 설정을 위한 빌더 패턴
    class builder {
    public:
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_compression(compression_mode mode);
        builder& with_compression_level(compression_level level);
        builder& with_chunk_size(std::size_t size);
        builder& with_bandwidth_limit(std::size_t bytes_per_second);
        builder& with_transport(transport_type type);
        builder& with_auto_reconnect(bool enabled, duration interval = 5s);
        [[nodiscard]] auto build() -> Result<file_transfer_client>;
    };

    // 연결 관리
    [[nodiscard]] auto connect(const endpoint& server_addr) -> Result<void>;
    [[nodiscard]] auto disconnect() -> Result<void>;
    [[nodiscard]] auto is_connected() const -> bool;

    // 업로드 작업
    [[nodiscard]] auto upload_file(
        const std::filesystem::path& local_path,
        const std::string& remote_name = {},
        const transfer_options& options = {}
    ) -> Result<transfer_handle>;

    [[nodiscard]] auto upload_files(
        std::span<const std::filesystem::path> files,
        const transfer_options& options = {}
    ) -> Result<batch_transfer_handle>;

    // 다운로드 작업
    [[nodiscard]] auto download_file(
        const std::string& remote_name,
        const std::filesystem::path& local_path,
        const transfer_options& options = {}
    ) -> Result<transfer_handle>;

    [[nodiscard]] auto download_files(
        std::span<const std::string> remote_names,
        const std::filesystem::path& output_dir,
        const transfer_options& options = {}
    ) -> Result<batch_transfer_handle>;

    // 파일 목록 조회
    [[nodiscard]] auto list_files(
        const list_options& options = {}
    ) -> Result<std::vector<file_info>>;

    // 전송 제어
    [[nodiscard]] auto cancel(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto pause(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto resume(const transfer_id& id) -> Result<void>;

    // 콜백
    void on_progress(std::function<void(const transfer_progress&)> callback);
    void on_connection_state(std::function<void(connection_state)> callback);

    // 통계
    [[nodiscard]] auto get_statistics() -> client_statistics;
};

} // namespace kcenon::file_transfer
```

#### 4.2.3 데이터 구조

```cpp
namespace kcenon::file_transfer {

// 연결 상태
enum class connection_state {
    disconnected,
    connecting,
    connected,
    reconnecting
};

// 파일 정보
struct file_info {
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    std::chrono::system_clock::time_point created_time;
    std::chrono::system_clock::time_point modified_time;
};

// 업로드 요청 (서버 콜백)
struct upload_request {
    client_info             client;
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;
    compression_mode        compression;
};

// 다운로드 요청 (서버 콜백)
struct download_request {
    client_info             client;
    std::string             filename;
    uint64_t                offset;         // 재개용
    uint64_t                length;         // 0 = 전체 파일
};

// 목록 옵션
struct list_options {
    std::string             filter_pattern;     // Glob 패턴 (예: "*.pdf")
    sort_field              sort_by = sort_field::name;
    sort_order              order = sort_order::ascending;
    uint32_t                offset = 0;         // 페이징
    uint32_t                limit = 0;          // 0 = 제한 없음
};

// 전송 옵션
struct transfer_options {
    compression_mode            compression     = compression_mode::adaptive;
    compression_level           level           = compression_level::fast;
    std::size_t                 chunk_size      = 256 * 1024;  // 256KB
    bool                        verify_checksum = true;
    std::optional<std::size_t>  bandwidth_limit;
    std::optional<int>          priority;
    bool                        overwrite_existing = false;
};

// 전송 진행 상황
struct transfer_progress {
    transfer_id         id;
    transfer_direction  direction;          // 업로드 또는 다운로드
    std::string         filename;
    uint64_t            bytes_transferred;  // 원본 바이트
    uint64_t            bytes_on_wire;      // 압축 바이트
    uint64_t            total_bytes;
    double              transfer_rate;      // 바이트/초
    double              effective_rate;     // 압축 고려
    double              compression_ratio;
    duration            elapsed_time;
    duration            estimated_remaining;
    transfer_state      state;
};

// 저장소 통계
struct storage_stats {
    uint64_t            total_capacity;
    uint64_t            used_bytes;
    uint64_t            available_bytes;
    uint64_t            file_count;
};

// 클라이언트 정보
struct client_info {
    std::string         client_id;
    endpoint            address;
    std::chrono::system_clock::time_point connected_at;
    uint64_t            bytes_uploaded;
    uint64_t            bytes_downloaded;
};

} // namespace kcenon::file_transfer
```

---

## 5. 비기능 요구사항

### 5.1 성능

| 요구사항 | 목표값 | 측정 방법 |
|----------|--------|-----------|
| **NFR-01** 업로드 처리량 | ≥500 MB/s (LAN) | 1GB 파일 업로드 시간 |
| **NFR-02** 다운로드 처리량 | ≥500 MB/s (LAN) | 1GB 파일 다운로드 시간 |
| **NFR-03** 지연시간 | <10ms 청크 처리 | 종단간 청크 지연시간 |
| **NFR-04** 메모리 (서버) | <100MB + 연결당 1MB | 운영 중 RSS |
| **NFR-05** 메모리 (클라이언트) | <50MB 기본 | 유휴 시 RSS |
| **NFR-06** CPU 사용률 | 코어당 <30% | 지속 전송 중 |
| **NFR-07** LZ4 압축 | ≥400 MB/s | 압축 처리량 |
| **NFR-08** LZ4 해제 | ≥1.5 GB/s | 해제 처리량 |
| **NFR-09** 압축률 | 텍스트에서 2:1 ~ 4:1 | 일반 압축 가능 데이터 |
| **NFR-10** 목록 응답 | 10K 파일에 <100ms | 파일 목록 조회 |

### 5.2 신뢰성

| 요구사항 | 목표값 |
|----------|--------|
| **NFR-11** 데이터 무결성 | 100% (SHA-256 검증) |
| **NFR-12** 재개 정확도 | 100% 성공적 재개 |
| **NFR-13** 오류 복구 | 지수 백오프를 통한 자동 재시도 |
| **NFR-14** 우아한 성능 저하 | 부하 시 처리량 감소 |
| **NFR-15** 서버 가동률 | 99.9% 가용성 |

### 5.3 보안

| 요구사항 | 설명 |
|----------|------|
| **NFR-16** 암호화 | 네트워크 전송에 TLS 1.3 |
| **NFR-17** 인증 | 선택적 토큰/인증서 기반 인증 |
| **NFR-18** 경로 순회 | 디렉토리 탈출 공격 방지 |
| **NFR-19** 리소스 제한 | 최대 파일 크기, 연결 제한 |
| **NFR-20** 파일 검증 | 파일명 정규화 |

### 5.4 호환성

| 요구사항 | 설명 |
|----------|------|
| **NFR-21** C++ 표준 | C++20 이상 |
| **NFR-22** 플랫폼 | Linux, macOS, Windows |
| **NFR-23** 컴파일러 | GCC 11+, Clang 14+, MSVC 19.29+ |
| **NFR-24** LZ4 라이브러리 | LZ4 1.9.0+ (BSD 라이선스) |

---

## 6. 프로토콜 설계

### 6.1 메시지 타입

```cpp
enum class message_type : uint8_t {
    // 세션 관리 (0x01-0x0F)
    handshake_request   = 0x01,
    handshake_response  = 0x02,

    // 업로드 작업 (0x10-0x1F)
    upload_request      = 0x10,
    upload_accept       = 0x11,
    upload_reject       = 0x12,
    upload_cancel       = 0x13,

    // 다운로드 작업 (0x50-0x5F)
    download_request    = 0x50,
    download_accept     = 0x51,
    download_reject     = 0x52,
    download_cancel     = 0x53,

    // 파일 목록 조회 (0x60-0x6F)
    list_request        = 0x60,
    list_response       = 0x61,

    // 데이터 전송 (0x20-0x2F)
    chunk_data          = 0x20,
    chunk_ack           = 0x21,
    chunk_nack          = 0x22,

    // 재개 (0x30-0x3F)
    resume_request      = 0x30,
    resume_response     = 0x31,

    // 완료 (0x40-0x4F)
    transfer_complete   = 0x40,
    transfer_verify     = 0x41,

    // 제어 (0xF0-0xFF)
    keepalive           = 0xF0,
    error               = 0xFF
};
```

### 6.2 메시지 프레임

```
┌─────────────────────────────────┐
│ 메시지 타입      │ 1 바이트      │
├─────────────────────────────────┤
│ 페이로드 길이    │ 4 바이트 (BE) │
├─────────────────────────────────┤
│ 페이로드         │ 가변          │
└─────────────────────────────────┘

총 오버헤드: 메시지당 5 바이트
```

---

## 7. 오류 코드

에코시스템 규칙에 따라 file_trans_system은 **-700 ~ -799** 범위의 오류 코드를 예약합니다:

| 범위 | 카테고리 |
|------|----------|
| -700 ~ -719 | 전송 오류 (초기화, 취소, 타임아웃) |
| -720 ~ -739 | 청크 오류 (체크섬, 순서, 크기) |
| -740 ~ -759 | 파일 I/O 오류 (읽기, 쓰기, 권한, 없음) |
| -760 ~ -779 | 재개 오류 (상태, 손상) |
| -780 ~ -789 | 압축 오류 (압축, 해제, 무효) |
| -790 ~ -799 | 설정 오류 |

### 7.1 클라이언트-서버용 새 오류 코드

| 코드 | 이름 | 설명 |
|------|------|------|
| -744 | file_already_exists | 업로드: 서버에 파일이 이미 존재 |
| -745 | storage_full | 서버 저장소 할당량 초과 |
| -746 | file_not_found_on_server | 다운로드: 요청한 파일 없음 |
| -747 | access_denied | 작업에 대한 권한 없음 |
| -748 | invalid_filename | 파일명에 잘못된 문자 포함 |
| -749 | connection_refused | 서버가 연결 거부 |
| -750 | server_busy | 서버 최대 용량 도달 |

---

## 8. 디렉토리 구조

```
file_trans_system/
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── PRD.md
│   ├── PRD_KR.md
│   ├── SRS.md
│   ├── SDS.md
│   └── reference/
├── include/
│   └── kcenon/
│       └── file_transfer/
│           ├── file_transfer.h           # 메인 헤더
│           ├── server/
│           │   ├── file_transfer_server.h
│           │   ├── server_handler.h
│           │   └── storage_manager.h
│           ├── client/
│           │   ├── file_transfer_client.h
│           │   ├── upload_engine.h
│           │   └── download_engine.h
│           ├── core/
│           │   ├── transfer_manager.h
│           │   └── error_codes.h
│           ├── chunk/
│           │   ├── chunk.h
│           │   ├── chunk_splitter.h
│           │   ├── chunk_assembler.h
│           │   └── checksum.h
│           ├── compression/
│           │   ├── lz4_engine.h
│           │   ├── chunk_compressor.h
│           │   └── compression_stats.h
│           ├── transport/
│           │   ├── transport_interface.h
│           │   ├── tcp_transport.h
│           │   ├── quic_transport.h
│           │   └── protocol_messages.h
│           └── resume/
│               ├── resume_handler.h
│               └── transfer_state.h
├── src/
│   ├── server/
│   ├── client/
│   ├── core/
│   ├── chunk/
│   ├── compression/
│   ├── transport/
│   └── resume/
├── tests/
│   ├── unit/
│   ├── integration/
│   └── benchmark/
└── examples/
    ├── simple_server/
    ├── simple_client/
    ├── upload_example/
    ├── download_example/
    └── batch_transfer/
```

---

## 9. 개발 단계

### 1단계: 핵심 인프라 (2-3주)
- [ ] CMake를 통한 프로젝트 설정
- [ ] LZ4 라이브러리 통합
- [ ] 청크 데이터 구조 및 직렬화
- [ ] 기본 청크 분할기/조립기
- [ ] CRC32 체크섬 구현

### 2단계: 서버 기반 (2-3주)
- [ ] file_transfer_server 기본 구현
- [ ] 파일 인덱싱이 포함된 저장소 관리자
- [ ] 클라이언트 연결을 위한 서버 핸들러
- [ ] 업로드 요청 처리
- [ ] 다운로드 요청 처리

### 3단계: 클라이언트 기반 (2-3주)
- [ ] file_transfer_client 구현
- [ ] 자동 재연결을 포함한 연결 관리
- [ ] 파이프라인이 포함된 업로드 엔진
- [ ] 파이프라인이 포함된 다운로드 엔진
- [ ] 파일 목록 조회 기능

### 4단계: 압축 및 재개 (2주)
- [ ] LZ4 압축 통합
- [ ] 적응형 압축 감지
- [ ] 재개 핸들러 구현
- [ ] 전송 상태 영속화

### 5단계: 고급 기능 (2주)
- [ ] 배치 업로드/다운로드
- [ ] 대역폭 조절
- [ ] 저장소 할당량 관리
- [ ] 압축 지표를 포함한 진행 상황 추적

### 6단계: 통합 및 마무리 (1-2주)
- [ ] logger_system 통합
- [ ] monitoring_system 통합
- [ ] 성능 벤치마크
- [ ] 문서화 및 예제

### 7단계: QUIC 전송 (선택, 2-3주)
- [ ] QUIC 전송 구현
- [ ] 0-RTT 연결 재개
- [ ] 연결 마이그레이션 지원

---

## 10. 성공 기준

### 10.1 기능 완성도
- [ ] 모든 P0 요구사항 구현 및 테스트 완료
- [ ] 모든 P1 요구사항 구현 및 테스트 완료
- [ ] 서버 및 클라이언트 API 완성
- [ ] 업로드/다운로드/목록 작업 동작

### 10.2 품질 기준
- [ ] ≥80% 코드 커버리지
- [ ] ThreadSanitizer 경고 없음
- [ ] AddressSanitizer 메모리 누수 없음
- [ ] 모든 통합 테스트 통과

### 10.3 성능 검증
- [ ] 업로드 처리량 목표 달성
- [ ] 다운로드 처리량 목표 달성
- [ ] 압축 속도 목표 달성
- [ ] 메모리 목표 달성
- [ ] 재개 기능 검증 완료

---

## 부록 A: 용어집

| 용어 | 정의 |
|------|------|
| **서버** | 파일을 저장하고 클라이언트 요청을 처리하는 중앙 컴포넌트 |
| **클라이언트** | 파일 작업을 위해 서버에 연결하는 컴포넌트 |
| **업로드** | 클라이언트에서 서버로 파일 전송 |
| **다운로드** | 서버에서 클라이언트로 파일 전송 |
| **청크** | 스트리밍 전송을 위한 고정 크기의 파일 세그먼트 |
| **재개** | 마지막 성공 지점부터 중단된 전송 계속 |
| **저장소** | 서버 측 파일 저장소 |

---

## 부록 B: 참고 자료

### 내부 문서
- [common_system 문서](../../../common_system/README.md)
- [thread_system 문서](../../../thread_system/README.md)
- [network_system 문서](../../../network_system/README.md)
- [container_system 문서](../../../container_system/README.md)

### 외부 참조
- [LZ4 공식 저장소](https://github.com/lz4/lz4)
- [RFC 9000 - QUIC](https://tools.ietf.org/html/rfc9000)
