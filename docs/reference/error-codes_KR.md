# 오류 코드 레퍼런스

**file_trans_system** 라이브러리에서 사용되는 오류 코드의 완전한 레퍼런스입니다.

**버전**: 0.2.0
**최종 업데이트**: 2025-12-11

## 개요

오류 코드는 생태계 규칙에 따라 **-700 ~ -799** 범위를 사용합니다.

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

---

## 연결 오류 (-700 ~ -709)

| 코드 | 이름 | 설명 | 해결 방법 |
|------|------|------|----------|
| **-700** | `connection_failed` | 서버 연결 실패 | 네트워크 연결 및 서버 주소 확인 |
| **-701** | `connection_timeout` | 연결 시도 시간 초과 | 네트워크 상태 확인, 타임아웃 증가 |
| **-702** | `connection_refused` | 서버가 연결 거부 | 서버 실행 중인지, 포트가 올바른지 확인 |
| **-703** | `connection_lost` | 서버와의 연결 끊김 | 네트워크 안정성 확인; 자동 재연결로 복구 가능 |
| **-704** | `reconnect_failed` | 최대 시도 후 자동 재연결 실패 | 수동 개입 필요; 서버 가용성 확인 |
| **-705** | `session_expired` | 서버 세션 만료 | 재연결하여 새 세션 수립 |
| **-706** | `server_busy` | 서버가 최대 연결 수 도달 | 나중에 재시도하거나 서버 용량 증가 |
| **-707** | `protocol_mismatch` | 프로토콜 버전 비호환 | 클라이언트/서버를 호환 버전으로 업데이트 |

### 예제: 연결 오류 처리

```cpp
auto result = client->connect(endpoint{"192.168.1.100", 19000});

if (!result) {
    switch (result.error().code()) {
        case error::connection_refused:
            std::cerr << "서버를 사용할 수 없습니다. 서버 상태를 확인하세요.\n";
            break;
        case error::connection_timeout:
            std::cerr << "연결 시간 초과. 네트워크를 확인하세요.\n";
            break;
        case error::server_busy:
            std::cerr << "서버 용량 초과. 30초 후 재시도합니다.\n";
            std::this_thread::sleep_for(30s);
            result = client->connect(endpoint);
            break;
        case error::protocol_mismatch:
            std::cerr << "프로토콜 버전 불일치. 업데이트가 필요합니다.\n";
            break;
        default:
            std::cerr << "연결 실패: " << result.error().message() << "\n";
    }
}
```

### 자동 재연결 동작

`auto_reconnect`가 활성화되면 클라이언트가 자동으로 연결 끊김을 처리합니다:

```cpp
client->on_disconnected([](disconnect_reason reason) {
    switch (reason) {
        case disconnect_reason::network_error:
            // 자동 재연결이 복구 시도
            std::cout << "재연결 중...\n";
            break;
        case disconnect_reason::server_shutdown:
            // 서버가 연결 해제 시작
            std::cout << "서버가 종료됩니다.\n";
            break;
        case disconnect_reason::max_reconnects_exceeded:
            // 오류 코드 -704 보고
            std::cerr << "자동 재연결 실패.\n";
            break;
    }
});
```

---

## 전송 오류 (-710 ~ -719)

| 코드 | 이름 | 설명 | 해결 방법 |
|------|------|------|----------|
| **-710** | `transfer_init_failed` | 전송 초기화 실패 | 파일 접근 권한 및 서버 수락 여부 확인 |
| **-711** | `transfer_cancelled` | 사용자가 전송 취소 | N/A - 사용자 시작 |
| **-712** | `transfer_timeout` | 전송 시간 초과 | 네트워크 상태 확인, 타임아웃 증가 |
| **-713** | `upload_rejected` | 서버가 업로드 거부 | 서버 정책 또는 파일 제한 확인 |
| **-714** | `download_rejected` | 서버가 다운로드 거부 | 파일 가용성 또는 접근 권한 확인 |
| **-715** | `transfer_already_exists` | 전송 ID가 이미 사용 중 | 고유한 전송 ID 사용 |
| **-716** | `transfer_not_found` | 전송 ID를 찾을 수 없음 | 전송 ID 정확성 확인 |
| **-717** | `transfer_in_progress` | 같은 파일에 대한 다른 전송이 진행 중 | 기존 전송 완료 대기 또는 취소 |

