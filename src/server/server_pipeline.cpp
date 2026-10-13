/**
 * @file server_pipeline.cpp
 * @brief Server-side upload/download pipeline implementation using thread_system
 */

#include "kcenon/file_transfer/server/server_pipeline.h"
#include "kcenon/file_transfer/server/pipeline_jobs.h"

#include "kcenon/file_transfer/core/bandwidth_limiter.h"
#include "kcenon/file_transfer/core/checksum.h"
#include "kcenon/file_transfer/core/compression_engine.h"
#include "kcenon/file_transfer/core/logging.h"
#include "kcenon/file_transfer/encryption/encryption_config.h"

#ifdef FILE_TRANS_ENABLE_ENCRYPTION
#include "kcenon/file_transfer/encryption/aes_gcm_engine.h"
#endif

#include <kcenon/thread/core/thread_pool.h>
#include <kcenon/thread/core/job_queue.h>

#include <filesystem>
#include <fstream>
#include <thread>
#include <limits>
#include <vector>

namespace kcenon::file_transfer {

struct server_pipeline::impl {
    pipeline_config config;
    std::atomic<bool> running{false};
    std::atomic<bool> accepting{false};
    std::mutex lifecycle_mutex;
    pipeline_stats statistics;
    std::shared_ptr<detail::pipeline_admission> admission;

    // Thread pool for job execution
    std::shared_ptr<thread::thread_pool> thread_pool;

    // Pipeline context shared with jobs
    std::shared_ptr<pipeline_context> context;

    // Bandwidth limiters
    std::unique_ptr<bandwidth_limiter> send_limiter;
    std::unique_ptr<bandwidth_limiter> recv_limiter;

    // Worker ID counter for round-robin assignment
    std::atomic<std::size_t> next_worker_id{0};

    explicit impl(pipeline_config cfg) : config(std::move(cfg)) {
        // Create thread pool
        auto total_threads = config.worker_threads ? config.worker_threads :
            config.io_workers + config.compression_workers + config.network_workers;
        admission = std::make_shared<detail::pipeline_admission>(config, statistics);
        thread_pool = std::make_shared<thread::thread_pool>(
            "server_pipeline_pool");

        // Create pipeline context
        context = std::make_shared<pipeline_context>();
        context->thread_pool = thread_pool;

        // Create compression engines for workers
        auto total_compression_workers = config.compression_workers;
        context->compression_engines.reserve(total_compression_workers);
        for (std::size_t i = 0; i < total_compression_workers; ++i) {
            context->compression_engines.push_back(
                std::make_unique<compression_engine>(compression_level::fast));
        }

        context->encryption_enabled = config.enable_encryption;
        context->encryption_provider = config.encryption_provider;

        // Set statistics pointer in context
        context->statistics = &statistics;
        context->running = &running;

        // Initialize bandwidth limiters if configured
        if (config.send_bandwidth_limit > 0) {
            send_limiter = std::make_unique<bandwidth_limiter>(
                config.send_bandwidth_limit);
            context->send_limiter = send_limiter.get();
        }
        if (config.recv_bandwidth_limit > 0) {
            recv_limiter = std::make_unique<bandwidth_limiter>(
                config.recv_bandwidth_limit);
            context->recv_limiter = recv_limiter.get();
        }

        // Add workers to thread pool
        for (std::size_t i = 0; i < total_threads; ++i) {
            auto worker = std::make_unique<thread::thread_worker>();
            worker->set_job_queue(thread_pool->get_job_queue());
            thread_pool->enqueue(std::move(worker));
        }
    }

    ~impl() {
        accepting = false;
        admission->close();
        running = false;
        if (thread_pool) {
            (void)thread_pool->stop(true);
            thread_pool->get_job_queue()->clear();
        }
        admission->drain();
        context->thread_pool.reset();
    }

    auto start() -> common::VoidResult {
        auto started = thread_pool->start();
        if (started.is_ok()) { admission->open(); accepting = true; }
        return started;
    }

    auto stop(bool wait_for_completion) -> void {
        accepting = false;
        admission->close(); // Wake blocked producers before waiting for workers.
        if (wait_for_completion) admission->drain();
        running = false;
        (void)thread_pool->stop(true);
        thread_pool->get_job_queue()->clear(); // Release every pending token.
        admission->drain();
    }

    auto reserve(const transfer_id& id, std::size_t largest_buffer,
                 std::chrono::milliseconds timeout, std::stop_token cancellation)
        -> result<std::shared_ptr<detail::pipeline_reservation>> {
        // Original/input plus transformed output and crypto/compression scratch,
        // with a fixed allowance for metadata and at most two overlapping jobs.
        if (largest_buffer > (std::numeric_limits<std::size_t>::max() - 4096) / 4)
            return unexpected{error{error_code::invalid_configuration, "Chunk size overflows memory budget"}};
        if (detail::executing_pipeline == context.get() && timeout != std::chrono::milliseconds(0))
            return unexpected{error{error_code::invalid_state_transition, "Use nonblocking submission from pipeline callbacks"}};
        return admission->reserve(id, 4 * largest_buffer + 4096, timeout, cancellation);
    }

