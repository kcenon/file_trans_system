# 파일 전송 시스템 - 소프트웨어 요구사항 명세서

## 문서 정보

| 항목 | 내용 |
|------|------|
| **프로젝트명** | file_trans_system |
| **문서 유형** | 소프트웨어 요구사항 명세서 (SRS) |
| **버전** | 1.1.0 |
| **상태** | 초안 |
| **작성일** | 2025-12-11 |
| **작성자** | kcenon@naver.com |
| **관련 문서** | PRD_KR.md v1.0.0 |

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

file_trans_system은 다음 기능을 제공하는 C++20 라이브러리입니다:
- 고성능 파일 전송 기능
- 대용량 파일을 위한 청크 기반 스트리밍
- 실시간 LZ4 압축/해제
- 전송 재개 기능
- 다중 파일 배치 작업
- 기존 에코시스템(common_system, thread_system, network_system 등)과 통합

### 1.3 정의, 약어 및 약자

| 용어 | 정의 |
|------|------|
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

file_trans_system은 더 큰 에코시스템 내의 라이브러리 컴포넌트로 동작합니다:

```
┌─────────────────────────────────────────────────────────────────────┐
│                      애플리케이션 계층                               │
├─────────────────────────────────────────────────────────────────────┤
│                     file_trans_system                                │
│  ┌──────────────┐  ┌───────────────┐  ┌──────────────────────────┐ │
│  │ 송신         │  │ 수신          │  │ 전송 관리자              │ │
│  │ 파이프라인   │  │ 파이프라인    │  │                          │ │
│  └──────┬───────┘  └───────┬───────┘  └────────────┬─────────────┘ │
│         │                  │                        │               │
│  ┌──────▼──────────────────▼────────────────────────▼─────────────┐ │
│  │                    청크 관리자                                  │ │
│  │  ┌─────────┐ ┌──────────┐ ┌─────────┐ ┌──────────┐ ┌────────┐ │ │
│  │  │ 분할기  │ │ 조립기   │ │ 체크섬  │ │  재개    │ │  LZ4   │ │ │
│  │  └─────────┘ └──────────┘ └─────────┘ └──────────┘ └────────┘ │ │
│  └─────────────────────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────────────┤
│  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────────────┐│
│  │ common     │ │ thread     │ │ network    │ │ container          ││
│  │ _system    │ │ _system    │ │ _system    │ │ _system            ││
│  └────────────┘ └────────────┘ └────────────┘ └────────────────────┘│
└─────────────────────────────────────────────────────────────────────┘
```

### 2.2 제품 기능 요약

| 기능 | 설명 | PRD 참조 |
|------|------|----------|
| 단일 파일 전송 | 무결성 검증과 함께 하나의 파일 전송 | FR-01 |
| 배치 파일 전송 | 한 번의 작업으로 여러 파일 전송 | FR-02 |
| 청크 기반 스트리밍 | 전송을 위해 파일을 청크로 분할 | FR-03 |
| 전송 재개 | 중단된 전송 계속 | FR-04 |
| 진행 상황 모니터링 | 실시간 진행 콜백 | FR-05 |
| 무결성 검증 | CRC32/SHA-256 체크섬 | FR-06 |
| 동시 전송 | 다수의 동시 전송 | FR-07 |
| LZ4 압축 | 청크별 압축/해제 | FR-09, FR-10 |
| 파이프라인 처리 | 다단계 병렬 처리 | FR-12, FR-13 |

### 2.3 사용자 특성

| 사용자 유형 | 설명 | 기술 수준 |
|------------|------|-----------|
| 라이브러리 통합자 | file_trans_system을 통합하는 개발자 | 고급 C++ |
| 시스템 관리자 | 전송을 구성하고 모니터링 | 중급 |
| 최종 사용자 | file_trans_system 기반 애플리케이션 사용 | 기본 |

### 2.4 제약사항

| 제약사항 | 요구사항 |
|----------|----------|
| **언어** | C++20 표준 필요 |
| **플랫폼** | Linux, macOS, Windows |
| **컴파일러** | GCC 11+, Clang 14+, MSVC 19.29+ |
| **의존성** | common_system, thread_system, network_system, container_system, LZ4 |
| **라이선스** | BSD(LZ4)와 호환되어야 함 |

### 2.5 가정 및 의존성

