# 파일 전송 시스템 - 소프트웨어 요구사항 명세서

## 문서 정보

| 항목 | 내용 |
|------|------|
| **프로젝트명** | file_trans_system |
| **문서 유형** | 소프트웨어 요구사항 명세서 (SRS) |
| **버전** | 2.0.0 |
| **상태** | 승인됨 |
| **작성일** | 2025-12-11 |
| **작성자** | kcenon@naver.com |
| **관련 문서** | PRD_KR.md v2.0.0 |

---

## 1. 소개

### 1.1 목적

본 소프트웨어 요구사항 명세서(SRS)는 **file_trans_system** 라이브러리의 소프트웨어 요구사항에 대한 완전하고 상세한 설명을 제공합니다. 이 문서는 기술 구현, 테스트 및 검증 활동의 주요 참조 자료로 사용됩니다.

본 문서의 대상 독자:
- 시스템을 구현하는 소프트웨어 개발자
- 테스트 케이스를 설계하는 QA 엔지니어
- 설계를 검증하는 시스템 아키텍트
- 구현 진행 상황을 추적하는 프로젝트 관리자

### 1.2 범위

file_trans_system은 **클라이언트-서버 아키텍처** 기반의 고성능 파일 전송 C++20 라이브러리입니다:

**핵심 컴포넌트:**
- **file_transfer_server**: 중앙 파일 저장소를 관리하는 서버
- **file_transfer_client**: 서버에 연결하여 파일 업로드/다운로드를 수행하는 클라이언트

**주요 기능:**
- 양방향 파일 전송 (업로드 및 다운로드)
- 대용량 파일을 위한 청크 기반 스트리밍
- 실시간 LZ4 압축/해제
- 전송 재개 기능
- 다중 파일 배치 작업
- 서버측 파일 목록 조회
- 기존 에코시스템(common_system, thread_system, network_system 등)과 통합

### 1.3 정의, 약어 및 약자

| 용어 | 정의 |
|------|------|
| **서버** | 파일 저장소를 관리하고 클라이언트 연결을 수락하는 file_transfer_server |
| **클라이언트** | 서버에 연결하여 파일 업로드/다운로드를 요청하는 file_transfer_client |
| **업로드** | 클라이언트에서 서버로 파일을 전송 |
| **다운로드** | 서버에서 클라이언트로 파일을 전송 |
| **청크(Chunk)** | 스트리밍 전송을 위한 고정 크기의 파일 세그먼트 |
| **LZ4** | 빠른 무손실 압축 알고리즘 |
| **CRC32** | 무결성을 위한 32비트 순환 중복 검사 |
| **SHA-256** | 파일 검증을 위한 256비트 보안 해시 알고리즘 |
| **백프레셔(Backpressure)** | 버퍼 오버플로우 방지를 위한 흐름 제어 메커니즘 |
| **파이프라인(Pipeline)** | 다단계 처리 아키텍처 |
| **TLS** | 암호화를 위한 전송 계층 보안 |

### 1.4 참조 문서

| 문서 | 설명 |
|------|------|
| PRD_KR.md | file_trans_system 제품 요구사항 명세서 |
| common_system/README.md | 공통 시스템 인터페이스 및 Result<T> |
| thread_system/README.md | 스레드 풀 및 비동기 실행 |
| network_system/README.md | TCP/TLS 전송 계층 |
| LZ4 문서 | https://github.com/lz4/lz4 |

### 1.5 문서 개요

- **섹션 2**: 전체 시스템 설명 및 컨텍스트
- **섹션 3**: PRD 추적성이 포함된 구체적 소프트웨어 요구사항
- **섹션 4**: 인터페이스 요구사항
- **섹션 5**: 성능 요구사항
- **섹션 6**: 설계 제약사항
- **섹션 7**: 품질 속성

---

## 2. 전체 설명

### 2.1 제품 관점

file_trans_system은 클라이언트-서버 아키텍처를 사용하는 파일 전송 라이브러리입니다:

```
                    ┌─────────────────────────────────┐
                    │     file_transfer_server        │
                    │  ┌───────────────────────────┐  │
                    │  │     Storage Manager       │  │
                    │  │   /data/files/            │  │
                    │  └───────────────────────────┘  │
                    │  ┌───────────────────────────┐  │
                    │  │   Connection Manager      │  │
                    │  │   (max 100 clients)       │  │
                    │  └───────────────────────────┘  │
                    │  ┌───────────────────────────┐  │
                    │  │   Upload/Download         │  │
                    │  │   Pipeline                │  │
                    │  └───────────────────────────┘  │
                    └───────────────┬─────────────────┘
                                    │
        ┌───────────────────────────┼───────────────────────────┐
        │                           │                           │
        ▼                           ▼                           ▼
┌───────────────┐           ┌───────────────┐           ┌───────────────┐
│   Client A    │           │   Client B    │           │   Client C    │
│ upload_file() │           │download_file()│           │ list_files()  │
└───────────────┘           └───────────────┘           └───────────────┘
```

### 2.2 시스템 컴포넌트

```
┌─────────────────────────────────────────────────────────────────────┐
│                      애플리케이션 계층                               │
├─────────────────────────────────────────────────────────────────────┤
│                     file_trans_system                               │
│  ┌──────────────────┐              ┌──────────────────────────────┐ │
│  │ file_transfer    │              │ file_transfer                │ │
│  │ _server          │              │ _client                      │ │
│  │                  │              │                              │ │
│  │ - Storage Mgr    │◄────────────►│ - upload_file()              │ │
│  │ - Connection Mgr │   Network    │ - download_file()            │ │
│  │ - Pipeline       │              │ - list_files()               │ │
│  └──────────────────┘              └──────────────────────────────┘ │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                    공통 컴포넌트                              │  │
│  │  ┌─────────┐ ┌──────────┐ ┌─────────┐ ┌──────────┐ ┌────────┐│  │
│  │  │ 청크    │ │ 압축     │ │ 체크섬  │ │  재개    │ │ 파이프 ││  │
│  │  │ 관리자  │ │ 엔진     │ │ 검증기  │ │ 관리자   │ │ 라인   ││  │
│  │  └─────────┘ └──────────┘ └─────────┘ └──────────┘ └────────┘│  │
│  └──────────────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────────┤
│  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────────────┐│
│  │ common     │ │ thread     │ │ network    │ │ container          ││
│  │ _system    │ │ _system    │ │ _system    │ │ _system            ││
│  └────────────┘ └────────────┘ └────────────┘ └────────────────────┘│
└─────────────────────────────────────────────────────────────────────┘
```

### 2.3 제품 기능 요약

| 기능 | 설명 | PRD 참조 |
|------|------|----------|
| 서버 시작/종료 | 클라이언트 연결을 수락하는 서버 시작 및 종료 | FR-SERVER-01 |
| 파일 업로드 | 클라이언트에서 서버로 파일 전송 | FR-UPLOAD-01 |
| 파일 다운로드 | 서버에서 클라이언트로 파일 전송 | FR-DOWNLOAD-01 |
| 파일 목록 조회 | 서버 저장소의 파일 목록 조회 | FR-LIST-01 |
| 청크 기반 스트리밍 | 전송을 위해 파일을 청크로 분할 | FR-03 |
| 전송 재개 | 중단된 전송 계속 | FR-04 |
| 진행 상황 모니터링 | 실시간 진행 콜백 | FR-05 |
| 무결성 검증 | CRC32/SHA-256 체크섬 | FR-06 |
| 동시 전송 | 다수의 동시 전송 | FR-07 |
| LZ4 압축 | 청크별 압축/해제 | FR-09, FR-10 |
| 파이프라인 처리 | 다단계 병렬 처리 | FR-12, FR-13 |

### 2.4 사용자 특성

| 사용자 유형 | 설명 | 기술 수준 |
|------------|------|-----------|
| 라이브러리 통합자 | file_trans_system을 통합하는 개발자 | 고급 C++ |
| 시스템 관리자 | 전송을 구성하고 모니터링 | 중급 |
| 최종 사용자 | file_trans_system 기반 애플리케이션 사용 | 기본 |

### 2.5 제약사항

| 제약사항 | 요구사항 |
|----------|----------|
| **언어** | C++20 표준 필요 |
| **플랫폼** | Linux, macOS, Windows |
| **컴파일러** | GCC 11+, Clang 14+, MSVC 19.29+ |
| **의존성** | common_system, thread_system, network_system, container_system, LZ4 |
| **라이선스** | BSD(LZ4)와 호환되어야 함 |

### 2.6 가정 및 의존성

| ID | 가정/의존성 |
|----|------------|
| A-01 | 클라이언트와 서버 간 네트워크 연결이 가능함 |
| A-02 | 서버 파일 시스템에 파일을 위한 충분한 공간이 있음 |
| A-03 | LZ4 라이브러리 버전 1.9.0 이상이 사용 가능함 |
| A-04 | thread_system이 typed_thread_pool 기능을 제공함 |
| A-05 | network_system이 TCP 및 TLS 1.3 연결을 지원함 |

