/**
 * @file server_pipeline.cpp
 * @brief Server-side upload/download pipeline implementation
 */

#include "kcenon/file_transfer/server/server_pipeline.h"

#include "kcenon/file_transfer/core/bandwidth_limiter.h"
#include "kcenon/file_transfer/core/checksum.h"
#include "kcenon/file_transfer/core/compression_engine.h"
#include "kcenon/file_transfer/core/logging.h"

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace kcenon::file_transfer {

// Bounded queue with backpressure support
template <typename T>
class bounded_queue {
public:
    explicit bounded_queue(std::size_t max_size) : max_size_(max_size) {}

    auto push(T item) -> bool {
        std::unique_lock lock(mutex_);
        cv_not_full_.wait(lock, [this] { return queue_.size() < max_size_ || stopped_; });
        if (stopped_) return false;
        queue_.push_back(std::move(item));
        lock.unlock();
        cv_not_empty_.notify_one();
        return true;
    }

    auto try_push(T item) -> bool {
        std::lock_guard lock(mutex_);
        if (stopped_ || queue_.size() >= max_size_) return false;
        queue_.push_back(std::move(item));
        cv_not_empty_.notify_one();
        return true;
    }

    auto pop() -> std::optional<T> {
        std::unique_lock lock(mutex_);
        cv_not_empty_.wait(lock, [this] { return !queue_.empty() || stopped_; });
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop_front();
        lock.unlock();
        cv_not_full_.notify_one();
        return item;
    }

    auto try_pop() -> std::optional<T> {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop_front();
        cv_not_full_.notify_one();
        return item;
    }

    auto size() const -> std::size_t {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    auto stop() -> void {
        std::lock_guard lock(mutex_);
        stopped_ = true;
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }

    auto clear() -> void {
        std::lock_guard lock(mutex_);
        queue_.clear();
        cv_not_full_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;
    std::deque<T> queue_;
    std::size_t max_size_;
    bool stopped_ = false;
};

// Download request structure
struct download_request {
    transfer_id id;
    uint64_t chunk_index;
    std::filesystem::path file_path;
    uint64_t offset;
    std::size_t size;
};

struct server_pipeline::impl {
    pipeline_config config;
    std::atomic<bool> running{false};
    pipeline_stats statistics;

    // Upload pipeline queues
    bounded_queue<pipeline_chunk> recv_queue;
    bounded_queue<pipeline_chunk> decompress_queue;
    bounded_queue<pipeline_chunk> verify_queue;
    bounded_queue<pipeline_chunk> write_queue;

    // Download pipeline queues
    bounded_queue<download_request> read_queue;
    bounded_queue<pipeline_chunk> compress_queue;
    bounded_queue<pipeline_chunk> send_queue;

    // Worker threads
    std::vector<std::thread> io_workers;
    std::vector<std::thread> compression_workers;
    std::vector<std::thread> network_workers;

    // Compression engine (per-thread)
    std::vector<std::unique_ptr<compression_engine>> compression_engines;

    // Callbacks
    stage_callback on_stage_complete_cb;
    error_callback on_error_cb;
    completion_callback on_upload_complete_cb;
    std::function<void(const pipeline_chunk&)> on_download_ready_cb;

    // Output file handles cache
    std::mutex file_handles_mutex;
    std::unordered_map<std::string, std::shared_ptr<std::ofstream>> file_handles;

    // Bandwidth limiters
    std::unique_ptr<bandwidth_limiter> send_limiter;
    std::unique_ptr<bandwidth_limiter> recv_limiter;

    explicit impl(pipeline_config cfg)
        : config(std::move(cfg))
        , recv_queue(cfg.queue_size)
        , decompress_queue(cfg.queue_size)
        , verify_queue(cfg.queue_size)
        , write_queue(cfg.queue_size)
        , read_queue(cfg.queue_size)
        , compress_queue(cfg.queue_size)
        , send_queue(cfg.queue_size) {
        // Create compression engines for workers
        auto total_compression_workers = config.compression_workers;
        compression_engines.reserve(total_compression_workers);
        for (std::size_t i = 0; i < total_compression_workers; ++i) {
            compression_engines.push_back(
                std::make_unique<compression_engine>(compression_level::fast));
        }

        // Initialize bandwidth limiters if configured
        if (config.send_bandwidth_limit > 0) {
            send_limiter = std::make_unique<bandwidth_limiter>(
                config.send_bandwidth_limit);
        }
        if (config.recv_bandwidth_limit > 0) {
            recv_limiter = std::make_unique<bandwidth_limiter>(
                config.recv_bandwidth_limit);
        }
    }

    // Upload pipeline workers
    auto run_decompress_worker(std::size_t worker_id) -> void {
        auto& engine = compression_engines[worker_id % compression_engines.size()];
        FT_LOG_DEBUG(log_category::pipeline,
            "Decompress worker " + std::to_string(worker_id) + " started");

        while (running) {
            auto chunk_opt = decompress_queue.pop();
            if (!chunk_opt) continue;

            auto& chunk = *chunk_opt;
            FT_LOG_TRACE(log_category::pipeline,
                "Processing chunk " + std::to_string(chunk.chunk_index) +
                " in decompress stage");

            if (chunk.is_compressed) {
                auto result = engine->decompress(
                    std::span<const std::byte>(chunk.data),
                    chunk.original_size);

                if (result.has_value()) {
                    chunk.data = std::move(result.value());
                    chunk.is_compressed = false;
                    statistics.compression_saved_bytes +=
                        chunk.original_size - chunk.data.size();
                    FT_LOG_TRACE(log_category::pipeline,
                        "Chunk " + std::to_string(chunk.chunk_index) + " decompressed");
                } else {
                    FT_LOG_ERROR(log_category::pipeline,
                        "Decompression failed for chunk " +
                        std::to_string(chunk.chunk_index) + ": " + result.error().message);
                    if (on_error_cb) {
                        on_error_cb(pipeline_stage::decompress, result.error().message);
                    }
                    continue;
                }
            }

            if (on_stage_complete_cb) {
                on_stage_complete_cb(pipeline_stage::decompress, chunk);
            }

            verify_queue.push(std::move(chunk));
        }

        FT_LOG_DEBUG(log_category::pipeline,
            "Decompress worker " + std::to_string(worker_id) + " stopped");
    }

    auto run_verify_worker() -> void {
        FT_LOG_DEBUG(log_category::pipeline, "Verify worker started");

        while (running) {
            auto chunk_opt = verify_queue.pop();
            if (!chunk_opt) continue;

            auto& chunk = *chunk_opt;
            FT_LOG_TRACE(log_category::pipeline,
                "Verifying chunk " + std::to_string(chunk.chunk_index));

            // Verify CRC32 checksum
            auto calculated = checksum::crc32(std::span<const std::byte>(chunk.data));
            if (calculated != chunk.checksum) {
                FT_LOG_ERROR(log_category::pipeline,
                    "Checksum mismatch for chunk " + std::to_string(chunk.chunk_index) +
                    " (expected: " + std::to_string(chunk.checksum) +
                    ", got: " + std::to_string(calculated) + ")");
                if (on_error_cb) {
                    on_error_cb(pipeline_stage::chunk_verify,
                               "Checksum mismatch for chunk " +
                               std::to_string(chunk.chunk_index));
                }
                continue;
            }

            statistics.chunks_processed++;
            statistics.bytes_processed += chunk.data.size();
            FT_LOG_TRACE(log_category::pipeline,
                "Chunk " + std::to_string(chunk.chunk_index) + " verified successfully");

            if (on_stage_complete_cb) {
                on_stage_complete_cb(pipeline_stage::chunk_verify, chunk);
            }

            write_queue.push(std::move(chunk));
        }

        FT_LOG_DEBUG(log_category::pipeline, "Verify worker stopped");
    }

    auto run_write_worker() -> void {
        FT_LOG_DEBUG(log_category::pipeline, "Write worker started");

        while (running) {
            auto chunk_opt = write_queue.pop();
            if (!chunk_opt) continue;

            auto& chunk = *chunk_opt;
            FT_LOG_TRACE(log_category::pipeline,
                "Writing chunk " + std::to_string(chunk.chunk_index) +
                " (" + std::to_string(chunk.data.size()) + " bytes)");

            // Write to file (simplified - in production would use proper file management)
            if (on_stage_complete_cb) {
                on_stage_complete_cb(pipeline_stage::file_write, chunk);
            }

            if (on_upload_complete_cb) {
                on_upload_complete_cb(chunk.id, chunk.data.size());
            }
        }

        FT_LOG_DEBUG(log_category::pipeline, "Write worker stopped");
    }

    // Download pipeline workers
    auto run_read_worker() -> void {
        FT_LOG_DEBUG(log_category::pipeline, "Read worker started");

        while (running) {
            auto request_opt = read_queue.pop();
            if (!request_opt) continue;

            auto& request = *request_opt;
            FT_LOG_TRACE(log_category::pipeline,
                "Reading chunk " + std::to_string(request.chunk_index) +
                " from " + request.file_path.filename().string());

            std::ifstream file(request.file_path, std::ios::binary);
            if (!file) {
                FT_LOG_ERROR(log_category::pipeline,
                    "Failed to open file: " + request.file_path.string());
                if (on_error_cb) {
                    on_error_cb(pipeline_stage::file_read,
                               "Failed to open file: " + request.file_path.string());
                }
                continue;
            }

            file.seekg(static_cast<std::streamoff>(request.offset));
            if (!file) {
                FT_LOG_ERROR(log_category::pipeline,
                    "Failed to seek in file: " + request.file_path.string());
                if (on_error_cb) {
                    on_error_cb(pipeline_stage::file_read,
                               "Failed to seek in file: " + request.file_path.string());
                }
                continue;
            }

            pipeline_chunk chunk;
            chunk.id = request.id;
            chunk.chunk_index = request.chunk_index;
            chunk.data.resize(request.size);
            chunk.is_compressed = false;
            chunk.original_size = request.size;

            file.read(reinterpret_cast<char*>(chunk.data.data()),
                     static_cast<std::streamsize>(request.size));

            auto bytes_read = static_cast<std::size_t>(file.gcount());
            if (bytes_read < request.size) {
                chunk.data.resize(bytes_read);
            }

            chunk.checksum = checksum::crc32(std::span<const std::byte>(chunk.data));
            FT_LOG_TRACE(log_category::pipeline,
                "Chunk " + std::to_string(request.chunk_index) + " read (" +
                std::to_string(bytes_read) + " bytes)");

            if (on_stage_complete_cb) {
                on_stage_complete_cb(pipeline_stage::file_read, chunk);
            }

            compress_queue.push(std::move(chunk));
        }

        FT_LOG_DEBUG(log_category::pipeline, "Read worker stopped");
    }

    auto run_compress_worker(std::size_t worker_id) -> void {
        auto& engine = compression_engines[worker_id % compression_engines.size()];
        FT_LOG_DEBUG(log_category::pipeline,
            "Compress worker " + std::to_string(worker_id) + " started");

        while (running) {
            auto chunk_opt = compress_queue.pop();
            if (!chunk_opt) continue;

            auto& chunk = *chunk_opt;
            FT_LOG_TRACE(log_category::pipeline,
                "Compressing chunk " + std::to_string(chunk.chunk_index));

            // Check if compression would be beneficial
            if (engine->is_compressible(std::span<const std::byte>(chunk.data))) {
                auto result = engine->compress(std::span<const std::byte>(chunk.data));
                if (result.has_value() && result.value().size() < chunk.data.size()) {
                    auto original = chunk.data.size();
                    chunk.original_size = chunk.data.size();
                    chunk.data = std::move(result.value());
                    chunk.is_compressed = true;
                    statistics.compression_saved_bytes +=
                        chunk.original_size - chunk.data.size();
                    FT_LOG_TRACE(log_category::pipeline,
                        "Chunk " + std::to_string(chunk.chunk_index) +
                        " compressed: " + std::to_string(original) + " -> " +
                        std::to_string(chunk.data.size()) + " bytes");
                }
            } else {
                FT_LOG_TRACE(log_category::pipeline,
                    "Chunk " + std::to_string(chunk.chunk_index) +
                    " skipped compression (not compressible)");
            }

            if (on_stage_complete_cb) {
                on_stage_complete_cb(pipeline_stage::compress, chunk);
            }

            send_queue.push(std::move(chunk));
        }

        FT_LOG_DEBUG(log_category::pipeline,
            "Compress worker " + std::to_string(worker_id) + " stopped");
    }

    auto run_send_worker() -> void {
        FT_LOG_DEBUG(log_category::pipeline, "Send worker started");

        while (running) {
            auto chunk_opt = send_queue.pop();
            if (!chunk_opt) continue;

            auto& chunk = *chunk_opt;
            FT_LOG_TRACE(log_category::pipeline,
                "Sending chunk " + std::to_string(chunk.chunk_index) +
                " (" + std::to_string(chunk.data.size()) + " bytes)");

            // Apply send bandwidth limit if configured
            if (send_limiter) {
                send_limiter->acquire(chunk.data.size());
            }

            statistics.chunks_processed++;
            statistics.bytes_processed += chunk.data.size();

            if (on_stage_complete_cb) {
                on_stage_complete_cb(pipeline_stage::network_send, chunk);
            }

            if (on_download_ready_cb) {
                on_download_ready_cb(chunk);
            }
        }

        FT_LOG_DEBUG(log_category::pipeline, "Send worker stopped");
    }

    auto start_workers() -> void {
        // Start I/O workers (read + write)
        for (std::size_t i = 0; i < config.io_workers; ++i) {
            io_workers.emplace_back([this] { run_read_worker(); });
            io_workers.emplace_back([this] { run_write_worker(); });
        }

        // Start compression workers (decompress + compress)
        for (std::size_t i = 0; i < config.compression_workers; ++i) {
            compression_workers.emplace_back([this, i] { run_decompress_worker(i); });
            compression_workers.emplace_back([this, i] { run_compress_worker(i); });
        }

        // Start verification workers
        for (std::size_t i = 0; i < config.network_workers; ++i) {
            network_workers.emplace_back([this] { run_verify_worker(); });
            network_workers.emplace_back([this] { run_send_worker(); });
        }
    }

    auto stop_workers(bool wait_for_completion) -> void {
        // Stop all queues
        recv_queue.stop();
        decompress_queue.stop();
        verify_queue.stop();
        write_queue.stop();
        read_queue.stop();
        compress_queue.stop();
        send_queue.stop();

        if (!wait_for_completion) {
            recv_queue.clear();
            decompress_queue.clear();
            verify_queue.clear();
            write_queue.clear();
            read_queue.clear();
            compress_queue.clear();
            send_queue.clear();
        }

        // Join all threads
        for (auto& t : io_workers) {
            if (t.joinable()) t.join();
        }
        for (auto& t : compression_workers) {
            if (t.joinable()) t.join();
        }
        for (auto& t : network_workers) {
            if (t.joinable()) t.join();
        }

        io_workers.clear();
        compression_workers.clear();
        network_workers.clear();
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
    impl_->start_workers();

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
    impl_->stop_workers(wait_for_completion);

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

    if (!impl_->decompress_queue.push(std::move(data))) {
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

    if (!impl_->decompress_queue.try_push(std::move(data))) {
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

    download_request request{id, chunk_index, file_path, offset, size};

    if (!impl_->read_queue.push(std::move(request))) {
        impl_->statistics.backpressure_events++;
        return unexpected{error{error_code::internal_error,
                               "Queue full - backpressure applied"}};
    }

    return {};
}

auto server_pipeline::on_stage_complete(stage_callback callback) -> void {
    impl_->on_stage_complete_cb = std::move(callback);
}

auto server_pipeline::on_error(error_callback callback) -> void {
    impl_->on_error_cb = std::move(callback);
}

auto server_pipeline::on_upload_complete(completion_callback callback) -> void {
    impl_->on_upload_complete_cb = std::move(callback);
}

auto server_pipeline::on_download_ready(
    std::function<void(const pipeline_chunk&)> callback) -> void {
    impl_->on_download_ready_cb = std::move(callback);
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
        {pipeline_stage::decompress, impl_->decompress_queue.size()},
        {pipeline_stage::chunk_verify, impl_->verify_queue.size()},
        {pipeline_stage::file_write, impl_->write_queue.size()},
        {pipeline_stage::file_read, impl_->read_queue.size()},
        {pipeline_stage::compress, impl_->compress_queue.size()},
        {pipeline_stage::network_send, impl_->send_queue.size()},
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
