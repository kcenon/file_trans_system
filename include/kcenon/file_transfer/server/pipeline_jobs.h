/**
 * @file pipeline_jobs.h
 * @brief Pipeline job types for thread_system integration
 *
 * Defines job classes for each pipeline stage that inherit from
 * kcenon::thread::job for use with thread_system's thread_pool.
 *
 * Job types:
 * - decompress_job: Handles LZ4 decompression of chunks
 * - verify_job: Handles CRC32 checksum verification
 * - write_job: Handles file write operations
 * - read_job: Handles file read operations
 * - compress_job: Handles LZ4 compression of chunks
 * - send_job: Handles network send preparation
 */

#ifndef KCENON_FILE_TRANSFER_SERVER_PIPELINE_JOBS_H
#define KCENON_FILE_TRANSFER_SERVER_PIPELINE_JOBS_H

#include "kcenon/file_transfer/core/types.h"
#include "kcenon/file_transfer/server/server_pipeline.h"

#include <kcenon/thread/core/job.h>
#include <kcenon/thread/core/job_queue.h>
#include <kcenon/thread/core/thread_pool.h>
#include <kcenon/common/patterns/result.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace kcenon::file_transfer {

// Forward declarations
class compression_engine;
class bandwidth_limiter;
class encryption_interface;

/**
 * @brief Shared context for pipeline jobs
 *
 * Contains shared resources and callbacks that are used across
 * multiple pipeline jobs. This allows jobs to access common
 * functionality without tight coupling.
 */
struct pipeline_context {
    /// Thread pool for job execution
    std::shared_ptr<thread::thread_pool> thread_pool;

    /// Bounded job queues for each stage
    std::shared_ptr<thread::job_queue> decompress_queue;
    std::shared_ptr<thread::job_queue> verify_queue;
    std::shared_ptr<thread::job_queue> write_queue;
    std::shared_ptr<thread::job_queue> read_queue;
    std::shared_ptr<thread::job_queue> compress_queue;
    std::shared_ptr<thread::job_queue> send_queue;

    /// Compression engines for workers
    std::vector<std::unique_ptr<compression_engine>> compression_engines;

    /// Encryption engines for workers (one per encryption worker)
    std::vector<std::shared_ptr<encryption_interface>> encryption_engines;

    /// Decrypt queue (for upload pipeline: decompress -> decrypt -> verify)
    std::shared_ptr<thread::job_queue> decrypt_queue;

    /// Encrypt queue (for download pipeline: read -> encrypt -> compress)
    std::shared_ptr<thread::job_queue> encrypt_queue;

    /// Whether encryption is enabled for this pipeline
    bool encryption_enabled = false;

    /// Stage completion callback
    stage_callback on_stage_complete_cb;

    /// Error callback
    error_callback on_error_cb;

    /// Upload completion callback
    completion_callback on_upload_complete_cb;

    /// Download ready callback
    std::function<void(const pipeline_chunk&)> on_download_ready_cb;

    /// Statistics reference
    pipeline_stats* statistics = nullptr;

    /// Running flag
    std::atomic<bool>* running = nullptr;

    /// Bandwidth limiters
    bandwidth_limiter* send_limiter = nullptr;
    bandwidth_limiter* recv_limiter = nullptr;

    /**
     * @brief Report an error through the error callback
     * @param stage The pipeline stage where error occurred
     * @param message Error message
     */
    auto report_error(pipeline_stage stage, const std::string& message) const -> void {
        if (on_error_cb) {
            on_error_cb(stage, message);
        }
    }

    /**
     * @brief Report stage completion through the callback
     * @param stage The completed pipeline stage
     * @param chunk The processed chunk
     */
    auto report_stage_complete(pipeline_stage stage, const pipeline_chunk& chunk) const -> void {
        if (on_stage_complete_cb) {
            on_stage_complete_cb(stage, chunk);
        }
    }

    /**
     * @brief Check if pipeline is still running
     */
    [[nodiscard]] auto is_running() const -> bool {
        return running != nullptr && running->load();
    }
};

/**
 * @brief Base class for pipeline jobs
 *
 * Provides common functionality for all pipeline jobs including:
 * - Access to shared pipeline context
 * - Cancellation token handling
 * - Next stage queue reference for job chaining
 */
