// BSD 3-Clause License
//
// Copyright (c) 2025, kcenon
// All rights reserved.

/**
 * @file thread_pool_adapter.cpp
 * @brief Thread pool adapter implementation for file_trans_system
 */

#include "kcenon/file_transfer/adapters/thread_pool_adapter.h"

#include <thread>

#if KCENON_WITH_THREAD_SYSTEM
// Suppress deprecation warnings from thread_system headers
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <kcenon/thread/core/job.h>
#include <kcenon/thread/core/job_queue.h>
#include <kcenon/thread/core/thread_worker.h>
#pragma clang diagnostic pop
#endif

namespace kcenon::file_transfer::adapters {

// ============================================================================
// Stage tracking helper (shared implementation)
// ============================================================================

namespace {

class stage_tracker {
public:
    void increment(const std::string& stage_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = counts_.find(stage_name);
        if (it == counts_.end()) {
            counts_.emplace(stage_name, 1);
        } else {
            it->second.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void decrement(const std::string& stage_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = counts_.find(stage_name);
        if (it != counts_.end()) {
            auto current = it->second.load();
            if (current > 0) {
                it->second.fetch_sub(1, std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] size_t count(const std::string& stage_name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = counts_.find(stage_name);
        if (it != counts_.end()) {
            return it->second.load();
        }
        return 0;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::atomic<size_t>> counts_;
};

}  // namespace

// ============================================================================
// thread_system_transfer_adapter implementation
// ============================================================================

#if KCENON_WITH_THREAD_SYSTEM

/**
 * @brief Simple job that wraps a function for thread_system execution
 */
class function_job : public kcenon::thread::job {
public:
    explicit function_job(std::function<void()> func, const std::string& name = "function_job")
        : job(name), func_(std::move(func)) {}

    [[nodiscard]] auto do_work() -> common::VoidResult override {
        if (func_) {
            func_();
        }
        return common::ok();
    }

private:
    std::function<void()> func_;
};

// Implementation struct for thread_system_transfer_adapter (PIMPL for mutex)
struct thread_system_transfer_adapter::impl {
    std::shared_ptr<kcenon::thread::thread_pool> pool;
    std::string pool_name;
    size_t worker_count{0};
    stage_tracker tracker;
};

thread_system_transfer_adapter::thread_system_transfer_adapter(
    std::shared_ptr<kcenon::thread::thread_pool> pool,
    const std::string& pool_name,
    size_t worker_count)
    : pimpl_(std::make_unique<impl>()) {
    pimpl_->pool = std::move(pool);
    pimpl_->pool_name = pool_name;
    pimpl_->worker_count = worker_count;
}

thread_system_transfer_adapter::~thread_system_transfer_adapter() = default;

thread_system_transfer_adapter::thread_system_transfer_adapter(
    thread_system_transfer_adapter&&) noexcept = default;

thread_system_transfer_adapter& thread_system_transfer_adapter::operator=(
    thread_system_transfer_adapter&&) noexcept = default;

std::shared_ptr<thread_system_transfer_adapter>
thread_system_transfer_adapter::create_default(size_t worker_count,
                                                const std::string& pool_name) {
    if (worker_count == 0) {
        worker_count = std::thread::hardware_concurrency();
        if (worker_count == 0) {
            worker_count = 4;
        }
    }

    auto pool = std::make_shared<kcenon::thread::thread_pool>(pool_name);

    // Add workers to the pool
    for (size_t i = 0; i < worker_count; ++i) {
        auto worker = std::make_unique<kcenon::thread::thread_worker>();
        worker->set_job_queue(pool->get_job_queue());
        pool->enqueue(std::move(worker));
    }

    // Start the pool
    pool->start();

    return std::make_shared<thread_system_transfer_adapter>(std::move(pool), pool_name, worker_count);
}

std::future<void> thread_system_transfer_adapter::submit(
    std::function<void()> task) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();

    auto wrapped_task = [task = std::move(task), promise]() {
        try {
            task();
            promise->set_value();
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };

    auto job = std::make_unique<function_job>(std::move(wrapped_task), "transfer_task");
    pimpl_->pool->enqueue(std::move(job));

    return future;
}

std::future<void> thread_system_transfer_adapter::submit_delayed(
    std::function<void()> task, std::chrono::milliseconds delay) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();

    // Create a task that sleeps then executes
    auto delayed_task = [task = std::move(task), promise, delay]() {
        std::this_thread::sleep_for(delay);
        try {
            task();
            promise->set_value();
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };

    auto job = std::make_unique<function_job>(std::move(delayed_task), "delayed_transfer_task");
    pimpl_->pool->enqueue(std::move(job));

    return future;
}

std::future<void> thread_system_transfer_adapter::submit_to_stage(
    std::function<void()> task, const std::string& stage_name) {
    pimpl_->tracker.increment(stage_name);

    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();

    // Capture pimpl_ for stage tracking (raw pointer is safe - adapter outlives tasks)
    auto* tracker = &pimpl_->tracker;
    auto wrapped_task = [task = std::move(task), promise, tracker,
                         stage = stage_name]() {
        try {
            task();
            promise->set_value();
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
        tracker->decrement(stage);
    };

    auto job = std::make_unique<function_job>(std::move(wrapped_task), "staged_transfer_task");
    pimpl_->pool->enqueue(std::move(job));

    return future;
}

size_t thread_system_transfer_adapter::worker_count() const {
    return pimpl_->worker_count;
}

bool thread_system_transfer_adapter::is_running() const {
    return pimpl_->pool != nullptr;
}

size_t thread_system_transfer_adapter::pending_tasks() const {
    if (pimpl_->pool) {
        auto queue = pimpl_->pool->get_job_queue();
        return queue ? queue->size() : 0;
    }
    return 0;
}

size_t thread_system_transfer_adapter::pending_tasks(
    const std::string& stage_name) const {
    return pimpl_->tracker.count(stage_name);
}

std::shared_ptr<kcenon::thread::thread_pool>
thread_system_transfer_adapter::underlying_pool() const {
    return pimpl_->pool;
}

std::string thread_system_transfer_adapter::pool_name() const {
    return pimpl_->pool_name;
}

#endif  // KCENON_WITH_THREAD_SYSTEM

// ============================================================================
// network_pool_transfer_adapter implementation
// ============================================================================

#if KCENON_WITH_NETWORK_SYSTEM

// Implementation struct for network_pool_transfer_adapter (PIMPL for mutex)
struct network_pool_transfer_adapter::impl {
    std::shared_ptr<kcenon::network::integration::thread_pool_interface> pool;
    std::string pool_name;
    stage_tracker tracker;
};

network_pool_transfer_adapter::network_pool_transfer_adapter(
    std::shared_ptr<kcenon::network::integration::thread_pool_interface> pool,
    const std::string& pool_name)
    : pimpl_(std::make_unique<impl>()) {
    pimpl_->pool = std::move(pool);
    pimpl_->pool_name = pool_name;
}

network_pool_transfer_adapter::~network_pool_transfer_adapter() = default;

network_pool_transfer_adapter::network_pool_transfer_adapter(
    network_pool_transfer_adapter&&) noexcept = default;

network_pool_transfer_adapter& network_pool_transfer_adapter::operator=(
    network_pool_transfer_adapter&&) noexcept = default;

std::shared_ptr<network_pool_transfer_adapter>
network_pool_transfer_adapter::create_basic(size_t worker_count,
                                             const std::string& pool_name) {
    auto pool =
        std::make_shared<kcenon::network::integration::basic_thread_pool>(
            worker_count);
    return std::make_shared<network_pool_transfer_adapter>(std::move(pool),
                                                            pool_name);
}

std::future<void> network_pool_transfer_adapter::submit(
    std::function<void()> task) {
    return pimpl_->pool->submit(std::move(task));
}

std::future<void> network_pool_transfer_adapter::submit_delayed(
    std::function<void()> task, std::chrono::milliseconds delay) {
    return pimpl_->pool->submit_delayed(std::move(task), delay);
}

std::future<void> network_pool_transfer_adapter::submit_to_stage(
    std::function<void()> task, const std::string& stage_name) {
    pimpl_->tracker.increment(stage_name);

    // Capture pimpl_ for stage tracking
    auto* tracker = &pimpl_->tracker;
    auto wrapped_task = [task = std::move(task), tracker, stage = stage_name]() {
        try {
            task();
        } catch (...) {
            tracker->decrement(stage);
            throw;
        }
        tracker->decrement(stage);
    };

    return pimpl_->pool->submit(std::move(wrapped_task));
}

size_t network_pool_transfer_adapter::worker_count() const {
    return pimpl_->pool ? pimpl_->pool->worker_count() : 0;
}

bool network_pool_transfer_adapter::is_running() const {
    return pimpl_->pool ? pimpl_->pool->is_running() : false;
}

size_t network_pool_transfer_adapter::pending_tasks() const {
    return pimpl_->pool ? pimpl_->pool->pending_tasks() : 0;
}

size_t network_pool_transfer_adapter::pending_tasks(
    const std::string& stage_name) const {
    return pimpl_->tracker.count(stage_name);
}

#endif  // KCENON_WITH_NETWORK_SYSTEM

// ============================================================================
// async_transfer_pool implementation
// ============================================================================

// Implementation struct for async_transfer_pool (PIMPL for mutex)
struct async_transfer_pool::impl {
    std::atomic<size_t> active_tasks{0};
    stage_tracker tracker;
};

async_transfer_pool::async_transfer_pool()
    : pimpl_(std::make_unique<impl>()) {}

async_transfer_pool::~async_transfer_pool() = default;

std::future<void> async_transfer_pool::submit(std::function<void()> task) {
    pimpl_->active_tasks.fetch_add(1, std::memory_order_relaxed);

    auto* pimpl = pimpl_.get();
    return std::async(std::launch::async,
                      [pimpl, task = std::move(task)]() {
                          try {
                              task();
                          } catch (...) {
                              pimpl->active_tasks.fetch_sub(1, std::memory_order_relaxed);
                              throw;
                          }
                          pimpl->active_tasks.fetch_sub(1, std::memory_order_relaxed);
                      });
}

std::future<void> async_transfer_pool::submit_delayed(
    std::function<void()> task, std::chrono::milliseconds delay) {
    pimpl_->active_tasks.fetch_add(1, std::memory_order_relaxed);

    auto* pimpl = pimpl_.get();
    return std::async(std::launch::async,
                      [pimpl, task = std::move(task), delay]() {
                          std::this_thread::sleep_for(delay);
                          try {
                              task();
                          } catch (...) {
                              pimpl->active_tasks.fetch_sub(1, std::memory_order_relaxed);
                              throw;
                          }
                          pimpl->active_tasks.fetch_sub(1, std::memory_order_relaxed);
                      });
}

std::future<void> async_transfer_pool::submit_to_stage(
    std::function<void()> task, const std::string& stage_name) {
    pimpl_->tracker.increment(stage_name);
    pimpl_->active_tasks.fetch_add(1, std::memory_order_relaxed);

    auto* pimpl = pimpl_.get();
    return std::async(std::launch::async,
                      [pimpl, task = std::move(task), stage = stage_name]() {
                          try {
                              task();
                          } catch (...) {
                              pimpl->active_tasks.fetch_sub(1, std::memory_order_relaxed);
                              pimpl->tracker.decrement(stage);
                              throw;
                          }
                          pimpl->active_tasks.fetch_sub(1, std::memory_order_relaxed);
                          pimpl->tracker.decrement(stage);
                      });
}

size_t async_transfer_pool::worker_count() const {
    auto count = std::thread::hardware_concurrency();
    return count > 0 ? count : 4;
}

bool async_transfer_pool::is_running() const { return true; }

size_t async_transfer_pool::pending_tasks() const {
    return pimpl_->active_tasks.load(std::memory_order_relaxed);
}

size_t async_transfer_pool::pending_tasks(const std::string& stage_name) const {
    return pimpl_->tracker.count(stage_name);
}

// ============================================================================
// transfer_pool_factory implementation
// ============================================================================

std::shared_ptr<transfer_thread_pool_interface> transfer_pool_factory::create(
    size_t worker_count, const std::string& pool_name) {
    // Priority: thread_system > network_system > async fallback

#if KCENON_WITH_THREAD_SYSTEM
    return thread_system_transfer_adapter::create_default(worker_count,
                                                           pool_name);
#elif KCENON_WITH_NETWORK_SYSTEM
    return network_pool_transfer_adapter::create_basic(worker_count, pool_name);
#else
    (void)worker_count;
    (void)pool_name;
    return std::make_shared<async_transfer_pool>();
#endif
}

}  // namespace kcenon::file_transfer::adapters
