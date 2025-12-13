/**
 * @file server_pipeline.h
 * @brief Server-side upload/download pipeline implementation
 *
 * Implements multi-stage pipelines for efficient file transfer processing:
 * - Upload pipeline: network_recv -> decompress -> chunk_verify -> file_write
 * - Download pipeline: file_read -> compress -> network_send
 */

#ifndef KCENON_FILE_TRANSFER_SERVER_SERVER_PIPELINE_H
#define KCENON_FILE_TRANSFER_SERVER_SERVER_PIPELINE_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

#include "kcenon/file_transfer/core/chunk_types.h"
#include "kcenon/file_transfer/core/types.h"

namespace kcenon::file_transfer {

/**
 * @brief Pipeline processing stages
 */
enum class pipeline_stage {
    network_recv,   ///< Network receive stage
    decompress,     ///< LZ4 decompression stage
    chunk_verify,   ///< CRC32 verification stage
    file_write,     ///< Disk write stage
    network_send,   ///< Network send stage
    file_read,      ///< Disk read stage
    compress        ///< LZ4 compression stage
};

/**
 * @brief Convert pipeline_stage to string
 */
[[nodiscard]] constexpr auto to_string(pipeline_stage stage) -> const char* {
    switch (stage) {
        case pipeline_stage::network_recv: return "network_recv";
        case pipeline_stage::decompress: return "decompress";
        case pipeline_stage::chunk_verify: return "chunk_verify";
        case pipeline_stage::file_write: return "file_write";
        case pipeline_stage::network_send: return "network_send";
        case pipeline_stage::file_read: return "file_read";
        case pipeline_stage::compress: return "compress";
        default: return "unknown";
    }
}

/**
 * @brief Pipeline configuration
 */
struct pipeline_config {
    std::size_t io_workers = 2;            ///< Number of I/O worker threads
    std::size_t compression_workers = 4;   ///< Number of compression worker threads
    std::size_t network_workers = 2;       ///< Number of network worker threads
    std::size_t queue_size = 64;           ///< Maximum queue size per stage
    std::size_t max_memory_per_transfer = 32 * 1024 * 1024;  ///< ~32MB per transfer

    // Bandwidth limiting (0 = unlimited)
    std::size_t send_bandwidth_limit = 0;  ///< Max bytes/sec for network send (0 = unlimited)
    std::size_t recv_bandwidth_limit = 0;  ///< Max bytes/sec for network recv (0 = unlimited)

    /**
     * @brief Auto-detect optimal configuration based on hardware
     * @return Optimized pipeline configuration
     */
    [[nodiscard]] static auto auto_detect() -> pipeline_config;

    /**
     * @brief Validate configuration
     * @return true if configuration is valid
     */
    [[nodiscard]] auto is_valid() const -> bool;
};

/**
 * @brief Pipeline statistics
 */
struct pipeline_stats {
    std::atomic<uint64_t> chunks_processed{0};
    std::atomic<uint64_t> bytes_processed{0};
    std::atomic<uint64_t> compression_saved_bytes{0};
    std::atomic<uint64_t> stalls_detected{0};
    std::atomic<uint64_t> backpressure_events{0};

    /**
     * @brief Reset all statistics
     */
    auto reset() -> void;
};

/**
 * @brief Chunk data for pipeline processing
 */
struct pipeline_chunk {
    transfer_id id;
    uint64_t chunk_index;
    std::vector<std::byte> data;
    uint32_t checksum;
    bool is_compressed;
    std::size_t original_size;

    pipeline_chunk() = default;
    explicit pipeline_chunk(const chunk& c);
};

/**
 * @brief Pipeline stage result
 */
struct stage_result {
    bool success;
    pipeline_chunk chunk;
    std::string error_message;

    static auto ok(pipeline_chunk c) -> stage_result;
    static auto fail(const std::string& msg) -> stage_result;
};

/**
 * @brief Callback types for pipeline events
 */
using stage_callback = std::function<void(pipeline_stage, const pipeline_chunk&)>;
using error_callback = std::function<void(pipeline_stage, const std::string&)>;
using completion_callback = std::function<void(const transfer_id&, uint64_t bytes)>;

/**
 * @brief Server-side upload/download pipeline
 *
 * Implements parallel processing pipeline for file transfers using
 * multiple worker threads for each stage. Supports backpressure control
 * through bounded queues.
 *
 * @example
 * @code
 * auto config = pipeline_config::auto_detect();
 * auto pipeline_result = server_pipeline::create(config);
 *
 * if (pipeline_result.has_value()) {
 *     auto& pipeline = pipeline_result.value();
 *     pipeline.start();
 *
 *     // Submit chunks for upload processing
 *     pipeline.submit_upload_chunk(chunk);
 *
 *     // Or submit download request
 *     pipeline.submit_download_request(transfer_id, chunk_index);
 * }
 * @endcode
 */
class server_pipeline {
public:
    /**
     * @brief Create a new pipeline instance
     * @param config Pipeline configuration
     * @return Result containing the pipeline or an error
     */
    [[nodiscard]] static auto create(const pipeline_config& config = pipeline_config{})
        -> result<server_pipeline>;