### 예제: 전송 오류 처리

```cpp
// 업로드 예제
auto upload = client->upload_file("/local/data.zip", "data.zip");

if (!upload) {
    switch (upload.error().code()) {
        case error::upload_rejected:
            std::cerr << "서버가 업로드를 거부했습니다: "
                      << upload.error().message() << "\n";
            break;
        case error::transfer_timeout:
            std::cerr << "업로드 시간 초과. 재개 시도 중...\n";
            // 재개 로직
            break;
        default:
            std::cerr << "업로드 실패: " << upload.error().message() << "\n";
    }
}

// 다운로드 예제
auto download = client->download_file("report.pdf", "/local/report.pdf");

if (!download) {
    switch (download.error().code()) {
        case error::download_rejected:
            std::cerr << "파일을 다운로드할 수 없습니다\n";
            break;
        default:
            std::cerr << "다운로드 실패: " << download.error().message() << "\n";
    }
}
```

---

## 청크 오류 (-720 ~ -739)

| 코드 | 이름 | 설명 | 해결 방법 |
|------|------|------|----------|
| **-720** | `chunk_checksum_error` | 청크 CRC32 검증 실패 | 자동 재시도로 일반적으로 해결됨 |
| **-721** | `chunk_sequence_error` | 청크가 예상 순서를 벗어나 수신됨 | 누락된 청크 대기 또는 재전송 요청 |
| **-722** | `chunk_size_error` | 청크 크기가 최대값(1MB) 초과 | 송신 측 설정 확인 |
| **-723** | `file_hash_mismatch` | 조립 후 SHA-256 검증 실패 | 파일 재전송; 원본이 변경되었을 수 있음 |
| **-724** | `chunk_timeout` | 청크 확인 응답 시간 초과 | 네트워크 문제; 재시도 또는 재개 |
| **-725** | `chunk_duplicate` | 중복 청크 수신 | 정상 - 청크가 중복 제거됨 |

### 청크 오류 상세

#### -720: chunk_checksum_error

전송 중 데이터 손상을 나타냅니다. 수신 데이터의 CRC32 체크섬이 예상 값과 일치하지 않습니다.

**자동 처리:**
- 시스템이 자동으로 재전송 요청 (CHUNK_NACK)
- 일반적으로 사용자 개입 불필요

**수동 개입:**
```cpp
client->on_complete([](const transfer_result& result) {
    if (result.error && result.error->code() == error::chunk_checksum_error) {
        // 여러 청크가 검증 실패
        // 네트워크 문제 또는 하드웨어 문제 고려
        log_error("데이터 무결성 문제 감지됨");
    }
});
```

#### -723: file_hash_mismatch

완전한 파일의 SHA-256 해시가 예상 값과 일치하지 않습니다. 이는 다음을 나타냅니다:
- 전송 중 원본 파일 변경
- 감지되지 않은 청크 손상
- 조립 오류

**해결:**
```cpp
if (result.error && result.error->code() == error::file_hash_mismatch) {
    // 부분 파일 삭제 및 재전송
    std::filesystem::remove(result.local_path);
    // 전송 재시작
}
```

---

## 저장소 오류 (-740 ~ -749)

