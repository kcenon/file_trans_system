/**
 * @file statistics_collector.cpp
 * @brief Implementation of statistics collection for transfer progress monitoring
 */

#include "kcenon/file_transfer/core/statistics_collector.h"

#include <algorithm>
#include <numeric>

namespace kcenon::file_transfer {

/**
 * @brief Rate sample for moving average calculation
 */
struct rate_sample {
    time_point timestamp;
    uint64_t bytes;
};

/**
 * @brief Implementation details for statistics_collector
 */
struct statistics_collector::impl {
    config cfg;

    // State
    std::atomic<bool> active{false};
    time_point start_time;
    time_point last_sample_time;

    // Counters
    std::atomic<uint64_t> bytes_transferred{0};
    std::atomic<uint64_t> bytes_on_wire{0};
    std::atomic<uint64_t> total_bytes{0};
    std::atomic<uint64_t> chunks_transferred{0};
    std::atomic<uint64_t> chunks_compressed{0};
    std::atomic<uint64_t> error_count{0};

    // Rate calculation
    mutable std::mutex rate_mutex;
    std::deque<rate_sample> rate_samples;
    uint64_t last_sample_bytes{0};

    // ETA smoothing
    mutable std::mutex eta_mutex;
    time_point last_eta_update;
    duration cached_eta{0};

    impl() : cfg{} {}
    explicit impl(config c) : cfg(std::move(c)) {}

    void update_rate_samples() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed_since_sample =
            std::chrono::duration_cast<duration>(now - last_sample_time);

        if (elapsed_since_sample >= cfg.rate_sample_interval) {
            std::lock_guard<std::mutex> lock(rate_mutex);

            uint64_t current_bytes = bytes_transferred.load();
            rate_samples.push_back({now, current_bytes});

            // Trim old samples
            while (rate_samples.size() > cfg.rate_window_size) {
                rate_samples.pop_front();
            }

            last_sample_bytes = current_bytes;
            last_sample_time = now;
        }
    }

    [[nodiscard]] auto calculate_current_rate() const -> double {
        std::lock_guard<std::mutex> lock(rate_mutex);

        if (rate_samples.size() < 2) {
            return 0.0;
        }

        const auto& oldest = rate_samples.front();
        const auto& newest = rate_samples.back();

        auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(
            newest.timestamp - oldest.timestamp);

        if (time_diff.count() == 0) {
            return 0.0;
        }

        auto bytes_diff = newest.bytes - oldest.bytes;
        return static_cast<double>(bytes_diff) * 1000.0 /
               static_cast<double>(time_diff.count());
    }

    [[nodiscard]] auto calculate_average_rate() const -> double {
        if (!active.load()) {
            return 0.0;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start_time);

        if (elapsed.count() == 0) {
            return 0.0;
        }

        return static_cast<double>(bytes_transferred.load()) * 1000.0 /
               static_cast<double>(elapsed.count());
    }

    [[nodiscard]] auto calculate_eta() const -> duration {
        uint64_t transferred = bytes_transferred.load();
        uint64_t total = total_bytes.load();

        if (transferred >= total || total == 0) {
            return duration{0};
        }

        double avg_rate = calculate_average_rate();
        if (avg_rate <= 0.0) {
            return duration{0};
        }

        uint64_t remaining = total - transferred;
        auto eta_ms = static_cast<int64_t>(
            static_cast<double>(remaining) * 1000.0 / avg_rate);

        return duration{eta_ms};
    }
};

statistics_collector::statistics_collector()
    : impl_(std::make_unique<impl>()) {}

statistics_collector::statistics_collector(config cfg)
    : impl_(std::make_unique<impl>(std::move(cfg))) {}

statistics_collector::statistics_collector(statistics_collector&&) noexcept = default;
auto statistics_collector::operator=(statistics_collector&&) noexcept
    -> statistics_collector& = default;
statistics_collector::~statistics_collector() = default;

void statistics_collector::start(uint64_t total_bytes) {
    impl_->total_bytes.store(total_bytes);
    impl_->start_time = std::chrono::steady_clock::now();
    impl_->last_sample_time = impl_->start_time;
    impl_->last_eta_update = impl_->start_time;
    impl_->active.store(true);

    // Initialize with first sample
    std::lock_guard<std::mutex> lock(impl_->rate_mutex);
    impl_->rate_samples.clear();
    impl_->rate_samples.push_back({impl_->start_time, 0});
    impl_->last_sample_bytes = 0;
}