| ID | 가정/의존성 |
|----|------------|
| A-01 | 송신자와 수신자 간 네트워크 연결이 가능함 |
| A-02 | 파일 시스템에 전송된 파일을 위한 충분한 공간이 있음 |
| A-03 | LZ4 라이브러리 버전 1.9.0 이상이 사용 가능함 |
| A-04 | thread_system이 typed_thread_pool 기능을 제공함 |
| A-05 | network_system이 TCP 및 TLS 1.3 연결을 지원함 |

---

## 3. 구체적 요구사항

### 3.1 기능 요구사항

#### 3.1.1 파일 전송 핵심 (SRS-CORE)

##### SRS-CORE-001: 단일 파일 송신
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CORE-001 |
| **PRD 추적** | FR-01 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 송신자에서 수신자로 단일 파일을 전송하는 기능을 제공해야 한다 |

**입력:**
- 파일 경로 (std::filesystem::path)
- 대상 엔드포인트 (IP 주소, 포트)
- 전송 옵션 (선택사항)

**처리:**
1. 파일이 존재하고 읽기 가능한지 검증
2. 전체 파일의 SHA-256 해시 계산
3. 설정 가능한 크기의 청크로 파일 분할
4. 각 청크에 대해 CRC32 계산
5. 활성화된 경우 LZ4 압축 적용
6. network_system을 통해 청크 전송
7. 수신자 확인 대기

**출력:**
- 고유 전송 식별자가 포함된 Result<transfer_handle>
- 실패 시 특정 오류 코드와 함께 오류 결과

**인수 조건:**
- AC-001-1: 100% 데이터 무결성으로 파일 전송 (SHA-256 검증)
- AC-001-2: 성능 목표 내에서 전송 완료
- AC-001-3: 전송 중 진행 콜백 호출

---

##### SRS-CORE-002: 단일 파일 수신
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CORE-002 |
| **PRD 추적** | FR-01 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 송신자로부터 단일 파일을 수신하는 기능을 제공해야 한다 |

**입력:**
- 수신 대기 엔드포인트 (IP 주소, 포트)
- 출력 디렉토리 경로
- 수락/거부 콜백 (선택사항)

**처리:**
1. 수신 연결 대기
2. 파일 메타데이터가 포함된 전송 요청 수신
3. 등록된 경우 수락/거부 콜백 호출
4. 순서대로 청크 수신
5. 각 청크의 CRC32 검증
6. 압축 플래그가 설정된 경우 LZ4 해제
7. 출력 파일에 청크 기록
8. 완료된 파일의 SHA-256 해시 검증

**출력:**
- 파일 경로와 검증 상태가 포함된 transfer_result
- 무결성 실패 또는 타임아웃 시 오류

**인수 조건:**
- AC-002-1: 수신된 파일이 원본 SHA-256 해시와 일치
- AC-002-2: 손상된 청크가 감지되고 보고됨
- AC-002-3: 성공 시 완료 콜백 호출

---

##### SRS-CORE-003: 다중 파일 배치 전송
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CORE-003 |
| **PRD 추적** | FR-02 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 단일 배치 작업에서 여러 파일 전송을 지원해야 한다 |

**입력:**
- 파일 경로 벡터 (std::span<const std::filesystem::path>)
- 대상 엔드포인트
- 전송 옵션

**처리:**
1. 모든 파일이 존재하고 읽기 가능한지 검증
2. 배치 전송 세션 생성
3. 순차적 또는 동시 파일 전송 (설정 가능)
4. 개별 파일 진행 상황 추적
5. 부분 실패 처리 (나머지 파일 계속 전송)

**출력:**
- Result<batch_transfer_handle>
- 파일별 개별 상태

**인수 조건:**
- AC-003-1: 모든 파일이 개별 상태 추적과 함께 전송됨
- AC-003-2: 배치 진행 상황에 파일별 분석 포함
- AC-003-3: 부분 실패가 전체 배치를 중단하지 않음

---

#### 3.1.2 청크 관리 (SRS-CHUNK)

##### SRS-CHUNK-001: 파일 분할
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

##### SRS-CHUNK-002: 파일 조립
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

##### SRS-CHUNK-003: 청크 체크섬 검증
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

##### SRS-CHUNK-004: 파일 해시 검증
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
- AC-CHUNK-004-1: 송신 전 SHA-256 계산, 수신 후 검증
- AC-CHUNK-004-2: 해시 불일치 시 오류 코드 -722 반환
- AC-CHUNK-004-3: 해시가 transfer_result에 포함됨

---

#### 3.1.3 압축 (SRS-COMP)

##### SRS-COMP-001: LZ4 압축
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

