/**
 * @file chunk_splitter.h
 * @brief File splitting into chunks for transfer
 */

#ifndef KCENON_FILE_TRANSFER_CORE_CHUNK_SPLITTER_H
#define KCENON_FILE_TRANSFER_CORE_CHUNK_SPLITTER_H

#include <kcenon/file_transfer/core/chunk_config.h>
#include <kcenon/file_transfer/core/types.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace kcenon::file_transfer {

/**
 * @brief Splits files into chunks for streaming transfer
 *
 * Provides memory-efficient file splitting using an iterator pattern.
 * Files are read in chunks without loading the entire file into memory.
 */
class chunk_splitter {
public:
    /**
     * @brief Iterator for streaming chunk access
     *
     * Allows iterating over chunks without loading entire file into memory.
     */
    class chunk_iterator {
    public:
        /**
         * @brief Check if more chunks are available
         * @return true if more chunks available
         */
        [[nodiscard]] auto has_next() const -> bool;

        /**
         * @brief Get next chunk
         * @return Next chunk or error
         */
        [[nodiscard]] auto next() -> result<chunk>;

        /**
         * @brief Get current chunk index (0-based)
         * @return Current chunk index
         */
        [[nodiscard]] auto current_index() const -> uint64_t;

        /**
         * @brief Get total number of chunks
         * @return Total chunk count
         */
        [[nodiscard]] auto total_chunks() const -> uint64_t;

        /**
         * @brief Get file size
         * @return File size in bytes
         */
        [[nodiscard]] auto file_size() const -> uint64_t;

        // Move-only
        chunk_iterator(chunk_iterator&&) noexcept;
        auto operator=(chunk_iterator&&) noexcept -> chunk_iterator&;
        ~chunk_iterator();

        // No copy
        chunk_iterator(const chunk_iterator&) = delete;
        auto operator=(const chunk_iterator&) -> chunk_iterator& = delete;

    private:
        friend class chunk_splitter;

        chunk_iterator(
            std::ifstream file,
            chunk_config config,
            transfer_id id,
            uint64_t file_size,
            uint64_t total_chunks);

        std::ifstream file_;
        chunk_config config_;
        transfer_id transfer_id_;
        uint64_t file_size_;
        uint64_t total_chunks_;
        uint64_t current_index_;
        std::vector<std::byte> buffer_;
    };

    /**
     * @brief Construct with default configuration
     */
    chunk_splitter();

    /**
     * @brief Construct with custom configuration
     * @param config Chunk configuration
     */
    explicit chunk_splitter(const chunk_config& config);

    /**
     * @brief Create chunk iterator for a file
     * @param file_path Path to the file to split
     * @param id Transfer ID for the chunks
     * @return Chunk iterator or error
     */
    [[nodiscard]] auto split(
        const std::filesystem::path& file_path,
        const transfer_id& id) -> result<chunk_iterator>;

    /**
     * @brief Calculate file metadata without splitting
     * @param file_path Path to the file
     * @return File metadata or error
     */
    [[nodiscard]] auto calculate_metadata(const std::filesystem::path& file_path)
        -> result<file_metadata>;

    /**
     * @brief Get current configuration
     * @return Chunk configuration
     */
    [[nodiscard]] auto config() const -> const chunk_config&;

private:
    chunk_config config_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CORE_CHUNK_SPLITTER_H