---

## 3. 구체적 요구사항

### 3.1 서버 요구사항 (SRS-SERVER)

#### SRS-SERVER-001: 서버 초기화
| 속성 | 값 |
|------|-----|
| **ID** | SRS-SERVER-001 |
| **PRD 추적** | FR-SERVER-01 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 저장소 디렉토리와 설정을 가진 서버를 초기화해야 한다 |

**입력:**
- 저장소 디렉토리 경로 (std::filesystem::path)
- 최대 동시 연결 수 (기본값: 100)
- 최대 파일 크기 제한 (기본값: 10GB)
- 파이프라인 설정 (선택사항)

**처리:**
1. 저장소 디렉토리 존재 여부 확인, 없으면 생성
2. 저장소 쓰기 권한 검증
3. 연결 관리자 초기화
4. 파이프라인 워커 풀 초기화
5. 전송 상태 영속화 경로 설정

**출력:**
- Result<file_transfer_server>
- 실패 시 특정 오류 코드와 함께 오류 결과

**인수 조건:**
- AC-SERVER-001-1: 유효한 경로로 서버 초기화 성공
- AC-SERVER-001-2: 권한 없는 경로에서 적절한 오류 반환
- AC-SERVER-001-3: 초기화된 서버가 시작 가능 상태

---

#### SRS-SERVER-002: 서버 시작
| 속성 | 값 |
|------|-----|
| **ID** | SRS-SERVER-002 |
| **PRD 추적** | FR-SERVER-01 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 지정된 엔드포인트에서 클라이언트 연결을 수락해야 한다 |

**명세:**
```cpp
[[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;
```

**처리:**
1. 지정된 IP:포트에서 리스닝 소켓 바인드
2. TLS 설정된 경우 인증서 로드
3. 클라이언트 연결 수락 루프 시작
4. 각 연결에 대해 핸들러 스레드 할당

**인수 조건:**
- AC-SERVER-002-1: 시작 후 클라이언트 연결 수락
- AC-SERVER-002-2: 포트 충돌 시 적절한 오류 반환
- AC-SERVER-002-3: 다중 클라이언트 동시 연결 처리

---

#### SRS-SERVER-003: 서버 종료
| 속성 | 값 |
|------|-----|
| **ID** | SRS-SERVER-003 |
| **PRD 추적** | FR-SERVER-01 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 활성 전송을 완료하고 우아하게 종료해야 한다 |

**명세:**
```cpp
[[nodiscard]] auto stop() -> Result<void>;
```

**처리:**
1. 새 연결 수락 중지
2. 활성 전송에 종료 예정 알림
3. 진행 중인 전송 완료 대기 (타임아웃 적용)
4. 모든 연결 종료
5. 워커 스레드 정리

**인수 조건:**
- AC-SERVER-003-1: 진행 중 전송이 완료되거나 재개 가능 상태로 저장
- AC-SERVER-003-2: 모든 리소스 해제
- AC-SERVER-003-3: 종료 후 재시작 가능

---

#### SRS-SERVER-004: 업로드 요청 처리
| 속성 | 값 |
|------|-----|
| **ID** | SRS-SERVER-004 |
| **PRD 추적** | FR-UPLOAD-01 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 클라이언트의 업로드 요청을 수락 또는 거부해야 한다 |

**명세:**
```cpp
void on_upload_request(
    std::function<bool(const upload_request&)> callback
);

struct upload_request {
    transfer_id         id;
    std::string         remote_name;     // 서버에 저장될 파일명
    uint64_t            file_size;
    std::string         sha256_hash;
    compression_mode    compression;
    bool                overwrite;       // 기존 파일 덮어쓰기 여부
};
```

**처리:**
1. 업로드 요청 메시지 수신
2. 저장소 할당량 확인
3. 파일명 유효성 검증 (경로 순회 방지)
4. 등록된 콜백으로 수락/거부 결정
5. UPLOAD_ACCEPT 또는 UPLOAD_REJECT 응답

**인수 조건:**
- AC-SERVER-004-1: 콜백이 true 반환 시 업로드 진행
- AC-SERVER-004-2: 콜백이 false 반환 시 거부 메시지 전송
- AC-SERVER-004-3: 콜백 미등록 시 기본 정책 적용 (수락)

---

#### SRS-SERVER-005: 다운로드 요청 처리
| 속성 | 값 |
|------|-----|
| **ID** | SRS-SERVER-005 |
| **PRD 추적** | FR-DOWNLOAD-01 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 클라이언트의 다운로드 요청을 처리해야 한다 |

**명세:**
```cpp
void on_download_request(
    std::function<bool(const download_request&)> callback
);

struct download_request {
    transfer_id         id;
    std::string         remote_name;     // 요청된 파일명
    client_info         client;          // 요청 클라이언트 정보
};
```

**처리:**
1. 다운로드 요청 메시지 수신
2. 파일 존재 여부 확인
3. 등록된 콜백으로 수락/거부 결정
4. 수락 시 파일 메타데이터와 함께 DOWNLOAD_ACCEPT 전송
5. 청크 전송 파이프라인 시작

**인수 조건:**
- AC-SERVER-005-1: 파일 존재하고 콜백 수락 시 다운로드 시작
- AC-SERVER-005-2: 파일 미존재 시 FILE_NOT_FOUND 오류 반환
- AC-SERVER-005-3: 동시 다운로드 지원

---

#### SRS-SERVER-006: 파일 목록 요청 처리
| 속성 | 값 |
|------|-----|
| **ID** | SRS-SERVER-006 |
| **PRD 추적** | FR-LIST-01 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 저장소의 파일 목록을 클라이언트에게 제공해야 한다 |

**명세:**
```cpp
struct list_request {
    std::string         pattern;         // 와일드카드 패턴 (예: "*.txt")
    std::size_t         offset;          // 페이지네이션 오프셋
    std::size_t         limit;           // 최대 결과 수
};

struct file_info {
    std::string         name;
    uint64_t            size;
    std::string         sha256_hash;
    std::chrono::system_clock::time_point modified_time;
};
```

**인수 조건:**
- AC-SERVER-006-1: 요청된 패턴에 맞는 파일 목록 반환
- AC-SERVER-006-2: 페이지네이션 지원
- AC-SERVER-006-3: 빈 저장소 시 빈 목록 반환

---

### 3.2 클라이언트 요구사항 (SRS-CLIENT)

#### SRS-CLIENT-001: 서버 연결
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CLIENT-001 |
| **PRD 추적** | FR-CLIENT-01 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 지정된 서버에 연결할 수 있어야 한다 |

**명세:**
```cpp
[[nodiscard]] auto connect(const endpoint& server_addr) -> Result<void>;
```

**처리:**
1. 서버 엔드포인트로 TCP 연결 시도
2. TLS 핸드셰이크 수행
3. 프로토콜 버전 협상
4. 연결 확인 및 준비 상태 설정

**인수 조건:**
- AC-CLIENT-001-1: 유효한 서버에 연결 성공
- AC-CLIENT-001-2: 연결 실패 시 적절한 오류 코드 반환
- AC-CLIENT-001-3: 연결 타임아웃 적용 (기본 10초)

---

#### SRS-CLIENT-002: 자동 재연결
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CLIENT-002 |
| **PRD 추적** | FR-CLIENT-02 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 연결 끊김 시 자동 재연결을 지원해야 한다 |

**명세:**
```cpp
builder& with_auto_reconnect(bool enable, reconnect_policy policy = {});

struct reconnect_policy {
    std::size_t max_attempts = 5;
    duration    initial_delay = 1s;
    duration    max_delay = 30s;
    double      backoff_multiplier = 2.0;
};
```

**인수 조건:**
- AC-CLIENT-002-1: 연결 끊김 감지 시 자동 재연결 시도
- AC-CLIENT-002-2: 지수 백오프 적용
- AC-CLIENT-002-3: 최대 재시도 횟수 초과 시 오류 보고

---

#### SRS-CLIENT-003: 파일 업로드
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CLIENT-003 |
| **PRD 추적** | FR-UPLOAD-01 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 로컬 파일을 서버에 업로드할 수 있어야 한다 |

**명세:**
```cpp
[[nodiscard]] auto upload_file(
    const std::filesystem::path& local_path,
    const std::string& remote_name,
    const upload_options& options = {}
) -> Result<transfer_handle>;

struct upload_options {
    compression_mode    compression = compression_mode::adaptive;
    bool                overwrite = false;
    std::optional<std::size_t> bandwidth_limit;
};
```

