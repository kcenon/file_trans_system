/**
 * @file bandwidth_limiter.h
 * @brief Bandwidth limiting using token bucket algorithm
 *
 * Provides bandwidth throttling functionality for upload/download transfers.
 * Implements the token bucket algorithm for smooth rate limiting with burst support.
 */

#ifndef KCENON_FILE_TRANSFER_CORE_BANDWIDTH_LIMITER_H
#define KCENON_FILE_TRANSFER_CORE_BANDWIDTH_LIMITER_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <future>
#include <mutex>

namespace kcenon::file_transfer {

/**
 * @brief Token bucket bandwidth limiter
 *
 * Implements rate limiting using the token bucket algorithm.
 * Allows controlled bursts while maintaining average rate limits.
 *
 * @code
 * bandwidth_limiter limiter(10 * 1024 * 1024);  // 10 MB/s
 *
 * // Before each transfer operation
 * limiter.acquire(chunk_size);  // Blocks if rate exceeded
 *
 * // Dynamic adjustment
 * limiter.set_limit(20 * 1024 * 1024);  // Change to 20 MB/s
 * @endcode
 */
class bandwidth_limiter {
public:
    /**
     * @brief Construct a bandwidth limiter
     * @param bytes_per_second Maximum transfer rate in bytes per second
     *                         Value of 0 means unlimited
     */
    explicit bandwidth_limiter(std::size_t bytes_per_second);

    /**
     * @brief Destructor
     */
    ~bandwidth_limiter();

    // Non-copyable, movable
    bandwidth_limiter(const bandwidth_limiter&) = delete;
    auto operator=(const bandwidth_limiter&) -> bandwidth_limiter& = delete;
    bandwidth_limiter(bandwidth_limiter&&) noexcept;
    auto operator=(bandwidth_limiter&&) noexcept -> bandwidth_limiter&;

    /**
     * @brief Acquire tokens for transfer
     *
     * Blocks until sufficient tokens are available.
     * Call this before transferring data.
     *
     * @param bytes Number of bytes to transfer
     */
    auto acquire(std::size_t bytes) -> void;

    /**
     * @brief Try to acquire tokens without blocking
     *
     * @param bytes Number of bytes to transfer
     * @return true if tokens acquired, false if would need to wait
     */
    [[nodiscard]] auto try_acquire(std::size_t bytes) -> bool;

    /**
     * @brief Async version of acquire
     *
     * @param bytes Number of bytes to transfer
     * @return Future that completes when tokens are available
     */
    [[nodiscard]] auto acquire_async(std::size_t bytes) -> std::future<void>;

    /**
     * @brief Set new rate limit
     *
     * Takes effect immediately for subsequent acquire calls.
     *
     * @param bytes_per_second New rate limit (0 = unlimited)
     */
    auto set_limit(std::size_t bytes_per_second) -> void;

    /**
     * @brief Get current rate limit
     * @return Current limit in bytes per second (0 = unlimited)
     */
    [[nodiscard]] auto get_limit() const noexcept -> std::size_t;

    /**
     * @brief Check if limiter is enabled
     * @return true if limit is set (> 0)
     */
    [[nodiscard]] auto is_enabled() const noexcept -> bool;

    /**
     * @brief Disable rate limiting temporarily
     *
     * Rate limit setting is preserved but not enforced.
     */
    auto disable() -> void;

    /**
     * @brief Re-enable rate limiting
     *
     * Restores enforcement of the configured rate limit.
     */
    auto enable() -> void;

    /**
     * @brief Reset the token bucket
     *
     * Fills the bucket to capacity, allowing immediate burst.
     */
    auto reset() -> void;

    /**
     * @brief Get current available tokens
     * @return Number of bytes available for immediate transfer
     */
    [[nodiscard]] auto available_tokens() const -> std::size_t;

    /**
     * @brief Get bucket capacity (burst size)
     * @return Maximum burst size in bytes
     */
    [[nodiscard]] auto bucket_capacity() const noexcept -> std::size_t;

private:
    /**
     * @brief Refill tokens based on elapsed time
     */
    auto refill_tokens() -> void;

    /**
     * @brief Calculate wait time for required tokens
     * @param bytes Required bytes
     * @return Duration to wait
     */
    [[nodiscard]] auto calculate_wait_time(std::size_t bytes) const
        -> std::chrono::microseconds;

    mutable std::mutex mutex_;
    std::condition_variable cv_;

    std::atomic<std::size_t> bytes_per_second_;
    std::atomic<bool> enabled_;

    // Token bucket state
    double tokens_;
    double capacity_;
    std::chrono::steady_clock::time_point last_refill_;
};

/**
 * @brief Scoped bandwidth acquisition
 *
 * RAII helper for bandwidth limiting. Acquires tokens on construction.
 *
 * @code
 * bandwidth_limiter limiter(10 * 1024 * 1024);
 *
 * {
 *     scoped_bandwidth_acquire guard(limiter, chunk_size);
 *     // Transfer chunk...
 * }
 * @endcode
 */
class scoped_bandwidth_acquire {
public:
    /**
     * @brief Acquire bandwidth tokens
     * @param limiter Bandwidth limiter to use
     * @param bytes Bytes to acquire
     */
    scoped_bandwidth_acquire(bandwidth_limiter& limiter, std::size_t bytes);

    // Non-copyable, non-movable
    scoped_bandwidth_acquire(const scoped_bandwidth_acquire&) = delete;
    auto operator=(const scoped_bandwidth_acquire&)
        -> scoped_bandwidth_acquire& = delete;
    scoped_bandwidth_acquire(scoped_bandwidth_acquire&&) = delete;
    auto operator=(scoped_bandwidth_acquire&&)
        -> scoped_bandwidth_acquire& = delete;

    ~scoped_bandwidth_acquire() = default;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CORE_BANDWIDTH_LIMITER_H
