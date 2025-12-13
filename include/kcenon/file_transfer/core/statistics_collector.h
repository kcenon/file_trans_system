/**
 * @file statistics_collector.h
 * @brief Statistics collection for transfer progress monitoring
 * @version 0.2.0
 *
 * This file defines the statistics_collector class for tracking transfer
 * metrics including transfer rate, compression ratio, and ETA calculation.
 */

#ifndef KCENON_FILE_TRANSFER_CORE_STATISTICS_COLLECTOR_H
#define KCENON_FILE_TRANSFER_CORE_STATISTICS_COLLECTOR_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

#include "error_codes.h"

namespace kcenon::file_transfer {

/**
 * @brief Duration type for time measurements
 */
using duration = std::chrono::milliseconds;
using time_point = std::chrono::steady_clock::time_point;

/**
 * @brief Statistics collector for transfer progress monitoring
 *
 * Thread-safe class for collecting and calculating transfer statistics
 * including transfer rate, compression ratio, and ETA.
 *
 * @code
 * statistics_collector stats;
 * stats.start(total_bytes);
 *
 * // During transfer
 * stats.record_bytes_transferred(chunk_size);
 * stats.record_chunk_processed(true);  // compressed
 *
 * // Get current metrics
 * auto rate = stats.get_transfer_rate();
 * auto eta = stats.get_eta();
 * @endcode
 */
class statistics_collector {
public:
    /**
     * @brief Configuration for statistics collection
     */
    struct config {
        std::size_t rate_window_size = 10;      ///< Number of samples for moving average
        duration rate_sample_interval{100};     ///< Interval between rate samples
        duration min_eta_update_interval{500};  ///< Minimum interval between ETA updates
    };

    /**
     * @brief Snapshot of current statistics
     */
    struct snapshot {
        uint64_t bytes_transferred = 0;         ///< Total bytes transferred
        uint64_t bytes_on_wire = 0;             ///< Compressed bytes on wire
        uint64_t total_bytes = 0;               ///< Total file size
        uint64_t chunks_transferred = 0;        ///< Chunks transferred
        uint64_t chunks_compressed = 0;         ///< Compressed chunks count
        uint64_t error_count = 0;               ///< Number of errors
        double current_rate = 0.0;              ///< Current transfer rate (bytes/sec)
        double average_rate = 0.0;              ///< Average transfer rate (bytes/sec)
        double compression_ratio = 1.0;         ///< Compression ratio
        duration elapsed{0};                    ///< Elapsed time
        duration estimated_remaining{0};        ///< Estimated time remaining
    };

    /**
     * @brief Default constructor
     */
    statistics_collector();

    /**
     * @brief Constructor with custom configuration
     * @param cfg Configuration settings
     */
    explicit statistics_collector(config cfg);

    // Non-copyable, movable
    statistics_collector(const statistics_collector&) = delete;
    auto operator=(const statistics_collector&) -> statistics_collector& = delete;
    statistics_collector(statistics_collector&&) noexcept;
    auto operator=(statistics_collector&&) noexcept -> statistics_collector&;

    ~statistics_collector();

    /**
     * @brief Start statistics collection
     * @param total_bytes Total bytes expected for transfer
     */
    void start(uint64_t total_bytes);

    /**
     * @brief Stop statistics collection
     */
    void stop();

    /**
     * @brief Reset all statistics
     */
    void reset();

    /**
     * @brief Check if collection is active
     * @return true if actively collecting
     */
    [[nodiscard]] auto is_active() const noexcept -> bool;

    // Recording methods

    /**
     * @brief Record bytes transferred
     * @param bytes Number of bytes transferred
     * @param bytes_on_wire Compressed bytes (optional, defaults to bytes)
     */
    void record_bytes_transferred(uint64_t bytes,
                                  std::optional<uint64_t> bytes_on_wire = std::nullopt);

    /**
     * @brief Record a chunk processed
     * @param compressed Whether the chunk was compressed
     */
    void record_chunk_processed(bool compressed);

    /**
     * @brief Record an error
     * @param code Error code
     */
    void record_error(transfer_error_code code);

    // Metric retrieval methods

    /**
     * @brief Get current transfer rate
     * @return Current rate in bytes per second
     */
    [[nodiscard]] auto get_transfer_rate() const -> double;

    /**
     * @brief Get average transfer rate
     * @return Average rate in bytes per second
     */
    [[nodiscard]] auto get_average_rate() const -> double;

    /**
     * @brief Get compression ratio
     * @return Ratio of compressed to original size (1.0 = no compression)
     */
    [[nodiscard]] auto get_compression_ratio() const -> double;

    /**
     * @brief Get estimated time remaining
     * @return Estimated duration until completion
     */
    [[nodiscard]] auto get_eta() const -> duration;

    /**
     * @brief Get elapsed time
     * @return Duration since start
     */
    [[nodiscard]] auto get_elapsed() const -> duration;

    /**
     * @brief Get completion percentage
     * @return Percentage complete (0.0 - 100.0)
     */
    [[nodiscard]] auto get_completion_percentage() const -> double;

    /**
     * @brief Get total bytes transferred
     * @return Bytes transferred so far
     */
    [[nodiscard]] auto get_bytes_transferred() const -> uint64_t;

    /**
     * @brief Get total bytes on wire (compressed)
     * @return Compressed bytes transferred
     */
    [[nodiscard]] auto get_bytes_on_wire() const -> uint64_t;

    /**
     * @brief Get chunks transferred count
     * @return Number of chunks transferred
     */
    [[nodiscard]] auto get_chunks_transferred() const -> uint64_t;

    /**
     * @brief Get error count
     * @return Number of errors recorded
     */
    [[nodiscard]] auto get_error_count() const -> uint64_t;

    /**
     * @brief Get snapshot of all statistics
     * @return Current statistics snapshot
     */
    [[nodiscard]] auto get_snapshot() const -> snapshot;

    /**
     * @brief Set total bytes (for dynamic updates)
     * @param total_bytes New total bytes
     */
    void set_total_bytes(uint64_t total_bytes);

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CORE_STATISTICS_COLLECTOR_H
