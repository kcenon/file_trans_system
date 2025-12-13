/**
 * @file compression_engine.h
 * @brief LZ4 compression engine for chunk-level compression/decompression
 */

#ifndef KCENON_FILE_TRANSFER_CORE_COMPRESSION_ENGINE_H
#define KCENON_FILE_TRANSFER_CORE_COMPRESSION_ENGINE_H

#include <kcenon/file_transfer/core/types.h>

#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace kcenon::file_transfer {

/**
 * @brief Compression mode for chunk processing
 */
enum class compression_mode {
    disabled,  ///< No compression
    enabled,   ///< Always compress
    adaptive   ///< Auto-detect based on data compressibility (default)
};

/**
 * @brief Compression level
 */
enum class compression_level {
    fast,     ///< LZ4 default compression (faster)
    high      ///< LZ4 HC compression (higher ratio)
};

/**
 * @brief Compression statistics for adaptive compression optimization
 *
 * Tracks compression performance including:
 * - Bytes processed and saved
 * - Compression/decompression call counts
 * - Skipped compressions for pre-compressed data
 * - CPU time saved by skipping unnecessary compressions
 */
struct compression_stats {
    uint64_t total_input_bytes = 0;      ///< Total bytes before compression
    uint64_t total_output_bytes = 0;     ///< Total bytes after compression
    uint64_t compression_calls = 0;      ///< Number of compression operations
    uint64_t decompression_calls = 0;    ///< Number of decompression operations
    uint64_t skipped_compressions = 0;   ///< Chunks skipped due to pre-compressed format
    uint64_t total_chunks = 0;           ///< Total chunks processed
    uint64_t compressed_chunks = 0;      ///< Chunks that were actually compressed

    /**
     * @brief Calculate compression ratio (output/input)
     * @return Compression ratio (1.0 means no compression benefit)
     */
    [[nodiscard]] auto compression_ratio() const -> double {
        if (total_input_bytes == 0) return 1.0;
        return static_cast<double>(total_output_bytes) /
               static_cast<double>(total_input_bytes);
    }

    /**
     * @brief Calculate average compression ratio
     * @return Average compression ratio across all compressed chunks
     */
    [[nodiscard]] auto average_ratio() const -> double {
        return compression_ratio();
    }

    /**
     * @brief Calculate bytes saved by compression
     * @return Number of bytes saved (input - output)
     */
    [[nodiscard]] auto bytes_saved() const -> uint64_t {
        if (total_output_bytes >= total_input_bytes) return 0;
        return total_input_bytes - total_output_bytes;
    }

    /**
     * @brief Calculate skip rate (skipped/total)
     * @return Percentage of chunks that were skipped
     */
    [[nodiscard]] auto skip_rate() const -> double {
        if (total_chunks == 0) return 0.0;
        return static_cast<double>(skipped_compressions) /
               static_cast<double>(total_chunks) * 100.0;
    }
};

/**
 * @brief LZ4-based compression engine for chunk-level compression
 *
 * Provides methods for:
 * - LZ4 compression with configurable levels
 * - LZ4 decompression
 * - Adaptive compression detection
 * - Pre-compressed file detection (zip, gzip, jpeg, png, etc.)
 *
 * @example
 * @code
 * compression_engine engine(compression_level::fast);
 *
 * // Check if data is worth compressing
 * if (engine.is_compressible(data)) {
 *     auto result = engine.compress(data);
 *     if (result.has_value()) {
 *         // Use compressed data
 *     }
 * }
 * @endcode
 */
class compression_engine {
public:
    /**
     * @brief Construct compression engine with specified level
     * @param level Compression level (default: fast)
     */
    explicit compression_engine(compression_level level = compression_level::fast);

    /**
     * @brief Destructor
     */
    ~compression_engine();

    // Non-copyable but movable
    compression_engine(const compression_engine&) = delete;
    auto operator=(const compression_engine&) -> compression_engine& = delete;
    compression_engine(compression_engine&&) noexcept;
    auto operator=(compression_engine&&) noexcept -> compression_engine&;

    /**
     * @brief Compress data using LZ4
     * @param input Input data to compress
     * @return Compressed data or error
     */
    [[nodiscard]] auto compress(std::span<const std::byte> input)
        -> result<std::vector<std::byte>>;

    /**
     * @brief Decompress LZ4 compressed data
     * @param input Compressed input data
     * @param original_size Expected size of decompressed data
     * @return Decompressed data or error
     */
    [[nodiscard]] auto decompress(std::span<const std::byte> input,
                                  std::size_t original_size)
        -> result<std::vector<std::byte>>;

    /**
     * @brief Check if data is worth compressing
     *
     * Analyzes a sample of data (first 4KB) to determine if compression
     * would be beneficial. Returns false for:
     * - Already compressed files (zip, gzip, jpeg, png, etc.)
     * - Data with low compression ratio (< 1.1x)
     *
     * @param data Data sample to analyze
     * @return true if data should be compressed, false otherwise
     */
    [[nodiscard]] auto is_compressible(std::span<const std::byte> data) const -> bool;

    /**
     * @brief Get compression statistics
     * @return Current compression statistics
     */
    [[nodiscard]] auto stats() const -> compression_stats;

    /**
     * @brief Reset compression statistics
     */
    auto reset_stats() -> void;

    /**
     * @brief Record a skipped compression (for pre-compressed data)
     * @param data_size Size of the skipped data
     *
     * Call this when is_compressible() returns false and compression is skipped.
     */
    auto record_skipped(std::size_t data_size) -> void;

    /**
     * @brief Compress chunk with adaptive decision
     *
     * Automatically decides whether to compress based on data compressibility.
     * Updates statistics for both compressed and skipped chunks.
     *
     * @param input Input data to potentially compress
     * @param mode Compression mode (adaptive by default)
     * @return Pair of (compressed data, was_compressed flag) or error
     */
    [[nodiscard]] auto compress_adaptive(std::span<const std::byte> input,
                                         compression_mode mode = compression_mode::adaptive)
        -> result<std::pair<std::vector<std::byte>, bool>>;

    /**
     * @brief Get current compression level
     * @return Current compression level
     */
    [[nodiscard]] auto level() const -> compression_level;

    /**
     * @brief Set compression level
     * @param level New compression level
     */
    auto set_level(compression_level level) -> void;

    /**
     * @brief Get maximum compressed size for given input size
     * @param input_size Size of input data
     * @return Maximum possible compressed size
     */
    [[nodiscard]] static auto max_compressed_size(std::size_t input_size) -> std::size_t;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CORE_COMPRESSION_ENGINE_H