| 코드 | 이름 | 설명 | 해결 방법 |
|------|------|------|----------|
| **-740** | `storage_error` | 서버의 일반 저장소 오류 | 서버 로그 확인 |
| **-741** | `storage_unavailable` | 서버 저장소가 일시적으로 사용 불가 | 나중에 재시도 |
| **-742** | `storage_quota_exceeded` | 서버 저장소 할당량 초과 | 관리자가 공간 확보 또는 할당량 증가 필요 |
| **-743** | `max_file_size_exceeded` | 파일이 서버 최대 허용 크기 초과 | 파일 분할 또는 더 큰 제한 요청 |
| **-744** | `file_already_exists` | 같은 이름의 파일이 이미 존재 | `overwrite_existing` 옵션 사용 또는 파일 이름 변경 |
| **-745** | `storage_full` | 서버 디스크가 가득 참 | 관리자가 공간 확보 필요 |
| **-746** | `file_not_found_on_server` | 요청한 파일이 서버에 없음 | 파일명 확인; `list_files()`로 확인 |
| **-747** | `access_denied` | 서버 정책에 의해 접근 거부 | 권한 확인 또는 관리자 문의 |
| **-748** | `invalid_filename` | 잘못된 파일명 (특수 문자, 경로 탐색) | 특수 문자 없는 간단한 파일명 사용 |
| **-749** | `client_quota_exceeded` | 클라이언트별 할당량 초과 | 대기 또는 할당량 증가 요청 |

### 예제: 저장소 오류 처리 (클라이언트 측)

```cpp
auto upload = client->upload_file("/local/large_file.zip", "large_file.zip");

if (!upload) {
    switch (upload.error().code()) {
        case error::file_already_exists:
            std::cerr << "파일이 이미 존재합니다. 덮어쓰기 중...\n";
            upload = client->upload_file(
                "/local/large_file.zip",
                "large_file.zip",
                upload_options{.overwrite_existing = true}
            );
            break;
        case error::max_file_size_exceeded:
            std::cerr << "파일이 서버에 비해 너무 큽니다. 최대: "
                      << server_max_size << " 바이트\n";
            break;
        case error::storage_quota_exceeded:
            std::cerr << "서버 저장소가 가득 찼습니다. 관리자에게 문의하세요.\n";
            break;
        case error::invalid_filename:
            std::cerr << "잘못된 파일명입니다. 영숫자 문자를 사용하세요.\n";
            break;
        default:
            std::cerr << "저장소 오류: " << upload.error().message() << "\n";
    }
}
```

### 예제: 저장소 오류 처리 (서버 측)

```cpp
server->on_upload_request([](const upload_request& req) {
    // 클라이언트별 사용자 정의 할당량 확인
    auto client_usage = get_client_usage(req.client.id);
    if (client_usage + req.file_size > per_client_quota) {
        // 클라이언트에서 -749 오류 발생
        return false;
    }
    return true;
});
```

### 보안 참고: -748 invalid_filename

경로 탐색 또는 잘못된 문자가 감지되면 이 오류가 반환됩니다:

```cpp
// 이러한 파일명은 거부됩니다:
"../../../etc/passwd"        // 경로 탐색
"/absolute/path/file.txt"    // 절대 경로
"file/../../../secret.txt"   // 내장된 경로 탐색
".hidden_file"               // 숨김 파일 (앞에 점)
"file<name>.txt"             // 특수 문자
"CON"                        // Windows 예약 이름
"file\x00name.txt"           // 널 문자

// 이러한 파일명은 허용됩니다:
"document.pdf"
"report-2024-q1.csv"
"backup_2024_12_11.zip"
```

---

## 파일 I/O 오류 (-750 ~ -759)

| 코드 | 이름 | 설명 | 해결 방법 |
|------|------|------|----------|
| **-750** | `file_read_error` | 로컬 파일 읽기 실패 | 파일 권한 및 존재 확인 |
| **-751** | `file_write_error` | 로컬 파일 쓰기 실패 | 디스크 공간 및 권한 확인 |
| **-752** | `file_permission_error` | 파일 권한 부족 | 파일/디렉토리 권한 조정 |
| **-753** | `file_not_found` | 로컬 원본 파일을 찾을 수 없음 | 파일 경로 확인 |
| **-754** | `disk_full` | 로컬 디스크가 가득 참 | 디스크 공간 확보 |
| **-755** | `directory_not_found` | 대상 디렉토리를 찾을 수 없음 | 대상 디렉토리 생성 |
| **-756** | `file_locked` | 다른 프로세스가 파일을 잠금 | 파일을 사용하는 다른 애플리케이션 종료 |

