# 오류 코드 레퍼런스

**file_trans_system** 라이브러리에서 사용되는 오류 코드의 완전한 레퍼런스입니다.

## 개요

오류 코드는 생태계 규칙에 따라 **-700 ~ -799** 범위를 사용합니다.

| 범위 | 카테고리 |
|------|---------|
| -700 ~ -719 | 전송 오류 |
| -720 ~ -739 | 청크 오류 |
| -740 ~ -759 | 파일 I/O 오류 |
| -760 ~ -779 | 재개 오류 |
| -780 ~ -789 | 압축 오류 |
| -790 ~ -799 | 설정 오류 |

---

## 전송 오류 (-700 ~ -719)

| 코드 | 이름 | 설명 | 해결 방법 |
|------|------|------|----------|
| **-700** | `transfer_init_failed` | 전송 초기화 실패 | 네트워크 연결 및 엔드포인트 가용성 확인 |
| **-701** | `transfer_cancelled` | 사용자가 전송 취소 | N/A - 사용자 시작 |
| **-702** | `transfer_timeout` | 전송 시간 초과 | 네트워크 상태 확인, 타임아웃 증가 |
| **-703** | `transfer_rejected` | 수신자가 전송 거부 | 수신자 수락 정책 확인 |
| **-704** | `transfer_already_exists` | 전송 ID가 이미 사용 중 | 고유한 전송 ID 사용 |
| **-705** | `transfer_not_found` | 전송 ID를 찾을 수 없음 | 전송 ID 정확성 확인 |

### 예제: 전송 오류 처리

```cpp
auto result = sender->send_file(path, endpoint);

if (!result) {
    switch (result.error().code()) {
        case error::transfer_timeout:
            std::cerr << "전송 시간 초과. 재개 시도 중...\n";
            // 재개로 재시도
            break;
        case error::transfer_rejected:
            std::cerr << "수신자가 전송을 거부했습니다\n";
            break;
        default:
            std::cerr << "전송 실패: " << result.error().message() << "\n";
    }
}
```

---

## 청크 오류 (-720 ~ -739)

| 코드 | 이름 | 설명 | 해결 방법 |
|------|------|------|----------|
| **-720** | `chunk_checksum_error` | 청크 CRC32 검증 실패 | 자동 재시도가 일반적으로 처리 |
| **-721** | `chunk_sequence_error` | 예상 순서와 다르게 청크 수신 | 누락된 청크 대기 또는 재전송 요청 |
| **-722** | `chunk_size_error` | 청크 크기가 최대값(1MB) 초과 | 송신자 설정 확인 |
| **-723** | `file_hash_mismatch` | 조립 후 SHA-256 검증 실패 | 파일 재전송; 소스가 변경되었을 수 있음 |
| **-724** | `chunk_timeout` | 청크 확인응답 시간 초과 | 네트워크 문제; 재시도 또는 재개 |
| **-725** | `chunk_duplicate` | 중복 청크 수신 | 정상 - 청크는 중복 제거됨 |

### 청크 오류 상세

#### -720: chunk_checksum_error

전송 중 데이터 손상을 나타냅니다. 수신된 데이터의 CRC32 체크섬이 예상 값과 일치하지 않습니다.

**자동 처리:**
- 시스템이 자동으로 재전송 요청
- 일반적으로 사용자 개입 불필요

**수동 개입:**
```cpp
receiver->on_complete([](const transfer_result& result) {
    if (result.error && result.error->code() == error::chunk_checksum_error) {
        // 여러 청크에서 검증 실패
        // 네트워크 문제 또는 하드웨어 문제 고려
        log_error("데이터 무결성 문제 감지");
    }
});
```

#### -723: file_hash_mismatch

완성된 파일의 SHA-256 해시가 예상 값과 일치하지 않습니다. 이는 다음을 나타냅니다:
- 전송 중 소스 파일 변경
- 감지되지 않은 청크 손상
- 조립 오류

**해결:**
```cpp
if (result.error && result.error->code() == error::file_hash_mismatch) {
    // 부분 파일 삭제 후 재전송
    std::filesystem::remove(result.output_path);
    // 전송 재개시
}
```