**처리:**
1. 로컬 파일 존재 및 읽기 가능 확인
2. SHA-256 해시 계산
3. UPLOAD_REQUEST 전송 및 응답 대기
4. 승인 시 청크 단위로 파일 전송
5. 전송 완료 후 무결성 검증

**인수 조건:**
- AC-CLIENT-003-1: 파일이 서버에 정확히 업로드됨 (SHA-256 일치)
- AC-CLIENT-003-2: 진행 콜백이 호출됨
- AC-CLIENT-003-3: 서버 거부 시 적절한 오류 반환

---

#### SRS-CLIENT-004: 파일 다운로드
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CLIENT-004 |
| **PRD 추적** | FR-DOWNLOAD-01 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 서버의 파일을 로컬로 다운로드할 수 있어야 한다 |

**명세:**
```cpp
[[nodiscard]] auto download_file(
    const std::string& remote_name,
    const std::filesystem::path& local_path,
    const download_options& options = {}
) -> Result<transfer_handle>;

struct download_options {
    bool                overwrite = false;
    std::optional<std::size_t> bandwidth_limit;
};
```

**처리:**
1. DOWNLOAD_REQUEST 전송
2. 서버 응답에서 파일 메타데이터 수신
3. 로컬 파일 경로 검증 (덮어쓰기 옵션 확인)
4. 청크 수신 및 파일 조립
5. SHA-256 검증

**인수 조건:**
- AC-CLIENT-004-1: 파일이 정확히 다운로드됨 (SHA-256 일치)
- AC-CLIENT-004-2: 서버에 파일 없을 시 오류 반환
- AC-CLIENT-004-3: 로컬 파일 이미 존재하고 overwrite=false 시 오류

---

#### SRS-CLIENT-005: 파일 목록 조회
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CLIENT-005 |
| **PRD 추적** | FR-LIST-01 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 서버의 파일 목록을 조회할 수 있어야 한다 |

**명세:**
```cpp
[[nodiscard]] auto list_files(
    const list_options& options = {}
) -> Result<std::vector<file_info>>;

struct list_options {
    std::string         pattern = "*";
    std::size_t         offset = 0;
    std::size_t         limit = 1000;
};
```

**인수 조건:**
- AC-CLIENT-005-1: 서버 파일 목록 반환
- AC-CLIENT-005-2: 패턴 필터링 적용
- AC-CLIENT-005-3: 페이지네이션 지원

---

#### SRS-CLIENT-006: 배치 업로드
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CLIENT-006 |
| **PRD 추적** | FR-UPLOAD-02 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 여러 파일을 한 번의 작업으로 업로드할 수 있어야 한다 |

**명세:**
```cpp
[[nodiscard]] auto upload_files(
    std::span<const upload_entry> files,
    const upload_options& options = {}
) -> Result<batch_transfer_handle>;

struct upload_entry {
    std::filesystem::path   local_path;
    std::string             remote_name;
};
```

**인수 조건:**
- AC-CLIENT-006-1: 모든 파일이 개별 상태 추적과 함께 업로드됨
- AC-CLIENT-006-2: 일부 실패가 전체 배치를 중단하지 않음
- AC-CLIENT-006-3: 배치 진행 상황에 파일별 분석 포함

---

#### SRS-CLIENT-007: 배치 다운로드
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CLIENT-007 |
| **PRD 추적** | FR-DOWNLOAD-02 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 여러 파일을 한 번의 작업으로 다운로드할 수 있어야 한다 |

**명세:**
```cpp
[[nodiscard]] auto download_files(
    std::span<const download_entry> files,
    const download_options& options = {}
) -> Result<batch_transfer_handle>;

struct download_entry {
    std::string             remote_name;
    std::filesystem::path   local_path;
};
```

**인수 조건:**
- AC-CLIENT-007-1: 모든 파일이 개별 상태 추적과 함께 다운로드됨
- AC-CLIENT-007-2: 일부 실패가 전체 배치를 중단하지 않음
- AC-CLIENT-007-3: 배치 진행 상황에 파일별 분석 포함

---

### 3.3 저장소 관리 요구사항 (SRS-STORAGE)

#### SRS-STORAGE-001: 저장소 할당량 관리
| 속성 | 값 |
|------|-----|
| **ID** | SRS-STORAGE-001 |
| **PRD 추적** | FR-SERVER-02 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 서버 저장소 할당량을 관리해야 한다 |

**명세:**
```cpp
struct storage_config {
    uint64_t            max_total_size = 100ULL * 1024 * 1024 * 1024;  // 100GB
    uint64_t            max_file_size = 10ULL * 1024 * 1024 * 1024;    // 10GB
    uint64_t            reserved_space = 1ULL * 1024 * 1024 * 1024;    // 1GB
};

struct storage_stats {
    uint64_t            total_size;
    uint64_t            used_size;
    uint64_t            available_size;
    std::size_t         file_count;
};
```

**인수 조건:**
- AC-STORAGE-001-1: 할당량 초과 시 업로드 거부
- AC-STORAGE-001-2: 저장소 통계 조회 가능
- AC-STORAGE-001-3: 예약 공간 유지

---

#### SRS-STORAGE-002: 파일명 검증
| 속성 | 값 |
|------|-----|
| **ID** | SRS-STORAGE-002 |
| **PRD 추적** | 보안 요구사항 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 경로 순회 공격을 방지해야 한다 |

**처리:**
1. 파일명에서 경로 구분자 제거/거부
2. ".." 패턴 거부
3. 절대 경로 거부
4. 허용된 문자만 포함 확인

**인수 조건:**
- AC-STORAGE-002-1: "../" 포함 파일명 거부
- AC-STORAGE-002-2: 절대 경로 거부
- AC-STORAGE-002-3: 검증 통과한 파일명만 저장소에 기록

---

### 3.4 청크 관리 (SRS-CHUNK)

#### SRS-CHUNK-001: 파일 분할
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CHUNK-001 |
| **PRD 추적** | FR-03 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 스트리밍 전송을 위해 파일을 고정 크기 청크로 분할해야 한다 |

**명세:**
```cpp
struct chunk_config {
    std::size_t chunk_size = 256 * 1024;      // 기본값: 256KB
    std::size_t min_chunk_size = 64 * 1024;   // 최소값: 64KB
    std::size_t max_chunk_size = 1024 * 1024; // 최대값: 1MB
};
```

**처리:**
1. chunk_size 블록 단위로 파일 읽기
2. 각 청크에 순차적 chunk_index 할당
3. chunk_offset 계산 (파일 내 바이트 위치)
4. 첫 번째 청크에 first_chunk 플래그 설정
5. 마지막 청크에 last_chunk 플래그 설정
6. 청크 데이터의 CRC32 계산

**인수 조건:**
- AC-CHUNK-001-1: 청크로부터 파일이 올바르게 재구성됨
- AC-CHUNK-001-2: 청크 크기가 설정된 범위 내에 있음
- AC-CHUNK-001-3: 마지막 청크는 chunk_size보다 작을 수 있음

---

#### SRS-CHUNK-002: 파일 조립
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CHUNK-002 |
| **PRD 추적** | FR-03 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 수신된 청크로부터 올바른 순서로 파일을 재조립해야 한다 |

**처리:**
1. 청크 수신 (순서가 뒤바뀔 수 있음)
2. 순차적 쓰기가 가능할 때까지 청크 버퍼링
3. 올바른 오프셋에 파일로 청크 기록
4. 중복 청크 처리 (멱등성)
5. 누락된 청크 감지
6. last_chunk 수신 및 모든 갭이 채워지면 조립 완료

**인수 조건:**
- AC-CHUNK-002-1: 순서가 뒤바뀐 청크가 올바르게 조립됨
- AC-CHUNK-002-2: 누락된 청크가 타임아웃 내에 감지됨
- AC-CHUNK-002-3: 중복 청크가 오류 없이 처리됨

---

#### SRS-CHUNK-003: 청크 체크섬 검증
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CHUNK-003 |
| **PRD 추적** | FR-06 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 CRC32를 사용하여 각 청크의 무결성을 검증해야 한다 |

**명세:**
```cpp
// CRC32는 원본(비압축) 데이터에 대해 계산됨
uint32_t calculate_crc32(std::span<const std::byte> data);
bool verify_crc32(std::span<const std::byte> data, uint32_t expected);
```

**인수 조건:**
- AC-CHUNK-003-1: 모든 청크가 처리 전에 검증됨
- AC-CHUNK-003-2: 손상된 청크가 오류 코드 -721과 함께 거부됨
- AC-CHUNK-003-3: CRC32 검증이 < 1% 오버헤드 추가

---

#### SRS-CHUNK-004: 파일 해시 검증
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CHUNK-004 |
| **PRD 추적** | FR-06 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 SHA-256을 사용하여 완전한 파일의 무결성을 검증해야 한다 |

