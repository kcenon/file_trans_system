// BSD 3-Clause License
//
// Copyright (c) 2025, kcenon
// All rights reserved.

/**
 * @file thread_pool_adapter.h
 * @brief Thread pool adapter for file_trans_system
 *
 * This adapter provides a unified thread pool interface for file transfer operations,
 * supporting both thread_system integration and standalone fallback modes.
 *
 * Features:
 * - Stage-based task tracking for pipeline monitoring
 * - Delayed task scheduling for retry operations
 * - Seamless integration with thread_system when available
 * - Fallback to std::async when thread_system is unavailable
 *
 * @since 0.3.0
 */

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "../config/feature_flags.h"

#if KCENON_WITH_NETWORK_SYSTEM
#include <kcenon/network/integration/thread_integration.h>
#endif

#if KCENON_WITH_THREAD_SYSTEM
#include <kcenon/thread/core/thread_pool.h>
#endif

namespace kcenon::file_transfer::adapters {

/**
 * @brief Interface for thread pool operations in file_trans_system
 *
 * This abstraction allows:
 * - Use of thread_system's thread_pool when available
 * - Fallback to basic_thread_pool from network_system or std::async
 * - Delayed task scheduling for retry operations
 * - Stage-based task tracking for pipeline monitoring
 */
class transfer_thread_pool_interface {
public:
    virtual ~transfer_thread_pool_interface() = default;

    /**
     * @brief Submit a task for execution
     * @param task The task to execute
     * @return Future for the task completion
     */
    virtual std::future<void> submit(std::function<void()> task) = 0;

    /**
     * @brief Submit a task with delay (useful for retries with backoff)
     * @param task The task to execute
     * @param delay Time to wait before executing the task
     * @return Future for the task completion
     */
    virtual std::future<void> submit_delayed(
        std::function<void()> task,
        std::chrono::milliseconds delay) = 0;

    /**
     * @brief Submit a task to a specific pipeline stage for tracking
     * @param task The task to execute
     * @param stage_name Name of the pipeline stage (e.g., "chunk_processing")
     * @return Future for the task completion
     *
     * @note Stage names are used for metrics collection and debugging.
     *       The actual execution is the same as submit(), but task counts
     *       are tracked per stage.
     */
    virtual std::future<void> submit_to_stage(
        std::function<void()> task,
        const std::string& stage_name) = 0;

    /**
     * @brief Get the number of worker threads
     * @return Worker thread count
     */
    [[nodiscard]] virtual size_t worker_count() const = 0;

    /**
     * @brief Check if the pool is running
     * @return true if the pool is active
     */
    [[nodiscard]] virtual bool is_running() const = 0;

    /**
     * @brief Get total pending task count
     * @return Number of tasks waiting to be executed
     */
    [[nodiscard]] virtual size_t pending_tasks() const = 0;

    /**
     * @brief Get pending task count for a specific stage
     * @param stage_name Name of the pipeline stage
     * @return Number of tasks waiting for that stage
     */
    [[nodiscard]] virtual size_t pending_tasks(const std::string& stage_name) const = 0;
};

#if KCENON_WITH_THREAD_SYSTEM

/**
 * @brief Adapter that wraps thread_system::thread_pool for file transfers
 *
 * This adapter provides:
 * - Direct integration with thread_system's thread_pool
 * - Stage-based task tracking for pipeline monitoring
 * - Delayed task scheduling via thread_pool's native support
 *
 * @note Thread-safe: All public methods are safe to call from multiple threads.
 */
class thread_system_transfer_adapter : public transfer_thread_pool_interface {
public:
    /**
     * @brief Construct with an existing thread_pool
     * @param pool Shared pointer to thread_system's thread_pool
     * @param pool_name Name for identification in logs and metrics
     * @param worker_count Number of workers in the pool (for reporting)
     */
    explicit thread_system_transfer_adapter(
        std::shared_ptr<kcenon::thread::thread_pool> pool,
        const std::string& pool_name = "file_transfer_pool",
        size_t worker_count = 0);

    /**
     * @brief Destructor
     */
    ~thread_system_transfer_adapter() override;

    // Non-copyable
    thread_system_transfer_adapter(const thread_system_transfer_adapter&) = delete;
    thread_system_transfer_adapter& operator=(const thread_system_transfer_adapter&) = delete;

    // Movable
    thread_system_transfer_adapter(thread_system_transfer_adapter&&) noexcept;
    thread_system_transfer_adapter& operator=(thread_system_transfer_adapter&&) noexcept;

    /**
     * @brief Factory method to create a default adapter
     * @param worker_count Number of worker threads (0 = auto-detect from hardware)
     * @param pool_name Name for identification
     * @return Shared pointer to the adapter
     */
    [[nodiscard]] static std::shared_ptr<thread_system_transfer_adapter> create_default(
        size_t worker_count = 0,
        const std::string& pool_name = "file_transfer_pool");

    // transfer_thread_pool_interface implementation
    std::future<void> submit(std::function<void()> task) override;
    std::future<void> submit_delayed(
        std::function<void()> task,
        std::chrono::milliseconds delay) override;
    std::future<void> submit_to_stage(
        std::function<void()> task,
        const std::string& stage_name) override;

    [[nodiscard]] size_t worker_count() const override;
    [[nodiscard]] bool is_running() const override;
    [[nodiscard]] size_t pending_tasks() const override;
    [[nodiscard]] size_t pending_tasks(const std::string& stage_name) const override;

