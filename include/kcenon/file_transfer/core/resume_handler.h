/**
 * @file resume_handler.h
 * @brief Transfer resume handler for interrupted transfers
 * @version 0.2.0
 *
 * This file defines the resume_handler class for persisting and resuming
 * interrupted file transfers. It provides checkpoint-based state persistence
 * and efficient chunk bitmap tracking.
 */

#ifndef KCENON_FILE_TRANSFER_CORE_RESUME_HANDLER_H
#define KCENON_FILE_TRANSFER_CORE_RESUME_HANDLER_H

#include <kcenon/file_transfer/core/chunk_types.h>
#include <kcenon/file_transfer/core/types.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace kcenon::file_transfer {

/**
 * @brief Transfer state for resumable transfers
 *
 * Contains all information needed to resume an interrupted transfer,
 * including chunk bitmap for tracking received chunks.
 */
struct transfer_state {
    transfer_id id;                    ///< Unique transfer identifier
    std::string filename;              ///< Original filename
    uint64_t total_size = 0;           ///< Total file size in bytes
    uint64_t transferred_bytes = 0;    ///< Bytes successfully transferred
    uint32_t total_chunks = 0;         ///< Total number of chunks
    std::vector<bool> chunk_bitmap;    ///< Bitmap of received chunks
    std::string sha256;                ///< SHA-256 hash of the file
    std::chrono::system_clock::time_point started_at;    ///< Transfer start time
    std::chrono::system_clock::time_point last_activity; ///< Last activity time

    /**
     * @brief Default constructor
     */
    transfer_state() = default;

    /**
     * @brief Initialize transfer state for a new transfer
     * @param transfer_id Transfer identifier
     * @param file_name Name of the file
     * @param file_size Total file size
     * @param num_chunks Number of chunks
     * @param file_hash SHA-256 hash
     */
    transfer_state(
        const transfer_id& transfer_id,
        std::string file_name,
        uint64_t file_size,
        uint32_t num_chunks,
        std::string file_hash);

    /**
     * @brief Get count of received chunks
     * @return Number of chunks marked as received
     */
    [[nodiscard]] auto received_chunk_count() const -> uint32_t;

    /**
     * @brief Get completion percentage
     * @return Percentage of transfer completed (0.0 to 100.0)
     */
    [[nodiscard]] auto completion_percentage() const -> double;

    /**
     * @brief Check if transfer is complete
     * @return true if all chunks received
     */
    [[nodiscard]] auto is_complete() const -> bool;
};

/**
 * @brief Configuration for resume_handler
 */
struct resume_handler_config {
    std::filesystem::path state_directory;  ///< Directory for state files
    uint32_t checkpoint_interval = 10;      ///< Save state every N chunks
    std::chrono::seconds state_ttl{86400};  ///< State file TTL (default: 24h)
    bool auto_cleanup = true;               ///< Auto cleanup expired states

    /**
     * @brief Default constructor with default values
     */
    resume_handler_config();

    /**
     * @brief Construct with custom state directory
     * @param dir State directory path
     */
    explicit resume_handler_config(std::filesystem::path dir);
};

/**
 * @brief Handler for resumable file transfers
 *
 * Provides functionality to:
 * - Save and load transfer states
 * - Track received chunks using bitmap
 * - Determine missing chunks for resume
 * - Auto-checkpoint during transfers
 *
 * State files are stored in JSON format for easy debugging and portability.
 *
 * @example
 * @code
 * resume_handler handler({"/tmp/transfer_states"});
 *
 * // Create new transfer state
 * transfer_state state(id, "file.txt", file_size, num_chunks, hash);
 *
 * // Save state
 * auto result = handler.save_state(state);
 *
 * // Mark chunks as received
 * handler.mark_chunk_received(id, 0);
 * handler.mark_chunk_received(id, 1);
 *
 * // Get missing chunks for resume
 * auto missing = handler.get_missing_chunks(id);
 * @endcode
 */
class resume_handler {
public:
    /**
     * @brief Construct with configuration
     * @param config Resume handler configuration
     */
    explicit resume_handler(const resume_handler_config& config);

    /**
     * @brief Destructor
     */
    ~resume_handler();

    // Non-copyable but movable
    resume_handler(const resume_handler&) = delete;
    auto operator=(const resume_handler&) -> resume_handler& = delete;
    resume_handler(resume_handler&&) noexcept;
    auto operator=(resume_handler&&) noexcept -> resume_handler&;

    // ========================================================================
    // State persistence
    // ========================================================================

    /**
     * @brief Save transfer state to persistent storage
     * @param state Transfer state to save
     * @return Success or error
     */
    [[nodiscard]] auto save_state(const transfer_state& state) -> result<void>;

    /**
     * @brief Load transfer state from persistent storage
     * @param id Transfer identifier
     * @return Transfer state or error
     */
    [[nodiscard]] auto load_state(const transfer_id& id) -> result<transfer_state>;

    /**
     * @brief Delete transfer state from persistent storage
     * @param id Transfer identifier
     * @return Success or error
     */
    [[nodiscard]] auto delete_state(const transfer_id& id) -> result<void>;

    /**
     * @brief Check if state exists for transfer
     * @param id Transfer identifier
     * @return true if state exists
     */
    [[nodiscard]] auto has_state(const transfer_id& id) const -> bool;

    // ========================================================================
    // Chunk tracking
    // ========================================================================

    /**
     * @brief Mark a chunk as received
     * @param id Transfer identifier
     * @param chunk_index Index of received chunk
     * @return Success or error
     *
     * Automatically saves state at checkpoint intervals.
     */
    [[nodiscard]] auto mark_chunk_received(
        const transfer_id& id,
        uint32_t chunk_index) -> result<void>;

    /**
     * @brief Mark multiple chunks as received
     * @param id Transfer identifier
     * @param chunk_indices Indices of received chunks
     * @return Success or error
     */
    [[nodiscard]] auto mark_chunks_received(
        const transfer_id& id,
        const std::vector<uint32_t>& chunk_indices) -> result<void>;

    /**
     * @brief Get list of missing (unreceived) chunks
     * @param id Transfer identifier
     * @return Vector of missing chunk indices or error
     */
    [[nodiscard]] auto get_missing_chunks(const transfer_id& id)
        -> result<std::vector<uint32_t>>;

    /**
     * @brief Check if a specific chunk was received
     * @param id Transfer identifier
     * @param chunk_index Chunk index to check
     * @return true if chunk was received, false otherwise
     */
    [[nodiscard]] auto is_chunk_received(
        const transfer_id& id,
        uint32_t chunk_index) const -> bool;

    // ========================================================================
    // State query
    // ========================================================================

    /**
     * @brief List all resumable transfers
     * @return Vector of transfer states
     */
    [[nodiscard]] auto list_resumable_transfers() -> std::vector<transfer_state>;

    /**
     * @brief Cleanup expired transfer states
     * @return Number of states removed
     */
    [[nodiscard]] auto cleanup_expired_states() -> std::size_t;

    /**
     * @brief Get configuration
     * @return Current configuration
     */
    [[nodiscard]] auto config() const -> const resume_handler_config&;

    /**
     * @brief Update transferred bytes count
     * @param id Transfer identifier
     * @param bytes Bytes to add
     * @return Success or error
     */
    [[nodiscard]] auto update_transferred_bytes(
        const transfer_id& id,
        uint64_t bytes) -> result<void>;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CORE_RESUME_HANDLER_H
