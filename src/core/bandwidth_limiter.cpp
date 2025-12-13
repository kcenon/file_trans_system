/**
 * @file bandwidth_limiter.cpp
 * @brief Bandwidth limiting using token bucket algorithm
 */

#include "kcenon/file_transfer/core/bandwidth_limiter.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace kcenon::file_transfer {

bandwidth_limiter::bandwidth_limiter(std::size_t bytes_per_second)
    : bytes_per_second_(bytes_per_second)
    , enabled_(bytes_per_second > 0)
    , tokens_(0.0)
    , capacity_(0.0)
    , last_refill_(std::chrono::steady_clock::now()) {
    // Bucket capacity = 1 second worth of tokens (allows burst)
    if (bytes_per_second > 0) {
        capacity_ = static_cast<double>(bytes_per_second);
        tokens_ = capacity_;  // Start with full bucket
    }
}

bandwidth_limiter::~bandwidth_limiter() {
    // Wake up any waiting threads
    {
        std::lock_guard lock(mutex_);
        enabled_ = false;
    }
    cv_.notify_all();
}

bandwidth_limiter::bandwidth_limiter(bandwidth_limiter&& other) noexcept
    : bytes_per_second_(other.bytes_per_second_.load())
    , enabled_(other.enabled_.load())
    , tokens_(other.tokens_)
    , capacity_(other.capacity_)
    , last_refill_(other.last_refill_) {
    other.enabled_ = false;
    other.tokens_ = 0.0;
}

auto bandwidth_limiter::operator=(bandwidth_limiter&& other) noexcept
    -> bandwidth_limiter& {
    if (this != &other) {
        std::lock_guard lock(mutex_);
        bytes_per_second_ = other.bytes_per_second_.load();
        enabled_ = other.enabled_.load();
        tokens_ = other.tokens_;
        capacity_ = other.capacity_;
        last_refill_ = other.last_refill_;

        other.enabled_ = false;
        other.tokens_ = 0.0;
    }
    return *this;
}

auto bandwidth_limiter::acquire(std::size_t bytes) -> void {
    if (bytes == 0 || !enabled_.load(std::memory_order_relaxed)) {
        return;
    }

    std::unique_lock lock(mutex_);

    while (enabled_.load(std::memory_order_relaxed)) {
        refill_tokens();

        if (tokens_ >= static_cast<double>(bytes)) {
            tokens_ -= static_cast<double>(bytes);
            return;
        }

        // Calculate wait time
        auto wait_time = calculate_wait_time(bytes);
        if (wait_time <= std::chrono::microseconds::zero()) {
            tokens_ -= static_cast<double>(bytes);
            return;
        }

        // Wait for tokens to refill
        cv_.wait_for(lock, wait_time, [this, bytes] {
            refill_tokens();
            return tokens_ >= static_cast<double>(bytes) ||
                   !enabled_.load(std::memory_order_relaxed);
        });

        // Re-check if disabled during wait
        if (!enabled_.load(std::memory_order_relaxed)) {
            return;
        }
    }
}

auto bandwidth_limiter::try_acquire(std::size_t bytes) -> bool {
    if (bytes == 0) {
        return true;
    }

    if (!enabled_.load(std::memory_order_relaxed)) {
        return true;
    }

    std::lock_guard lock(mutex_);
    refill_tokens();

    if (tokens_ >= static_cast<double>(bytes)) {
        tokens_ -= static_cast<double>(bytes);
        return true;
    }

    return false;
}

auto bandwidth_limiter::acquire_async(std::size_t bytes) -> std::future<void> {
    return std::async(std::launch::async, [this, bytes] { acquire(bytes); });
}

auto bandwidth_limiter::set_limit(std::size_t bytes_per_second) -> void {
    std::lock_guard lock(mutex_);

    auto old_limit = bytes_per_second_.exchange(bytes_per_second);
    bool was_enabled = enabled_.load(std::memory_order_relaxed);

    if (bytes_per_second > 0) {
        // Update capacity
        double new_capacity = static_cast<double>(bytes_per_second);

        // Scale existing tokens proportionally if limit increased
        if (old_limit > 0 && capacity_ > 0) {
            double ratio = new_capacity / capacity_;
            tokens_ = std::min(tokens_ * ratio, new_capacity);
        } else {
            // Start with full bucket when enabling
            tokens_ = new_capacity;
        }

        capacity_ = new_capacity;
        enabled_ = true;
        last_refill_ = std::chrono::steady_clock::now();
    } else {
        enabled_ = false;
    }

    // Wake up waiting threads if limit increased or disabled
    if (!was_enabled || bytes_per_second == 0 ||
        bytes_per_second > old_limit) {
        cv_.notify_all();
    }
}

auto bandwidth_limiter::get_limit() const noexcept -> std::size_t {
    return bytes_per_second_.load(std::memory_order_relaxed);
}

auto bandwidth_limiter::is_enabled() const noexcept -> bool {
    return enabled_.load(std::memory_order_relaxed);
}

auto bandwidth_limiter::disable() -> void {
    enabled_.store(false, std::memory_order_relaxed);
    cv_.notify_all();
}

auto bandwidth_limiter::enable() -> void {
    if (bytes_per_second_.load(std::memory_order_relaxed) > 0) {
        std::lock_guard lock(mutex_);
        last_refill_ = std::chrono::steady_clock::now();
        enabled_.store(true, std::memory_order_relaxed);
    }
}

auto bandwidth_limiter::reset() -> void {
    std::lock_guard lock(mutex_);
    tokens_ = capacity_;
    last_refill_ = std::chrono::steady_clock::now();
    cv_.notify_all();
}

auto bandwidth_limiter::available_tokens() const -> std::size_t {
    std::lock_guard lock(mutex_);
    const_cast<bandwidth_limiter*>(this)->refill_tokens();
    return static_cast<std::size_t>(std::max(0.0, tokens_));
}

auto bandwidth_limiter::bucket_capacity() const noexcept -> std::size_t {
    return static_cast<std::size_t>(capacity_);
}

auto bandwidth_limiter::refill_tokens() -> void {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - last_refill_);

    if (elapsed.count() > 0.0) {
        double rate = static_cast<double>(
            bytes_per_second_.load(std::memory_order_relaxed));
        double new_tokens = elapsed.count() * rate;
        tokens_ = std::min(tokens_ + new_tokens, capacity_);
        last_refill_ = now;
    }
}

auto bandwidth_limiter::calculate_wait_time(std::size_t bytes) const
    -> std::chrono::microseconds {
    if (!enabled_.load(std::memory_order_relaxed)) {
        return std::chrono::microseconds::zero();
    }

    double needed = static_cast<double>(bytes) - tokens_;
    if (needed <= 0.0) {
        return std::chrono::microseconds::zero();
    }

    double rate = static_cast<double>(
        bytes_per_second_.load(std::memory_order_relaxed));
    if (rate <= 0.0) {
        return std::chrono::microseconds::zero();
    }

    double seconds = needed / rate;
    auto microseconds = static_cast<int64_t>(seconds * 1'000'000.0);
    return std::chrono::microseconds(microseconds);
}

// scoped_bandwidth_acquire implementation
scoped_bandwidth_acquire::scoped_bandwidth_acquire(
    bandwidth_limiter& limiter, std::size_t bytes) {
    limiter.acquire(bytes);
}

}  // namespace kcenon::file_transfer