##### SRS-COMP-002: LZ4 해제
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

##### SRS-COMP-003: 적응형 압축 감지
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

##### SRS-COMP-004: 압축 모드 설정
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

##### SRS-COMP-005: 압축 통계
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

#### 3.1.4 파이프라인 처리 (SRS-PIPE)

##### SRS-PIPE-001: 송신자 파이프라인
| 속성 | 값 |
|------|-----|
| **ID** | SRS-PIPE-001 |
| **PRD 추적** | FR-12 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 다단계 송신자 파이프라인을 구현해야 한다 |

**파이프라인 단계:**
```
파일 읽기 → 청크 조립 → LZ4 압축 → 네트워크 전송
(io_read)   (chunk_process)  (compression)   (network)
```

**단계 설정:**
| 단계 | 유형 | 기본 워커 수 |
|------|------|-------------|
| io_read | I/O 바운드 | 2 |
| chunk_process | CPU 경량 | 2 |
| compression | CPU 바운드 | 4 |
| network | I/O 바운드 | 2 |

**인수 조건:**
- AC-PIPE-001-1: 모든 단계가 동시에 실행됨
- AC-PIPE-001-2: 데이터가 순서대로 단계를 통과
- AC-PIPE-001-3: 단계별 워커 수가 설정 가능

---

##### SRS-PIPE-002: 수신자 파이프라인
| 속성 | 값 |
|------|-----|
| **ID** | SRS-PIPE-002 |
| **PRD 추적** | FR-12 |
| **우선순위** | P0 (필수) |
| **설명** | 시스템은 다단계 수신자 파이프라인을 구현해야 한다 |

**파이프라인 단계:**
```
네트워크 수신 → LZ4 해제 → 청크 조립 → 파일 쓰기
(network)      (compression)    (chunk_process)  (io_write)
```

**단계 설정:**
| 단계 | 유형 | 기본 워커 수 |
|------|------|-------------|
| network | I/O 바운드 | 2 |
| compression | CPU 바운드 | 4 |
| chunk_process | CPU 경량 | 2 |
| io_write | I/O 바운드 | 2 |

**인수 조건:**
- AC-PIPE-002-1: 모든 단계가 동시에 실행됨
- AC-PIPE-002-2: 순서가 뒤바뀐 청크가 올바르게 처리됨
- AC-PIPE-002-3: 단계별 워커 수가 설정 가능

---

##### SRS-PIPE-003: 파이프라인 백프레셔
| 속성 | 값 |
|------|-----|
| **ID** | SRS-PIPE-003 |
| **PRD 추적** | FR-13 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 파이프라인 단계 간 백프레셔를 구현해야 한다 |

**큐 설정:**
```cpp
struct pipeline_config {
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

##### SRS-PIPE-004: 파이프라인 통계
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

#### 3.1.5 전송 재개 (SRS-RESUME)

##### SRS-RESUME-001: 전송 상태 영속화
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
    std::string         file_path;
    uint64_t            file_size;
    std::string         sha256_hash;
    uint64_t            chunks_completed;
    uint64_t            chunks_total;
    std::vector<bool>   chunk_bitmap;      // 수신된 청크
    compression_mode    compression;
    std::chrono::system_clock::time_point last_update;
};
```

**인수 조건:**
- AC-RESUME-001-1: 각 청크 후에 상태가 영속화됨
- AC-RESUME-001-2: 프로세스 재시작 후에도 상태 복구 가능
- AC-RESUME-001-3: 모든 전송 크기에 대해 상태 파일 < 1MB

---

##### SRS-RESUME-002: 전송 재개
| 속성 | 값 |
|------|-----|
| **ID** | SRS-RESUME-002 |
| **PRD 추적** | FR-04 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 마지막 체크포인트에서 중단된 전송을 재개해야 한다 |

**처리:**
1. 영속화에서 transfer_state 로드
2. 파일이 여전히 존재(송신자)하거나 부분 파일이 유효(수신자)한지 검증
3. chunk_bitmap에서 누락된 청크 계산
4. 누락된 청크만 송신/수신 재개
5. 모든 청크 수신 후 완전한 파일 SHA-256 검증

**인수 조건:**
- AC-RESUME-002-1: 1초 이내에 재개 시작
- AC-RESUME-002-2: 재개 시 데이터 손실 또는 손상 없음
- AC-RESUME-002-3: 네트워크 연결 끊김 후에도 재개 작동

---

#### 3.1.6 진행 상황 모니터링 (SRS-PROGRESS)