**명세:**
```cpp
// 전체 파일의 SHA-256 (원본, 비압축)
std::string calculate_sha256(const std::filesystem::path& file);
bool verify_sha256(const std::filesystem::path& file, const std::string& expected);
```

**인수 조건:**
- AC-CHUNK-004-1: 전송 전 SHA-256 계산, 수신 후 검증
- AC-CHUNK-004-2: 해시 불일치 시 오류 코드 -722 반환
- AC-CHUNK-004-3: 해시가 transfer_result에 포함됨

---

### 3.5 압축 (SRS-COMP)

#### SRS-COMP-001: LZ4 압축
| 속성 | 값 |
|------|-----|
| **ID** | SRS-COMP-001 |
| **PRD 추적** | FR-09 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 LZ4 알고리즘을 사용하여 청크를 압축해야 한다 |

**명세:**
```cpp
class lz4_engine {
public:
    // 표준 LZ4 압축
    [[nodiscard]] static auto compress(
        std::span<const std::byte> input,
        std::span<std::byte> output
    ) -> Result<std::size_t>;

    // 더 높은 압축률을 위한 LZ4-HC
    [[nodiscard]] static auto compress_hc(
        std::span<const std::byte> input,
        std::span<std::byte> output,
        int level = 9  // 1-12, 기본값 9
    ) -> Result<std::size_t>;
};
```

**성능 목표:**
| 모드 | 압축 속도 | 해제 속도 | 압축률 |
|------|-----------|-----------|--------|
| LZ4 (빠름) | ≥ 400 MB/s | ≥ 1.5 GB/s | ~2.1:1 |
| LZ4-HC | ≥ 50 MB/s | ≥ 1.5 GB/s | ~2.7:1 |

**인수 조건:**
- AC-COMP-001-1: 압축 속도 ≥ 400 MB/s (빠른 모드)
- AC-COMP-001-2: 해제 속도 ≥ 1.5 GB/s
- AC-COMP-001-3: 압축된 데이터가 원본으로 정확히 해제됨

---

#### SRS-COMP-002: LZ4 해제
| 속성 | 값 |
|------|-----|
| **ID** | SRS-COMP-002 |
| **PRD 추적** | FR-09 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 LZ4로 압축된 청크를 해제해야 한다 |

**명세:**
```cpp
[[nodiscard]] static auto decompress(
    std::span<const std::byte> compressed,
    std::span<std::byte> output,
    std::size_t original_size
) -> Result<std::size_t>;
```

**오류 처리:**
- 유효하지 않은 압축 데이터: 오류 코드 -781
- 출력 버퍼 너무 작음: 오류 코드 -782
- 손상된 스트림: 오류 코드 -783

**인수 조건:**
- AC-COMP-002-1: 유효한 압축 데이터가 올바르게 해제됨
- AC-COMP-002-2: 유효하지 않은 데이터에 대해 적절한 오류 반환
- AC-COMP-002-3: original_size가 해제된 출력과 일치

---

#### SRS-COMP-003: 적응형 압축 감지
| 속성 | 값 |
|------|-----|
| **ID** | SRS-COMP-003 |
| **PRD 추적** | FR-10 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 압축 불가능한 데이터를 자동으로 감지하고 압축을 건너뛰어야 한다 |

**알고리즘:**
```cpp
bool is_compressible(std::span<const std::byte> data) {
    // 처음 1KB 샘플링 (또는 작은 청크의 경우 더 적게)
    const auto sample_size = std::min(data.size(), 1024uz);
    auto sample = data.first(sample_size);

    // 샘플 압축 시도
    auto compressed = lz4_compress(sample);

    // >= 10% 감소가 있을 때만 압축
    return compressed.size() < sample.size() * 0.9;
}
```

**인수 조건:**
- AC-COMP-003-1: 감지가 < 100μs 내에 완료
- AC-COMP-003-2: 이미 압축된 파일(zip, jpg)이 압축 불가로 감지됨
- AC-COMP-003-3: 텍스트/로그 파일이 압축 가능으로 감지됨

---

#### SRS-COMP-004: 압축 모드 설정
| 속성 | 값 |
|------|-----|
| **ID** | SRS-COMP-004 |
| **PRD 추적** | FR-09, FR-10 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 설정 가능한 압축 모드를 지원해야 한다 |

**명세:**
```cpp
enum class compression_mode {
    disabled,   // 압축 적용 안 함
    enabled,    // 항상 압축
    adaptive    // 압축 가능 여부 자동 감지 (기본값)
};

enum class compression_level {
    fast,            // LZ4 표준 (기본값)
    high_compression // LZ4-HC
};
```

**인수 조건:**
- AC-COMP-004-1: disabled 모드에서 원시 데이터 전송
- AC-COMP-004-2: enabled 모드에서 항상 압축
- AC-COMP-004-3: adaptive 모드에서 is_compressible() 검사 사용

---

#### SRS-COMP-005: 압축 통계
| 속성 | 값 |
|------|-----|
| **ID** | SRS-COMP-005 |
| **PRD 추적** | FR-11 |
| **우선순위** | P2 (중간) |
| **설명** | 시스템은 압축 통계를 추적하고 보고해야 한다 |

**명세:**
```cpp
struct compression_statistics {
    uint64_t total_raw_bytes;        // 원본 데이터 크기
    uint64_t total_compressed_bytes; // 압축된 데이터 크기
    double   compression_ratio;      // 원본 / 압축
    double   compression_speed_mbps;
    double   decompression_speed_mbps;
    uint64_t chunks_compressed;      // 압축된 청크 수
    uint64_t chunks_skipped;         // 압축이 건너뛰어진 청크 수
};
```

**인수 조건:**
- AC-COMP-005-1: get_compression_stats()를 통해 통계 사용 가능
- AC-COMP-005-2: 전송별 및 전체 통계 제공
- AC-COMP-005-3: 전송 중 실시간으로 통계 업데이트

---

### 3.6 파이프라인 처리 (SRS-PIPE)

#### SRS-PIPE-001: 업로드 파이프라인
| 속성 | 값 |
|------|-----|
| **ID** | SRS-PIPE-001 |
| **PRD 추적** | FR-12 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 다단계 업로드 파이프라인을 구현해야 한다 |

**파이프라인 단계 (클라이언트 → 서버):**
```
[클라이언트]
파일 읽기 → 청크 조립 → LZ4 압축 → 네트워크 전송
(io_read)   (chunk_process)  (compression)   (network)

[서버]
네트워크 수신 → LZ4 해제 → 청크 조립 → 파일 쓰기
(network)      (compression)    (chunk_process)  (io_write)
```

**단계 설정:**
| 단계 | 유형 | 기본 워커 수 |
|------|------|-------------|
| io_read | I/O 바운드 | 2 |
| chunk_process | CPU 경량 | 2 |
| compression | CPU 바운드 | 4 |
| network | I/O 바운드 | 2 |
| io_write | I/O 바운드 | 2 |

**인수 조건:**
- AC-PIPE-001-1: 모든 단계가 동시에 실행됨
- AC-PIPE-001-2: 데이터가 순서대로 단계를 통과
- AC-PIPE-001-3: 단계별 워커 수가 설정 가능

---

#### SRS-PIPE-002: 다운로드 파이프라인
| 속성 | 값 |
|------|-----|
| **ID** | SRS-PIPE-002 |
| **PRD 추적** | FR-12 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 다단계 다운로드 파이프라인을 구현해야 한다 |

**파이프라인 단계 (서버 → 클라이언트):**
```
[서버]
파일 읽기 → 청크 조립 → LZ4 압축 → 네트워크 전송
(io_read)   (chunk_process)  (compression)   (network)

[클라이언트]
네트워크 수신 → LZ4 해제 → 청크 조립 → 파일 쓰기
(network)      (compression)    (chunk_process)  (io_write)
```

**인수 조건:**
- AC-PIPE-002-1: 모든 단계가 동시에 실행됨
- AC-PIPE-002-2: 순서가 뒤바뀐 청크가 올바르게 처리됨
- AC-PIPE-002-3: 단계별 워커 수가 설정 가능

---

#### SRS-PIPE-003: 파이프라인 백프레셔
| 속성 | 값 |
|------|-----|
| **ID** | SRS-PIPE-003 |
| **PRD 추적** | FR-13 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 파이프라인 단계 간 백프레셔를 구현해야 한다 |

**큐 설정:**
```cpp
struct pipeline_config {
    // 스테이지별 워커 수
    std::size_t io_read_workers      = 2;
    std::size_t chunk_workers        = 2;
    std::size_t compression_workers  = 4;
    std::size_t network_workers      = 2;
    std::size_t io_write_workers     = 2;

    // 큐 크기 (항목 수, 바이트가 아님)
    std::size_t read_queue_size      = 16;
    std::size_t compress_queue_size  = 32;
    std::size_t send_queue_size      = 64;
    std::size_t decompress_queue_size = 32;
    std::size_t write_queue_size     = 16;
};
```

