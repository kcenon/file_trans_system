# 레퍼런스 문서

**file_trans_system**의 상세 기술 레퍼런스 문서 색인입니다.

## 문서 목록

### 핵심 레퍼런스

| 문서 | 설명 |
|------|------|
| [API 레퍼런스](api-reference_KR.md) | 완전한 API 문서 - 클래스, 메서드, 타입 |
| [빠른 참조 카드](quick-reference_KR.md) | 일반적인 작업을 위한 빠른 참조 |

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

### 시작하기

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;

// 송신자 생성
auto sender = file_sender::builder()
    .with_compression(compression_mode::adaptive)
    .build();

// 파일 전송
sender->send_file("/path/to/file", endpoint{"host", 19000});
```

→ [시작 가이드](../getting-started_KR.md)에서 자세히 알아보기

### API 개요

| 클래스 | 용도 |
|-------|------|
| `file_sender` | 파일 전송 |
| `file_receiver` | 파일 수신 |
| `transfer_manager` | 다중 전송 관리 |

→ [API 레퍼런스](api-reference_KR.md)에서 상세 API 확인

### 오류 범위

| 범위 | 카테고리 |
|------|---------|
| -700 ~ -719 | 전송 오류 |
| -720 ~ -739 | 청크 오류 |
| -740 ~ -759 | 파일 I/O 오류 |
| -760 ~ -779 | 재개 오류 |
| -780 ~ -789 | 압축 오류 |
| -790 ~ -799 | 설정 오류 |

→ [오류 코드](error-codes_KR.md)에서 상세 내용 확인

---

## 문서 버전

| 문서 | 버전 | 최종 업데이트 |
|------|------|--------------|
| API 레퍼런스 | 1.0.0 | 2025-12-11 |
| 파이프라인 아키텍처 | 1.0.0 | 2025-12-11 |
| 프로토콜 명세 | 1.0.0 | 2025-12-11 |
| 설정 가이드 | 1.0.0 | 2025-12-11 |
| LZ4 압축 가이드 | 1.0.0 | 2025-12-11 |
| 오류 코드 | 1.0.0 | 2025-12-11 |
| 빠른 참조 카드 | 1.0.0 | 2025-12-11 |

---

*최종 업데이트: 2025-12-11*