##### SRS-PROGRESS-001: 진행 콜백
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
    uint64_t        bytes_transferred;      // 원본 바이트
    uint64_t        bytes_on_wire;          // 압축된 바이트
    uint64_t        total_bytes;
    double          transfer_rate;          // 바이트/초
    double          effective_rate;         // 압축 포함
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

##### SRS-PROGRESS-002: 전송 상태
| 속성 | 값 |
|------|-----|
| **ID** | SRS-PROGRESS-002 |
| **PRD 추적** | FR-05 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 전송 생명주기 상태를 추적해야 한다 |

**상태 머신:**
```
pending → initializing → transferring → verifying → completed
                ↓              ↓
            failed ←──────────┘
                ↑
          cancelled
```

**인수 조건:**
- AC-PROGRESS-002-1: 모든 상태 전환이 콜백을 통해 보고됨
- AC-PROGRESS-002-2: 오류 상태에 오류 코드와 메시지 포함
- AC-PROGRESS-002-3: 최종 상태가 항상 보고됨 (completed/failed/cancelled)

---

#### 3.1.7 동시 전송 (SRS-CONCURRENT)

##### SRS-CONCURRENT-001: 다중 동시 전송
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CONCURRENT-001 |
| **PRD 추적** | FR-07 |
| **우선순위** | P1 (높음) |
| **설명** | 시스템은 다중 동시 파일 전송을 지원해야 한다 |

**설정:**
```cpp
void set_max_concurrent_transfers(std::size_t max_count);
// 기본값: 100
```

**인수 조건:**
- AC-CONCURRENT-001-1: ≥100개 동시 전송 지원
- AC-CONCURRENT-001-2: 각 전송에 독립적인 진행 상황 추적
- AC-CONCURRENT-001-3: 전송들이 스레드 풀을 효율적으로 공유

---

##### SRS-CONCURRENT-002: 대역폭 조절
| 속성 | 값 |
|------|-----|
| **ID** | SRS-CONCURRENT-002 |
| **PRD 추적** | FR-08 |
| **우선순위** | P2 (중간) |
| **설명** | 시스템은 대역폭 제한을 지원해야 한다 |

**명세:**
```cpp
void set_bandwidth_limit(std::size_t bytes_per_second);
// 0 = 무제한 (기본값)

// 전송별 제한
struct transfer_options {
    std::optional<std::size_t> bandwidth_limit;
};
```

**인수 조건:**
- AC-CONCURRENT-002-1: 실제 대역폭이 제한의 5% 이내
- AC-CONCURRENT-002-2: 전역 및 전송별 제한 지원
- AC-CONCURRENT-002-3: 제한 변경이 즉시 적용됨

---

#### 3.1.8 전송 계층 (SRS-TRANS)

##### SRS-TRANS-001: 전송 추상화
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

##### SRS-TRANS-002: TCP 전송
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

##### SRS-TRANS-003: QUIC 전송 (Phase 2)
| 속성 | 값 |
|------|-----|
| **ID** | SRS-TRANS-003 |
| **PRD 추적** | 섹션 6.2.4 |
| **우선순위** | P2 (중간) |
| **설명** | 시스템은 network_system을 통해 QUIC 전송을 구현해야 한다 |

**설정:**
```cpp
struct quic_transport_config {
    bool        enable_0rtt         = true;
    std::size_t max_streams         = 100;
    std::size_t initial_window      = 10 * 1024 * 1024;
    duration    idle_timeout        = 30s;
    bool        enable_migration    = true;
};
```

**QUIC 장점:**
- 0-RTT 연결 재개
- Head-of-line 블로킹 없음
- 연결 마이그레이션 (IP 변경 생존)
- 내장 TLS 1.3

**인수 조건:**
- AC-TRANS-003-1: 0-RTT 연결 재개 기능 작동
- AC-TRANS-003-2: 동시 청크 전송을 위한 멀티스트림 지원
- AC-TRANS-003-3: 네트워크 변경 시 연결 마이그레이션 작동

---

##### SRS-TRANS-004: 프로토콜 폴백
| 속성 | 값 |
|------|-----|
| **ID** | SRS-TRANS-004 |
| **PRD 추적** | 7단계 |
| **우선순위** | P2 (중간) |
| **설명** | 시스템은 QUIC에서 TCP로의 자동 폴백을 지원해야 한다 |

**처리:**
1. 설정된 경우 QUIC 연결 시도
2. QUIC 실패 시 (UDP 차단, 타임아웃) TCP로 폴백
3. 진단을 위한 폴백 이벤트 로깅
4. 중단 없이 전송 계속