    // Non-copyable, movable
    server_pipeline(const server_pipeline&) = delete;
    auto operator=(const server_pipeline&) -> server_pipeline& = delete;
    server_pipeline(server_pipeline&&) noexcept;
    auto operator=(server_pipeline&&) noexcept -> server_pipeline&;
    ~server_pipeline();

    /**
     * @brief Start the pipeline
     * @return Result indicating success or failure
     */
    [[nodiscard]] auto start() -> result<void>;

    /**
     * @brief Stop the pipeline gracefully
     * @param wait_for_completion If true, wait for pending work to complete
     * @return Result indicating success or failure
     */
    [[nodiscard]] auto stop(bool wait_for_completion = true) -> result<void>;

    /**
     * @brief Check if pipeline is running
     * @return true if running
     */
    [[nodiscard]] auto is_running() const -> bool;

    // Upload pipeline operations

    /**
     * @brief Submit a chunk for upload processing
     *
     * Pipeline: network_recv -> decompress -> chunk_verify -> file_write
     *
     * @param data Chunk data to process
     * @return Result indicating success or backpressure
     */
    [[nodiscard]] auto submit_upload_chunk(pipeline_chunk data) -> result<void>;

    /**
     * @brief Try to submit a chunk without blocking
     * @param data Chunk data to process
     * @return true if submitted, false if queue is full
     */
    [[nodiscard]] auto try_submit_upload_chunk(pipeline_chunk data) -> bool;

    // Download pipeline operations

    /**
     * @brief Submit a download request
     *
     * Pipeline: file_read -> compress -> network_send
     *
     * @param id Transfer ID
     * @param chunk_index Index of chunk to download
     * @param file_path Path to source file
     * @param offset Byte offset in file
     * @param size Chunk size
     * @return Result indicating success or backpressure
     */
    [[nodiscard]] auto submit_download_request(
        const transfer_id& id,
        uint64_t chunk_index,
        const std::filesystem::path& file_path,
        uint64_t offset,
        std::size_t size) -> result<void>;

    // Callbacks

    /**
     * @brief Set callback for stage completion events
     */
    auto on_stage_complete(stage_callback callback) -> void;

    /**
     * @brief Set callback for error events
     */
    auto on_error(error_callback callback) -> void;

    /**
     * @brief Set callback for upload completion (chunk written to disk)
     */
    auto on_upload_complete(completion_callback callback) -> void;

    /**
     * @brief Set callback for download completion (chunk ready for send)
     */
    auto on_download_ready(std::function<void(const pipeline_chunk&)> callback) -> void;

    // Statistics

    /**
     * @brief Get pipeline statistics
     * @return Reference to statistics
     */
    [[nodiscard]] auto stats() const -> const pipeline_stats&;

    /**
     * @brief Reset pipeline statistics
     */
    auto reset_stats() -> void;

    /**
     * @brief Get current queue sizes for each stage
     * @return Map of stage to queue size
     */
    [[nodiscard]] auto queue_sizes() const
        -> std::vector<std::pair<pipeline_stage, std::size_t>>;

    /**
     * @brief Get configuration
     * @return Pipeline configuration
     */
    [[nodiscard]] auto config() const -> const pipeline_config&;

    // Bandwidth control

    /**
     * @brief Set send bandwidth limit
     * @param bytes_per_second Limit in bytes/sec (0 = unlimited)
     */
    auto set_send_bandwidth_limit(std::size_t bytes_per_second) -> void;

    /**
     * @brief Set recv bandwidth limit
     * @param bytes_per_second Limit in bytes/sec (0 = unlimited)
     */
    auto set_recv_bandwidth_limit(std::size_t bytes_per_second) -> void;

    /**
     * @brief Get current send bandwidth limit
     * @return Current limit (0 = unlimited)
     */
    [[nodiscard]] auto get_send_bandwidth_limit() const -> std::size_t;

    /**
     * @brief Get current recv bandwidth limit
     * @return Current limit (0 = unlimited)
     */
    [[nodiscard]] auto get_recv_bandwidth_limit() const -> std::size_t;

private:
    explicit server_pipeline(pipeline_config config);

    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_SERVER_SERVER_PIPELINE_H