    auto get_worker_id() -> std::size_t {
        return next_worker_id.fetch_add(1, std::memory_order_relaxed);
    }
};

// pipeline_config implementation
auto pipeline_config::auto_detect() -> pipeline_config {
    pipeline_config config;

    auto hw_threads = std::thread::hardware_concurrency();
    if (hw_threads == 0) hw_threads = 4;

    // Distribute workers based on typical workload
    // Compression is CPU-bound, so give it more threads
    config.compression_workers = std::max(2u, hw_threads / 2);
    config.io_workers = std::max(2u, hw_threads / 4);
    config.network_workers = std::max(2u, hw_threads / 4);

    // Queue size based on available memory (heuristic)
    config.queue_size = 64;
    config.max_memory_per_transfer = 32 * 1024 * 1024;  // 32MB

    return config;
}

auto pipeline_config::is_valid() const -> bool {
    return io_workers > 0 &&
           compression_workers > 0 &&
           network_workers > 0 &&
           queue_size > 0 &&
           max_memory_per_transfer > 0 && max_buffered_bytes > 0 && worker_threads <= 256;
}

// pipeline_stats implementation
auto pipeline_stats::reset() -> void {
    chunks_processed = 0;
    bytes_processed = 0;
    compression_saved_bytes = 0;
    stalls_detected = 0;
    backpressure_events = 0;
    peak_admitted_chunks = admitted_chunks.load();
    peak_reserved_bytes = reserved_bytes.load();
    chunks_encrypted = 0;
    chunks_decrypted = 0;
    encryption_bytes = 0;
}

// pipeline_chunk implementation
pipeline_chunk::pipeline_chunk(const chunk& c)
    : id(c.header.id)
    , chunk_index(c.header.chunk_index)
    , data(c.data)
    , checksum(c.header.checksum)
    , is_compressed(c.is_compressed())
    , original_size(c.header.original_size)
    , is_encrypted(kcenon::file_transfer::is_encrypted(c.header.flags))
    , enc_metadata(nullptr) {}

pipeline_chunk::pipeline_chunk(const pipeline_chunk& other)
    : id(other.id)
    , chunk_index(other.chunk_index)
    , data(other.data)
    , checksum(other.checksum)
    , is_compressed(other.is_compressed)
    , original_size(other.original_size)
    , is_encrypted(other.is_encrypted)
    , enc_metadata(other.enc_metadata
        ? std::make_unique<encryption_metadata>(*other.enc_metadata)
        : nullptr) {}

auto pipeline_chunk::operator=(const pipeline_chunk& other) -> pipeline_chunk& {
    if (this != &other) {
        id = other.id;
        chunk_index = other.chunk_index;
        data = other.data;
        checksum = other.checksum;
        is_compressed = other.is_compressed;
        original_size = other.original_size;
        is_encrypted = other.is_encrypted;
        enc_metadata = other.enc_metadata
            ? std::make_unique<encryption_metadata>(*other.enc_metadata)
            : nullptr;
    }
    return *this;
}

// stage_result implementation
auto stage_result::ok(pipeline_chunk c) -> stage_result {
    return {true, std::move(c), {}};
}

auto stage_result::fail(const std::string& msg) -> stage_result {
    return {false, {}, msg};
}

// server_pipeline implementation
auto server_pipeline::create(const pipeline_config& config)
    -> result<server_pipeline> {
    if (!config.is_valid()) {
        return unexpected{error{error_code::invalid_configuration,
                               "Invalid pipeline configuration"}};
    }

    return server_pipeline{config};
}

server_pipeline::server_pipeline(pipeline_config config)
    : impl_(std::make_unique<impl>(std::move(config))) {}

server_pipeline::server_pipeline(server_pipeline&&) noexcept = default;
auto server_pipeline::operator=(server_pipeline&&) noexcept
    -> server_pipeline& = default;

server_pipeline::~server_pipeline() {
    if (impl_ && impl_->running) {
        (void)stop(false);
    }
}

auto server_pipeline::start() -> result<void> {
    std::lock_guard lock(impl_->lifecycle_mutex);
    if (impl_->running) {
        FT_LOG_WARN(log_category::pipeline, "Pipeline start failed: already running");
        return unexpected{error{error_code::already_initialized,
                               "Pipeline is already running"}};
    }

    if (impl_->config.enable_encryption) {
#ifndef FILE_TRANS_ENABLE_ENCRYPTION
        return unexpected{error{error_code::invalid_configuration, "Pipeline encryption support is disabled"}};
#else
        if (!impl_->config.encryption_provider)
            return unexpected{error{error_code::invalid_configuration, "Pipeline encryption requires a transfer key provider"}};
#endif
    }

    FT_LOG_INFO(log_category::pipeline,
        "Starting pipeline with " + std::to_string(impl_->config.io_workers) +
        " I/O workers, " + std::to_string(impl_->config.compression_workers) +
        " compression workers, " + std::to_string(impl_->config.network_workers) +
        " network workers");

    impl_->running = true;
    auto result = impl_->start();
    if (!result.is_ok()) {
        impl_->running = false;
        return unexpected{error{error_code::internal_error,
                               "Failed to start thread pool"}};
    }

    FT_LOG_INFO(log_category::pipeline, "Pipeline started successfully");
    return {};
}

auto server_pipeline::stop(bool wait_for_completion) -> result<void> {
    if (detail::executing_pipeline == impl_->context.get())
        return unexpected{error{error_code::invalid_state_transition, "Cannot join a pipeline from its callback"}};
    std::lock_guard lock(impl_->lifecycle_mutex);
    if (!impl_->running) {
        FT_LOG_WARN(log_category::pipeline, "Pipeline stop failed: not running");
        return unexpected{error{error_code::not_initialized,
                               "Pipeline is not running"}};
    }

    FT_LOG_INFO(log_category::pipeline,
        "Stopping pipeline (wait_for_completion=" +
        std::string(wait_for_completion ? "true" : "false") + ")");

    impl_->stop(wait_for_completion);

    FT_LOG_INFO(log_category::pipeline,
        "Pipeline stopped. Stats: " + std::to_string(impl_->statistics.chunks_processed) +
        " chunks processed, " + std::to_string(impl_->statistics.bytes_processed) +
        " bytes, " + std::to_string(impl_->statistics.compression_saved_bytes) +
        " bytes saved by compression");
    return {};
}

auto server_pipeline::is_running() const -> bool {
    return impl_->accepting;
}

auto server_pipeline::submit_upload_chunk(pipeline_chunk data) -> result<void> {
    return submit_upload_chunk(std::move(data), std::chrono::milliseconds::max());
}

auto server_pipeline::submit_upload_chunk(pipeline_chunk data,
    std::chrono::milliseconds timeout, std::stop_token cancellation) -> result<void> {
    if (!impl_->accepting)
        return unexpected{error{error_code::not_initialized, "Pipeline is not running"}};
    if (data.is_encrypted != impl_->config.enable_encryption)
        return unexpected{error{error_code::invalid_configuration, "Pipeline chunk encryption policy mismatch"}};
    auto largest = std::max(data.data.capacity(), data.original_size);
    if (data.enc_metadata) {
        for (auto capacity : {data.enc_metadata->iv.capacity(), data.enc_metadata->salt.capacity(),
                             data.enc_metadata->auth_tag.capacity(), data.enc_metadata->aad.capacity()}) {
            if (capacity > std::numeric_limits<std::size_t>::max() - largest)
                return unexpected{error{error_code::invalid_configuration, "Metadata size overflows memory budget"}};
            largest += capacity;
        }
    }
    auto deadline = timeout == std::chrono::milliseconds::max()
        ? std::chrono::steady_clock::time_point::max() : std::chrono::steady_clock::now() + timeout;
    auto admitted = impl_->reserve(data.id, largest, timeout, cancellation);
    if (!admitted) return unexpected{admitted.error()};
    data.admission_ = std::move(admitted.value());
    auto remaining = data.data.size();
    while (remaining && impl_->recv_limiter && impl_->recv_limiter->is_enabled()) {
        if (cancellation.stop_requested() || !impl_->admission->accepting())
            return unexpected{error{error_code::invalid_state_transition, "Pipeline submission interrupted"}};
        auto step = std::min(remaining, std::max<std::size_t>(1, impl_->recv_limiter->bucket_capacity()));
        if (impl_->recv_limiter->try_acquire(step)) remaining -= step;
        else {
            if (std::chrono::steady_clock::now() >= deadline)
                return unexpected{error{error_code::transfer_timeout, "Pipeline bandwidth wait timed out"}};
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    std::unique_ptr<pipeline_job_base> job;
    auto worker = impl_->get_worker_id();
    if (data.is_encrypted) job = std::make_unique<decrypt_job>(impl_->context, std::move(data), worker);
    else job = std::make_unique<decompress_job>(impl_->context, std::move(data), worker);
    if (!impl_->thread_pool->enqueue(std::move(job)).is_ok())
        return unexpected{error{error_code::not_initialized, "Pipeline stopped before enqueue"}};
    return {};
}

auto server_pipeline::try_submit_upload_chunk(pipeline_chunk data) -> bool {
    return submit_upload_chunk(std::move(data), std::chrono::milliseconds(0)).has_value();
}

auto server_pipeline::submit_download_request(const transfer_id& id, uint64_t chunk_index,
    const std::filesystem::path& file_path, uint64_t offset, std::size_t size) -> result<void> {
    return submit_download_request(id, chunk_index, file_path, offset, size, std::chrono::milliseconds::max());
}
auto server_pipeline::submit_download_request(const transfer_id& id, uint64_t chunk_index,
    const std::filesystem::path& file_path, uint64_t offset, std::size_t size,
    std::chrono::milliseconds timeout, std::stop_token cancellation) -> result<void> {
    if (!impl_->accepting)
        return unexpected{error{error_code::not_initialized, "Pipeline is not running"}};
    auto admitted = impl_->reserve(id, std::max(size, file_path.native().capacity() * sizeof(std::filesystem::path::value_type)), timeout, cancellation);
    if (!admitted) return unexpected{admitted.error()};
    auto job = std::make_unique<read_job>(impl_->context, id, chunk_index, file_path, offset, size,
                                         std::move(admitted.value()));
    if (!impl_->thread_pool->enqueue(std::move(job)).is_ok())
        return unexpected{error{error_code::not_initialized, "Pipeline stopped before enqueue"}};
    return {};
}

auto server_pipeline::on_stage_complete(stage_callback callback) -> void {
    impl_->context->on_stage_complete_cb = std::move(callback);
}

auto server_pipeline::on_error(error_callback callback) -> void {
    impl_->context->on_error_cb = std::move(callback);
}

auto server_pipeline::on_upload_complete(completion_callback callback) -> void {
    impl_->context->on_upload_complete_cb = std::move(callback);
}

auto server_pipeline::on_download_ready(
    std::function<void(const pipeline_chunk&)> callback) -> void {
    impl_->context->on_download_ready_cb = std::move(callback);
}

auto server_pipeline::stats() const -> const pipeline_stats& {
    return impl_->statistics;
}

auto server_pipeline::reset_stats() -> void {
    impl_->statistics.reset();
}

auto server_pipeline::queue_sizes() const
    -> std::vector<std::pair<pipeline_stage, std::size_t>> {
    std::vector<std::pair<pipeline_stage, std::size_t>> sizes;
    for (auto stage : {pipeline_stage::decompress, pipeline_stage::chunk_verify,
                      pipeline_stage::file_write, pipeline_stage::file_read,
                      pipeline_stage::compress, pipeline_stage::network_send})
        sizes.emplace_back(stage, impl_->context->queued[static_cast<std::size_t>(stage)].load());
    if (impl_->config.enable_encryption)
        for (auto stage : {pipeline_stage::decrypt, pipeline_stage::encrypt})
            sizes.emplace_back(stage, impl_->context->queued[static_cast<std::size_t>(stage)].load());
    return sizes;
}

auto server_pipeline::config() const -> const pipeline_config& {
    return impl_->config;
}

auto server_pipeline::set_send_bandwidth_limit(std::size_t bytes_per_second) -> void {
    if (bytes_per_second > 0) {
        if (impl_->send_limiter) {
            impl_->send_limiter->set_limit(bytes_per_second);
        } else {
            impl_->send_limiter = std::make_unique<bandwidth_limiter>(bytes_per_second);
            impl_->context->send_limiter = impl_->send_limiter.get();
        }
    } else {
        if (impl_->send_limiter) {
            impl_->send_limiter->disable();
        }
    }
    impl_->config.send_bandwidth_limit = bytes_per_second;
}

auto server_pipeline::set_recv_bandwidth_limit(std::size_t bytes_per_second) -> void {
    if (bytes_per_second > 0) {
        if (impl_->recv_limiter) {
            impl_->recv_limiter->set_limit(bytes_per_second);
        } else {
            impl_->recv_limiter = std::make_unique<bandwidth_limiter>(bytes_per_second);
            impl_->context->recv_limiter = impl_->recv_limiter.get();
        }
    } else {
        if (impl_->recv_limiter) {
            impl_->recv_limiter->disable();
        }
    }
    impl_->config.recv_bandwidth_limit = bytes_per_second;
}

auto server_pipeline::get_send_bandwidth_limit() const -> std::size_t {
    return impl_->config.send_bandwidth_limit;
}

auto server_pipeline::get_recv_bandwidth_limit() const -> std::size_t {
    return impl_->config.recv_bandwidth_limit;
}

}  // namespace kcenon::file_transfer