**인수 조건:**
- AC-TRANS-004-1: QUIC 실패 후 5초 이내 자동 폴백
- AC-TRANS-004-2: 데이터 손실 없이 전송 계속
- AC-TRANS-004-3: 진단을 위한 폴백 이벤트 로깅

---

### 3.2 데이터 요구사항

#### 3.2.1 청크 데이터 구조

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

#### 3.2.2 전송 메타데이터

```cpp
struct file_metadata {
    std::string             filename;
    uint64_t                file_size;
    std::string             sha256_hash;        // 64 16진수 문자
    std::filesystem::perms  permissions;
    std::chrono::system_clock::time_point modified_time;
    bool                    compressible_hint;  // 확장자 기반
};

struct transfer_request {
    transfer_id                     id;
    std::vector<file_metadata>      files;
    transfer_options                options;
};

struct transfer_result {
    transfer_id             id;
    std::filesystem::path   output_path;
    uint64_t                bytes_transferred;
    uint64_t                bytes_on_wire;
    bool                    verified;           // SHA-256 일치
    std::optional<error>    error;
    duration                elapsed_time;
};
```

---

## 4. 인터페이스 요구사항

### 4.1 사용자 인터페이스

해당 없음 - GUI가 없는 라이브러리 컴포넌트입니다.

### 4.2 소프트웨어 인터페이스

#### 4.2.1 송신자 인터페이스

```cpp
namespace kcenon::file_transfer {

class file_sender {
public:
    class builder {
    public:
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_compression(compression_mode mode);
        builder& with_compression_level(compression_level level);
        builder& with_chunk_size(std::size_t size);
        builder& with_bandwidth_limit(std::size_t bytes_per_second);
        [[nodiscard]] auto build() -> Result<file_sender>;
    };

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

    // 제어 작업
    [[nodiscard]] auto cancel(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto pause(const transfer_id& id) -> Result<void>;
    [[nodiscard]] auto resume(const transfer_id& id) -> Result<void>;

    // 진행 콜백
    void on_progress(std::function<void(const transfer_progress&)> callback);
};

} // namespace kcenon::file_transfer
```

#### 4.2.2 수신자 인터페이스

```cpp
namespace kcenon::file_transfer {

class file_receiver {
public:
    class builder {
    public:
        builder& with_pipeline_config(const pipeline_config& config);
        builder& with_output_directory(const std::filesystem::path& dir);
        builder& with_bandwidth_limit(std::size_t bytes_per_second);
        [[nodiscard]] auto build() -> Result<file_receiver>;
    };

    // 생명주기
    [[nodiscard]] auto start(const endpoint& listen_addr) -> Result<void>;
    [[nodiscard]] auto stop() -> Result<void>;

    // 설정
    void set_output_directory(const std::filesystem::path& dir);

    // 콜백
    void on_transfer_request(
        std::function<bool(const transfer_request&)> callback
    );
    void on_progress(std::function<void(const transfer_progress&)> callback);
    void on_complete(std::function<void(const transfer_result&)> callback);
};

} // namespace kcenon::file_transfer
```

#### 4.2.3 전송 관리자 인터페이스

```cpp
namespace kcenon::file_transfer {

class transfer_manager {
public:
    class builder {
    public:
        builder& with_max_concurrent(std::size_t max_count);
        builder& with_default_compression(compression_mode mode);
        builder& with_global_bandwidth_limit(std::size_t bytes_per_second);
        [[nodiscard]] auto build() -> Result<transfer_manager>;
    };

    // 상태 조회
    [[nodiscard]] auto get_status(const transfer_id& id)
        -> Result<transfer_status>;
    [[nodiscard]] auto list_transfers()
        -> Result<std::vector<transfer_info>>;

    // 통계
    [[nodiscard]] auto get_statistics() -> transfer_statistics;
    [[nodiscard]] auto get_compression_stats() -> compression_statistics;

    // 설정
    void set_bandwidth_limit(std::size_t bytes_per_second);
    void set_max_concurrent_transfers(std::size_t max_count);
    void set_default_compression(compression_mode mode);
};

} // namespace kcenon::file_transfer
```

### 4.3 하드웨어 인터페이스

해당 없음 - 라이브러리는 OS API를 통해 하드웨어를 추상화합니다.

### 4.4 통신 인터페이스

#### 4.4.1 네트워크 프로토콜