**동작:**
- 큐가 가득 차면 업스트림 단계가 차단됨
- 큐가 비어 있으면 다운스트림 단계가 대기
- 메모리가 queue_size × chunk_size로 제한됨

**인수 조건:**
- AC-PIPE-003-1: 파일 크기와 관계없이 메모리 사용량이 제한됨
- AC-PIPE-003-2: 느린 단계가 업스트림 차단을 유발
- AC-PIPE-003-3: 모니터링을 위한 큐 깊이 사용 가능

---

#### SRS-PIPE-004: 파이프라인 통계
| 속성 | 값 |
|------|-----|
| **ID** | SRS-PIPE-004 |
| **PRD 추적** | FR-12 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 파이프라인 성능 통계를 제공해야 한다 |

**명세:**
```cpp
struct pipeline_statistics {
    struct stage_stats {
        uint64_t    jobs_processed;
        uint64_t    bytes_processed;
        double      avg_latency_us;
        double      throughput_mbps;
        std::size_t current_queue_depth;
        std::size_t max_queue_depth;
    };

    stage_stats io_read_stats;
    stage_stats chunk_stats;
    stage_stats compression_stats;
    stage_stats network_stats;
    stage_stats io_write_stats;

    pipeline_stage bottleneck_stage;  // 식별된 병목
};
```

**인수 조건:**
- AC-PIPE-004-1: 단계별 통계 사용 가능
- AC-PIPE-004-2: 병목 단계가 자동으로 식별됨
- AC-PIPE-004-3: 통계가 실시간으로 업데이트됨

---

### 3.7 전송 재개 (SRS-RESUME)

#### SRS-RESUME-001: 전송 상태 영속화
| 속성 | 값 |
|------|-----|
| **ID** | SRS-RESUME-001 |
| **PRD 추적** | FR-04 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 재개 기능을 위해 전송 상태를 영속화해야 한다 |

**상태 데이터:**
```cpp
struct transfer_state {
    transfer_id         id;
    transfer_direction  direction;       // upload 또는 download
    std::string         local_path;
    std::string         remote_name;
    uint64_t            file_size;
    std::string         sha256_hash;
    uint64_t            chunks_completed;
    uint64_t            chunks_total;
    std::vector<bool>   chunk_bitmap;    // 전송된 청크
    compression_mode    compression;
    std::chrono::system_clock::time_point last_update;
};
```

**인수 조건:**
- AC-RESUME-001-1: 각 청크 후에 상태가 영속화됨
- AC-RESUME-001-2: 프로세스 재시작 후에도 상태 복구 가능
- AC-RESUME-001-3: 모든 전송 크기에 대해 상태 파일 < 1MB

---

#### SRS-RESUME-002: 전송 재개
| 속성 | 값 |
|------|-----|
| **ID** | SRS-RESUME-002 |
| **PRD 추적** | FR-04 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 마지막 체크포인트에서 중단된 전송을 재개해야 한다 |

**처리:**
1. 영속화에서 transfer_state 로드
2. 서버/클라이언트 연결 재수립
3. 재개 요청 전송 (chunk_bitmap 포함)
4. 서버에서 청크 상태 확인
5. 누락된 청크만 전송/수신 재개
6. 모든 청크 완료 후 SHA-256 검증

**인수 조건:**
- AC-RESUME-002-1: 1초 이내에 재개 시작
- AC-RESUME-002-2: 재개 시 데이터 손실 또는 손상 없음
- AC-RESUME-002-3: 네트워크 연결 끊김 후에도 재개 작동

---

### 3.8 진행 상황 모니터링 (SRS-PROGRESS)

#### SRS-PROGRESS-001: 진행 콜백
| 속성 | 값 |
|------|-----|
| **ID** | SRS-PROGRESS-001 |
| **PRD 추적** | FR-05 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 실시간 진행 콜백을 제공해야 한다 |

**명세:**
```cpp
struct transfer_progress {
    transfer_id     id;
    transfer_direction direction;       // upload 또는 download
    uint64_t        bytes_transferred;  // 원본 바이트
    uint64_t        bytes_on_wire;      // 압축된 바이트
    uint64_t        total_bytes;
    double          transfer_rate;      // 바이트/초
    double          effective_rate;     // 압축 포함
    double          compression_ratio;
    duration        elapsed_time;
    duration        estimated_remaining;
    transfer_state  state;
};

void on_progress(std::function<void(const transfer_progress&)> callback);
```

**인수 조건:**
- AC-PROGRESS-001-1: 설정 가능한 간격으로 콜백 호출
- AC-PROGRESS-001-2: 진행 상황에 압축 지표 포함
- AC-PROGRESS-001-3: 콜백이 전송을 차단하지 않음

---

#### SRS-PROGRESS-002: 전송 상태
| 속성 | 값 |
|------|-----|
| **ID** | SRS-PROGRESS-002 |
| **PRD 추적** | FR-05 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 전송 생명주기 상태를 추적해야 한다 |

**상태 머신:**
```
pending → connecting → transferring → verifying → completed
                ↓           ↓
            failed ←────────┘
                ↑
          cancelled
```

**인수 조건:**
- AC-PROGRESS-002-1: 모든 상태 전환이 콜백을 통해 보고됨
- AC-PROGRESS-002-2: 오류 상태에 오류 코드와 메시지 포함
- AC-PROGRESS-002-3: 최종 상태가 항상 보고됨 (completed/failed/cancelled)

---

### 3.9 동시 전송 (SRS-CONCURRENT)

#### SRS-CONCURRENT-001: 다중 동시 전송
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CONCURRENT-001 |
| **PRD 추적** | FR-07 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 다중 동시 파일 전송을 지원해야 한다 |

**서버 설정:**
```cpp
builder& with_max_connections(std::size_t max_count);  // 기본값: 100
builder& with_max_transfers_per_client(std::size_t max_count);  // 기본값: 10
```

**클라이언트 설정:**
```cpp
builder& with_max_concurrent_uploads(std::size_t max_count);  // 기본값: 5
builder& with_max_concurrent_downloads(std::size_t max_count);  // 기본값: 5
```

**인수 조건:**
- AC-CONCURRENT-001-1: 서버가 ≥100개 동시 클라이언트 지원
- AC-CONCURRENT-001-2: 클라이언트가 ≥10개 동시 전송 지원
- AC-CONCURRENT-001-3: 각 전송에 독립적인 진행 상황 추적

---

#### SRS-CONCURRENT-002: 대역폭 조절
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CONCURRENT-002 |
| **PRD 추적** | FR-08 |
| **우선순위** | P2 (중간) |
| **설명** | 시스템은 대역폭 제한을 지원해야 한다 |

**명세:**
```cpp
// 서버 전역 제한
builder& with_global_bandwidth_limit(std::size_t bytes_per_second);

// 클라이언트별 제한
builder& with_upload_bandwidth_limit(std::size_t bytes_per_second);
builder& with_download_bandwidth_limit(std::size_t bytes_per_second);

// 전송별 제한
struct upload_options {
    std::optional<std::size_t> bandwidth_limit;
};
```

**인수 조건:**
- AC-CONCURRENT-002-1: 실제 대역폭이 제한의 5% 이내
- AC-CONCURRENT-002-2: 전역 및 전송별 제한 지원
- AC-CONCURRENT-002-3: 제한 변경이 즉시 적용됨

---

### 3.10 전송 계층 (SRS-TRANS)

#### SRS-TRANS-001: 전송 추상화
| 속성 | 값 |
|------|-----|
| **ID** | SRS-TRANS-001 |
| **PRD 추적** | 섹션 6.2 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 여러 프로토콜을 지원하는 추상 전송 계층을 제공해야 한다 |

**명세:**
```cpp
enum class transport_type {
    tcp,    // TCP + TLS 1.3 (기본값, Phase 1)
    quic    // QUIC (선택적, Phase 2)
};

class transport_interface {
public:
    [[nodiscard]] virtual auto connect(const endpoint& ep) -> Result<void> = 0;
    [[nodiscard]] virtual auto disconnect() -> Result<void> = 0;
    [[nodiscard]] virtual auto send(std::span<const std::byte> data) -> Result<void> = 0;
    [[nodiscard]] virtual auto receive(std::span<std::byte> buffer) -> Result<std::size_t> = 0;
    [[nodiscard]] virtual auto is_connected() const -> bool = 0;
};
```

**인수 조건:**
- AC-TRANS-001-1: 전송 추상화가 API 변경 없이 프로토콜 전환 허용
- AC-TRANS-001-2: TCP 전송이 Phase 1에서 완전히 기능함
- AC-TRANS-001-3: QUIC 전송이 Phase 2에서 선택적으로 사용 가능

---

