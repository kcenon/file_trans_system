# 레퍼런스 문서

**file_trans_system**의 상세 기술 레퍼런스 문서 색인입니다.

**버전**: 0.2.0
**최종 업데이트**: 2025-12-11

## 문서 목록

### 핵심 레퍼런스

| 문서 | 설명 |
|------|------|
| [API 레퍼런스](api-reference_KR.md) | 완전한 API 문서 - 클래스, 메서드, 타입 |
| [빠른 참조 카드](quick-reference_KR.md) | 일반적인 작업을 위한 빠른 참조 |
| [의존성 요구사항](dependencies_KR.md) | **필수** 의존성 시스템 및 통합 가이드 |

### 아키텍처 레퍼런스

| 문서 | 설명 |
|------|------|
| [파이프라인 아키텍처](pipeline-architecture_KR.md) | 다단계 파이프라인 설계 상세 |
| [프로토콜 명세](protocol-spec_KR.md) | 와이어 프로토콜 및 메시지 형식 |

### 설정 및 튜닝

| 문서 | 설명 |
|------|------|
| [설정 가이드](configuration_KR.md) | 모든 설정 옵션의 완전한 레퍼런스 |
| [LZ4 압축 가이드](lz4-compression_KR.md) | 압축 모드, 수준, 튜닝 |

### 오류 처리

| 문서 | 설명 |
|------|------|
| [오류 코드](error-codes_KR.md) | 완전한 오류 코드 레퍼런스 및 해결 방법 |

---

## 빠른 링크

### 서버 시작하기

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

// 서버 생성
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_max_connections(100)
    .with_max_file_size(10ULL * 1024 * 1024 * 1024)  // 10GB
    .build();

// 업로드 검증
server->on_upload_request([](const upload_request& req) {
    return req.file_size < 5ULL * 1024 * 1024 * 1024;  // 5GB 미만 허용
});

// 서버 시작
server->start(endpoint{"0.0.0.0", 19000});
```

→ [시작 가이드](../getting-started_KR.md)에서 자세히 알아보기

### 클라이언트 사용하기

```cpp
// 클라이언트 생성
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::adaptive)
    .with_auto_reconnect(true)
    .build();

// 서버에 연결
client->connect(endpoint{"192.168.1.100", 19000});

// 업로드
auto upload = client->upload_file("/local/data.zip", "data.zip");

// 다운로드
auto download = client->download_file("report.pdf", "/local/report.pdf");

// 파일 목록 조회
auto files = client->list_files();
```

### API 개요

| 클래스 | 용도 |
|-------|------|
| `file_transfer_server` | 파일 저장 및 클라이언트 관리 |
| `file_transfer_client` | 서버 연결 및 파일 업로드/다운로드 |

→ [API 레퍼런스](api-reference_KR.md)에서 상세 API 확인

### 핵심 개념

| 개념 | 설명 |
|------|------|
| **청크** | 파일의 고정 크기 세그먼트 (기본값: 256KB) |
| **파이프라인** | 다단계 병렬 처리 아키텍처 |
| **적응형 압축** | 압축 가능한 데이터 자동 감지 |
| **재개** | 체크포인트에서 중단된 전송 계속 |
| **자동 재연결** | 연결 끊김 시 자동 복구 |

### 오류 범위

| 범위 | 카테고리 |
|------|----------|
| -700 ~ -709 | 연결 오류 |
| -710 ~ -719 | 전송 오류 |
| -720 ~ -739 | 청크 오류 |
| -740 ~ -749 | 저장소 오류 |
| -750 ~ -759 | 파일 I/O 오류 |
| -760 ~ -779 | 재개 오류 |
| -780 ~ -789 | 압축 오류 |
| -790 ~ -799 | 설정 오류 |

→ [오류 코드](error-codes_KR.md)에서 상세 내용 확인

### 성능 목표

| 지표 | 목표 |
|------|------|
| LAN 처리량 | >= 500 MB/s |
| WAN 처리량 | >= 100 MB/s |
| LZ4 압축 | >= 400 MB/s |
| LZ4 압축 해제 | >= 1.5 GB/s |
| 메모리 기준 | < 50 MB |
| 동시 클라이언트 | >= 100 |

---

## 문서 버전

| 문서 | 버전 | 최종 업데이트 |
|------|------|--------------|
| API 레퍼런스 | 0.2.0 | 2025-12-11 |
| 파이프라인 아키텍처 | 0.2.0 | 2025-12-11 |
| 프로토콜 명세 | 0.2.0 | 2025-12-11 |
| 설정 가이드 | 0.2.0 | 2025-12-11 |
| LZ4 압축 가이드 | 0.2.0 | 2025-12-11 |
| 오류 코드 | 0.2.0 | 2025-12-11 |
| 빠른 참조 카드 | 0.2.0 | 2025-12-11 |
| 의존성 요구사항 | 0.2.0 | 2025-12-11 |

---

*최종 업데이트: 2025-12-11*
*버전: 0.2.0*