    /**
     * @brief Get the underlying thread_pool
     * @return Shared pointer to the underlying pool
     */
    [[nodiscard]] std::shared_ptr<kcenon::thread::thread_pool> underlying_pool() const;

    /**
     * @brief Get the pool name
     * @return Pool name string
     */
    [[nodiscard]] std::string pool_name() const;

private:
    struct impl;
    std::unique_ptr<impl> pimpl_;
};

#endif  // KCENON_WITH_THREAD_SYSTEM

#if KCENON_WITH_NETWORK_SYSTEM

/**
 * @brief Adapter that wraps network_system's thread_pool_interface
 *
 * This adapter bridges network_system's thread pool abstraction to
 * file_transfer's transfer_thread_pool_interface, adding stage tracking.
 *
 * @note Use this when you want to share a thread pool with network_system,
 *       or when thread_system is not available but network_system is.
 */
class network_pool_transfer_adapter : public transfer_thread_pool_interface {
public:
    /**
     * @brief Construct with a network_system thread_pool_interface
     * @param pool Shared pointer to network_system's thread pool
     * @param pool_name Name for identification
     */
    explicit network_pool_transfer_adapter(
        std::shared_ptr<kcenon::network::integration::thread_pool_interface> pool,
        const std::string& pool_name = "file_transfer_pool");

    ~network_pool_transfer_adapter() override;

    // Non-copyable
    network_pool_transfer_adapter(const network_pool_transfer_adapter&) = delete;
    network_pool_transfer_adapter& operator=(const network_pool_transfer_adapter&) = delete;

    // Movable
    network_pool_transfer_adapter(network_pool_transfer_adapter&&) noexcept;
    network_pool_transfer_adapter& operator=(network_pool_transfer_adapter&&) noexcept;

    /**
     * @brief Factory method using network_system's basic_thread_pool
     * @param worker_count Number of worker threads (0 = auto-detect)
     * @param pool_name Name for identification
     * @return Shared pointer to the adapter
     */
    [[nodiscard]] static std::shared_ptr<network_pool_transfer_adapter> create_basic(
        size_t worker_count = 0,
        const std::string& pool_name = "file_transfer_pool");

    // transfer_thread_pool_interface implementation
    std::future<void> submit(std::function<void()> task) override;
    std::future<void> submit_delayed(
        std::function<void()> task,
        std::chrono::milliseconds delay) override;
    std::future<void> submit_to_stage(
        std::function<void()> task,
        const std::string& stage_name) override;

    [[nodiscard]] size_t worker_count() const override;
    [[nodiscard]] bool is_running() const override;
    [[nodiscard]] size_t pending_tasks() const override;
    [[nodiscard]] size_t pending_tasks(const std::string& stage_name) const override;

private:
    struct impl;
    std::unique_ptr<impl> pimpl_;
};

#endif  // KCENON_WITH_NETWORK_SYSTEM

/**
 * @brief Fallback implementation using std::async
 *
 * This implementation is used when neither thread_system nor network_system
 * thread pools are available. It uses std::async for task execution.
 *
 * @note This fallback has limited functionality:
 *       - No delayed execution (returns immediately completed future with delay)
 *       - worker_count() returns hardware_concurrency
 *       - pending_tasks() is always 0 (no queue)
 */
class async_transfer_pool : public transfer_thread_pool_interface {
public:
    async_transfer_pool();
    ~async_transfer_pool() override;

    // Non-copyable, non-movable (stateless singleton-like)
    async_transfer_pool(const async_transfer_pool&) = delete;
    async_transfer_pool& operator=(const async_transfer_pool&) = delete;

    std::future<void> submit(std::function<void()> task) override;
    std::future<void> submit_delayed(
        std::function<void()> task,
        std::chrono::milliseconds delay) override;
    std::future<void> submit_to_stage(
        std::function<void()> task,
        const std::string& stage_name) override;

    [[nodiscard]] size_t worker_count() const override;
    [[nodiscard]] bool is_running() const override;
    [[nodiscard]] size_t pending_tasks() const override;
    [[nodiscard]] size_t pending_tasks(const std::string& stage_name) const override;

private:
    struct impl;
    std::unique_ptr<impl> pimpl_;
};

/**
 * @brief Factory for creating appropriate thread pool adapter
 *
 * This factory automatically selects the best available implementation:
 * 1. thread_system_transfer_adapter (when KCENON_WITH_THREAD_SYSTEM)
 * 2. network_pool_transfer_adapter (when KCENON_WITH_NETWORK_SYSTEM only)
 * 3. async_transfer_pool (fallback)
 */
class transfer_pool_factory {
public:
    /**
     * @brief Create the best available thread pool adapter
     * @param worker_count Number of worker threads (0 = auto-detect)
     * @param pool_name Name for identification
     * @return Shared pointer to the adapter
     */
    [[nodiscard]] static std::shared_ptr<transfer_thread_pool_interface> create(
        size_t worker_count = 0,
        const std::string& pool_name = "file_transfer_pool");

    /**
     * @brief Check if thread_system is available
     * @return true if thread_system can be used
     */
    [[nodiscard]] static constexpr bool has_thread_system() noexcept {
#if KCENON_WITH_THREAD_SYSTEM
        return true;
#else
        return false;
#endif
    }

    /**
     * @brief Check if network_system thread pool is available
     * @return true if network_system thread pool can be used
     */
    [[nodiscard]] static constexpr bool has_network_pool() noexcept {
#if KCENON_WITH_NETWORK_SYSTEM
        return true;
#else
        return false;
#endif
    }
};

}  // namespace kcenon::file_transfer::adapters
