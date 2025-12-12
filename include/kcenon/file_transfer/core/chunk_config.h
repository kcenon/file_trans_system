/**
 * @file chunk_config.h
 * @brief Configuration for chunk operations
 */

#ifndef KCENON_FILE_TRANSFER_CORE_CHUNK_CONFIG_H
#define KCENON_FILE_TRANSFER_CORE_CHUNK_CONFIG_H

#include <kcenon/file_transfer/core/types.h>

#include <cstddef>

namespace kcenon::file_transfer {

/**
 * @brief Configuration for chunk operations
 */
struct chunk_config {
    /// Default chunk size (256KB)
    static constexpr std::size_t default_chunk_size = 256 * 1024;

    /// Minimum allowed chunk size (64KB)
    static constexpr std::size_t min_chunk_size = 64 * 1024;

    /// Maximum allowed chunk size (1MB)
    static constexpr std::size_t max_chunk_size = 1024 * 1024;

    /// Chunk size to use for splitting
    std::size_t chunk_size = default_chunk_size;

    /**
     * @brief Default constructor
     */
    chunk_config() = default;

    /**
     * @brief Constructor with custom chunk size
     * @param size Chunk size in bytes
     */
    explicit chunk_config(std::size_t size) : chunk_size(size) {}

    /**
     * @brief Validate configuration
     * @return Success if valid, error otherwise
     */
    [[nodiscard]] auto validate() const -> result<void> {
        if (chunk_size < min_chunk_size) {
            return unexpected(error{
                error_code::invalid_chunk_size,
                "chunk size too small (minimum: " + std::to_string(min_chunk_size) + ")"});
        }
        if (chunk_size > max_chunk_size) {
            return unexpected(error{
                error_code::invalid_chunk_size,
                "chunk size too large (maximum: " + std::to_string(max_chunk_size) + ")"});
        }
        return {};
    }

    /**
     * @brief Calculate number of chunks for a given file size
     * @param file_size Size of the file in bytes
     * @return Number of chunks needed
     */
    [[nodiscard]] auto calculate_chunk_count(uint64_t file_size) const -> uint64_t {
        if (file_size == 0) return 0;
        return (file_size + chunk_size - 1) / chunk_size;
    }
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CORE_CHUNK_CONFIG_H
