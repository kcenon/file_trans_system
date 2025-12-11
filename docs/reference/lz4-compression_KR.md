# LZ4 압축 가이드

**file_trans_system** 라이브러리의 LZ4 압축에 대한 완전한 가이드입니다.

## 개요

file_trans_system은 **LZ4**를 사용하여 실시간으로 청크별 압축을 수행하여 네트워크를 통한 실효 처리량을 높입니다.

### 왜 LZ4인가?

| 알고리즘 | 압축 속도 | 해제 속도 | 비율 | 라이선스 |
|---------|----------|----------|------|---------|
| **LZ4** | ~500 MB/s | ~2 GB/s | 2.1:1 | BSD |
| LZ4-HC | ~50 MB/s | ~2 GB/s | 2.7:1 | BSD |
| zstd | ~400 MB/s | ~1 GB/s | 2.9:1 | BSD |
| gzip | ~30 MB/s | ~300 MB/s | 2.7:1 | - |
| snappy | ~400 MB/s | ~800 MB/s | 1.8:1 | BSD |

LZ4는 다음의 최적의 균형을 제공합니다:
- **속도**: 메모리 대역폭에 근접한 압축/해제 속도
- **단순성**: 단일 헤더 라이브러리, 최소한의 의존성
- **라이선스**: BSD 라이선스 (상용 친화적)
- **성숙도**: 검증된 기술 (Linux 커널, ZFS 등)

---

## 압축 모드

### 모드 열거형

```cpp
enum class compression_mode {
    disabled,   // 압축 없음
    enabled,    // 항상 압축
    adaptive    // 압축 가능성 자동 감지 (기본값)
};
```

### 모드 비교

| 모드 | 설명 | CPU 오버헤드 | 적합한 용도 |
|------|------|-------------|-----------|
| `disabled` | 압축 없음 | 없음 | 이미 압축된 파일 (ZIP, 미디어) |
| `enabled` | 항상 압축 | 중간 | 텍스트, 로그, 소스 코드 |
| `adaptive` | 자동 감지 | 낮음-중간 | 혼합 콘텐츠 |

### 사용 예제

```cpp
// 미디어 파일에 대해 압축 비활성화
auto sender = file_sender::builder()
    .with_compression(compression_mode::disabled)
    .build();

// 로그 파일에 대해 항상 압축
auto sender = file_sender::builder()
    .with_compression(compression_mode::enabled)
    .build();

// 시스템이 결정하도록 (기본값)
auto sender = file_sender::builder()
    .with_compression(compression_mode::adaptive)
    .build();
```

---

## 압축 수준

### 수준 열거형

```cpp
enum class compression_level {
    fast,             // LZ4 표준
    high_compression  // LZ4-HC
};
```

### 수준 비교

| 수준 | 알고리즘 | 압축 속도 | 비율 | 사용 사례 |
|------|---------|----------|------|----------|
| `fast` | LZ4 | ~400 MB/s | ~2.1:1 | 실시간 전송 |
| `high_compression` | LZ4-HC | ~50 MB/s | ~2.7:1 | 보관용, 대역폭 제한 환경 |

### 성능 트레이드오프

```
처리량 = min(네트워크_속도 × 압축_비율, 압축_속도)
```

**예제: 100 Mbps 네트워크**

| 수준 | 압축 속도 | 비율 | 실효 처리량 |
|------|----------|------|-----------|
| `fast` | 400 MB/s | 2.1:1 | 26.25 MB/s (네트워크 제한) |
| `high_compression` | 50 MB/s | 2.7:1 | 33.75 MB/s (네트워크 제한) |

**예제: 10 Gbps 네트워크**

| 수준 | 압축 속도 | 비율 | 실효 처리량 |
|------|----------|------|-----------|
| `fast` | 400 MB/s | 2.1:1 | 400 MB/s (CPU 제한) |
| `high_compression` | 50 MB/s | 2.7:1 | 50 MB/s (CPU 제한) |

**권장사항:**
- 고대역폭 네트워크 (>1 Gbps)에서는 `fast` 사용
- 대역폭 제한 네트워크 (<100 Mbps)에서는 `high_compression` 사용