#### SRS-TRANS-002: TCP 전송
| 속성 | 값 |
|------|-----|
| **ID** | SRS-TRANS-002 |
| **PRD 추적** | 섹션 6.2.3 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 network_system을 통해 TCP 전송을 구현해야 한다 |

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

**인수 조건:**
- AC-TRANS-002-1: TCP 전송이 TLS 1.3 암호화 지원
- AC-TRANS-002-2: 저지연을 위해 TCP_NODELAY 기본 활성화
- AC-TRANS-002-3: 설정 가능한 버퍼 크기 및 타임아웃

---

### 3.11 데이터 요구사항

#### 3.11.1 청크 데이터 구조

```cpp
enum class chunk_flags : uint8_t {
    none            = 0x00,
    first_chunk     = 0x01,     // 파일의 첫 번째 청크
    last_chunk      = 0x02,     // 파일의 마지막 청크
    compressed      = 0x04,     // LZ4 압축됨
    encrypted       = 0x08      // TLS용 예약
};

struct chunk_header {
    transfer_id     transfer_id;        // 16바이트 UUID
    uint64_t        file_index;         // 배치의 파일 인덱스
    uint64_t        chunk_index;        // 청크 순서 번호
    uint64_t        chunk_offset;       // 파일 내 바이트 오프셋
    uint32_t        original_size;      // 비압축 크기
    uint32_t        compressed_size;    // 압축 크기
    uint32_t        checksum;           // 원본 데이터의 CRC32
    chunk_flags     flags;              // 청크 플래그
    // 총 헤더 크기: 49바이트 + 패딩
};

struct chunk {
    chunk_header            header;
    std::vector<std::byte>  data;   // 압축 또는 원시 데이터
};
```

#### 3.11.2 전송 메타데이터

```cpp
struct file_metadata {
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;        // 64 16진수 문자
    std::filesystem::perms  permissions;
    std::chrono::system_clock::time_point modified_time;
    bool                    compressible_hint;  // 확장자 기반
};

struct transfer_result {
    transfer_id             id;
    transfer_direction      direction;          // upload 또는 download
    std::filesystem::path   local_path;
    std::string             remote_name;
    uint64_t                bytes_transferred;
    uint64_t                bytes_on_wire;
    bool                    verified;           // SHA-256 일치
    std::optional<error>    error;
    duration                elapsed_time;
};
```

---

## 4. 인터페이스 요구사항

### 4.1 서버 인터페이스

```cpp
namespace kcenon::file_transfer {

class file_transfer_server {
public:
    class builder {
    public:
        builder& with_storage_directory(const std::filesystem::path& dir);
        builder& with_max_connections(std::size_t max_count);
        builder& with_max_file_size(uint64_t max_bytes);
        builder& with_storage_quota(uint64_t max_bytes);
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_transport(transport_type type);
        [[nodiscard]] auto build() -> Result<file_transfer_server>;
    };

    // 생명주기
    [[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;
    [[nodiscard]] auto stop() -> Result<void>;

    // 요청 콜백
    void on_upload_request(
        std::function<bool(const upload_request&)> callback
    );
    void on_download_request(
        std::function<bool(const download_request&)> callback
    );

    // 이벤트 콜백
    void on_client_connected(
        std::function<void(const client_info&)> callback
    );
    void on_client_disconnected(
        std::function<void(const client_info&)> callback
    );
    void on_transfer_complete(
        std::function<void(const transfer_result&)> callback
    );

    // 진행 상황 모니터링
    void on_progress(std::function<void(const transfer_progress&)> callback);

    // 통계
    [[nodiscard]] auto get_statistics() -> server_statistics;
    [[nodiscard]] auto get_storage_stats() -> storage_stats;
    [[nodiscard]] auto list_active_transfers() -> std::vector<transfer_info>;
};

} // namespace kcenon::file_transfer
```

### 4.2 클라이언트 인터페이스

```cpp
namespace kcenon::file_transfer {

class file_transfer_client {
public:
    class builder {
    public:
        builder& with_compression(compression_mode mode);
        builder& with_compression_level(compression_level level);
        builder& with_chunk_size(std::size_t size);
        builder& with_auto_reconnect(bool enable, reconnect_policy policy = {});
        builder& with_upload_bandwidth_limit(std::size_t bytes_per_second);
        builder& with_download_bandwidth_limit(std::size_t bytes_per_second);
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_transport(transport_type type);
        [[nodiscard]] auto build() -> Result<file_transfer_client>;
    };

    // 연결
    [[nodiscard]] auto connect(const endpoint& server_addr) -> Result<void>;
    [[nodiscard]] auto disconnect() -> Result<void>;
    [[nodiscard]] auto is_connected() const -> bool;

    // 단일 파일 작업
    [[nodiscard]] auto upload_file(
        const std::filesystem::path& local_path,
        const std::string& remote_name,
        const upload_options& options = {}
    ) -> Result<transfer_handle>;

    [[nodiscard]] auto download_file(
        const std::string& remote_name,
        const std::filesystem::path& local_path,
        const download_options& options = {}
    ) -> Result<transfer_handle>;

    // 배치 작업
    [[nodiscard]] auto upload_files(
        std::span<const upload_entry> files,
        const upload_options& options = {}
    ) -> Result<batch_transfer_handle>;

    [[nodiscard]] auto download_files(
        std::span<const download_entry> files,
        const download_options& options = {}
    ) -> Result<batch_transfer_handle>;

    // 파일 목록
    [[nodiscard]] auto list_files(
        const list_options& options = {}
    ) -> Result<std::vector<file_info>>;

    // 전송 제어
    [[nodiscard]] auto cancel(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto pause(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto resume(const transfer_id& id) -> Result<void>;

    // 콜백
    void on_progress(std::function<void(const transfer_progress&)> callback);
    void on_complete(std::function<void(const transfer_result&)> callback);
    void on_connection_state_changed(
        std::function<void(connection_state)> callback
    );

    // 통계
    [[nodiscard]] auto get_statistics() -> client_statistics;
    [[nodiscard]] auto get_compression_stats() -> compression_statistics;
};

} // namespace kcenon::file_transfer
```

### 4.3 통신 인터페이스

#### 4.3.1 네트워크 프로토콜

**지원되는 전송 프로토콜:**

| 계층 | 프로토콜 | 단계 | 설명 |
|------|----------|------|------|
| 전송 (기본) | TCP + TLS 1.3 | Phase 1 | 기본값, 모든 환경 |
| 전송 (선택) | QUIC | Phase 2 | 고손실 네트워크, 모바일 |
| 응용 | 커스텀 청크 기반 프로토콜 | - | 최소 오버헤드 (54 바이트/청크) |

#### 4.3.2 메시지 형식

```
┌──────────────────────────────────────────────────────────────┐
│                    전송 프로토콜                              │
├──────────────────────────────────────────────────────────────┤
│ 메시지 유형 (1바이트)                                        │
│   0x01 = HANDSHAKE_REQUEST                                   │
│   0x02 = HANDSHAKE_RESPONSE                                  │
│   0x10 = UPLOAD_REQUEST                                      │
│   0x11 = UPLOAD_ACCEPT                                       │
│   0x12 = UPLOAD_REJECT                                       │
│   0x20 = CHUNK_DATA                                          │
│   0x21 = CHUNK_ACK                                           │
│   0x22 = CHUNK_NACK (재전송 요청)                            │
│   0x30 = RESUME_REQUEST                                      │
│   0x31 = RESUME_RESPONSE                                     │
│   0x40 = TRANSFER_COMPLETE                                   │
│   0x41 = TRANSFER_VERIFY                                     │
│   0x50 = DOWNLOAD_REQUEST                                    │
│   0x51 = DOWNLOAD_ACCEPT                                     │
│   0x52 = DOWNLOAD_REJECT                                     │
│   0x60 = LIST_REQUEST                                        │
│   0x61 = LIST_RESPONSE                                       │
│   0xF0 = KEEPALIVE                                           │
│   0xFF = ERROR                                               │
├──────────────────────────────────────────────────────────────┤
│ 페이로드 길이 (4바이트, 빅 엔디안)                           │
├──────────────────────────────────────────────────────────────┤
│ 페이로드 (가변 길이)                                         │
│   - UPLOAD_REQUEST: 직렬화된 upload_request                  │
│   - DOWNLOAD_REQUEST: 직렬화된 download_request              │
│   - CHUNK_DATA: chunk_header + data                          │
│   - LIST_RESPONSE: 직렬화된 file_info 배열                   │
│   - 등등.                                                    │
└──────────────────────────────────────────────────────────────┘

총 프레임 오버헤드: 5 바이트
```

---

## 5. 성능 요구사항

### 5.1 처리량 요구사항