### 예제: I/O 오류 처리

```cpp
auto upload = client->upload_file("/local/data.bin", "data.bin");

if (!upload) {
    switch (upload.error().code()) {
        case error::file_not_found:
            std::cerr << "로컬 파일을 찾을 수 없음: /local/data.bin\n";
            break;
        case error::file_permission_error:
            std::cerr << "파일을 읽을 수 없음: 권한 거부됨\n";
            break;
        case error::file_locked:
            std::cerr << "파일이 다른 프로세스에서 사용 중\n";
            break;
    }
}

auto download = client->download_file("report.pdf", "/local/downloads/report.pdf");

if (!download) {
    switch (download.error().code()) {
        case error::directory_not_found:
            std::filesystem::create_directories("/local/downloads");
            download = client->download_file("report.pdf", "/local/downloads/report.pdf");
            break;
        case error::disk_full:
            std::cerr << "디스크 공간이 부족합니다\n";
            break;
    }
}
```

---

## 재개 오류 (-760 ~ -779)

| 코드 | 이름 | 설명 | 해결 방법 |
|------|------|------|----------|
| **-760** | `resume_state_invalid` | 재개 상태 파일 손상 | 상태 삭제 후 새로 시작 |
| **-761** | `resume_file_changed` | 마지막 체크포인트 이후 원본 파일 수정 | 새 전송 시작 |
| **-762** | `resume_state_corrupted` | 상태 데이터 무결성 검사 실패 | 상태 삭제 후 새로 시작 |
| **-763** | `resume_not_supported` | 서버가 이 전송에 대해 재개를 지원하지 않음 | 전체 전송으로 대체 |
| **-764** | `resume_transfer_not_found` | 재개할 전송 ID를 찾을 수 없음 | 새 전송 시작 |
| **-765** | `resume_session_mismatch` | 다른 세션에서 재개 시도 | 재연결 후 다시 시도 |

### 재개 오류 처리

```cpp
// 재연결 시 자동 재개
client->with_reconnect_policy(reconnect_policy{
    .resume_transfers = true  // 중단된 전송 자동 재개
});

// 수동 재개 처리
client->on_disconnected([&](disconnect_reason reason) {
    if (reason == disconnect_reason::network_error) {
        // 재연결 시 전송이 자동으로 재개됨
        std::cout << "연결 끊김. 재연결 시 전송이 재개됩니다.\n";
    }
});

// 재개 오류 처리
auto result = client->resume_transfer(transfer_id);

if (!result) {
    switch (result.error().code()) {
        case error::resume_file_changed:
            std::cerr << "원본 파일이 변경되었습니다. 새 전송을 시작합니다.\n";
            client->upload_file(path, remote_name);
            break;
        case error::resume_transfer_not_found:
            std::cerr << "전송을 찾을 수 없습니다. 새로 시작합니다.\n";
            client->upload_file(path, remote_name);
            break;
    }
}
```

---

## 압축 오류 (-780 ~ -789)

| 코드 | 이름 | 설명 | 해결 방법 |
|------|------|------|----------|
| **-780** | `compression_failed` | LZ4 압축 실패 | 드문 경우; 입력 데이터 확인 |
| **-781** | `decompression_failed` | LZ4 압축 해제 실패 | 데이터 손상; 재전송 요청 |
| **-782** | `compression_buffer_error` | 출력 버퍼가 너무 작음 | 내부 오류; 버그 보고 |
| **-783** | `invalid_compression_data` | 압축된 데이터가 잘못됨 | 데이터 손상; 재전송 요청 |

### 압축 오류 상세

#### -781: decompression_failed

가장 일반적인 압축 오류입니다. 다음을 나타냅니다:
- 손상된 압축 데이터
- 잘린 청크
- 프로토콜 비동기화

