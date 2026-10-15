/**
 * @file pipeline_jobs.cpp
 * @brief Implementation of pipeline job types for thread_system integration
 */

#include "kcenon/file_transfer/server/pipeline_jobs.h"

#include "kcenon/file_transfer/core/bandwidth_limiter.h"
#include "kcenon/file_transfer/core/checksum.h"
#include "kcenon/file_transfer/core/protocol_codec.h"
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
                                     std::shared_ptr<pipeline_context> context, pipeline_stage stage)
    : kcenon::thread::job(name), stage_(stage), context_(std::move(context)) {
    ++context_->queued[static_cast<std::size_t>(stage_)];
}
pipeline_job_base::~pipeline_job_base() {
    if (queued_) --context_->queued[static_cast<std::size_t>(stage_)];
}
auto pipeline_job_base::begin_work() -> detail::pipeline_execution_scope {
    if (queued_) { --context_->queued[static_cast<std::size_t>(stage_)]; queued_ = false; }
    return detail::pipeline_execution_scope(context_.get());
}

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
    : pipeline_job_base("decompress_job", std::move(context), pipeline_stage::decompress)
    , chunk_(std::move(chunk))
    , worker_id_(worker_id) {}

auto decompress_job::do_work() -> common::VoidResult {
    auto execution = begin_work();
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

    if (context_->thread_pool) {
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
    : pipeline_job_base("verify_job", std::move(context), pipeline_stage::chunk_verify), chunk_(std::move(chunk)) {}

auto verify_job::do_work() -> common::VoidResult {
    auto execution = begin_work();
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
    if (context_->thread_pool) {
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
    : pipeline_job_base("write_job", std::move(context), pipeline_stage::file_write), chunk_(std::move(chunk)) {}

auto write_job::do_work() -> common::VoidResult {
    auto execution = begin_work();
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
                   std::size_t size,
                   std::shared_ptr<detail::pipeline_reservation> admission)
    : pipeline_job_base("read_job", std::move(context), pipeline_stage::file_read)
    , id_(id)
    , chunk_index_(chunk_index)
    , file_path_(std::move(file_path))
    , offset_(offset)
    , size_(size) { chunk_.admission_ = std::move(admission); }

auto read_job::do_work() -> common::VoidResult {
    auto execution = begin_work();
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

    auto worker_id = static_cast<std::size_t>(chunk_index_);
    if (context_->thread_pool) {
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
    : pipeline_job_base("compress_job", std::move(context), pipeline_stage::compress)
    , chunk_(std::move(chunk))
    , worker_id_(worker_id) {}

auto compress_job::do_work() -> common::VoidResult {
    auto execution = begin_work();
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

    if (context_->encryption_enabled) {
        auto next = std::make_unique<encrypt_job>(context_, std::move(chunk_), worker_id_);
        auto queued = context_->thread_pool->enqueue(std::move(next));
        if (!queued.is_ok()) return thread::make_error_result(thread::error_code::queue_full, "Cannot enqueue encryption");
    } else if (context_->thread_pool) {
        auto next = std::make_unique<send_job>(std::move(chunk_), context_);
        auto queued = context_->thread_pool->enqueue(std::move(next));
        if (!queued.is_ok()) return thread::make_error_result(thread::error_code::queue_full, "Cannot enqueue send");
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
    : pipeline_job_base("send_job", std::move(context), pipeline_stage::network_send), chunk_(std::move(chunk)) {}

auto send_job::do_work() -> common::VoidResult {
    auto execution = begin_work();
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
        auto remaining = chunk_.data.size();
        while (remaining && context_->send_limiter->is_enabled()) {
            if (is_cancelled() || !context_->is_running())
                return thread::make_error_result(thread::error_code::operation_canceled, "Send interrupted");
            auto step = std::min(remaining, std::max<std::size_t>(1, context_->send_limiter->bucket_capacity()));
            if (context_->send_limiter->try_acquire(step)) remaining -= step;
            else std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
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
    : pipeline_job_base("encrypt_job", std::move(context), pipeline_stage::encrypt)
    , chunk_(std::move(chunk))
    , worker_id_(worker_id) {}

auto encrypt_job::do_work() -> common::VoidResult {
    auto execution = begin_work();
    auto fail = [&](const std::string& message) {
        context_->report_error(pipeline_stage::encrypt, message);
        return thread::make_error_result(thread::error_code::job_execution_failed, message);
    };
    if (is_cancelled() || !context_->is_running()) return fail("Encryption job interrupted");
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    auto engine = context_->encryption_provider ? context_->encryption_provider(chunk_.id) : nullptr;
    if (!engine || !engine->has_key()) return fail("No valid encryption key for transfer");
    auto size = static_cast<uint64_t>(chunk_.original_size);
    auto compressed = static_cast<uint8_t>(chunk_.is_compressed);
    wire::writer writer; writer(chunk_.id.bytes, chunk_.chunk_index, chunk_.checksum, size, compressed);
    std::vector<std::byte> aad; for (auto b : writer.data) aad.push_back(std::byte{b});
    auto transformed = engine->encrypt(chunk_.data, aad);
    if (!transformed) return fail(transformed.error().message);
    chunk_.data = std::move(transformed.value().ciphertext);
    chunk_.enc_metadata = std::make_unique<encryption_metadata>(std::move(transformed.value().metadata));
    chunk_.is_encrypted = true;
    if (context_->statistics) { ++context_->statistics->chunks_encrypted; context_->statistics->encryption_bytes += chunk_.data.size(); }
#else
    return fail("Encryption support is disabled");
#endif
    context_->report_stage_complete(pipeline_stage::encrypt, chunk_);
    if (context_->thread_pool) {
        auto next = std::make_unique<send_job>(std::move(chunk_), context_);
        if (!context_->thread_pool->enqueue(std::move(next)).is_ok()) return fail("Cannot enqueue next stage");
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
    : pipeline_job_base("decrypt_job", std::move(context), pipeline_stage::decrypt)
    , chunk_(std::move(chunk))
    , worker_id_(worker_id) {}

auto decrypt_job::do_work() -> common::VoidResult {
    auto execution = begin_work();
    auto fail = [&](const std::string& message) {
        context_->report_error(pipeline_stage::decrypt, message);
        return thread::make_error_result(thread::error_code::job_execution_failed, message);
    };
    if (is_cancelled() || !context_->is_running()) return fail("Encryption job interrupted");
#ifdef FILE_TRANS_ENABLE_ENCRYPTION
    auto engine = context_->encryption_provider ? context_->encryption_provider(chunk_.id) : nullptr;
    if (!engine || !engine->has_key()) return fail("No valid encryption key for transfer");
    auto size = static_cast<uint64_t>(chunk_.original_size);
    auto compressed = static_cast<uint8_t>(chunk_.is_compressed);
    wire::writer writer; writer(chunk_.id.bytes, chunk_.chunk_index, chunk_.checksum, size, compressed);
    std::vector<std::byte> aad; for (auto b : writer.data) aad.push_back(std::byte{b});
    if (!chunk_.is_encrypted || !chunk_.enc_metadata) return fail("Missing encrypted chunk metadata");
    auto metadata = *chunk_.enc_metadata; metadata.aad = aad;
    auto transformed = engine->decrypt(chunk_.data, metadata);
    if (!transformed) return fail(transformed.error().message);
    chunk_.data = std::move(transformed.value().plaintext);
    chunk_.enc_metadata.reset(); chunk_.is_encrypted = false;
    if (context_->statistics) ++context_->statistics->chunks_decrypted;
#else
    return fail("Encryption support is disabled");
#endif
    context_->report_stage_complete(pipeline_stage::decrypt, chunk_);
    if (context_->thread_pool) {
        auto next = std::make_unique<decompress_job>(context_, std::move(chunk_), worker_id_);
        if (!context_->thread_pool->enqueue(std::move(next)).is_ok()) return fail("Cannot enqueue next stage");
    }
    return common::ok();
}

auto decrypt_job::get_chunk() const -> const pipeline_chunk& {
    return chunk_;
}

}  // namespace kcenon::file_transfer
