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
 * @brief Compression statistics
 */
struct compression_stats {
    uint64_t total_input_bytes = 0;
    uint64_t total_output_bytes = 0;
    uint64_t compression_calls = 0;
    uint64_t decompression_calls = 0;
    uint64_t skipped_compressions = 0;

    [[nodiscard]] auto compression_ratio() const -> double {
        if (total_input_bytes == 0) return 1.0;
        return static_cast<double>(total_output_bytes) /
               static_cast<double>(total_input_bytes);
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