class pipeline_job_base : public kcenon::thread::job {
public:
    /**
     * @brief Construct a pipeline job
     * @param name Job name for identification
     * @param context Shared pipeline context
     */
    pipeline_job_base(const std::string& name, std::shared_ptr<pipeline_context> context);

    ~pipeline_job_base() override = default;

protected:
    /**
     * @brief Check if the job has been cancelled
     * @return true if cancelled
     */
    [[nodiscard]] auto is_cancelled() const -> bool;

    /**
     * @brief Get the shared pipeline context
     * @return Reference to pipeline context
     */
    [[nodiscard]] auto context() const -> const pipeline_context&;

    /// Shared pipeline context
    std::shared_ptr<pipeline_context> context_;
};

/**
 * @brief Job for LZ4 decompression of chunks
 *
 * Decompresses compressed chunks using the compression engine selected
 * by worker_id. On success, passes the decompressed chunk to the verify stage.
 */
class decompress_job : public pipeline_job_base {
public:
    /**
     * @brief Construct a decompress job
     * @param context Shared pipeline context
     * @param chunk Chunk to decompress
     * @param worker_id Worker ID for selecting compression engine
     */
    decompress_job(std::shared_ptr<pipeline_context> context,
                   pipeline_chunk chunk,
                   std::size_t worker_id);

    /**
     * @brief Execute the decompression work
     * @return Success or error result
     */
    [[nodiscard]] auto do_work() -> kcenon::common::VoidResult override;

    /**
     * @brief Get the processed chunk after job completion
     * @return The decompressed chunk
     */
    [[nodiscard]] auto get_chunk() const -> const pipeline_chunk&;

private:
    pipeline_chunk chunk_;
    std::size_t worker_id_;
};

/**
 * @brief Job for CRC32 checksum verification
 *
 * Verifies the CRC32 checksum of a chunk's data against the expected value.
 * On success, passes the verified chunk to the next stage (write).
 */
class verify_job : public pipeline_job_base {
public:
    /**
     * @brief Construct a verify job
     * @param chunk Chunk to verify
     * @param context Shared pipeline context
     */
    verify_job(pipeline_chunk chunk, std::shared_ptr<pipeline_context> context);

    /**
     * @brief Execute the verification work
     * @return Success or error result
     */
    [[nodiscard]] auto do_work() -> kcenon::common::VoidResult override;

    /**
     * @brief Get the processed chunk after job completion
     * @return The verified chunk
     */
    [[nodiscard]] auto get_chunk() const -> const pipeline_chunk&;

private:
    pipeline_chunk chunk_;
};

/**
 * @brief Job for file write operations
 *
 * Writes chunk data to disk. This is the final stage of the upload pipeline.
 */
class write_job : public pipeline_job_base {
public:
    /**
     * @brief Construct a write job
     * @param chunk Chunk to write
     * @param context Shared pipeline context
     */
    write_job(pipeline_chunk chunk, std::shared_ptr<pipeline_context> context);

    /**
     * @brief Execute the write work
     * @return Success or error result
     */
    [[nodiscard]] auto do_work() -> kcenon::common::VoidResult override;

    /**
     * @brief Get the processed chunk after job completion
     * @return The written chunk
     */
    [[nodiscard]] auto get_chunk() const -> const pipeline_chunk&;

private:
    pipeline_chunk chunk_;
};

/**
 * @brief Job for file read operations
 *
 * Reads chunk data from disk. This is the first stage of the download pipeline.
 * On success, passes the read chunk to the compress stage.
 */
class read_job : public pipeline_job_base {
public:
    /**
     * @brief Construct a read job
     * @param context Shared pipeline context
     * @param id Transfer identifier
     * @param chunk_index Index of chunk to read
     * @param file_path Path to source file
     * @param offset Byte offset in file
     * @param size Number of bytes to read
     */
    read_job(std::shared_ptr<pipeline_context> context,
             const transfer_id& id,
             uint64_t chunk_index,
             std::filesystem::path file_path,
             uint64_t offset,
             std::size_t size);

    /**
     * @brief Execute the read work
     * @return Success or error result
     */
    [[nodiscard]] auto do_work() -> kcenon::common::VoidResult override;

