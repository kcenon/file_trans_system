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

#include <kcenon/thread/core/thread_pool.h>
#include <kcenon/thread/core/bounded_job_queue.h>

#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace kcenon::file_transfer {

struct server_pipeline::impl {
    pipeline_config config;
    std::atomic<bool> running{false};
    pipeline_stats statistics;

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
        auto total_threads = config.io_workers + config.compression_workers +
                            config.network_workers;
        thread_pool = std::make_shared<thread::thread_pool>(
            "server_pipeline_pool");

        // Create pipeline context
        context = std::make_shared<pipeline_context>();
        context->thread_pool = thread_pool;

        // Create bounded job queues for each stage (used for backpressure tracking)
        context->decompress_queue = std::make_shared<thread::bounded_job_queue>(
            config.queue_size, 0.8);
        context->verify_queue = std::make_shared<thread::bounded_job_queue>(
            config.queue_size, 0.8);
        context->write_queue = std::make_shared<thread::bounded_job_queue>(
            config.queue_size, 0.8);
        context->read_queue = std::make_shared<thread::bounded_job_queue>(
            config.queue_size, 0.8);
        context->compress_queue = std::make_shared<thread::bounded_job_queue>(
            config.queue_size, 0.8);
        context->send_queue = std::make_shared<thread::bounded_job_queue>(
            config.queue_size, 0.8);

        // Create compression engines for workers
        auto total_compression_workers = config.compression_workers;
        context->compression_engines.reserve(total_compression_workers);
        for (std::size_t i = 0; i < total_compression_workers; ++i) {
            context->compression_engines.push_back(
                std::make_unique<compression_engine>(compression_level::fast));
        }

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
        // Always cleanup regardless of running state to prevent memory leaks
        // from circular references (jobs hold shared_ptr<pipeline_context>,
        // which holds shared_ptr<thread_pool>)
        running = false;
        stop_queues();
        clear_queues();  // Break circular references by clearing pending jobs

        if (thread_pool) {
            // Clear the thread_pool's job queue to break circular references
            // This is necessary even if stop() was already called and returns early,
            // because pending jobs hold shared_ptr<pipeline_context> which holds
            // shared_ptr<thread_pool>, creating a reference cycle
            auto queue = thread_pool->get_job_queue();
            if (queue) {
                queue->stop();
                queue->clear();
            }
            thread_pool->stop(true);
        }

        // Explicitly break circular reference by clearing context's thread_pool pointer
        if (context) {
            context->thread_pool.reset();
        }
    }

    auto start() -> common::VoidResult {
        return thread_pool->start();
    }

    auto stop(bool wait_for_completion) -> void {
        stop_queues();

        if (!wait_for_completion) {
            clear_queues();
        }

        thread_pool->stop(!wait_for_completion);
    }

    auto stop_queues() -> void {
        context->decompress_queue->stop();
        context->verify_queue->stop();
        context->write_queue->stop();
        context->read_queue->stop();
        context->compress_queue->stop();
        context->send_queue->stop();
    }

    auto clear_queues() -> void {
        context->decompress_queue->clear();
        context->verify_queue->clear();
        context->write_queue->clear();
        context->read_queue->clear();
        context->compress_queue->clear();
        context->send_queue->clear();
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
           max_memory_per_transfer > 0;
}

// pipeline_stats implementation
auto pipeline_stats::reset() -> void {
    chunks_processed = 0;
    bytes_processed = 0;
    compression_saved_bytes = 0;
    stalls_detected = 0;
    backpressure_events = 0;
}