**HTTP는 다음 이유로 본 시스템에서 명시적으로 제외됩니다:**
- 스트리밍 파일 전송에 불필요한 추상화 계층
- 높은 헤더 오버헤드 (~800 바이트/요청)가 고빈도 청크 전송에 부적합
- 상태 비저장 설계가 연결 기반 재개 기능과 충돌

**지원되는 전송 프로토콜:**

| 계층 | 프로토콜 | 단계 | 설명 |
|------|----------|------|------|
| 전송 (기본) | TCP + TLS 1.3 | Phase 1 | 기본값, 모든 환경 |
| 전송 (선택) | QUIC | Phase 2 | 고손실 네트워크, 모바일 |
| 응용 | 커스텀 청크 기반 프로토콜 | - | 최소 오버헤드 (54 바이트/청크) |

> **참고**: TCP와 QUIC 모두 network_system에서 제공합니다. 외부 전송 라이브러리가 필요하지 않습니다.

#### 4.4.2 전송 추상화

```cpp
// 전송 유형 선택
enum class transport_type {
    tcp,    // TCP + TLS 1.3 (기본값)
    quic    // QUIC (선택적, Phase 2)
};

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

#### 4.4.3 메시지 형식

```
┌──────────────────────────────────────────────────────────────┐
│                    전송 프로토콜                              │
├──────────────────────────────────────────────────────────────┤
│ 메시지 유형 (1바이트)                                        │
│   0x01 = HANDSHAKE_REQUEST                                   │
│   0x02 = HANDSHAKE_RESPONSE                                  │
│   0x10 = TRANSFER_REQUEST                                    │
│   0x11 = TRANSFER_ACCEPT                                     │
│   0x12 = TRANSFER_REJECT                                     │
│   0x13 = TRANSFER_CANCEL                                     │
│   0x20 = CHUNK_DATA                                          │
│   0x21 = CHUNK_ACK                                           │
│   0x22 = CHUNK_NACK (재전송 요청)                            │
│   0x30 = RESUME_REQUEST                                      │
│   0x31 = RESUME_RESPONSE                                     │
│   0x40 = TRANSFER_COMPLETE                                   │
│   0x41 = TRANSFER_VERIFY                                     │
│   0xF0 = KEEPALIVE                                           │
│   0xFF = ERROR                                               │
├──────────────────────────────────────────────────────────────┤
│ 페이로드 길이 (4바이트, 빅 엔디안)                           │
├──────────────────────────────────────────────────────────────┤
│ 페이로드 (가변 길이)                                         │
│   - TRANSFER_REQUEST: 직렬화된 transfer_request              │
│   - CHUNK_DATA: chunk_header + data                          │
│   - 등등.                                                    │
└──────────────────────────────────────────────────────────────┘

