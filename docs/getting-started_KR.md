# 시작 가이드

C++20 프로젝트에서 **file_trans_system**을 사용하기 위한 단계별 가이드입니다.

**버전:** 2.0.0
**아키텍처:** 클라이언트-서버 모델

---

## 목차

1. [설치](#설치)
2. [빠른 시작](#빠른-시작)
3. [서버 예제](#서버-예제)
4. [클라이언트 예제](#클라이언트-예제)
5. [업로드 및 다운로드](#업로드-및-다운로드)
6. [오류 처리](#오류-처리)
7. [진행 상황 모니터링](#진행-상황-모니터링)
8. [고급 사용법](#고급-사용법)
9. [문제 해결](#문제-해결)

---

## 설치

### 요구 사항

- **컴파일러**: C++20 호환
  - GCC 11+
  - Clang 14+
  - MSVC 19.29+
- **빌드 시스템**: CMake 3.20+
- **외부 의존성**: LZ4 (v1.9.0+)

### 생태계 의존성

file_trans_system은 **kcenon 생태계** 라이브러리를 기반으로 구축됩니다:

| 라이브러리 | 용도 |
|-----------|------|
| common_system | Result<T>, 오류 처리, 시간 유틸리티 |
| thread_system | 파이프라인 병렬성을 위한 `typed_thread_pool` |
| **network_system** | **TCP/TLS 1.3 및 QUIC 전송 계층** |
| container_system | 백프레셔를 위한 제한된 큐 |

> **중요**: 전송 계층은 프로덕션 수준의 TCP 및 QUIC 구현을 제공하는 **network_system**을 사용하여 구현됩니다. Boost.Asio나 libuv 같은 외부 네트워크 라이브러리가 필요하지 않습니다.

### CMake 통합

#### 옵션 1: FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
    file_trans_system
    GIT_REPOSITORY https://github.com/kcenon/file_trans_system.git
    GIT_TAG v2.0.0
)

FetchContent_MakeAvailable(file_trans_system)

target_link_libraries(your_target PRIVATE kcenon::file_transfer)
```

#### 옵션 2: find_package

```cmake
find_package(file_trans_system REQUIRED)
target_link_libraries(your_target PRIVATE kcenon::file_transfer)
```

#### 옵션 3: 하위 디렉토리

```cmake
add_subdirectory(external/file_trans_system)
target_link_libraries(your_target PRIVATE kcenon::file_transfer)
```

### 헤더 포함

```cpp
#include <kcenon/file_transfer/file_transfer.h>

using namespace kcenon::file_transfer;
```

---

## 빠른 시작

### 5분 예제

**서버 (file_server.cpp):**

```cpp
#include <kcenon/file_transfer/file_transfer.h>
#include <iostream>
#include <csignal>

using namespace kcenon::file_transfer;

std::atomic<bool> running{true};

void signal_handler(int) { running = false; }

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "사용법: file_server <포트> <저장소_디렉토리>\n";
        return 1;
    }

    std::signal(SIGINT, signal_handler);

    // 서버 생성
    auto server = file_transfer_server::builder()
        .with_storage_directory(argv[2])
        .with_max_connections(100)
        .build();

    if (!server) {
        std::cerr << "서버 생성 실패: " << server.error().message() << "\n";
        return 1;
    }

    // 모든 업로드와 다운로드 허용
    server->on_upload_request([](const upload_request& req) {
        std::cout << "업로드 요청: " << req.filename
                  << " (" << req.file_size << " 바이트)\n";
        return true;  // 수락
    });

    server->on_download_request([](const download_request& req) {
        std::cout << "다운로드 요청: " << req.filename << "\n";
        return true;  // 수락
    });

    // 완료 처리
    server->on_upload_complete([](const transfer_result& r) {
        std::cout << "업로드 완료: " << r.filename << "\n";
    });

    server->on_download_complete([](const transfer_result& r) {
        std::cout << "다운로드 완료: " << r.filename << "\n";
    });

    // 수신 대기 시작
    auto port = static_cast<uint16_t>(std::stoi(argv[1]));
    auto result = server->start(endpoint{"0.0.0.0", port});

    if (!result) {
        std::cerr << "서버 시작 실패: " << result.error().message() << "\n";
        return 1;
    }

    std::cout << "포트 " << port << "에서 서버 수신 대기 중...\n";
    std::cout << "저장소 디렉토리: " << argv[2] << "\n";

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\n종료 중...\n";
    server->stop();
    return 0;
}
```

**클라이언트 (file_client.cpp):**

```cpp
#include <kcenon/file_transfer/file_transfer.h>
#include <iostream>

using namespace kcenon::file_transfer;

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "사용법: file_client <호스트> <포트> <명령> [인수...]\n";
        std::cerr << "명령:\n";
        std::cerr << "  upload <로컬_파일> <원격_이름>\n";
        std::cerr << "  download <원격_이름> <로컬_파일>\n";
        std::cerr << "  list [패턴]\n";
        return 1;
    }

    // 자동 재연결로 클라이언트 생성
    auto client = file_transfer_client::builder()
        .with_auto_reconnect(true)
        .with_compression(compression_mode::adaptive)
        .build();

    if (!client) {
        std::cerr << "클라이언트 생성 실패: " << client.error().message() << "\n";
        return 1;
    }

    // 진행 상황 콜백
    client->on_progress([](const transfer_progress& p) {
        double pct = 100.0 * p.bytes_transferred / p.total_bytes;
        std::cout << "\r진행률: " << std::fixed << std::setprecision(1)
                  << pct << "% (" << p.transfer_rate / 1e6 << " MB/s)" << std::flush;
    });

    // 서버에 연결
    auto port = static_cast<uint16_t>(std::stoi(argv[2]));
    auto connect_result = client->connect(endpoint{argv[1], port});

    if (!connect_result) {
        std::cerr << "연결 실패: " << connect_result.error().message() << "\n";
        return 1;
    }

    std::cout << argv[1] << ":" << port << "에 연결됨\n";

    std::string command = argv[3];

    if (command == "upload" && argc >= 6) {
        auto result = client->upload_file(argv[4], argv[5]);
        if (result) {
            std::cout << "\n업로드 완료!\n";
        } else {
            std::cerr << "\n업로드 실패: " << result.error().message() << "\n";
            return 1;
        }
    }
    else if (command == "download" && argc >= 6) {
        auto result = client->download_file(argv[4], argv[5]);
        if (result) {
            std::cout << "\n다운로드 완료: " << argv[5] << "\n";
        } else {
            std::cerr << "\n다운로드 실패: " << result.error().message() << "\n";
            return 1;
        }
    }
    else if (command == "list") {
        std::string pattern = argc >= 5 ? argv[4] : "*";
        auto result = client->list_files(pattern);
        if (result) {
            std::cout << "서버의 파일 목록:\n";
            for (const auto& file : *result) {
                std::cout << "  " << file.name << " (" << file.size << " 바이트)\n";
            }
        } else {
            std::cerr << "목록 조회 실패: " << result.error().message() << "\n";
            return 1;
        }
    }
    else {
        std::cerr << "알 수 없는 명령: " << command << "\n";
        return 1;
    }

    client->disconnect();
    return 0;
}
```

**사용:**

```bash
# 터미널 1 - 서버 시작
./file_server 19000 /data/files

# 터미널 2 - 파일 업로드
./file_client localhost 19000 upload /local/report.pdf report.pdf

# 터미널 3 - 파일 목록 조회
./file_client localhost 19000 list

# 터미널 4 - 파일 다운로드
./file_client localhost 19000 download report.pdf /local/downloaded.pdf
```

---

## 서버 예제

### 예제 1: 기본 서버

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .build();

server->start(endpoint{"0.0.0.0", 19000});
```

### 예제 2: 제한이 있는 프로덕션 서버

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_max_connections(500)
    .with_max_file_size(10ULL * 1024 * 1024 * 1024)  // 10GB
    .with_storage_quota(1ULL * 1024 * 1024 * 1024 * 1024)  // 1TB
    .with_connection_timeout(5min)
    .build();

// 사용자 정의 업로드 검증
server->on_upload_request([](const upload_request& req) {
    // 특정 확장자만 허용
    auto ext = std::filesystem::path(req.filename).extension();
    if (ext != ".pdf" && ext != ".doc" && ext != ".txt") {
        return false;  // 거부
    }

    // 파일 크기 확인
    if (req.file_size > 1e9) {
        return false;  // 1GB 초과 거부
    }

    return true;
});

// 연결 이벤트
server->on_client_connected([](const client_info& info) {
    std::cout << "클라이언트 연결됨: " << info.address << "\n";
});

server->on_client_disconnected([](const client_info& info, disconnect_reason reason) {
    std::cout << "클라이언트 연결 해제: " << info.address
              << " (" << to_string(reason) << ")\n";
});

server->start(endpoint{"0.0.0.0", 19000});
```

### 예제 3: 저장소 모니터링이 있는 서버

```cpp
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_storage_quota(100ULL * 1024 * 1024 * 1024)  // 100GB
    .build();

// 저장소 사용량 모니터링
server->on_storage_warning([](const storage_warning& warning) {
    if (warning.percent_used > 80) {
        std::cout << "경고: 저장소 " << warning.percent_used << "% 사용\n";
    }
});

server->on_storage_full([]() {
    std::cerr << "오류: 저장소 할당량 초과!\n";
});

// 주기적으로 저장소 상태 확인
std::thread monitor([&server]() {
    while (server->is_running()) {
        auto status = server->get_storage_status();
        std::cout << "저장소: " << (status.used_bytes / 1e9) << "GB / "
                  << (status.quota_bytes / 1e9) << "GB\n";
        std::this_thread::sleep_for(std::chrono::minutes(5));
    }
});
```

---

## 클라이언트 예제

### 예제 1: 간단한 업로드

```cpp
auto client = file_transfer_client::builder().build();

client->connect(endpoint{"192.168.1.100", 19000});

auto result = client->upload_file("/local/data.zip", "data.zip");
if (result) {
    std::cout << "업로드 성공!\n";
}

client->disconnect();
```

### 예제 2: 압축 옵션을 사용한 업로드

```cpp
auto client = file_transfer_client::builder()
    .with_compression(compression_mode::enabled)
    .with_compression_level(compression_level::high_compression)
    .build();

client->connect(endpoint{"192.168.1.100", 19000});

// 특정 업로드에 대해 압축 재정의
upload_options opts{
    .compression = compression_mode::disabled  // 이 파일은 압축하지 않음
};

auto result = client->upload_file("/local/video.mp4", "video.mp4", opts);
```

### 예제 3: 자동 재연결이 있는 클라이언트

```cpp
auto client = file_transfer_client::builder()
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy{
        .initial_delay = std::chrono::milliseconds(500),
        .max_delay = std::chrono::seconds(30),
        .multiplier = 1.5,
        .max_attempts = 20
    })
    .build();

// 연결 이벤트 핸들러
client->on_connected([](const server_info& info) {
    std::cout << "연결됨: " << info.address << "\n";
});

client->on_disconnected([](disconnect_reason reason) {
    std::cout << "연결 해제: " << to_string(reason) << "\n";
});

client->on_reconnecting([](int attempt, auto delay) {
    std::cout << "재연결 중 (시도 " << attempt << ")...\n";
});

client->on_reconnected([]() {
    std::cout << "재연결 성공!\n";
});

client->connect(endpoint{"192.168.1.100", 19000});
```

### 예제 4: 대역폭 제한 클라이언트

```cpp
auto client = file_transfer_client::builder()
    .with_bandwidth_limit(10 * 1024 * 1024)  // 10 MB/s
    .with_compression(compression_mode::enabled)  // 효과적 처리량 극대화
    .build();

client->connect(endpoint{"192.168.1.100", 19000});
client->upload_file("/local/large_file.dat", "large_file.dat");
```

---

## 업로드 및 다운로드

### 파일 업로드

```cpp
// 간단한 업로드
auto result = client->upload_file("/local/file.txt", "file.txt");

// 옵션을 사용한 업로드
upload_options opts{
    .compression = compression_mode::enabled,
    .level = compression_level::fast,
    .verify_checksum = true
};
auto result = client->upload_file("/local/file.txt", "file.txt", opts);
```

### 다중 파일 업로드

```cpp
std::vector<upload_entry> files = {
    {"/local/file1.txt", "file1.txt"},
    {"/local/file2.txt", "file2.txt"},
    {"/local/file3.txt", "dir/file3.txt"}  // 서버 하위 디렉토리
};

auto result = client->upload_files(files);
if (result) {
    std::cout << result->file_count << "개 파일 업로드됨\n";
}
```

### 파일 다운로드

```cpp
// 간단한 다운로드
auto result = client->download_file("report.pdf", "/local/report.pdf");

// 옵션을 사용한 다운로드
download_options opts{
    .verify_checksum = true,
    .overwrite = false  // 존재하면 덮어쓰지 않음
};
auto result = client->download_file("report.pdf", "/local/report.pdf", opts);

if (!result && result.error().code() == error::file_already_exists) {
    std::cout << "파일이 이미 로컬에 존재합니다\n";
}
```

### 다중 파일 다운로드

```cpp
std::vector<download_entry> files = {
    {"file1.txt", "/local/file1.txt"},
    {"file2.txt", "/local/file2.txt"}
};

auto result = client->download_files(files);
```

### 파일 목록 조회

```cpp
// 모든 파일 목록
auto result = client->list_files();

// 패턴으로 필터
auto result = client->list_files("*.pdf");

// 페이지네이션으로 목록
auto result = client->list_files("*", 0, 100);  // 첫 100개 파일

if (result) {
    for (const auto& file : *result) {
        std::cout << file.name << "\t"
                  << file.size << " 바이트\t"
                  << file.modified_time << "\n";
    }
}
```

---

## 오류 처리

### Result 패턴

모든 연산은 `Result<T>`를 반환합니다:

```cpp
auto result = client->upload_file(local, remote);

if (result) {
    // 성공 - 값에 접근
    std::cout << "전송 ID: " << result->id.to_string() << "\n";
} else {
    // 실패 - 오류에 접근
    auto code = result.error().code();
    auto message = result.error().message();
    std::cerr << "오류 " << code << ": " << message << "\n";
}
```

### 연결 오류

```cpp
auto result = client->connect(endpoint);
if (!result) {
    switch (result.error().code()) {
        case error::connection_failed:
            std::cerr << "서버에 연결할 수 없습니다\n";
            break;
        case error::connection_timeout:
            std::cerr << "연결 시간 초과\n";
            break;
        case error::server_unavailable:
            std::cerr << "서버를 사용할 수 없습니다\n";
            break;
    }
}
```

### 전송 오류

```cpp
auto result = client->upload_file(local, remote);
if (!result) {
    switch (result.error().code()) {
        case error::upload_rejected:
            std::cerr << "서버가 업로드를 거부했습니다\n";
            break;
        case error::storage_full:
            std::cerr << "서버 저장소가 가득 찼습니다\n";
            break;
        case error::file_already_exists:
            std::cerr << "파일이 서버에 이미 존재합니다\n";
            break;
        case error::file_not_found:
            std::cerr << "로컬 파일을 찾을 수 없습니다\n";
            break;
    }
}

auto result = client->download_file(remote, local);
if (!result) {
    switch (result.error().code()) {
        case error::download_rejected:
            std::cerr << "서버가 다운로드를 거부했습니다\n";
            break;
        case error::file_not_found_on_server:
            std::cerr << "서버에서 파일을 찾을 수 없습니다\n";
            break;
    }
}
```

### 일반적인 오류 코드

| 코드 | 이름 | 설명 | 조치 |
|------|------|------|------|
| -700 | `connection_failed` | 연결할 수 없음 | 서버 주소 확인 |
| -703 | `connection_lost` | 연결 끊김 | 자동 재연결 또는 재시도 |
| -704 | `reconnect_failed` | 모든 재연결 시도 실패 | 수동 개입 |
| -713 | `upload_rejected` | 서버가 업로드 거부 | 서버 규칙 확인 |
| -714 | `download_rejected` | 서버가 다운로드 거부 | 권한 확인 |
| -720 | `chunk_checksum_error` | 데이터 손상 | 자동 재시도 |
| -744 | `file_already_exists` | 서버에 파일 존재 | 덮어쓰기 옵션 사용 |
| -745 | `storage_full` | 서버 저장소 부족 | 관리자에게 문의 |
| -746 | `file_not_found_on_server` | 원격 파일 없음 | 파일명 확인 |
| -750 | `file_not_found` | 로컬 파일 없음 | 경로 확인 |

---

## 진행 상황 모니터링

### 진행 상황 콜백

```cpp
client->on_progress([](const transfer_progress& p) {
    // 백분율 계산
    double percent = 100.0 * p.bytes_transferred / p.total_bytes;

    // MB/s 단위 전송 속도
    double rate_mbps = p.transfer_rate / (1024 * 1024);

    // 예상 남은 시간
    auto remaining = p.estimated_remaining;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(remaining);

    // 압축 비율
    double ratio = p.compression_ratio;

    std::cout << std::fixed << std::setprecision(1)
              << percent << "% 완료, "
              << rate_mbps << " MB/s, "
              << seconds.count() << "초 남음, "
              << ratio << ":1 압축\n";
});
```

### 진행 상황 정보

| 필드 | 타입 | 설명 |
|------|------|------|
| `id` | `transfer_id` | 고유 전송 식별자 |
| `direction` | `transfer_direction` | upload 또는 download |
| `bytes_transferred` | `uint64_t` | 전송된 원시 바이트 |
| `bytes_on_wire` | `uint64_t` | 실제 전송 바이트 (압축됨) |
| `total_bytes` | `uint64_t` | 총 파일 크기 |
| `transfer_rate` | `double` | 현재 속도 (바이트/초) |
| `compression_ratio` | `double` | 압축 비율 |
| `elapsed_time` | `duration` | 시작 이후 경과 시간 |
| `estimated_remaining` | `duration` | 예상 완료 시간 |

### 클라이언트 통계

```cpp
auto stats = client->get_statistics();
std::cout << "총 업로드: " << stats.total_uploaded_bytes << "\n";
std::cout << "총 다운로드: " << stats.total_downloaded_bytes << "\n";
std::cout << "현재 업로드 속도: " << stats.current_upload_rate_mbps << " MB/s\n";
std::cout << "현재 다운로드 속도: " << stats.current_download_rate_mbps << " MB/s\n";
```

### 서버 통계

```cpp
auto stats = server->get_statistics();
std::cout << "활성 연결: " << stats.active_connections << "\n";
std::cout << "총 업로드: " << stats.total_uploaded_bytes << "\n";
std::cout << "총 다운로드: " << stats.total_downloaded_bytes << "\n";
std::cout << "업로드 처리량: " << stats.upload_throughput_mbps << " MB/s\n";
std::cout << "다운로드 처리량: " << stats.download_throughput_mbps << " MB/s\n";
```

---

## 고급 사용법

### 전송 제어

```cpp
// 업로드 시작
auto handle = client->upload_file(local, remote);
auto transfer_id = handle->id;

// 전송 일시 중지
client->pause(transfer_id);

// 전송 재개
client->resume(transfer_id);

// 전송 취소
client->cancel(transfer_id);
```

### 연결 끊김 후 전송 재개

```cpp
// 재연결 후 전송이 자동으로 재개됨
client->on_reconnected([&client]() {
    // 활성 전송이 중단된 위치에서 계속됨
    auto active = client->get_active_transfers();
    for (const auto& transfer : active) {
        std::cout << "재개 중: " << transfer.filename
                  << " " << transfer.bytes_transferred << " 바이트부터\n";
    }
});
```

### 파이프라인 통계

```cpp
auto pipeline_stats = client->get_pipeline_stats();

// 병목 찾기
auto bottleneck = pipeline_stats.bottleneck_stage();
std::cout << "병목: " << stage_name(bottleneck) << "\n";

// 스테이지 처리량
std::cout << "IO 읽기: " << pipeline_stats.io_read_stats.throughput_mbps() << " MB/s\n";
std::cout << "압축: " << pipeline_stats.compression_stats.throughput_mbps() << " MB/s\n";
std::cout << "네트워크: " << pipeline_stats.network_stats.throughput_mbps() << " MB/s\n";
```

---

## 문제 해결

### 일반적인 문제

#### "Connection refused" 오류

**원인**: 서버가 실행 중이지 않거나 주소/포트가 잘못됨.

**해결:**
```bash
# 서버가 수신 대기 중인지 확인
netstat -an | grep 19000

# 클라이언트보다 서버를 먼저 시작
./file_server 19000 /data/files
./file_client localhost 19000 list
```

#### "Upload rejected" 오류

**원인**: 서버가 업로드 요청을 거부함.

**해결**: 서버 검증 콜백 확인:
```cpp
// 서버 측 - 무엇이 거부되는지 확인
server->on_upload_request([](const upload_request& req) {
    std::cout << "업로드 요청: " << req.filename
              << " (" << req.file_size << " 바이트)\n";

    // 디버그를 위해 일시적으로 모두 수락
    return true;
});
```

#### 느린 전송 속도

**원인**: 파이프라인 또는 네트워크의 병목.

**해결:**
```cpp
// 병목 위치 확인
auto stats = client->get_pipeline_stats();
std::cout << "병목: " << stage_name(stats.bottleneck_stage()) << "\n";

// 압축 병목인 경우, 워커 추가
pipeline_config config = pipeline_config::auto_detect();
config.compression_workers = 8;

// 네트워크 병목인 경우, 대역폭/지연 시간 확인
// IO 병목인 경우, 더 빠른 스토리지 고려
```

#### 잦은 연결 끊김

**원인**: 네트워크 불안정 또는 서버 타임아웃.

**해결:**
```cpp
// 적극적인 재연결 활성화
auto client = file_transfer_client::builder()
    .with_auto_reconnect(true)
    .with_reconnect_policy(reconnect_policy::aggressive())
    .build();

// 또는 서버 타임아웃 조정
auto server = file_transfer_server::builder()
    .with_storage_directory("/data/files")
    .with_connection_timeout(10min)  // 더 긴 타임아웃
    .build();
```

#### 높은 메모리 사용량

**원인**: 큐 크기가 너무 큼.

**해결:**
```cpp
pipeline_config config{
    .read_queue_size = 4,
    .compress_queue_size = 8,
    .send_queue_size = 16,
    .recv_queue_size = 16
};
// 메모리: ~14MB vs 기본값 ~64MB
```

### 디버그 로깅

상세 로깅 활성화:

```cpp
#include <kcenon/logger/logger.h>

// 로그 수준을 debug로 설정
logger::set_level(log_level::debug);

// 이제 file_trans_system이 상세 로그 출력
```

### 성능 튜닝 가이드

| 시나리오 | 권장 설정 |
|---------|----------|
| **텍스트 파일** | `compression_mode::enabled`, `compression_level::fast` |
| **비디오/이미지** | `compression_mode::disabled` |
| **혼합 워크로드** | `compression_mode::adaptive` |
| **불안정한 네트워크** | 작은 청크 (64KB), 적극적 재연결 |
| **고대역폭** | 큰 청크 (512KB), 더 많은 워커 |
| **저메모리** | 작은 큐 크기 |
| **다수 클라이언트** | 서버 스레드 풀 튜닝 |

---

## 다음 단계

- 완전한 API 문서는 [API 레퍼런스](reference/api-reference_KR.md) 참조
- 고급 튜닝은 [파이프라인 아키텍처](reference/pipeline-architecture_KR.md) 탐색
- 포괄적인 오류 처리는 [오류 코드](reference/error-codes_KR.md) 확인
- 와이어 프로토콜 세부사항은 [프로토콜 명세](reference/protocol-spec_KR.md) 검토
- 모든 설정 옵션은 [설정 가이드](reference/configuration_KR.md) 참조

---

*버전: 2.0.0*
*최종 업데이트: 2025-12-11*