// pipeline_chunk implementation
pipeline_chunk::pipeline_chunk(const chunk& c)
    : id(c.header.id)
    , chunk_index(c.header.chunk_index)
    , data(c.data)
    , checksum(c.header.checksum)
    , is_compressed(c.is_compressed())
    , original_size(c.header.original_size) {}

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
    if (impl_->running) {
        FT_LOG_WARN(log_category::pipeline, "Pipeline start failed: already running");
        return unexpected{error{error_code::already_initialized,
                               "Pipeline is already running"}};
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
    if (!impl_->running) {
        FT_LOG_WARN(log_category::pipeline, "Pipeline stop failed: not running");
        return unexpected{error{error_code::not_initialized,
                               "Pipeline is not running"}};
    }

    FT_LOG_INFO(log_category::pipeline,
        "Stopping pipeline (wait_for_completion=" +
        std::string(wait_for_completion ? "true" : "false") + ")");

    impl_->running = false;
    impl_->stop(wait_for_completion);

    FT_LOG_INFO(log_category::pipeline,
        "Pipeline stopped. Stats: " + std::to_string(impl_->statistics.chunks_processed) +
        " chunks processed, " + std::to_string(impl_->statistics.bytes_processed) +
        " bytes, " + std::to_string(impl_->statistics.compression_saved_bytes) +
        " bytes saved by compression");
    return {};
}

auto server_pipeline::is_running() const -> bool {
    return impl_->running;
}

auto server_pipeline::submit_upload_chunk(pipeline_chunk data) -> result<void> {
    if (!impl_->running) {
        return unexpected{error{error_code::not_initialized,
                               "Pipeline is not running"}};
    }

    // Apply recv bandwidth limit if configured
    if (impl_->recv_limiter) {
        impl_->recv_limiter->acquire(data.data.size());
    }

    // Create decompress job and submit to thread pool
    auto worker_id = impl_->get_worker_id();
    auto job = std::make_unique<decompress_job>(
        impl_->context, std::move(data), worker_id);

    auto enqueue_result = impl_->thread_pool->enqueue(std::move(job));
    if (!enqueue_result.is_ok()) {
        impl_->statistics.backpressure_events++;
        return unexpected{error{error_code::internal_error,
                               "Queue full - backpressure applied"}};
    }

    return {};
}

auto server_pipeline::try_submit_upload_chunk(pipeline_chunk data) -> bool {
    if (!impl_->running) return false;

    // Try to apply recv bandwidth limit without blocking
    if (impl_->recv_limiter && !impl_->recv_limiter->try_acquire(data.data.size())) {
        return false;
    }

    // Create decompress job and submit to thread pool
    auto worker_id = impl_->get_worker_id();
    auto job = std::make_unique<decompress_job>(
        impl_->context, std::move(data), worker_id);

    auto enqueue_result = impl_->thread_pool->enqueue(std::move(job));
    if (!enqueue_result.is_ok()) {
        impl_->statistics.backpressure_events++;
        return false;
    }

    return true;
}

auto server_pipeline::submit_download_request(
    const transfer_id& id,
    uint64_t chunk_index,
    const std::filesystem::path& file_path,
    uint64_t offset,
    std::size_t size) -> result<void> {
    if (!impl_->running) {
        return unexpected{error{error_code::not_initialized,
                               "Pipeline is not running"}};
    }

    // Create read job and submit to thread pool
    auto job = std::make_unique<read_job>(
        impl_->context, id, chunk_index, file_path, offset, size);

    auto enqueue_result = impl_->thread_pool->enqueue(std::move(job));
    if (!enqueue_result.is_ok()) {
        impl_->statistics.backpressure_events++;
        return unexpected{error{error_code::internal_error,
                               "Queue full - backpressure applied"}};
    }

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
    return {
        {pipeline_stage::decompress, impl_->context->decompress_queue->size()},
        {pipeline_stage::chunk_verify, impl_->context->verify_queue->size()},
        {pipeline_stage::file_write, impl_->context->write_queue->size()},
        {pipeline_stage::file_read, impl_->context->read_queue->size()},
        {pipeline_stage::compress, impl_->context->compress_queue->size()},
        {pipeline_stage::network_send, impl_->context->send_queue->size()},
    };
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