| ID | 요구사항 | 목표 | 측정 방법 | PRD 추적 |
|----|----------|------|-----------|----------|
| PERF-001 | LAN 처리량 (1GB 파일) | ≥ 500 MB/s | 전송 시간 | NFR-01 |
| PERF-002 | WAN 처리량 | ≥ 100 MB/s | 네트워크 제한 | NFR-01 |
| PERF-003 | LZ4 압축 속도 | ≥ 400 MB/s | 코어당 | NFR-06 |
| PERF-004 | LZ4 해제 속도 | ≥ 1.5 GB/s | 코어당 | NFR-07 |
| PERF-005 | 실질 처리량 (압축 가능) | 기준의 2-4배 | 압축 포함 | NFR-08 |

### 5.2 지연시간 요구사항

| ID | 요구사항 | 목표 | PRD 추적 |
|----|----------|------|----------|
| PERF-010 | 청크 처리 지연시간 | < 10 ms | NFR-02 |
| PERF-011 | 압축 가능 여부 감지 | < 100 μs | NFR-09 |
| PERF-012 | 재개 시작 시간 | < 1초 | FR-04 |
| PERF-013 | 서버 연결 시간 | < 3초 | 새 요구사항 |

### 5.3 리소스 요구사항

| ID | 요구사항 | 목표 | PRD 추적 |
|----|----------|------|----------|
| PERF-020 | 서버 기본 메모리 | < 100 MB | NFR-03 |
| PERF-021 | 클라이언트 기본 메모리 | < 50 MB | NFR-03 |
| PERF-022 | 전송당 메모리 | < 100 MB / 1GB | NFR-04 |
| PERF-023 | CPU 사용률 | < 코어당 30% | NFR-05 |
| PERF-024 | 동시 클라이언트 | ≥ 100 | FR-07 |

### 5.4 용량 요구사항

| ID | 요구사항 | 목표 |
|----|----------|------|
| PERF-030 | 최대 파일 크기 | 파일 시스템에 의해 제한 (100GB까지 테스트) |
| PERF-031 | 최대 배치 크기 | 10,000 파일 |
| PERF-032 | 최대 청크 크기 | 1 MB |
| PERF-033 | 최소 청크 크기 | 64 KB |
| PERF-034 | 서버 저장소 | 구성 가능 (기본 100GB) |

---

## 6. 설계 제약사항

### 6.1 언어 및 표준

| 제약사항 | 요구사항 |
|----------|----------|
| 언어 | C++20 |
| 표준 라이브러리 | 완전한 C++20 지원 필요 |
| 사용 기능 | std::span, std::filesystem, 코루틴 (선택적) |

### 6.2 플랫폼 지원

| 플랫폼 | 최소 버전 | 컴파일러 |
|--------|-----------|----------|
| Linux | Kernel 4.x | GCC 11+, Clang 14+ |
| macOS | 11.0+ | Apple Clang 14+ |
| Windows | 10/Server 2019 | MSVC 19.29+ |

### 6.3 외부 의존성

| 의존성 | 버전 | 용도 | 라이선스 |
|--------|------|------|----------|
| common_system | 최신 | Result<T>, 인터페이스 | 프로젝트 |
| thread_system | 최신 | typed_thread_pool | 프로젝트 |
| network_system | 최신 | TCP/TLS (Phase 1) 및 QUIC (Phase 2) 전송 | 프로젝트 |
| container_system | 최신 | 직렬화 | 프로젝트 |
| LZ4 | 1.9.0+ | 압축 | BSD |

### 6.4 오류 코드 할당

에코시스템 규칙에 따라 file_trans_system은 오류 코드 **-700 ~ -799**를 사용합니다:

| 범위 | 카테고리 |
|------|----------|
| -700 ~ -719 | 전송 오류 |
| -720 ~ -739 | 청크 오류 |
| -740 ~ -759 | 파일 I/O 오류 |
| -760 ~ -779 | 재개 오류 |
| -780 ~ -789 | 압축 오류 |
| -790 ~ -799 | 설정 오류 |

**상세 오류 코드:**

| 코드 | 이름 | 설명 |
|------|------|------|
| -700 | transfer_init_failed | 전송 초기화 실패 |
| -701 | transfer_cancelled | 사용자에 의해 전송 취소 |
| -702 | transfer_timeout | 전송 타임아웃 |
| -703 | upload_rejected | 서버에 의해 업로드 거부 |
| -704 | download_rejected | 서버에 의해 다운로드 거부 |
| -705 | connection_refused | 서버 연결 거부 |
| -706 | connection_lost | 전송 중 연결 끊김 |
| -707 | server_busy | 서버 용량 초과 |
| -720 | chunk_checksum_error | 청크 CRC32 검증 실패 |
| -721 | chunk_sequence_error | 청크가 순서대로 수신되지 않음 |
| -722 | chunk_size_error | 청크 크기가 최대값 초과 |
| -723 | file_hash_mismatch | SHA-256 검증 실패 |
| -740 | file_read_error | 소스 파일 읽기 실패 |
| -741 | file_write_error | 대상 파일 쓰기 실패 |
| -742 | file_permission_error | 파일 권한 부족 |
| -743 | file_not_found | 소스 파일을 찾을 수 없음 |
| -744 | file_already_exists | 파일이 이미 존재 (overwrite=false) |
| -745 | storage_full | 서버 저장소 부족 |
| -746 | file_not_found_on_server | 서버에 파일 없음 |
| -747 | access_denied | 권한 없음 |
| -748 | invalid_filename | 잘못된 파일명 (경로 순회 시도 등) |
| -760 | resume_state_invalid | 재개 상태 손상 |
| -761 | resume_file_changed | 마지막 전송 이후 파일 수정됨 |
| -780 | compression_failed | LZ4 압축 실패 |
| -781 | decompression_failed | LZ4 해제 실패 |
| -782 | compression_buffer_error | 출력 버퍼 너무 작음 |
| -790 | config_invalid | 잘못된 설정 매개변수 |

---

## 7. 품질 속성

### 7.1 신뢰성

| ID | 요구사항 | 목표 | PRD 추적 |
|----|----------|------|----------|
| REL-001 | 데이터 무결성 | 100% (SHA-256 검증) | NFR-10 |
| REL-002 | 재개 성공률 | 100% | NFR-11 |
| REL-003 | 오류 복구 | 지수 백오프를 통한 자동 재시도 | NFR-12 |
| REL-004 | 우아한 성능 저하 | 부하 시 처리량 감소 | NFR-13 |
| REL-005 | 압축 폴백 | 해제 오류 시 원활한 폴백 | NFR-14 |
| REL-006 | 자동 재연결 | 연결 끊김 시 자동 복구 | 새 요구사항 |

### 7.2 보안

| ID | 요구사항 | 설명 | PRD 추적 |
|----|----------|------|----------|
| SEC-001 | 암호화 | 네트워크 전송에 TLS 1.3 | NFR-15 |
| SEC-002 | 인증 | 선택적 인증서 기반 | NFR-16 |
| SEC-003 | 경로 순회 방지 | 파일명 검증 | NFR-17 |
| SEC-004 | 리소스 제한 | 최대 파일 크기, 연결 횟수 | NFR-18 |
| SEC-005 | 저장소 격리 | 서버 저장소 디렉토리 외부 접근 차단 | 새 요구사항 |

### 7.3 유지보수성

| ID | 요구사항 | 목표 |
|----|----------|------|
| MAINT-001 | 코드 커버리지 | ≥ 80% |
| MAINT-002 | 문서화 | 모든 공개 인터페이스에 API 문서 |
| MAINT-003 | 코딩 표준 | C++ Core Guidelines 준수 |

### 7.4 테스트 가능성

| ID | 요구사항 | 설명 |
|----|----------|------|
| TEST-001 | 단위 테스트 | 모든 컴포넌트가 독립적으로 테스트 가능 |
| TEST-002 | 통합 테스트 | 종단간 업로드/다운로드 시나리오 |
| TEST-003 | 벤치마크 테스트 | 성능 회귀 감지 |
| TEST-004 | 새니타이저 클린 | TSan/ASan 경고 없음 |

---

## 8. 추적성 매트릭스

### 8.1 PRD to SRS 추적성

