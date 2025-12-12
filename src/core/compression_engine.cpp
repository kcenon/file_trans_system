/**
 * @file compression_engine.cpp
 * @brief LZ4 compression engine implementation
 */

#include <kcenon/file_transfer/core/compression_engine.h>

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
constexpr std::array<magic_signature, 14> compressed_signatures = {{
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
    // WEBP
    {{0x52, 0x49, 0x46, 0x46}, 4},
    // MP3
    {{0xFF, 0xFB}, 2},
    // MP4/MOV
    {{0x00, 0x00, 0x00}, 3},
}};

auto is_precompressed_format(std::span<const std::byte> data) -> bool {
    if (data.size() < 8) {
        return false;
    }

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
            return unexpected(error(error_code::internal_error, "LZ4 compression failed"));
        }

        output.resize(static_cast<std::size_t>(compressed_size));

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
            return unexpected(
                error(error_code::internal_error, "LZ4 decompression failed: corrupted data"));
        }

        if (static_cast<std::size_t>(decompressed_size) != original_size) {
            return unexpected(error(error_code::internal_error,
                                    "LZ4 decompression size mismatch"));
        }

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

        return ratio >= min_compression_ratio;
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

}  // namespace kcenon::file_transfer
