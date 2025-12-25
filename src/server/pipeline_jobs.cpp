/**
 * @file pipeline_jobs.cpp
 * @brief Implementation of pipeline job types for thread_system integration
 */

#include "kcenon/file_transfer/server/pipeline_jobs.h"

#include "kcenon/file_transfer/core/bandwidth_limiter.h"
#include "kcenon/file_transfer/core/checksum.h"
#include "kcenon/file_transfer/core/compression_engine.h"
#include "kcenon/file_transfer/core/logging.h"
#include "kcenon/file_transfer/encryption/encryption_config.h"

#ifdef FILE_TRANS_ENABLE_ENCRYPTION
#include "kcenon/file_transfer/encryption/encryption_interface.h"
#endif

#include <kcenon/thread/core/error_handling.h>

#include <fstream>
#include <span>

namespace kcenon::file_transfer {

// ----------------------------------------------------------------------------
// pipeline_job_base implementation
// ----------------------------------------------------------------------------

pipeline_job_base::pipeline_job_base(const std::string& name,
                                     std::shared_ptr<pipeline_context> context)
    : kcenon::thread::job(name), context_(std::move(context)) {}

auto pipeline_job_base::is_cancelled() const -> bool {
    return cancellation_token_.is_cancelled();
}

auto pipeline_job_base::context() const -> const pipeline_context& {
    return *context_;
}

// ----------------------------------------------------------------------------
// decompress_job implementation
// ----------------------------------------------------------------------------

decompress_job::decompress_job(std::shared_ptr<pipeline_context> context,
                               pipeline_chunk chunk,
                               std::size_t worker_id)
    : pipeline_job_base("decompress_job", std::move(context))
    , chunk_(std::move(chunk))
    , worker_id_(worker_id) {}

auto decompress_job::do_work() -> common::VoidResult {
    if (is_cancelled()) {
        return thread::make_error_result(
            thread::error_code::operation_canceled,
            "Decompress job cancelled");
    }

    if (!context_->is_running()) {
        return common::ok();
    }

    FT_LOG_TRACE(log_category::pipeline,
                 "Processing chunk " + std::to_string(chunk_.chunk_index) +
                     " in decompress stage");

    if (chunk_.is_compressed) {
        auto& engine = context_->compression_engines[
            worker_id_ % context_->compression_engines.size()];

        auto result = engine->decompress(std::span<const std::byte>(chunk_.data),
                                         chunk_.original_size);

        if (result.has_value()) {
            chunk_.data = std::move(result.value());
            chunk_.is_compressed = false;

            if (context_->statistics) {
                context_->statistics->compression_saved_bytes +=
                    chunk_.original_size - chunk_.data.size();
            }

            FT_LOG_TRACE(log_category::pipeline,
                         "Chunk " + std::to_string(chunk_.chunk_index) + " decompressed");
        } else {
            auto error_msg = "Decompression failed for chunk " +
                             std::to_string(chunk_.chunk_index) + ": " +
                             result.error().message;
            FT_LOG_ERROR(log_category::pipeline, error_msg);
            context_->report_error(pipeline_stage::decompress, result.error().message);

            return thread::make_error_result(
                thread::error_code::job_execution_failed,
                error_msg);
        }
    }

    context_->report_stage_complete(pipeline_stage::decompress, chunk_);

    // Submit to next stage
    // If encryption is enabled and chunk is encrypted, go to decrypt stage
    // Otherwise, go directly to verify stage
    if (context_->encryption_enabled && chunk_.is_encrypted && context_->decrypt_queue) {
        auto decrypt = std::make_unique<decrypt_job>(
            context_, std::move(chunk_), worker_id_);
        auto enqueue_result = context_->thread_pool->enqueue(std::move(decrypt));
        if (!enqueue_result.is_ok()) {
            return thread::make_error_result(
                thread::error_code::queue_full,
                "Failed to enqueue decrypt job");
        }
    } else if (context_->verify_queue) {
        auto verify = std::make_unique<verify_job>(std::move(chunk_), context_);
        auto enqueue_result = context_->thread_pool->enqueue(std::move(verify));
        if (!enqueue_result.is_ok()) {
            return thread::make_error_result(
                thread::error_code::queue_full,
                "Failed to enqueue verify job");
        }
    }

    return common::ok();
}

auto decompress_job::get_chunk() const -> const pipeline_chunk& {
    return chunk_;
}

// ----------------------------------------------------------------------------
// verify_job implementation
// ----------------------------------------------------------------------------

verify_job::verify_job(pipeline_chunk chunk, std::shared_ptr<pipeline_context> context)
    : pipeline_job_base("verify_job", std::move(context)), chunk_(std::move(chunk)) {}

auto verify_job::do_work() -> common::VoidResult {
    if (is_cancelled()) {
        return thread::make_error_result(
            thread::error_code::operation_canceled,
            "Verify job cancelled");
    }

    if (!context_->is_running()) {
        return common::ok();
    }

    FT_LOG_TRACE(log_category::pipeline,
                 "Verifying chunk " + std::to_string(chunk_.chunk_index));

    // Verify CRC32 checksum
    auto calculated = checksum::crc32(std::span<const std::byte>(chunk_.data));
    if (calculated != chunk_.checksum) {
        auto error_msg = "Checksum mismatch for chunk " +
                         std::to_string(chunk_.chunk_index) + " (expected: " +
                         std::to_string(chunk_.checksum) +
                         ", got: " + std::to_string(calculated) + ")";
        FT_LOG_ERROR(log_category::pipeline, error_msg);
        context_->report_error(pipeline_stage::chunk_verify, error_msg);

        return thread::make_error_result(
            thread::error_code::job_execution_failed,
            error_msg);
    }

    if (context_->statistics) {
        context_->statistics->chunks_processed++;
        context_->statistics->bytes_processed += chunk_.data.size();
    }

    FT_LOG_TRACE(log_category::pipeline,
                 "Chunk " + std::to_string(chunk_.chunk_index) + " verified successfully");

    context_->report_stage_complete(pipeline_stage::chunk_verify, chunk_);

    // Submit to next stage (write queue)
    if (context_->write_queue) {
        auto write = std::make_unique<write_job>(std::move(chunk_), context_);
        auto enqueue_result = context_->thread_pool->enqueue(std::move(write));
        if (!enqueue_result.is_ok()) {
            return thread::make_error_result(
                thread::error_code::queue_full,
                "Failed to enqueue write job");
        }
    }

    return common::ok();
}

auto verify_job::get_chunk() const -> const pipeline_chunk& {
    return chunk_;
}

// ----------------------------------------------------------------------------
// write_job implementation
// ----------------------------------------------------------------------------

write_job::write_job(pipeline_chunk chunk, std::shared_ptr<pipeline_context> context)
    : pipeline_job_base("write_job", std::move(context)), chunk_(std::move(chunk)) {}

auto write_job::do_work() -> common::VoidResult {
    if (is_cancelled()) {
        return thread::make_error_result(
            thread::error_code::operation_canceled,
            "Write job cancelled");
    }

    if (!context_->is_running()) {
        return common::ok();
    }

    FT_LOG_TRACE(log_category::pipeline,
                 "Writing chunk " + std::to_string(chunk_.chunk_index) + " (" +
                     std::to_string(chunk_.data.size()) + " bytes)");

    // Note: Actual file writing is handled by the callback
    // This job prepares the chunk and notifies completion
    context_->report_stage_complete(pipeline_stage::file_write, chunk_);

    if (context_->on_upload_complete_cb) {
        context_->on_upload_complete_cb(chunk_.id, chunk_.data.size());
    }

    return common::ok();
}

auto write_job::get_chunk() const -> const pipeline_chunk& {
    return chunk_;
}

// ----------------------------------------------------------------------------
// read_job implementation
// ----------------------------------------------------------------------------

read_job::read_job(std::shared_ptr<pipeline_context> context,
                   const transfer_id& id,
                   uint64_t chunk_index,
                   std::filesystem::path file_path,
                   uint64_t offset,
                   std::size_t size)
    : pipeline_job_base("read_job", std::move(context))
    , id_(id)
    , chunk_index_(chunk_index)
    , file_path_(std::move(file_path))
    , offset_(offset)
    , size_(size) {}

auto read_job::do_work() -> common::VoidResult {
    if (is_cancelled()) {
        return thread::make_error_result(
            thread::error_code::operation_canceled,
            "Read job cancelled");
    }

    if (!context_->is_running()) {
        return common::ok();
    }

    FT_LOG_TRACE(log_category::pipeline,
                 "Reading chunk " + std::to_string(chunk_index_) + " from " +
                     file_path_.filename().string());

    std::ifstream file(file_path_, std::ios::binary);
    if (!file) {
        auto error_msg = "Failed to open file: " + file_path_.string();
        FT_LOG_ERROR(log_category::pipeline, error_msg);
        context_->report_error(pipeline_stage::file_read, error_msg);

        return thread::make_error_result(
            thread::error_code::job_execution_failed,
            error_msg);
    }

    file.seekg(static_cast<std::streamoff>(offset_));
    if (!file) {
        auto error_msg = "Failed to seek in file: " + file_path_.string();
        FT_LOG_ERROR(log_category::pipeline, error_msg);
        context_->report_error(pipeline_stage::file_read, error_msg);

        return thread::make_error_result(
            thread::error_code::job_execution_failed,
            error_msg);
    }

    chunk_.id = id_;
    chunk_.chunk_index = chunk_index_;
    chunk_.data.resize(size_);
    chunk_.is_compressed = false;
    chunk_.original_size = size_;

    file.read(reinterpret_cast<char*>(chunk_.data.data()),
              static_cast<std::streamsize>(size_));

    auto bytes_read = static_cast<std::size_t>(file.gcount());
    if (bytes_read < size_) {
        chunk_.data.resize(bytes_read);
        chunk_.original_size = bytes_read;
    }

    chunk_.checksum = checksum::crc32(std::span<const std::byte>(chunk_.data));

    FT_LOG_TRACE(log_category::pipeline,
                 "Chunk " + std::to_string(chunk_index_) + " read (" +
                     std::to_string(bytes_read) + " bytes)");

    context_->report_stage_complete(pipeline_stage::file_read, chunk_);

    // Submit to next stage
    // If encryption is enabled, go to encrypt stage first
    // Otherwise, go directly to compress stage
    auto worker_id = static_cast<std::size_t>(chunk_index_);

    if (context_->encryption_enabled && context_->encrypt_queue) {
        auto encrypt = std::make_unique<encrypt_job>(
            context_, std::move(chunk_), worker_id);
        auto enqueue_result = context_->thread_pool->enqueue(std::move(encrypt));
        if (!enqueue_result.is_ok()) {
            return thread::make_error_result(
                thread::error_code::queue_full,
                "Failed to enqueue encrypt job");
        }
    } else if (context_->compress_queue) {
        auto compress = std::make_unique<compress_job>(
            context_, std::move(chunk_), worker_id);
        auto enqueue_result = context_->thread_pool->enqueue(std::move(compress));
        if (!enqueue_result.is_ok()) {
            return thread::make_error_result(
                thread::error_code::queue_full,
                "Failed to enqueue compress job");
        }
    }

    return common::ok();
}

auto read_job::get_chunk() const -> const pipeline_chunk& {
    return chunk_;
}

// ----------------------------------------------------------------------------
// compress_job implementation
// ----------------------------------------------------------------------------

compress_job::compress_job(std::shared_ptr<pipeline_context> context,
                           pipeline_chunk chunk,
                           std::size_t worker_id)
    : pipeline_job_base("compress_job", std::move(context))
    , chunk_(std::move(chunk))
    , worker_id_(worker_id) {}

auto compress_job::do_work() -> common::VoidResult {
    if (is_cancelled()) {
        return thread::make_error_result(
            thread::error_code::operation_canceled,
            "Compress job cancelled");
    }

    if (!context_->is_running()) {
        return common::ok();
    }

    FT_LOG_TRACE(log_category::pipeline,
                 "Compressing chunk " + std::to_string(chunk_.chunk_index));

    auto& engine = context_->compression_engines[
        worker_id_ % context_->compression_engines.size()];

    // Check if compression would be beneficial
    if (engine->is_compressible(std::span<const std::byte>(chunk_.data))) {
        auto result = engine->compress(std::span<const std::byte>(chunk_.data));
        if (result.has_value() && result.value().size() < chunk_.data.size()) {
            auto original = chunk_.data.size();
            chunk_.original_size = chunk_.data.size();
            chunk_.data = std::move(result.value());
            chunk_.is_compressed = true;

            if (context_->statistics) {
                context_->statistics->compression_saved_bytes +=
                    chunk_.original_size - chunk_.data.size();
            }

            FT_LOG_TRACE(log_category::pipeline,
                         "Chunk " + std::to_string(chunk_.chunk_index) +
                             " compressed: " + std::to_string(original) + " -> " +
                             std::to_string(chunk_.data.size()) + " bytes");
        }
    } else {
        FT_LOG_TRACE(log_category::pipeline,
                     "Chunk " + std::to_string(chunk_.chunk_index) +
                         " skipped compression (not compressible)");
    }

    context_->report_stage_complete(pipeline_stage::compress, chunk_);

    // Submit to next stage (send queue)
    if (context_->send_queue) {
        auto send = std::make_unique<send_job>(std::move(chunk_), context_);
        auto enqueue_result = context_->thread_pool->enqueue(std::move(send));
        if (!enqueue_result.is_ok()) {
            return thread::make_error_result(
                thread::error_code::queue_full,
                "Failed to enqueue send job");
        }
    }

    return common::ok();
}

auto compress_job::get_chunk() const -> const pipeline_chunk& {
    return chunk_;
}

// ----------------------------------------------------------------------------
// send_job implementation
// ----------------------------------------------------------------------------

send_job::send_job(pipeline_chunk chunk, std::shared_ptr<pipeline_context> context)
    : pipeline_job_base("send_job", std::move(context)), chunk_(std::move(chunk)) {}

auto send_job::do_work() -> common::VoidResult {
    if (is_cancelled()) {
        return thread::make_error_result(
            thread::error_code::operation_canceled,
            "Send job cancelled");
    }

    if (!context_->is_running()) {
        return common::ok();
    }

    FT_LOG_TRACE(log_category::pipeline,
                 "Sending chunk " + std::to_string(chunk_.chunk_index) + " (" +
                     std::to_string(chunk_.data.size()) + " bytes)");

    // Apply send bandwidth limit if configured
    if (context_->send_limiter) {
        context_->send_limiter->acquire(chunk_.data.size());
    }

    if (context_->statistics) {
        context_->statistics->chunks_processed++;
        context_->statistics->bytes_processed += chunk_.data.size();
    }

    context_->report_stage_complete(pipeline_stage::network_send, chunk_);

    if (context_->on_download_ready_cb) {
        context_->on_download_ready_cb(chunk_);
    }

    return common::ok();
}

auto send_job::get_chunk() const -> const pipeline_chunk& {
    return chunk_;
}

// ----------------------------------------------------------------------------
// encrypt_job implementation
// ----------------------------------------------------------------------------

encrypt_job::encrypt_job(std::shared_ptr<pipeline_context> context,
                         pipeline_chunk chunk,
                         std::size_t worker_id)
    : pipeline_job_base("encrypt_job", std::move(context))
    , chunk_(std::move(chunk))
    , worker_id_(worker_id) {}

auto encrypt_job::do_work() -> common::VoidResult {
    if (is_cancelled()) {
        return thread::make_error_result(
            thread::error_code::operation_canceled,
            "Encrypt job cancelled");
    }

    if (!context_->is_running()) {
        return common::ok();
    }

    FT_LOG_TRACE(log_category::pipeline,
                 "Encrypting chunk " + std::to_string(chunk_.chunk_index));

#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    if (!context_->encryption_engines.empty()) {
        auto& engine = context_->encryption_engines[
            worker_id_ % context_->encryption_engines.size()];

        if (engine && engine->has_key()) {
            auto result = engine->encrypt_chunk(
                std::span<const std::byte>(chunk_.data),
                chunk_.chunk_index);

            if (result.has_value()) {
                auto& enc_result = result.value();
                chunk_.data = std::move(enc_result.ciphertext);
                chunk_.is_encrypted = true;
                chunk_.enc_metadata = std::make_unique<encryption_metadata>(
                    std::move(enc_result.metadata));

                if (context_->statistics) {
                    context_->statistics->chunks_encrypted++;
                    context_->statistics->encryption_bytes += chunk_.data.size();
                }

                FT_LOG_TRACE(log_category::pipeline,
                             "Chunk " + std::to_string(chunk_.chunk_index) +
                                 " encrypted successfully");
            } else {
                auto error_msg = "Encryption failed for chunk " +
                                 std::to_string(chunk_.chunk_index) + ": " +
                                 result.error().message;
                FT_LOG_ERROR(log_category::pipeline, error_msg);
                context_->report_error(pipeline_stage::encrypt, result.error().message);

                return thread::make_error_result(
                    thread::error_code::job_execution_failed,
                    error_msg);
            }
        }
    }
#endif

