#ifndef KCENON_FILE_TRANSFER_PIPELINE_ADMISSION_H
#define KCENON_FILE_TRANSFER_PIPELINE_ADMISSION_H
#include <kcenon/file_transfer/server/server_pipeline.h>
#include <condition_variable>
#include <mutex>
#include <unordered_map>

namespace kcenon::file_transfer::detail {
// Worker callbacks must not wait for capacity held by their own pipeline.
inline thread_local const void* executing_pipeline = nullptr;
struct pipeline_execution_scope {
    const void* previous;
    explicit pipeline_execution_scope(const void* context)
        : previous(executing_pipeline) { executing_pipeline = context; }
    ~pipeline_execution_scope() { executing_pipeline = previous; }
};

class pipeline_admission;
struct pipeline_reservation {
    std::shared_ptr<pipeline_admission> budget;
    transfer_id id;
    std::size_t bytes;
    ~pipeline_reservation();
};
class pipeline_admission : public std::enable_shared_from_this<pipeline_admission> {
    std::mutex mutex_;
    std::condition_variable_any cv_;
    bool open_{false};
    std::size_t count_{}, bytes_{};
    std::unordered_map<transfer_id, std::size_t> per_transfer_;
    const std::size_t slots_, bytes_limit_, transfer_limit_;
    pipeline_stats& stats_;
public:
    pipeline_admission(const pipeline_config& config, pipeline_stats& stats)
        : slots_(config.queue_size), bytes_limit_(config.max_buffered_bytes),
          transfer_limit_(config.max_memory_per_transfer), stats_(stats) {}
    void open() { std::lock_guard lock(mutex_); open_ = true; }
    void close() { std::lock_guard lock(mutex_); open_ = false; cv_.notify_all(); }
    bool accepting() { std::lock_guard lock(mutex_); return open_; }
    void drain() { std::unique_lock lock(mutex_); cv_.wait(lock, [&] { return count_ == 0; }); }
    auto reserve(const transfer_id& id, std::size_t bytes,
                 std::chrono::milliseconds timeout, std::stop_token cancellation)
        -> result<std::shared_ptr<pipeline_reservation>> {
        if (bytes > bytes_limit_ || bytes > transfer_limit_)
            return unexpected{error{error_code::invalid_configuration, "Chunk exceeds pipeline memory budget"}};
        std::unique_lock lock(mutex_);
        auto available = [&] {
            auto found = per_transfer_.find(id);
            auto retained = found == per_transfer_.end() ? 0 : found->second;
            return count_ < slots_ && bytes <= bytes_limit_ - bytes_ &&
                   bytes <= transfer_limit_ - retained;
        };
        if (open_ && !cancellation.stop_requested() && !available()) {
            ++stats_.backpressure_events;
            auto ready = [&] { return !open_ || available(); };
            if (timeout == std::chrono::milliseconds::max()) cv_.wait(lock, cancellation, ready);
            else if (timeout <= std::chrono::milliseconds::zero() ||
                     !cv_.wait_for(lock, cancellation, timeout, ready)) {
                if (!cancellation.stop_requested())
                    return unexpected{error{error_code::transfer_timeout, "Pipeline admission timed out"}};
            }
        }
        if (cancellation.stop_requested())
            return unexpected{error{error_code::invalid_state_transition, "Pipeline admission cancelled"}};
        if (!open_)
            return unexpected{error{error_code::not_initialized, "Pipeline admission is closed"}};
        // Allocate before changing counters so an allocation failure releases no
        // unreserved capacity.
        auto token = std::make_shared<pipeline_reservation>();
        token->id = id; token->bytes = bytes;
        per_transfer_[id] += bytes;
        ++count_; bytes_ += bytes;
        stats_.admitted_chunks = count_; stats_.reserved_bytes = bytes_;
        stats_.peak_admitted_chunks = std::max<uint64_t>(stats_.peak_admitted_chunks, count_);
        stats_.peak_reserved_bytes = std::max<uint64_t>(stats_.peak_reserved_bytes, bytes_);
        token->budget = shared_from_this();
        return token;
    }
    void release(const transfer_id& id, std::size_t bytes) {
        std::lock_guard lock(mutex_);
        --count_; bytes_ -= bytes;
        auto found = per_transfer_.find(id);
        found->second -= bytes;
        if (!found->second) per_transfer_.erase(found);
        stats_.admitted_chunks = count_; stats_.reserved_bytes = bytes_;
        cv_.notify_all();
    }
};
inline pipeline_reservation::~pipeline_reservation() {
    if (budget) budget->release(id, bytes);
}
} // namespace kcenon::file_transfer::detail
#endif