---

## 적응형 압축

### 작동 방식

적응형 압축은 각 청크의 처음 1KB를 샘플링하여 압축 가능성을 판단합니다:

```cpp
bool is_compressible(std::span<const std::byte> data, double threshold = 0.9) {
    // 처음 1KB 샘플링
    const auto sample_size = std::min(data.size(), std::size_t{1024});
    auto sample = data.first(sample_size);

    // 샘플 압축 시도
    auto compressed = lz4_compress(sample);

    // >= 10% 감소 시에만 압축
    return compressed.size() < sample.size() * threshold;
}
```

### 감지 시간

- **목표**: 청크당 < 100 마이크로초
- **실제**: 1KB 샘플에 대해 일반적으로 20-50 마이크로초

### 파일 타입 휴리스틱

샘플링 외에도 파일 확장자가 힌트를 제공합니다:

| 카테고리 | 확장자 | 동작 |
|---------|-------|------|
| **압축 가능** | `.txt`, `.log`, `.json`, `.xml`, `.csv` | 압축할 가능성 높음 |
| **압축 가능** | `.cpp`, `.h`, `.py`, `.java`, `.js` | 압축할 가능성 높음 |
| **이미 압축됨** | `.zip`, `.gz`, `.tar.gz`, `.bz2`, `.xz` | 건너뜀 |
| **미디어** | `.jpg`, `.png`, `.gif`, `.mp4`, `.mp3` | 건너뜀 |
| **바이너리** | `.exe`, `.dll`, `.so`, `.bin` | 테스트 (적응형) |

### 적응형 동작

```cpp
// 적응형 모드 결정 트리
if (mode == compression_mode::adaptive) {
    if (is_known_compressed_extension(file)) {
        skip_compression();
    } else if (is_known_compressible_extension(file)) {
        compress();
    } else {
        // 첫 청크 테스트
        if (is_compressible(first_chunk)) {
            compress_all_chunks();
        } else {
            skip_compression();
        }
    }
}
```

---

## API 레퍼런스

### lz4_engine

저수준 압축 API:

```cpp
class lz4_engine {
public:
    // 표준 LZ4 압축 (~400 MB/s)
    [[nodiscard]] static auto compress(
        std::span<const std::byte> input,
        std::span<std::byte> output
    ) -> Result<std::size_t>;

    // LZ4-HC 압축 (~50 MB/s, 더 나은 비율)
    [[nodiscard]] static auto compress_hc(
        std::span<const std::byte> input,
        std::span<std::byte> output,
        int level = 9  // 1-12
    ) -> Result<std::size_t>;

    // 압축 해제 (~1.5 GB/s)
    [[nodiscard]] static auto decompress(
        std::span<const std::byte> compressed,
        std::span<std::byte> output,
        std::size_t original_size
    ) -> Result<std::size_t>;

    // 최대 압축 크기 계산
    [[nodiscard]] static auto max_compressed_size(std::size_t input_size)
        -> std::size_t;
};
```

### 사용 예제

```cpp
// 압축
std::vector<std::byte> input = read_file("data.txt");
std::vector<std::byte> output(lz4_engine::max_compressed_size(input.size()));

auto result = lz4_engine::compress(input, output);
if (result) {
    output.resize(result.value());  // 실제 크기로 축소
}

// 압축 해제
std::vector<std::byte> decompressed(original_size);
auto result = lz4_engine::decompress(output, decompressed, original_size);
```

### chunk_compressor

통계가 있는 고수준 청크 압축:

```cpp
class chunk_compressor {
public:
    explicit chunk_compressor(
        compression_mode mode = compression_mode::adaptive,
        compression_level level = compression_level::fast
    );

    [[nodiscard]] auto compress(const chunk& input) -> Result<chunk>;
    [[nodiscard]] auto decompress(const chunk& input) -> Result<chunk>;
    [[nodiscard]] auto get_statistics() const -> compression_statistics;
    void reset_statistics();
};
```

### adaptive_compression

압축 가능성 감지 유틸리티:

```cpp
class adaptive_compression {
public:
    // 샘플 기반 감지 (<100us)
    [[nodiscard]] static auto is_compressible(
        std::span<const std::byte> data,
        double threshold = 0.9
    ) -> bool;

    // 확장자 기반 휴리스틱
    [[nodiscard]] static auto is_likely_compressible(
        const std::filesystem::path& file
    ) -> bool;
};
```

---

## 압축 통계

### 통계 구조체

```cpp
struct compression_statistics {
    std::atomic<uint64_t> total_raw_bytes{0};
    std::atomic<uint64_t> total_compressed_bytes{0};
    std::atomic<uint64_t> chunks_compressed{0};
    std::atomic<uint64_t> chunks_skipped{0};
    std::atomic<uint64_t> compression_time_us{0};
    std::atomic<uint64_t> decompression_time_us{0};

    [[nodiscard]] auto compression_ratio() const -> double {
        return total_compressed_bytes > 0
            ? static_cast<double>(total_raw_bytes) / total_compressed_bytes
            : 1.0;
    }

    [[nodiscard]] auto compression_speed_mbps() const -> double {
        return compression_time_us > 0
            ? (total_raw_bytes / 1e6) / (compression_time_us / 1e6)
            : 0.0;
    }

    [[nodiscard]] auto decompression_speed_mbps() const -> double {
        return decompression_time_us > 0
            ? (total_raw_bytes / 1e6) / (decompression_time_us / 1e6)
            : 0.0;
    }
};
```

### 모니터링 예제

```cpp
auto stats = sender->get_compression_stats();

std::cout << "압축 통계:\n";
std::cout << "  원시 바이트:      " << stats.total_raw_bytes << "\n";
std::cout << "  압축 바이트:      " << stats.total_compressed_bytes << "\n";
std::cout << "  압축 비율:        " << stats.compression_ratio() << ":1\n";
std::cout << "  압축 속도:        " << stats.compression_speed_mbps() << " MB/s\n";
std::cout << "  해제 속도:        " << stats.decompression_speed_mbps() << " MB/s\n";
std::cout << "  압축된 청크:      " << stats.chunks_compressed << "\n";
std::cout << "  건너뛴 청크:      " << stats.chunks_skipped << "\n";
```

---

## 와이어 형식

### 청크 헤더 압축 필드

```cpp
struct chunk_header {
    // ... 기타 필드 ...
    uint32_t    original_size;      // 원본 (비압축) 크기
    uint32_t    compressed_size;    // 압축 후 크기
    chunk_flags flags;              // compressed 플래그 포함
};

enum class chunk_flags : uint8_t {
    compressed = 0x04    // 데이터가 LZ4 압축됨
};
```

### 압축 플래그 로직

```cpp
// 송신자 측
if (compressed && compressed.size() < original.size()) {
    header.original_size = original.size();
    header.compressed_size = compressed.size();
    header.flags |= chunk_flags::compressed;
    data = compressed;
} else {
    header.original_size = original.size();
    header.compressed_size = original.size();  // 동일
    // compressed 플래그 설정 안 함
    data = original;
}

// 수신자 측
if (has_flag(header.flags, chunk_flags::compressed)) {
    auto decompressed = lz4_decompress(data, header.original_size);
    // 압축 해제된 데이터 사용
} else {
    // 데이터 직접 사용
}
```

---

## 성능 벤치마크

### 압축 처리량

| 데이터 타입 | LZ4 Fast | LZ4-HC | 비율 (Fast) | 비율 (HC) |
|-----------|----------|--------|------------|----------|
| 텍스트/로그 | 450 MB/s | 55 MB/s | 3.2:1 | 4.1:1 |
| JSON | 420 MB/s | 50 MB/s | 2.8:1 | 3.5:1 |
| 소스 코드 | 430 MB/s | 52 MB/s | 3.0:1 | 3.8:1 |
| 바이너리 (랜덤) | 380 MB/s | 45 MB/s | 1.0:1 | 1.0:1 |
| 바이너리 (구조화) | 400 MB/s | 48 MB/s | 1.5:1 | 1.8:1 |

### 압축 해제 처리량