    /**
     * @brief Get the read chunk after job completion
     * @return The chunk read from file
     */
    [[nodiscard]] auto get_chunk() const -> const pipeline_chunk&;

private:
    transfer_id id_;
    uint64_t chunk_index_;
    std::filesystem::path file_path_;
    uint64_t offset_;
    std::size_t size_;
    pipeline_chunk chunk_;
};

/**
 * @brief Job for LZ4 compression of chunks
 *
 * Compresses chunks using the compression engine selected by worker_id.
 * Uses adaptive compression to skip incompressible data.
 * On success, passes the (possibly compressed) chunk to the send stage.
 */
class compress_job : public pipeline_job_base {
public:
    /**
     * @brief Construct a compress job
     * @param context Shared pipeline context
     * @param chunk Chunk to compress
     * @param worker_id Worker ID for selecting compression engine
     */
    compress_job(std::shared_ptr<pipeline_context> context,
                 pipeline_chunk chunk,
                 std::size_t worker_id);

    /**
     * @brief Execute the compression work
     * @return Success or error result
     */
    [[nodiscard]] auto do_work() -> kcenon::common::VoidResult override;

    /**
     * @brief Get the processed chunk after job completion
     * @return The (possibly compressed) chunk
     */
    [[nodiscard]] auto get_chunk() const -> const pipeline_chunk&;

private:
    pipeline_chunk chunk_;
    std::size_t worker_id_;
};

/**
 * @brief Job for network send preparation
 *
 * Prepares chunks for network transmission. This is the final stage
 * of the download pipeline. Applies bandwidth limiting if configured.
 */
class send_job : public pipeline_job_base {
public:
    /**
     * @brief Construct a send job
     * @param chunk Chunk to send
     * @param context Shared pipeline context
     */
    send_job(pipeline_chunk chunk, std::shared_ptr<pipeline_context> context);

    /**
     * @brief Execute the send preparation work
     * @return Success or error result
     */
    [[nodiscard]] auto do_work() -> kcenon::common::VoidResult override;

    /**
     * @brief Get the processed chunk after job completion
     * @return The chunk ready for sending
     */
    [[nodiscard]] auto get_chunk() const -> const pipeline_chunk&;

private:
    pipeline_chunk chunk_;
};

/**
 * @brief Job for encrypting chunks
 *
 * Encrypts chunks using the configured encryption engine.
 * Used in download pipeline: file_read -> encrypt -> compress -> network_send
 */
class encrypt_job : public pipeline_job_base {
public:
    /**
     * @brief Construct an encrypt job
     * @param context Shared pipeline context
     * @param chunk Chunk to encrypt
     * @param worker_id Worker ID for selecting encryption engine
     */
    encrypt_job(std::shared_ptr<pipeline_context> context,
                pipeline_chunk chunk,
                std::size_t worker_id);

    /**
     * @brief Execute the encryption work
     * @return Success or error result
     */
    [[nodiscard]] auto do_work() -> kcenon::common::VoidResult override;

    /**
     * @brief Get the processed chunk after job completion
     * @return The encrypted chunk
     */
    [[nodiscard]] auto get_chunk() const -> const pipeline_chunk&;

private:
    pipeline_chunk chunk_;
    std::size_t worker_id_;
};

/**
 * @brief Job for decrypting chunks
 *
 * Decrypts chunks using the configured encryption engine.
 * Used in upload pipeline: network_recv -> decompress -> decrypt -> verify -> write
 */
class decrypt_job : public pipeline_job_base {
public:
    /**
     * @brief Construct a decrypt job
     * @param context Shared pipeline context
     * @param chunk Chunk to decrypt
     * @param worker_id Worker ID for selecting encryption engine
     */
    decrypt_job(std::shared_ptr<pipeline_context> context,
                pipeline_chunk chunk,
                std::size_t worker_id);

    /**
     * @brief Execute the decryption work
     * @return Success or error result
     */
    [[nodiscard]] auto do_work() -> kcenon::common::VoidResult override;

    /**
     * @brief Get the processed chunk after job completion
     * @return The decrypted chunk
     */
    [[nodiscard]] auto get_chunk() const -> const pipeline_chunk&;

private:
    pipeline_chunk chunk_;
    std::size_t worker_id_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_SERVER_PIPELINE_JOBS_H