총 프레임 오버헤드: 5 바이트 (HTTP ~800 바이트 대비)
```

#### 4.4.4 프로토콜 오버헤드 비교

| 프로토콜 | 청크당 오버헤드 | 총합 (1GB, 256KB 청크) | 비율 |
|----------|----------------|------------------------|------|
| HTTP/1.1 | ~800 바이트 | ~3.2 MB | 0.31% |
| Custom/TCP | 54 바이트 | ~221 KB | 0.02% |
| Custom/QUIC | ~74 바이트 | ~303 KB | 0.03% |

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

### 5.3 리소스 요구사항

| ID | 요구사항 | 목표 | PRD 추적 |
|----|----------|------|----------|
| PERF-020 | 기본 메모리 | < 50 MB | NFR-03 |
| PERF-021 | 전송당 메모리 | < 100 MB / 1GB | NFR-04 |
| PERF-022 | CPU 사용률 | < 코어당 30% | NFR-05 |
| PERF-023 | 동시 전송 | ≥ 100 | FR-07 |

### 5.4 용량 요구사항

| ID | 요구사항 | 목표 |
|----|----------|------|
| PERF-030 | 최대 파일 크기 | 파일 시스템에 의해 제한 (100GB까지 테스트) |
| PERF-031 | 최대 배치 크기 | 10,000 파일 |
| PERF-032 | 최대 청크 크기 | 1 MB |
| PERF-033 | 최소 청크 크기 | 64 KB |

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

> **참고**: network_system이 TCP와 QUIC 전송 구현을 모두 제공합니다. 외부 전송 라이브러리가 필요하지 않습니다.

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
| -703 | transfer_rejected | 수신자에 의해 전송 거부 |
| -720 | chunk_checksum_error | 청크 CRC32 검증 실패 |
| -721 | chunk_sequence_error | 청크가 순서대로 수신되지 않음 |
| -722 | chunk_size_error | 청크 크기가 최대값 초과 |
| -723 | file_hash_mismatch | SHA-256 검증 실패 |
| -740 | file_read_error | 소스 파일 읽기 실패 |
| -741 | file_write_error | 대상 파일 쓰기 실패 |
| -742 | file_permission_error | 파일 권한 부족 |
| -743 | file_not_found | 소스 파일을 찾을 수 없음 |
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

### 7.2 보안

| ID | 요구사항 | 설명 | PRD 추적 |
|----|----------|------|----------|
| SEC-001 | 암호화 | 네트워크 전송에 TLS 1.3 | NFR-15 |
| SEC-002 | 인증 | 선택적 인증서 기반 | NFR-16 |
| SEC-003 | 경로 순회 방지 | 출력 경로 검증 | NFR-17 |
| SEC-004 | 리소스 제한 | 최대 파일 크기, 전송 횟수 | NFR-18 |

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
| TEST-002 | 통합 테스트 | 종단간 전송 시나리오 |
| TEST-003 | 벤치마크 테스트 | 성능 회귀 감지 |
| TEST-004 | 새니타이저 클린 | TSan/ASan 경고 없음 |

---

## 8. 추적성 매트릭스

### 8.1 PRD to SRS 추적성

| PRD ID | PRD 설명 | SRS 요구사항 |
|--------|----------|-------------|
| FR-01 | 단일 파일 전송 | SRS-CORE-001, SRS-CORE-002 |
| FR-02 | 다중 파일 배치 전송 | SRS-CORE-003 |
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
| 섹션 6.2 | 전송 프로토콜 설계 | SRS-TRANS-001 |
| 섹션 6.2.3 | TCP 전송 설정 | SRS-TRANS-002 |
| 섹션 6.2.4 | QUIC 전송 설정 | SRS-TRANS-003 |
| 7단계 | 프로토콜 폴백 | SRS-TRANS-004 |
| NFR-01 | 처리량 | PERF-001, PERF-002 |
| NFR-02 | 지연시간 | PERF-010 |
| NFR-03 | 메모리 (기본) | PERF-020 |
| NFR-04 | 메모리 (전송) | PERF-021 |
| NFR-05 | CPU 사용률 | PERF-022 |
| NFR-06 | LZ4 압축 속도 | PERF-003 |
| NFR-07 | LZ4 해제 속도 | PERF-004 |
| NFR-08 | 압축률 | PERF-005 |
| NFR-09 | 적응형 감지 속도 | PERF-011 |
| NFR-10 | 데이터 무결성 | REL-001 |
| NFR-11 | 재개 정확도 | REL-002 |
| NFR-12 | 오류 복구 | REL-003 |
| NFR-13 | 우아한 성능 저하 | REL-004 |
| NFR-14 | 압축 폴백 | REL-005 |
| NFR-15 | 암호화 | SEC-001 |
| NFR-16 | 인증 | SEC-002 |
| NFR-17 | 경로 순회 | SEC-003 |
| NFR-18 | 리소스 제한 | SEC-004 |

### 8.2 사용 사례 to SRS 추적성

| 사용 사례 | 설명 | SRS 요구사항 |
|----------|------|-------------|
| UC-01 | 대용량 파일 전송 (>10GB) | SRS-CORE-001, SRS-CHUNK-001, SRS-PIPE-001 |
| UC-02 | 소형 파일 배치 | SRS-CORE-003 |
| UC-03 | 중단된 전송 재개 | SRS-RESUME-001, SRS-RESUME-002 |
| UC-04 | 진행 상황 모니터링 | SRS-PROGRESS-001, SRS-PROGRESS-002 |
| UC-05 | 보안 전송 | SEC-001, SEC-002 |
| UC-06 | 우선순위 큐 | SRS-CONCURRENT-001, SRS-CONCURRENT-002 |
| UC-07 | 압축 가능 파일 압축 | SRS-COMP-001, SRS-COMP-003 |
| UC-08 | 압축된 파일 압축 건너뛰기 | SRS-COMP-003, SRS-COMP-004 |

---

## 부록 A: 인수 테스트 케이스

### A.1 핵심 전송 테스트

| 테스트 ID | SRS 추적 | 설명 | 예상 결과 |
|-----------|----------|------|-----------|
| TC-001 | SRS-CORE-001 | LAN에서 1GB 파일 전송 | 전송 완료, SHA-256 일치 |
| TC-002 | SRS-CORE-002 | 1GB 파일 수신 | 파일 수신, SHA-256 검증됨 |
| TC-003 | SRS-CORE-003 | 100개 파일 배치 전송 | 모든 파일이 상태와 함께 전송됨 |
| TC-004 | SRS-CHUNK-003 | 전송 중 청크 손상 | 손상 감지, 전송 재시도 |
| TC-005 | SRS-CHUNK-004 | 전송 중 파일 수정 | SHA-256 불일치 감지 |

### A.2 압축 테스트

| 테스트 ID | SRS 추적 | 설명 | 예상 결과 |
|-----------|----------|------|-----------|
| TC-010 | SRS-COMP-001 | 텍스트 파일 압축 | 압축률 ≥ 2:1 |
| TC-011 | SRS-COMP-002 | 청크 해제 | 원본 데이터 정확히 복원 |
| TC-012 | SRS-COMP-003 | ZIP 파일 전송 (적응형) | 압축 건너뛰어짐 |
| TC-013 | SRS-COMP-003 | 텍스트 파일 전송 (적응형) | 압축 적용됨 |
| TC-014 | SRS-COMP-005 | 압축 통계 확인 | 정확한 압축률 및 속도 보고 |

### A.3 파이프라인 테스트

| 테스트 ID | SRS 추적 | 설명 | 예상 결과 |
|-----------|----------|------|-----------|
| TC-020 | SRS-PIPE-001 | 병렬 단계 검증 | 모든 단계가 동시에 실행됨 |
| TC-021 | SRS-PIPE-003 | 느린 수신자 백프레셔 | 송신자 속도 감소, 메모리 제한됨 |
| TC-022 | SRS-PIPE-004 | 파이프라인 통계 가져오기 | 단계별 지표 사용 가능 |

### A.4 재개 테스트

| 테스트 ID | SRS 추적 | 설명 | 예상 결과 |
|-----------|----------|------|-----------|
| TC-030 | SRS-RESUME-001 | 50%에서 전송 중단 | 상태 영속화됨 |
| TC-031 | SRS-RESUME-002 | 중단된 전송 재개 | 50%에서 완료, SHA-256 OK |
| TC-032 | SRS-RESUME-002 | 네트워크 실패 후 재개 | 데이터 손실 없이 완료 |

### A.5 성능 테스트

| 테스트 ID | SRS 추적 | 설명 | 예상 결과 |
|-----------|----------|------|-----------|
| TC-040 | PERF-001 | 1GB LAN 전송 | ≥ 500 MB/s 처리량 |
| TC-041 | PERF-003 | LZ4 압축 벤치마크 | ≥ 400 MB/s |
| TC-042 | PERF-004 | LZ4 해제 벤치마크 | ≥ 1.5 GB/s |
| TC-043 | PERF-020 | 메모리 기준선 | < 50 MB RSS |
| TC-044 | PERF-023 | 100개 동시 전송 | 모두 오류 없이 완료 |

---

## 부록 B: 용어집

| 용어 | 정의 |
|------|------|
| **청크(Chunk)** | 스트리밍 전송을 위한 고정 크기의 파일 세그먼트 |
| **파이프라인(Pipeline)** | 동시 단계를 가진 다단계 처리 아키텍처 |
| **백프레셔(Backpressure)** | 버퍼 오버플로우 방지를 위한 흐름 제어 메커니즘 |
| **LZ4** | 빠른 무손실 압축 알고리즘 |
| **CRC32** | 청크 무결성을 위한 32비트 체크섬 |
| **SHA-256** | 파일 무결성을 위한 256비트 해시 |
| **전송 핸들(Transfer Handle)** | 전송 관리를 위한 불투명 식별자 |
| **적응형 압축(Adaptive Compression)** | 압축 불가능한 데이터의 자동 감지 및 건너뛰기 |
| **typed_thread_pool** | thread_system의 타입 기반 작업 라우팅을 가진 스레드 풀 |

---

## 부록 C: 개정 이력

| 버전 | 날짜 | 작성자 | 설명 |
|------|------|--------|------|
| 1.0.0 | 2025-12-11 | kcenon@naver.com | 초기 SRS 작성 |
| 1.1.0 | 2025-12-11 | kcenon@naver.com | TCP/QUIC 전송 계층 요구사항 추가 (SRS-TRANS), HTTP 제외 근거 명시 |

---

*문서 끝*