void statistics_collector::stop() {
    impl_->active.store(false);
}

void statistics_collector::reset() {
    impl_->active.store(false);
    impl_->bytes_transferred.store(0);
    impl_->bytes_on_wire.store(0);
    impl_->total_bytes.store(0);
    impl_->chunks_transferred.store(0);
    impl_->chunks_compressed.store(0);
    impl_->error_count.store(0);

    {
        std::lock_guard<std::mutex> lock(impl_->rate_mutex);
        impl_->rate_samples.clear();
        impl_->last_sample_bytes = 0;
    }

    {
        std::lock_guard<std::mutex> lock(impl_->eta_mutex);
        impl_->cached_eta = duration{0};
    }
}

auto statistics_collector::is_active() const noexcept -> bool {
    return impl_->active.load();
}

void statistics_collector::record_bytes_transferred(
    uint64_t bytes, std::optional<uint64_t> bytes_on_wire) {
    impl_->bytes_transferred.fetch_add(bytes);
    impl_->bytes_on_wire.fetch_add(bytes_on_wire.value_or(bytes));
    impl_->update_rate_samples();
}

void statistics_collector::record_chunk_processed(bool compressed) {
    impl_->chunks_transferred.fetch_add(1);
    if (compressed) {
        impl_->chunks_compressed.fetch_add(1);
    }
}

void statistics_collector::record_error(transfer_error_code /*code*/) {
    impl_->error_count.fetch_add(1);
}

auto statistics_collector::get_transfer_rate() const -> double {
    return impl_->calculate_current_rate();
}

auto statistics_collector::get_average_rate() const -> double {
    return impl_->calculate_average_rate();
}

auto statistics_collector::get_compression_ratio() const -> double {
    uint64_t transferred = impl_->bytes_transferred.load();
    uint64_t on_wire = impl_->bytes_on_wire.load();

    if (transferred == 0) {
        return 1.0;
    }

    return static_cast<double>(on_wire) / static_cast<double>(transferred);
}

auto statistics_collector::get_eta() const -> duration {
    auto now = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(impl_->eta_mutex);
        auto elapsed_since_update =
            std::chrono::duration_cast<duration>(now - impl_->last_eta_update);

        if (elapsed_since_update < impl_->cfg.min_eta_update_interval) {
            return impl_->cached_eta;
        }

        impl_->cached_eta = impl_->calculate_eta();
        impl_->last_eta_update = now;
        return impl_->cached_eta;
    }
}

auto statistics_collector::get_elapsed() const -> duration {
    if (!impl_->active.load()) {
        return duration{0};
    }

    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<duration>(now - impl_->start_time);
}

auto statistics_collector::get_completion_percentage() const -> double {
    uint64_t total = impl_->total_bytes.load();
    if (total == 0) {
        return 0.0;
    }

    return static_cast<double>(impl_->bytes_transferred.load()) /
           static_cast<double>(total) * 100.0;
}

auto statistics_collector::get_bytes_transferred() const -> uint64_t {
    return impl_->bytes_transferred.load();
}

auto statistics_collector::get_bytes_on_wire() const -> uint64_t {
    return impl_->bytes_on_wire.load();
}

auto statistics_collector::get_chunks_transferred() const -> uint64_t {
    return impl_->chunks_transferred.load();
}

auto statistics_collector::get_error_count() const -> uint64_t {
    return impl_->error_count.load();
}

auto statistics_collector::get_snapshot() const -> snapshot {
    snapshot s;
    s.bytes_transferred = impl_->bytes_transferred.load();
    s.bytes_on_wire = impl_->bytes_on_wire.load();
    s.total_bytes = impl_->total_bytes.load();
    s.chunks_transferred = impl_->chunks_transferred.load();
    s.chunks_compressed = impl_->chunks_compressed.load();
    s.error_count = impl_->error_count.load();
    s.current_rate = get_transfer_rate();
    s.average_rate = get_average_rate();
    s.compression_ratio = get_compression_ratio();
    s.elapsed = get_elapsed();
    s.estimated_remaining = get_eta();
    return s;
}

void statistics_collector::set_total_bytes(uint64_t total_bytes) {
    impl_->total_bytes.store(total_bytes);
}

}  // namespace kcenon::file_transfer
