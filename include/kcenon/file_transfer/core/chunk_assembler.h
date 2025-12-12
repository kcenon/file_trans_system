/**
 * @file chunk_assembler.h
 * @brief Chunk assembly into files
 */

#ifndef KCENON_FILE_TRANSFER_CORE_CHUNK_ASSEMBLER_H
#define KCENON_FILE_TRANSFER_CORE_CHUNK_ASSEMBLER_H

#include <kcenon/file_transfer/core/types.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace kcenon::file_transfer {

/**
 * @brief Reassembles chunks into complete files
 *
 * Handles out-of-order chunk reception, tracks missing chunks,
 * and performs integrity verification.
 */
class chunk_assembler {
public:
    /**
     * @brief Construct assembler with output directory
     * @param output_dir Directory for assembled files
     */
    explicit chunk_assembler(const std::filesystem::path& output_dir);

    /**
     * @brief Start a new assembly session
     * @param id Transfer ID
     * @param filename Output filename
     * @param file_size Expected file size
     * @param total_chunks Total number of chunks expected
     * @return Success or error
     */
    [[nodiscard]] auto start_session(
        const transfer_id& id,
        const std::string& filename,
        uint64_t file_size,
        uint64_t total_chunks) -> result<void>;

    /**
     * @brief Process an incoming chunk
     * @param c Chunk to process
     * @return Success or error
     */
    [[nodiscard]] auto process_chunk(const chunk& c) -> result<void>;

    /**
     * @brief Check if assembly is complete
     * @param id Transfer ID
     * @return true if all chunks received
     */
    [[nodiscard]] auto is_complete(const transfer_id& id) const -> bool;

    /**
     * @brief Get indices of missing chunks
     * @param id Transfer ID
     * @return Vector of missing chunk indices
     */
    [[nodiscard]] auto get_missing_chunks(const transfer_id& id) const
        -> std::vector<uint64_t>;

    /**
     * @brief Finalize assembly and verify integrity
     * @param id Transfer ID
     * @param expected_hash Expected SHA-256 hash (optional)
     * @return Path to assembled file or error
     */
    [[nodiscard]] auto finalize(
        const transfer_id& id,
        const std::string& expected_hash = "") -> result<std::filesystem::path>;

    /**
     * @brief Get assembly progress
     * @param id Transfer ID
     * @return Progress information if session exists
     */
    [[nodiscard]] auto get_progress(const transfer_id& id) const
        -> std::optional<assembly_progress>;

    /**
     * @brief Cancel and cleanup an assembly session
     * @param id Transfer ID
     */
    void cancel_session(const transfer_id& id);

    /**
     * @brief Check if a session exists
     * @param id Transfer ID
     * @return true if session exists
     */
    [[nodiscard]] auto has_session(const transfer_id& id) const -> bool;

    // Move-only
    chunk_assembler(chunk_assembler&&) noexcept;
    auto operator=(chunk_assembler&&) noexcept -> chunk_assembler&;
    ~chunk_assembler();

    // No copy
    chunk_assembler(const chunk_assembler&) = delete;
    auto operator=(const chunk_assembler&) -> chunk_assembler& = delete;

private:
    struct assembly_context {
        std::filesystem::path temp_file_path;
        std::filesystem::path final_path;
        std::string filename;
        std::unique_ptr<std::ofstream> file;
        uint64_t file_size;
        uint64_t total_chunks;
        std::vector<bool> received_chunks;
        uint64_t received_count;
        uint64_t bytes_written;
        mutable std::mutex mutex;

        assembly_context() : file_size(0), total_chunks(0), received_count(0), bytes_written(0) {}
    };

    std::filesystem::path output_dir_;
    std::unordered_map<transfer_id, std::unique_ptr<assembly_context>> contexts_;
    mutable std::shared_mutex contexts_mutex_;

    [[nodiscard]] auto verify_chunk_crc32(const chunk& c) const -> bool;
    [[nodiscard]] auto get_context(const transfer_id& id) -> assembly_context*;
    [[nodiscard]] auto get_context(const transfer_id& id) const -> const assembly_context*;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CORE_CHUNK_ASSEMBLER_H