| 데이터 타입 | 해제 속도 |
|-----------|----------|
| 모든 타입 | 1.5 - 2.0 GB/s |

### 메모리 사용량

```
압축 버퍼 = max_compressed_size(chunk_size)
         ≈ chunk_size + (chunk_size / 255) + 16

256KB 청크의 경우: ~257KB 버퍼 필요
```

---

## 모범 사례

### 1. 기본적으로 적응형 모드 사용

```cpp
// 좋음 - 시스템이 결정하도록
.with_compression(compression_mode::adaptive)

// 파일 타입을 알지 않으면 피함
.with_compression(compression_mode::enabled)  // 미디어 파일에 CPU 낭비 가능
```

### 2. 네트워크에 맞는 압축 수준 선택

```cpp
// 고대역폭 네트워크 (>1 Gbps)
.with_compression_level(compression_level::fast)

// 저대역폭 네트워크 (<100 Mbps)
.with_compression_level(compression_level::high_compression)
```

### 3. 압축 비율 모니터링

```cpp
auto stats = sender->get_compression_stats();
if (stats.compression_ratio() < 1.1) {
    // 데이터가 대부분 압축 불가능
    // 압축 비활성화 고려
    log_warning("낮은 압축 비율: {}", stats.compression_ratio());
}
```

### 4. 파일을 타입별로 미리 분류

```cpp
// 압축 가능성별 배치 분리
std::vector<std::filesystem::path> compressible;
std::vector<std::filesystem::path> incompressible;

for (const auto& file : files) {
    if (adaptive_compression::is_likely_compressible(file)) {
        compressible.push_back(file);
    } else {
        incompressible.push_back(file);
    }
}

// 적절한 설정으로 전송
sender->send_files(compressible, endpoint,
    {.compression = compression_mode::enabled});
sender->send_files(incompressible, endpoint,
    {.compression = compression_mode::disabled});
```

### 5. 압축 워커 확장

```cpp
// 압축이 종종 병목
// 가용 코어에 맞게 워커 확장
pipeline_config config{
    .compression_workers = std::thread::hardware_concurrency() - 2
};
```

---

## 문제 해결

### 낮은 압축 비율

**증상:** 압축 비율이 1.0에 가까움

**원인:**
- 데이터가 이미 압축됨 (ZIP, 미디어)
- 랜덤/암호화된 데이터
- 작은 청크 크기 (작업할 데이터가 적음)

**해결책:**
- 압축 불가능한 데이터를 건너뛰도록 적응형 모드 사용
- 더 나은 압축을 위해 청크 크기 증가

### 높은 CPU 사용률

**증상:** 전송 중 CPU 100%

**원인:**
- 너무 많은 압축 워커
- 빠른 네트워크에서 high_compression 수준

**해결책:**
- 압축 워커 감소
- fast 압축 수준으로 전환
- 이미 압축된 파일에 대해 압축 비활성화

### 느린 압축 해제

**증상:** 수신자 CPU 바운드

**원인:**
- 압축 해제 워커 부족
- 매우 높은 압축 비율 데이터

**해결책:**
- 압축 워커 증가 (압축 해제에도 사용됨)
- 손상된 압축 데이터 확인

---

## 오류 처리

### 압축 오류

| 오류 코드 | 설명 | 복구 |
|----------|------|------|
| -780 | `compression_failed` | 비압축으로 전송 |
| -781 | `decompression_failed` | 재전송 요청 |
| -782 | `compression_buffer_error` | 내부 오류 |
| -783 | `invalid_compression_data` | 재전송 요청 |

### 폴백 동작

```cpp
// 압축 실패 폴백
auto result = lz4_engine::compress(input, output);
if (!result) {
    // 비압축으로 전송
    chunk.header.flags &= ~chunk_flags::compressed;
    chunk.data = input;
    // 전송 계속
}

// 압축 해제 실패 폴백
auto result = lz4_engine::decompress(compressed, output, original_size);
if (!result) {
    // 재전송 요청
    send_chunk_nack(chunk.header.chunk_index);
}
```

---

*최종 업데이트: 2025-12-11*