---

## 파일 I/O 오류 (-740 ~ -759)

| 코드 | 이름 | 설명 | 해결 방법 |
|------|------|------|----------|
| **-740** | `file_read_error` | 소스 파일 읽기 실패 | 파일 권한 및 존재 여부 확인 |
| **-741** | `file_write_error` | 대상 파일 쓰기 실패 | 디스크 공간 및 권한 확인 |
| **-742** | `file_permission_error` | 파일 권한 부족 | 파일/디렉토리 권한 조정 |
| **-743** | `file_not_found` | 소스 파일을 찾을 수 없음 | 파일 경로 확인 |
| **-744** | `disk_full` | 디스크 공간 부족 | 디스크 공간 확보 또는 대상 변경 |
| **-745** | `invalid_path` | 잘못된 파일 경로 (경로 순회 시도) | 안전한 파일 이름 사용 |

### 예제: I/O 오류 처리

```cpp
auto result = sender->send_file(path, endpoint);

if (!result) {
    switch (result.error().code()) {
        case error::file_not_found:
            std::cerr << "파일을 찾을 수 없음: " << path << "\n";
            break;
        case error::file_permission_error:
            std::cerr << "권한 거부: " << path << "\n";
            break;
        case error::disk_full:
            std::cerr << "디스크 가득 참. "
                      << bytes_needed << " 바이트 필요\n";
            break;
    }
}
```

### 보안 참고: -745 invalid_path

경로 순회가 감지되면 이 오류가 반환됩니다:

```cpp
// 이 경로들은 거부됩니다:
"../../../etc/passwd"
"/absolute/path/file.txt"
"file/../../../secret.txt"

// 이 경로들은 허용됩니다:
"document.pdf"
"subdir/file.txt"
"reports/2024/q1.csv"
```

---

## 재개 오류 (-760 ~ -779)

| 코드 | 이름 | 설명 | 해결 방법 |
|------|------|------|----------|
| **-760** | `resume_state_invalid` | 재개 상태 파일 손상 | 상태 삭제 후 새로 시작 |
| **-761** | `resume_file_changed` | 마지막 체크포인트 이후 소스 파일 수정됨 | 새 전송 시작 |
| **-762** | `resume_state_corrupted` | 상태 데이터 무결성 검사 실패 | 상태 삭제 후 새로 시작 |
| **-763** | `resume_not_supported` | 원격 엔드포인트가 재개 미지원 | 전체 전송으로 폴백 |

### 재개 오류 처리

```cpp
auto result = sender->resume(transfer_id);

if (!result) {
    switch (result.error().code()) {
        case error::resume_file_changed:
            std::cerr << "마지막 전송 이후 파일이 변경됨. "
                      << "새 전송 시작.\n";
            // 상태 초기화 후 재시작
            resume_handler->delete_state(transfer_id);
            sender->send_file(path, endpoint);
            break;

        case error::resume_state_corrupted:
            std::cerr << "재개 상태 손상. "
                      << "새 전송 시작.\n";
            resume_handler->delete_state(transfer_id);
            sender->send_file(path, endpoint);
            break;
    }
}
```

---

## 압축 오류 (-780 ~ -789)

| 코드 | 이름 | 설명 | 해결 방법 |
|------|------|------|----------|
| **-780** | `compression_failed` | LZ4 압축 실패 | 드물음; 입력 데이터 확인 |
| **-781** | `decompression_failed` | LZ4 압축 해제 실패 | 데이터 손상; 재전송 요청 |
| **-782** | `compression_buffer_error` | 출력 버퍼 너무 작음 | 내부 오류; 버그 리포트 |
| **-783** | `invalid_compression_data` | 압축 데이터 형식 오류 | 데이터 손상; 재전송 요청 |

### 압축 오류 상세

#### -781: decompression_failed

가장 일반적인 압축 오류. 다음을 나타냅니다:
- 손상된 압축 데이터
- 잘린 청크
- 프로토콜 비동기화

