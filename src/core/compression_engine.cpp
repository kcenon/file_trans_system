/**
 * @file compression_engine.cpp
 * @brief LZ4 compression engine implementation
 */

#include <kcenon/file_transfer/core/compression_engine.h>
#include <kcenon/file_transfer/core/logging.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>

#ifdef FILE_TRANS_ENABLE_LZ4
#include <lz4.h>
#include <lz4hc.h>
#endif

namespace kcenon::file_transfer {

namespace {

// Magic bytes for detecting pre-compressed formats
struct magic_signature {
    std::array<uint8_t, 8> bytes;
    std::size_t length;
};

// Common compressed/binary file signatures
constexpr std::array<magic_signature, 15> compressed_signatures = {{
    // ZIP
    {{0x50, 0x4B, 0x03, 0x04}, 4},
    {{0x50, 0x4B, 0x05, 0x06}, 4},
    {{0x50, 0x4B, 0x07, 0x08}, 4},
    // GZIP
    {{0x1F, 0x8B}, 2},
    // ZSTD
    {{0x28, 0xB5, 0x2F, 0xFD}, 4},
    // XZ
    {{0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00}, 6},
    // BZIP2
    {{0x42, 0x5A, 0x68}, 3},
    // LZ4 frame
    {{0x04, 0x22, 0x4D, 0x18}, 4},
    // JPEG
    {{0xFF, 0xD8, 0xFF}, 3},
    // PNG
    {{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A}, 8},
    // GIF
    {{0x47, 0x49, 0x46, 0x38}, 4},
    // WEBP (RIFF container)
    {{0x52, 0x49, 0x46, 0x46}, 4},
    // MP3 (ID3 tag or frame sync)
    {{0xFF, 0xFB}, 2},
    // PDF
    {{0x25, 0x50, 0x44, 0x46}, 4},
    // 7-Zip
    {{0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C}, 6},
}};

// MP4/MOV ftyp signature (at offset 4)
auto is_mp4_format(std::span<const std::byte> data) -> bool {
    if (data.size() < 12) {
        return false;
    }
    // Check for 'ftyp' at offset 4
    return static_cast<uint8_t>(data[4]) == 0x66 &&   // 'f'
           static_cast<uint8_t>(data[5]) == 0x74 &&   // 't'
           static_cast<uint8_t>(data[6]) == 0x79 &&   // 'y'
           static_cast<uint8_t>(data[7]) == 0x70;     // 'p'
}

auto is_precompressed_format(std::span<const std::byte> data) -> bool {
    if (data.size() < 8) {
        return false;
    }

    // Check magic byte signatures
    for (const auto& sig : compressed_signatures) {
        if (data.size() >= sig.length) {
            bool match = true;
            for (std::size_t i = 0; i < sig.length; ++i) {
                if (static_cast<uint8_t>(data[i]) != sig.bytes[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
    }

    // Check MP4/MOV format (ftyp at offset 4)
    if (is_mp4_format(data)) {
        return true;
    }

    return false;
}

// Sample size for compressibility check (4KB)
constexpr std::size_t sample_size = 4096;

// Minimum compression ratio threshold
constexpr double min_compression_ratio = 1.1;

}  // namespace

class compression_engine::impl {
public:
    explicit impl(compression_level level) : level_(level) {}

    ~impl() = default;

    impl(const impl&) = delete;
    auto operator=(const impl&) -> impl& = delete;

    impl(impl&& other) noexcept
        : level_(other.level_), stats_(other.stats_) {}

    auto operator=(impl&& other) noexcept -> impl& {
        if (this != &other) {
            level_ = other.level_;
            stats_ = other.stats_;
        }
        return *this;
    }

    auto compress(std::span<const std::byte> input) -> result<std::vector<std::byte>> {
#ifndef FILE_TRANS_ENABLE_LZ4
        (void)input;
        FT_LOG_WARN(log_category::compression, "LZ4 compression not enabled");
        return unexpected(error(error_code::internal_error, "LZ4 compression not enabled"));
#else
        if (input.empty()) {
            std::lock_guard lock(stats_mutex_);
            stats_.compression_calls++;
            return std::vector<std::byte>{};
        }

        const int src_size = static_cast<int>(input.size());
        const int max_dst_size = LZ4_compressBound(src_size);

        std::vector<std::byte> output(static_cast<std::size_t>(max_dst_size));

        int compressed_size = 0;
        if (level_ == compression_level::high) {
            compressed_size = LZ4_compress_HC(
                reinterpret_cast<const char*>(input.data()),
                reinterpret_cast<char*>(output.data()),
                src_size,
                max_dst_size,
                LZ4HC_CLEVEL_DEFAULT);
        } else {
            compressed_size = LZ4_compress_default(
                reinterpret_cast<const char*>(input.data()),
                reinterpret_cast<char*>(output.data()),
                src_size,
                max_dst_size);
        }

        if (compressed_size <= 0) {
            FT_LOG_ERROR(log_category::compression,
                "LZ4 compression failed for " + std::to_string(input.size()) + " bytes");
            return unexpected(error(error_code::internal_error, "LZ4 compression failed"));
        }

        output.resize(static_cast<std::size_t>(compressed_size));

        double ratio = static_cast<double>(input.size()) /
                       static_cast<double>(compressed_size);
        FT_LOG_TRACE(log_category::compression,
            "Compressed " + std::to_string(input.size()) + " -> " +
            std::to_string(compressed_size) + " bytes (ratio: " +
            std::to_string(ratio).substr(0, 4) + ")");

        {
            std::lock_guard lock(stats_mutex_);
            stats_.compression_calls++;
            stats_.total_input_bytes += input.size();
            stats_.total_output_bytes += output.size();
        }

        return output;
#endif
    }

    auto decompress(std::span<const std::byte> input, std::size_t original_size)
        -> result<std::vector<std::byte>> {
#ifndef FILE_TRANS_ENABLE_LZ4
        (void)input;
        (void)original_size;
        FT_LOG_WARN(log_category::compression, "LZ4 compression not enabled");
        return unexpected(error(error_code::internal_error, "LZ4 compression not enabled"));
#else
        if (input.empty()) {
            std::lock_guard lock(stats_mutex_);
            stats_.decompression_calls++;
            return std::vector<std::byte>{};
        }

        std::vector<std::byte> output(original_size);

        const int decompressed_size = LZ4_decompress_safe(
            reinterpret_cast<const char*>(input.data()),
            reinterpret_cast<char*>(output.data()),
            static_cast<int>(input.size()),
            static_cast<int>(original_size));

        if (decompressed_size < 0) {
            FT_LOG_ERROR(log_category::compression,
                "LZ4 decompression failed: corrupted data (" +
                std::to_string(input.size()) + " bytes input)");
            return unexpected(
                error(error_code::internal_error, "LZ4 decompression failed: corrupted data"));
        }

        if (static_cast<std::size_t>(decompressed_size) != original_size) {
            FT_LOG_ERROR(log_category::compression,
                "LZ4 decompression size mismatch: expected " +
                std::to_string(original_size) + ", got " +
                std::to_string(decompressed_size));
            return unexpected(error(error_code::internal_error,
                                    "LZ4 decompression size mismatch"));
        }

        FT_LOG_TRACE(log_category::compression,
            "Decompressed " + std::to_string(input.size()) + " -> " +
            std::to_string(original_size) + " bytes");

        {
            std::lock_guard lock(stats_mutex_);
            stats_.decompression_calls++;
        }

        return output;
#endif
    }

    auto is_compressible(std::span<const std::byte> data) const -> bool {
#ifndef FILE_TRANS_ENABLE_LZ4
        (void)data;
        return false;
#else
        if (data.empty()) {
            return false;
        }

        // Check for pre-compressed formats
        if (is_precompressed_format(data)) {
            FT_LOG_TRACE(log_category::compression,
                "Data detected as pre-compressed format, skipping compression");
            return false;
        }

        // Use sample for large data
        const auto actual_sample_size = std::min(data.size(), sample_size);
        auto sample = data.subspan(0, actual_sample_size);

        // Try compressing the sample
        const int src_size = static_cast<int>(sample.size());
        const int max_dst_size = LZ4_compressBound(src_size);

        std::vector<char> compressed(static_cast<std::size_t>(max_dst_size));

        const int compressed_size = LZ4_compress_default(
            reinterpret_cast<const char*>(sample.data()),
            compressed.data(),
            src_size,
            max_dst_size);

        if (compressed_size <= 0) {
            return false;
        }

        // Check compression ratio
        const double ratio =
            static_cast<double>(sample.size()) / static_cast<double>(compressed_size);

        bool compressible = ratio >= min_compression_ratio;
        FT_LOG_TRACE(log_category::compression,
            "Compressibility check: ratio=" + std::to_string(ratio).substr(0, 4) +
            ", compressible=" + (compressible ? "true" : "false"));

        return compressible;
#endif
    }

    auto stats() const -> compression_stats {
        std::lock_guard lock(stats_mutex_);
        return stats_;
    }

    auto reset_stats() -> void {
        std::lock_guard lock(stats_mutex_);
        stats_ = compression_stats{};
    }

    auto level() const -> compression_level { return level_; }

    auto set_level(compression_level level) -> void { level_ = level; }

    auto record_skipped(std::size_t data_size) -> void {
        std::lock_guard lock(stats_mutex_);
        stats_.skipped_compressions++;
        stats_.total_chunks++;
        stats_.total_input_bytes += data_size;
        stats_.total_output_bytes += data_size;  // No compression, same size
    }

    auto compress_adaptive(std::span<const std::byte> input, compression_mode mode)
        -> result<std::pair<std::vector<std::byte>, bool>> {
#ifndef FILE_TRANS_ENABLE_LZ4
        (void)input;
        (void)mode;
        return unexpected(error(error_code::internal_error, "LZ4 compression not enabled"));
#else
        if (input.empty()) {
            std::lock_guard lock(stats_mutex_);
            stats_.total_chunks++;
            return std::make_pair(std::vector<std::byte>{}, false);
        }

        bool should_compress = false;
        switch (mode) {
            case compression_mode::disabled:
                should_compress = false;
                break;
            case compression_mode::enabled:
                should_compress = true;
                break;
            case compression_mode::adaptive:
            default:
                should_compress = is_compressible(input);
                break;
        }

        if (!should_compress) {
            // Skip compression - record stats and return original data
            record_skipped(input.size());
            return std::make_pair(
                std::vector<std::byte>(input.begin(), input.end()), false);
        }

        // Perform compression
        auto compress_result = compress(input);
        if (!compress_result.has_value()) {
            return unexpected(compress_result.error());
        }

        {
            std::lock_guard lock(stats_mutex_);
            stats_.total_chunks++;
            stats_.compressed_chunks++;
        }

        return std::make_pair(std::move(compress_result.value()), true);
#endif
    }

private:
    compression_level level_;
    compression_stats stats_;
    mutable std::mutex stats_mutex_;
};

compression_engine::compression_engine(compression_level level)
    : impl_(std::make_unique<impl>(level)) {}

compression_engine::~compression_engine() = default;

compression_engine::compression_engine(compression_engine&&) noexcept = default;

auto compression_engine::operator=(compression_engine&&) noexcept
    -> compression_engine& = default;

auto compression_engine::compress(std::span<const std::byte> input)
    -> result<std::vector<std::byte>> {
    return impl_->compress(input);
}

auto compression_engine::decompress(std::span<const std::byte> input,
                                    std::size_t original_size)
    -> result<std::vector<std::byte>> {
    return impl_->decompress(input, original_size);
}

auto compression_engine::is_compressible(std::span<const std::byte> data) const -> bool {
    return impl_->is_compressible(data);
}

auto compression_engine::stats() const -> compression_stats {
    return impl_->stats();
}

auto compression_engine::reset_stats() -> void {
    impl_->reset_stats();
}

auto compression_engine::level() const -> compression_level {
    return impl_->level();
}

auto compression_engine::set_level(compression_level level) -> void {
    impl_->set_level(level);
}

auto compression_engine::max_compressed_size(std::size_t input_size) -> std::size_t {
#ifdef FILE_TRANS_ENABLE_LZ4
    return static_cast<std::size_t>(LZ4_compressBound(static_cast<int>(input_size)));
#else
    return input_size;
#endif
}

auto compression_engine::record_skipped(std::size_t data_size) -> void {
    impl_->record_skipped(data_size);
}

auto compression_engine::compress_adaptive(std::span<const std::byte> input,
                                           compression_mode mode)
    -> result<std::pair<std::vector<std::byte>, bool>> {
    return impl_->compress_adaptive(input, mode);
}

}  // namespace kcenon::file_transfer