    context_->report_stage_complete(pipeline_stage::encrypt, chunk_);

    // Submit to next stage (compress queue)
    if (context_->compress_queue) {
        auto compress = std::make_unique<compress_job>(
            context_, std::move(chunk_), worker_id_);
        auto enqueue_result = context_->thread_pool->enqueue(std::move(compress));
        if (!enqueue_result.is_ok()) {
            return thread::make_error_result(
                thread::error_code::queue_full,
                "Failed to enqueue compress job");
        }
    }

    return common::ok();
}

auto encrypt_job::get_chunk() const -> const pipeline_chunk& {
    return chunk_;
}

// ----------------------------------------------------------------------------
// decrypt_job implementation
// ----------------------------------------------------------------------------

decrypt_job::decrypt_job(std::shared_ptr<pipeline_context> context,
                         pipeline_chunk chunk,
                         std::size_t worker_id)
    : pipeline_job_base("decrypt_job", std::move(context))
    , chunk_(std::move(chunk))
    , worker_id_(worker_id) {}

auto decrypt_job::do_work() -> common::VoidResult {
    if (is_cancelled()) {
        return thread::make_error_result(
            thread::error_code::operation_canceled,
            "Decrypt job cancelled");
    }

    if (!context_->is_running()) {
        return common::ok();
    }

    FT_LOG_TRACE(log_category::pipeline,
                 "Decrypting chunk " + std::to_string(chunk_.chunk_index));

#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    if (!context_->encryption_engines.empty() && chunk_.enc_metadata) {
        auto& engine = context_->encryption_engines[
            worker_id_ % context_->encryption_engines.size()];

        if (engine && engine->has_key()) {
            auto result = engine->decrypt_chunk(
                std::span<const std::byte>(chunk_.data),
                *chunk_.enc_metadata,
                chunk_.chunk_index);

            if (result.has_value()) {
                chunk_.data = std::move(result.value().plaintext);
                chunk_.is_encrypted = false;
                chunk_.enc_metadata.reset();

                if (context_->statistics) {
                    context_->statistics->chunks_decrypted++;
                }

                FT_LOG_TRACE(log_category::pipeline,
                             "Chunk " + std::to_string(chunk_.chunk_index) +
                                 " decrypted successfully");
            } else {
                auto error_msg = "Decryption failed for chunk " +
                                 std::to_string(chunk_.chunk_index) + ": " +
                                 result.error().message;
                FT_LOG_ERROR(log_category::pipeline, error_msg);
                context_->report_error(pipeline_stage::decrypt, result.error().message);

                return thread::make_error_result(
                    thread::error_code::job_execution_failed,
                    error_msg);
            }
        }
    }
#else
    // If encryption is not compiled in, just pass through
    chunk_.is_encrypted = false;
#endif

    context_->report_stage_complete(pipeline_stage::decrypt, chunk_);

    // Submit to next stage (verify queue)
    if (context_->verify_queue) {
        auto verify = std::make_unique<verify_job>(std::move(chunk_), context_);
        auto enqueue_result = context_->thread_pool->enqueue(std::move(verify));
        if (!enqueue_result.is_ok()) {
            return thread::make_error_result(
                thread::error_code::queue_full,
                "Failed to enqueue verify job");
        }
    }

    return common::ok();
}

auto decrypt_job::get_chunk() const -> const pipeline_chunk& {
    return chunk_;
}

}  // namespace kcenon::file_transfer