**자동 처리:**
- 시스템이 영향받은 청크에 대해 비압축 전송으로 폴백
- 자동으로 재전송 요청

```cpp
// 압축 상태 모니터링
auto stats = manager->get_compression_stats();
double failure_rate =
    static_cast<double>(decompression_failures) /
    static_cast<double>(stats.chunks_compressed);

if (failure_rate > 0.01) {  // >1% 실패
    log_warning("높은 압축 해제 실패율: {}%",
                failure_rate * 100);
    // 임시로 압축 비활성화 고려
    manager->set_default_compression(compression_mode::disabled);
}
```

---

## 설정 오류 (-790 ~ -799)

| 코드 | 이름 | 설명 | 해결 방법 |
|------|------|------|----------|
| **-790** | `config_invalid` | 잘못된 설정 매개변수 | 설정 값 확인 |
| **-791** | `config_chunk_size_error` | 청크 크기가 유효 범위 벗어남 | 64KB - 1MB 사용 |
| **-792** | `config_transport_error` | 전송 설정 오류 | 전송 설정 확인 |

### 설정 검증

```cpp
// 청크 크기는 64KB - 1MB여야 함
auto sender = file_sender::builder()
    .with_chunk_size(32 * 1024)  // 오류: 너무 작음 (32KB < 64KB)
    .build();

if (!sender) {
    // 오류 코드: -791 (config_chunk_size_error)
    std::cerr << sender.error().message() << "\n";
}

// 유효한 설정
auto sender = file_sender::builder()
    .with_chunk_size(256 * 1024)  // OK: 256KB
    .build();
```

---

## 오류 헬퍼 함수

### 오류 메시지 가져오기

```cpp
namespace kcenon::file_transfer::error {

[[nodiscard]] auto error_message(int code) -> std::string_view;

}

// 사용법
int code = -720;
std::cout << "오류: " << error::error_message(code) << "\n";
// 출력: "오류: Chunk CRC32 verification failed"
```

### 오류 카테고리

```cpp
namespace kcenon::file_transfer::error {

[[nodiscard]] auto is_transfer_error(int code) -> bool {
    return code >= -719 && code <= -700;
}

[[nodiscard]] auto is_chunk_error(int code) -> bool {
    return code >= -739 && code <= -720;
}

[[nodiscard]] auto is_io_error(int code) -> bool {
    return code >= -759 && code <= -740;
}

[[nodiscard]] auto is_resume_error(int code) -> bool {
    return code >= -779 && code <= -760;
}

[[nodiscard]] auto is_compression_error(int code) -> bool {
    return code >= -789 && code <= -780;
}

[[nodiscard]] auto is_config_error(int code) -> bool {
    return code >= -799 && code <= -790;
}

[[nodiscard]] auto is_retryable(int code) -> bool;  // 재시도 가능 여부

}
```

---

## 모범 사례

### 1. 항상 결과 확인

```cpp
auto result = sender->send_file(path, endpoint);
if (!result) {
    handle_error(result.error());
}
```

### 2. 컨텍스트와 함께 오류 로깅

```cpp
if (!result) {
    log_error("전송 실패: code={}, message={}, file={}, endpoint={}",
              result.error().code(),
              result.error().message(),
              path.string(),
              endpoint.address);
}
```

### 3. 일시적 오류에 대한 재시도 로직 구현

```cpp
constexpr int max_retries = 3;
int retries = 0;

Result<transfer_handle> result;
do {
    result = sender->send_file(path, endpoint);
    if (!result && error::is_retryable(result.error().code())) {
        std::this_thread::sleep_for(std::chrono::seconds(1 << retries));
        retries++;
    } else {
        break;
    }
} while (retries < max_retries);
```

### 4. 재개 오류의 우아한 처리

```cpp
auto resume_result = sender->resume(id);
if (!resume_result && error::is_resume_error(resume_result.error().code())) {
    // 새 전송으로 폴백
    resume_handler->delete_state(id);
    return sender->send_file(path, endpoint);
}
```

---

*최종 업데이트: 2025-12-11*