| PRD ID | PRD 설명 | SRS 요구사항 |
|--------|----------|-------------|
| FR-SERVER-01 | 서버 시작/종료 | SRS-SERVER-001, SRS-SERVER-002, SRS-SERVER-003 |
| FR-SERVER-02 | 저장소 관리 | SRS-STORAGE-001, SRS-STORAGE-002 |
| FR-UPLOAD-01 | 단일 파일 업로드 | SRS-SERVER-004, SRS-CLIENT-003 |
| FR-UPLOAD-02 | 배치 업로드 | SRS-CLIENT-006 |
| FR-DOWNLOAD-01 | 단일 파일 다운로드 | SRS-SERVER-005, SRS-CLIENT-004 |
| FR-DOWNLOAD-02 | 배치 다운로드 | SRS-CLIENT-007 |
| FR-LIST-01 | 파일 목록 조회 | SRS-SERVER-006, SRS-CLIENT-005 |
| FR-CLIENT-01 | 서버 연결 | SRS-CLIENT-001 |
| FR-CLIENT-02 | 자동 재연결 | SRS-CLIENT-002 |
| FR-03 | 청크 기반 전송 | SRS-CHUNK-001, SRS-CHUNK-002 |
| FR-04 | 전송 재개 | SRS-RESUME-001, SRS-RESUME-002 |
| FR-05 | 진행 상황 모니터링 | SRS-PROGRESS-001, SRS-PROGRESS-002 |
| FR-06 | 무결성 검증 | SRS-CHUNK-003, SRS-CHUNK-004 |
| FR-07 | 동시 전송 | SRS-CONCURRENT-001 |
| FR-08 | 대역폭 조절 | SRS-CONCURRENT-002 |
| FR-09 | 실시간 LZ4 압축 | SRS-COMP-001, SRS-COMP-002, SRS-COMP-004 |
| FR-10 | 적응형 압축 | SRS-COMP-003 |
| FR-11 | 압축 통계 | SRS-COMP-005 |
| FR-12 | 파이프라인 기반 처리 | SRS-PIPE-001, SRS-PIPE-002, SRS-PIPE-004 |
| FR-13 | 파이프라인 백프레셔 | SRS-PIPE-003 |

### 8.2 사용 사례 to SRS 추적성

| 사용 사례 | 설명 | SRS 요구사항 |
|----------|------|-------------|
| UC-01 | 대용량 파일 업로드 (>10GB) | SRS-CLIENT-003, SRS-CHUNK-001, SRS-PIPE-001 |
| UC-02 | 대용량 파일 다운로드 (>10GB) | SRS-CLIENT-004, SRS-CHUNK-002, SRS-PIPE-002 |
| UC-03 | 소형 파일 배치 업로드 | SRS-CLIENT-006 |
| UC-04 | 소형 파일 배치 다운로드 | SRS-CLIENT-007 |
| UC-05 | 중단된 업로드 재개 | SRS-RESUME-001, SRS-RESUME-002 |
| UC-06 | 중단된 다운로드 재개 | SRS-RESUME-001, SRS-RESUME-002 |
| UC-07 | 진행 상황 모니터링 | SRS-PROGRESS-001, SRS-PROGRESS-002 |
| UC-08 | 서버 파일 목록 탐색 | SRS-CLIENT-005, SRS-SERVER-006 |
| UC-09 | 압축 가능 파일 압축 | SRS-COMP-001, SRS-COMP-003 |
| UC-10 | 압축된 파일 압축 건너뛰기 | SRS-COMP-003, SRS-COMP-004 |

---

## 부록 A: 인수 테스트 케이스

### A.1 서버 테스트

| 테스트 ID | SRS 추적 | 설명 | 예상 결과 |
|-----------|----------|------|-----------|
| TC-S001 | SRS-SERVER-001 | 유효한 디렉토리로 서버 초기화 | 서버 객체 생성 성공 |
| TC-S002 | SRS-SERVER-002 | 서버 시작 및 클라이언트 연결 | 클라이언트 연결 수락 |
| TC-S003 | SRS-SERVER-003 | 활성 전송 중 서버 종료 | 전송 완료 후 종료 |
| TC-S004 | SRS-SERVER-004 | 업로드 요청 수락/거부 | 콜백 결과에 따른 응답 |
| TC-S005 | SRS-SERVER-005 | 다운로드 요청 처리 | 파일 전송 시작 |
| TC-S006 | SRS-SERVER-006 | 파일 목록 요청 처리 | 목록 응답 전송 |

### A.2 클라이언트 테스트

| 테스트 ID | SRS 추적 | 설명 | 예상 결과 |
|-----------|----------|------|-----------|
| TC-C001 | SRS-CLIENT-001 | 서버 연결 | 연결 성공 |
| TC-C002 | SRS-CLIENT-002 | 연결 끊김 후 자동 재연결 | 재연결 성공 |
| TC-C003 | SRS-CLIENT-003 | 1GB 파일 업로드 | 업로드 완료, SHA-256 일치 |
| TC-C004 | SRS-CLIENT-004 | 1GB 파일 다운로드 | 다운로드 완료, SHA-256 일치 |
| TC-C005 | SRS-CLIENT-005 | 파일 목록 조회 | 서버 파일 목록 수신 |
| TC-C006 | SRS-CLIENT-006 | 100개 파일 배치 업로드 | 모든 파일 업로드 완료 |
| TC-C007 | SRS-CLIENT-007 | 100개 파일 배치 다운로드 | 모든 파일 다운로드 완료 |

### A.3 압축 테스트

| 테스트 ID | SRS 추적 | 설명 | 예상 결과 |
|-----------|----------|------|-----------|
| TC-010 | SRS-COMP-001 | 텍스트 파일 압축 업로드 | 압축률 ≥ 2:1 |
| TC-011 | SRS-COMP-002 | 압축 파일 다운로드 및 해제 | 원본 데이터 정확히 복원 |
| TC-012 | SRS-COMP-003 | ZIP 파일 업로드 (적응형) | 압축 건너뛰어짐 |
| TC-013 | SRS-COMP-003 | 텍스트 파일 업로드 (적응형) | 압축 적용됨 |
| TC-014 | SRS-COMP-005 | 압축 통계 확인 | 정확한 압축률 및 속도 보고 |

### A.4 재개 테스트

| 테스트 ID | SRS 추적 | 설명 | 예상 결과 |
|-----------|----------|------|-----------|
| TC-030 | SRS-RESUME-001 | 50%에서 업로드 중단 | 상태 영속화됨 |
| TC-031 | SRS-RESUME-002 | 중단된 업로드 재개 | 50%에서 완료, SHA-256 OK |
| TC-032 | SRS-RESUME-001 | 50%에서 다운로드 중단 | 상태 영속화됨 |
| TC-033 | SRS-RESUME-002 | 중단된 다운로드 재개 | 50%에서 완료, SHA-256 OK |

### A.5 성능 테스트

| 테스트 ID | SRS 추적 | 설명 | 예상 결과 |
|-----------|----------|------|-----------|
| TC-040 | PERF-001 | 1GB LAN 업로드 | ≥ 500 MB/s 처리량 |
| TC-041 | PERF-001 | 1GB LAN 다운로드 | ≥ 500 MB/s 처리량 |
| TC-042 | PERF-003 | LZ4 압축 벤치마크 | ≥ 400 MB/s |
| TC-043 | PERF-004 | LZ4 해제 벤치마크 | ≥ 1.5 GB/s |
| TC-044 | PERF-020 | 서버 메모리 기준선 | < 100 MB RSS |
| TC-045 | PERF-021 | 클라이언트 메모리 기준선 | < 50 MB RSS |
| TC-046 | PERF-024 | 100개 동시 클라이언트 | 모두 오류 없이 처리 |

---

## 부록 B: 용어집

| 용어 | 정의 |
|------|------|
| **서버** | 파일 저장소를 관리하고 클라이언트 연결을 수락하는 file_transfer_server 인스턴스 |
| **클라이언트** | 서버에 연결하여 파일 작업을 수행하는 file_transfer_client 인스턴스 |
| **업로드** | 클라이언트에서 서버로 파일을 전송하는 작업 |
| **다운로드** | 서버에서 클라이언트로 파일을 전송하는 작업 |
| **청크(Chunk)** | 스트리밍 전송을 위한 고정 크기의 파일 세그먼트 |
| **파이프라인(Pipeline)** | 동시 단계를 가진 다단계 처리 아키텍처 |
| **백프레셔(Backpressure)** | 버퍼 오버플로우 방지를 위한 흐름 제어 메커니즘 |
| **LZ4** | 빠른 무손실 압축 알고리즘 |
| **CRC32** | 청크 무결성을 위한 32비트 체크섬 |
| **SHA-256** | 파일 무결성을 위한 256비트 해시 |
| **전송 핸들(Transfer Handle)** | 전송 관리를 위한 불투명 식별자 |
| **적응형 압축(Adaptive Compression)** | 압축 불가능한 데이터의 자동 감지 및 건너뛰기 |
| **자동 재연결(Auto Reconnect)** | 연결 끊김 시 자동 복구 기능 |

---

## 부록 C: 개정 이력

| 버전 | 날짜 | 작성자 | 설명 |
|------|------|--------|------|
| 1.0.0 | 2025-12-11 | kcenon@naver.com | 초기 SRS 작성 (P2P 모델) |
| 1.1.0 | 2025-12-11 | kcenon@naver.com | TCP/QUIC 전송 계층 요구사항 추가 |
| 2.0.0 | 2025-12-11 | kcenon@naver.com | 클라이언트-서버 아키텍처로 전면 재작성 |

---

*문서 끝*