**자동 처리:**
- 시스템이 영향받은 청크에 대해 비압축 전송으로 대체
- 재전송이 자동으로 요청됨

```cpp
// 압축 상태 모니터링
auto stats = client->get_compression_stats();
double failure_rate =
    static_cast<double>(stats.chunks_skipped) /
    static_cast<double>(stats.chunks_compressed + stats.chunks_skipped);

if (failure_rate > 0.01) {  // >1% 실패
    log_warning("높은 압축 실패율: {}%", failure_rate * 100);
    // 일시적으로 압축 비활성화 고려
}
```

---

## 설정 오류 (-790 ~ -799)

| 코드 | 이름 | 설명 | 해결 방법 |
|------|------|------|----------|
| **-790** | `config_invalid` | 잘못된 설정 매개변수 | 설정 값 확인 |
| **-791** | `config_chunk_size_error` | 청크 크기가 유효 범위 외 | 64KB - 1MB 사용 |
| **-792** | `config_transport_error` | 전송 설정 오류 | 전송 설정 확인 |
| **-793** | `config_storage_path_error` | 잘못된 저장소 디렉토리 | 디렉토리가 존재하고 쓰기 가능한지 확인 |
| **-794** | `config_quota_error` | 잘못된 할당량 설정 | 할당량은 > 0이어야 함 |
| **-795** | `config_reconnect_error` | 잘못된 재연결 정책 | 재연결 매개변수 확인 |

### 설정 검증

```cpp
// 서버 설정 오류
auto server = file_transfer_server::builder()
    .with_storage_directory("/nonexistent/path")  // 오류: -793
    .build();

if (!server) {
    std::cerr << "서버 설정 오류: " << server.error().message() << "\n";
}

// 클라이언트 설정 오류
auto client = file_transfer_client::builder()
    .with_chunk_size(32 * 1024)  // 오류: -791 (32KB < 64KB 최소)
    .build();

if (!client) {
    std::cerr << "클라이언트 설정 오류: " << client.error().message() << "\n";
}

// 유효한 설정
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")  // 유효
    .with_max_connections(100)
    .build();

auto client = file_transfer_client::builder()
    .with_chunk_size(256 * 1024)  // 유효: 256KB
    .with_auto_reconnect(true)
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
int code = -744;
std::cout << "오류: " << error::error_message(code) << "\n";
// 출력: "오류: File already exists on server"
```

### 오류 카테고리

```cpp
namespace kcenon::file_transfer::error {

[[nodiscard]] auto is_connection_error(int code) -> bool {
    return code >= -709 && code <= -700;
}

[[nodiscard]] auto is_transfer_error(int code) -> bool {
    return code >= -719 && code <= -710;
}

[[nodiscard]] auto is_chunk_error(int code) -> bool {
    return code >= -739 && code <= -720;
}

[[nodiscard]] auto is_storage_error(int code) -> bool {
    return code >= -749 && code <= -740;
}

[[nodiscard]] auto is_io_error(int code) -> bool {
    return code >= -759 && code <= -750;
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

[[nodiscard]] auto is_retryable(int code) -> bool;  // 작업 재시도 가능 여부

[[nodiscard]] auto is_client_error(int code) -> bool;   // 클라이언트 측 문제
[[nodiscard]] auto is_server_error(int code) -> bool;   // 서버 측 문제

}
```

### 예제: 종합적인 오류 처리

```cpp
void handle_transfer_error(const error& err, file_transfer_client& client) {
    int code = err.code();

    if (error::is_connection_error(code)) {
        // 연결 문제 - 자동 재연결로 도움될 수 있음
        std::cerr << "연결 문제: " << err.message() << "\n";
        // 대부분의 경우 자동 재연결로 처리
        return;
    }

    if (error::is_storage_error(code)) {
        // 서버 저장소 문제
        switch (code) {
            case error::file_already_exists:
                // 사용자에게 덮어쓰기 또는 이름 변경 요청
                break;
            case error::storage_full:
            case error::storage_quota_exceeded:
                // 관리자에게 문의
                break;
            case error::invalid_filename:
                // 파일명 정리 후 재시도
                break;
        }
        return;
    }

    if (error::is_retryable(code)) {
        std::cerr << "재시도 가능한 오류: " << err.message() << "\n";
        // 재시도 로직 구현
        return;
    }

    // 재시도 불가능한 오류
    std::cerr << "치명적 오류: " << err.message() << "\n";
}
```

---

## 모범 사례

### 1. 항상 결과 확인

```cpp
auto result = client->upload_file(path, remote_name);
if (!result) {
    handle_transfer_error(result.error(), *client);
}
```

### 2. 컨텍스트와 함께 오류 로깅

```cpp
if (!result) {
    log_error("전송 실패: code={}, message={}, file={}, server={}",
              result.error().code(),
              result.error().message(),
              path.string(),
              server_address);
}
```

### 3. 연결 오류에 자동 재연결 사용

```cpp
auto client = file_transfer_client::builder()
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy{
        .max_attempts = 10,
        .resume_transfers = true
    })
    .build();

client->on_disconnected([](disconnect_reason reason) {
    if (reason == disconnect_reason::max_reconnects_exceeded) {
        // 수동 개입이 필요한 경우만 여기서 처리
        alert_user("연결 끊김. 네트워크를 확인하세요.");
    }
});
```

### 4. 일시적 오류에 재시도 로직 구현

```cpp
constexpr int max_retries = 3;
int retries = 0;

Result<transfer_handle> result;
do {
    result = client->upload_file(path, remote_name);
    if (!result && error::is_retryable(result.error().code())) {
        auto delay = std::chrono::seconds(1 << retries);  // 지수 백오프
        std::this_thread::sleep_for(delay);
        retries++;
    } else {
        break;
    }
} while (retries < max_retries);
```

### 5. 재개 오류 우아하게 처리

```cpp
// reconnect_policy.resume_transfers = true로 자동 재개
// 수동 제어:
auto resume_result = client->resume_transfer(id);
if (!resume_result && error::is_resume_error(resume_result.error().code())) {
    // 새 전송으로 대체
    return client->upload_file(path, remote_name);
}
```

---

## 오류 코드 빠른 참조 표

| 코드 | 이름 | 카테고리 | 재시도 가능 |
|------|------|----------|------------|
| -700 | connection_failed | 연결 | 예 |
| -701 | connection_timeout | 연결 | 예 |
| -702 | connection_refused | 연결 | 예 |
| -703 | connection_lost | 연결 | 예 (자동) |
| -704 | reconnect_failed | 연결 | 아니오 |
| -710 | transfer_init_failed | 전송 | 예 |
| -711 | transfer_cancelled | 전송 | 아니오 |
| -712 | transfer_timeout | 전송 | 예 |
| -713 | upload_rejected | 전송 | 아니오 |
| -714 | download_rejected | 전송 | 아니오 |
| -720 | chunk_checksum_error | 청크 | 예 (자동) |
| -723 | file_hash_mismatch | 청크 | 예 |
| -744 | file_already_exists | 저장소 | 아니오* |
| -745 | storage_full | 저장소 | 아니오 |
| -746 | file_not_found_on_server | 저장소 | 아니오 |
| -747 | access_denied | 저장소 | 아니오 |
| -748 | invalid_filename | 저장소 | 아니오 |
| -750 | file_read_error | 파일 I/O | 아니오 |
| -753 | file_not_found | 파일 I/O | 아니오 |
| -760 | resume_state_invalid | 재개 | 아니오** |
| -780 | compression_failed | 압축 | 예 (대체) |
| -791 | config_chunk_size_error | 설정 | 아니오 |

*`overwrite_existing = true`로 재시도 가능
**새 전송으로 대체 가능

---

*최종 업데이트: 2025-12-11*
*버전: 0.2.0*
