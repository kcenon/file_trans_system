/**
 * @file test_bandwidth_limiter.cpp
 * @brief Unit tests for bandwidth limiter
 */

#include <gtest/gtest.h>

#include <kcenon/file_transfer/core/bandwidth_limiter.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

namespace kcenon::file_transfer::test {

class BandwidthLimiterTest : public ::testing::Test {
protected:
    static constexpr std::size_t MB = 1024 * 1024;
    static constexpr std::size_t KB = 1024;
};

// Basic construction tests

TEST_F(BandwidthLimiterTest, Construction_WithLimit) {
    bandwidth_limiter limiter(10 * MB);
    EXPECT_EQ(limiter.get_limit(), 10 * MB);
    EXPECT_TRUE(limiter.is_enabled());
}

TEST_F(BandwidthLimiterTest, Construction_ZeroMeansUnlimited) {
    bandwidth_limiter limiter(0);
    EXPECT_EQ(limiter.get_limit(), 0);
    EXPECT_FALSE(limiter.is_enabled());
}

TEST_F(BandwidthLimiterTest, BucketCapacity_EqualsOneSecondWorth) {
    bandwidth_limiter limiter(10 * MB);
    EXPECT_EQ(limiter.bucket_capacity(), 10 * MB);
}

// Enable/Disable tests

TEST_F(BandwidthLimiterTest, Disable_StopsEnforcement) {
    bandwidth_limiter limiter(1 * KB);  // Small limit
    EXPECT_TRUE(limiter.is_enabled());

    limiter.disable();
    EXPECT_FALSE(limiter.is_enabled());

    // Should not block even with large acquire
    auto start = std::chrono::steady_clock::now();
    limiter.acquire(1 * MB);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::milliseconds(100));
}

TEST_F(BandwidthLimiterTest, Enable_RestoresEnforcement) {
    bandwidth_limiter limiter(10 * MB);
    limiter.disable();
    EXPECT_FALSE(limiter.is_enabled());

    limiter.enable();
    EXPECT_TRUE(limiter.is_enabled());
}

TEST_F(BandwidthLimiterTest, Enable_DoesNothingIfLimitZero) {
    bandwidth_limiter limiter(0);
    limiter.enable();
    EXPECT_FALSE(limiter.is_enabled());
}

// Dynamic limit adjustment tests

TEST_F(BandwidthLimiterTest, SetLimit_ChangesLimit) {
    bandwidth_limiter limiter(10 * MB);
    EXPECT_EQ(limiter.get_limit(), 10 * MB);

    limiter.set_limit(20 * MB);
    EXPECT_EQ(limiter.get_limit(), 20 * MB);
    EXPECT_TRUE(limiter.is_enabled());
}

TEST_F(BandwidthLimiterTest, SetLimit_ZeroDisables) {
    bandwidth_limiter limiter(10 * MB);
    EXPECT_TRUE(limiter.is_enabled());

    limiter.set_limit(0);
    EXPECT_EQ(limiter.get_limit(), 0);
    EXPECT_FALSE(limiter.is_enabled());
}

TEST_F(BandwidthLimiterTest, SetLimit_FromZeroEnables) {
    bandwidth_limiter limiter(0);
    EXPECT_FALSE(limiter.is_enabled());

    limiter.set_limit(10 * MB);
    EXPECT_EQ(limiter.get_limit(), 10 * MB);
    EXPECT_TRUE(limiter.is_enabled());
}

// Acquire tests

TEST_F(BandwidthLimiterTest, Acquire_ZeroBytesImmediate) {
    bandwidth_limiter limiter(1 * KB);

    auto start = std::chrono::steady_clock::now();
    limiter.acquire(0);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::milliseconds(10));
}

TEST_F(BandwidthLimiterTest, Acquire_WithinBucketImmediate) {
    bandwidth_limiter limiter(10 * MB);

    auto start = std::chrono::steady_clock::now();
    limiter.acquire(5 * MB);  // Less than bucket capacity
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::milliseconds(100));
}

TEST_F(BandwidthLimiterTest, Acquire_ExceedsBucketBlocks) {
    constexpr std::size_t limit = 100 * KB;  // 100 KB/s
    bandwidth_limiter limiter(limit);

    // First acquire uses up the bucket
    limiter.acquire(limit);

    // Second acquire should block for approximately 1 second
    auto start = std::chrono::steady_clock::now();
    limiter.acquire(limit);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    // Should take approximately 1 second (within 20% tolerance)
    EXPECT_GE(elapsed.count(), 800);
    EXPECT_LE(elapsed.count(), 1200);
}

// TryAcquire tests

TEST_F(BandwidthLimiterTest, TryAcquire_ZeroBytesSucceeds) {
    bandwidth_limiter limiter(1 * KB);
    EXPECT_TRUE(limiter.try_acquire(0));
}

TEST_F(BandwidthLimiterTest, TryAcquire_WithinTokensSucceeds) {
    bandwidth_limiter limiter(10 * MB);
    EXPECT_TRUE(limiter.try_acquire(5 * MB));
}

TEST_F(BandwidthLimiterTest, TryAcquire_ExceedsTokensFails) {
    bandwidth_limiter limiter(10 * MB);

    // Use up all tokens
    limiter.acquire(10 * MB);

    // Try to acquire more should fail immediately
    EXPECT_FALSE(limiter.try_acquire(1 * MB));
}

TEST_F(BandwidthLimiterTest, TryAcquire_DisabledAlwaysSucceeds) {
    bandwidth_limiter limiter(0);
    EXPECT_TRUE(limiter.try_acquire(100 * MB));
}

// Reset tests

TEST_F(BandwidthLimiterTest, Reset_RefillsBucket) {
    bandwidth_limiter limiter(10 * MB);

    // Use up all tokens
    limiter.acquire(10 * MB);
    EXPECT_LT(limiter.available_tokens(), 10 * MB);

    // Reset should refill
    limiter.reset();
    EXPECT_EQ(limiter.available_tokens(), 10 * MB);
}

// Available tokens tests

TEST_F(BandwidthLimiterTest, AvailableTokens_InitiallyFull) {
    bandwidth_limiter limiter(10 * MB);
    EXPECT_EQ(limiter.available_tokens(), 10 * MB);
}

TEST_F(BandwidthLimiterTest, AvailableTokens_DecreasesAfterAcquire) {
    bandwidth_limiter limiter(10 * MB);

    limiter.acquire(3 * MB);
    // Allow small tolerance for token refill during test execution
    EXPECT_LE(limiter.available_tokens(), 7 * MB + 100 * KB);
}

TEST_F(BandwidthLimiterTest, AvailableTokens_RefillsOverTime) {
    constexpr std::size_t limit = 10 * MB;
    bandwidth_limiter limiter(limit);

    // Use up half the tokens
    limiter.acquire(5 * MB);
    auto initial_tokens = limiter.available_tokens();

    // Wait a bit for refill
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Tokens should have increased
    EXPECT_GT(limiter.available_tokens(), initial_tokens);
}

// Rate limiting accuracy tests

TEST_F(BandwidthLimiterTest, RateLimiting_WithinTolerance) {
    constexpr std::size_t limit = 500 * KB;  // 500 KB/s
    bandwidth_limiter limiter(limit);

    // Drain the initial bucket to start from a known state
    limiter.acquire(limit);

    std::size_t total_acquired = 0;
    auto start = std::chrono::steady_clock::now();

    // Acquire in chunks for 2 seconds
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
        limiter.acquire(50 * KB);
        total_acquired += 50 * KB;
    }

    auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start);

    // Calculate actual rate
    double actual_rate = static_cast<double>(total_acquired) / elapsed.count();
    double expected_rate = static_cast<double>(limit);

    // Should be within 15% of target rate (allow more tolerance due to timing)
    EXPECT_GE(actual_rate, expected_rate * 0.85);
    EXPECT_LE(actual_rate, expected_rate * 1.15);
}

// Thread safety tests

TEST_F(BandwidthLimiterTest, ThreadSafety_ConcurrentAcquire) {
    constexpr std::size_t limit = 10 * MB;
    bandwidth_limiter limiter(limit);

    // Drain initial bucket
    limiter.acquire(limit);

    std::atomic<std::size_t> total_acquired{0};
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;

    constexpr int num_threads = 4;
    constexpr std::size_t chunk_size = 100 * KB;

    auto start = std::chrono::steady_clock::now();

    // Run for a fixed duration instead of fixed amount
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&limiter, &total_acquired, &stop, chunk_size] {
            while (!stop.load(std::memory_order_relaxed)) {
                if (limiter.try_acquire(chunk_size)) {
                    total_acquired.fetch_add(chunk_size, std::memory_order_relaxed);
                }
            }
        });
    }

    // Run for 500ms
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop = true;

    for (auto& t : threads) {
        t.join();
    }

    auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start);

    // Verify that rate is limited (with tolerance for timing)
    double actual_rate = static_cast<double>(total_acquired) / elapsed.count();
    // Rate should be approximately equal to limit (within 50% tolerance due to try_acquire)
    EXPECT_LE(actual_rate, static_cast<double>(limit) * 1.5);
}

TEST_F(BandwidthLimiterTest, ThreadSafety_DynamicLimitChange) {
    bandwidth_limiter limiter(10 * MB);

    std::atomic<bool> done{false};

    // Thread 1: continuously acquires
    std::thread acquire_thread([&limiter, &done] {
        while (!done) {
            limiter.acquire(100 * KB);
        }
    });

    // Thread 2: changes limit
    std::thread limit_thread([&limiter, &done] {
        for (int i = 0; i < 10; ++i) {
            limiter.set_limit((i + 1) * 1 * MB);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        done = true;
    });

    acquire_thread.join();
    limit_thread.join();

    // If we got here without crash/hang, thread safety is working
    SUCCEED();
}

// Async acquire tests

TEST_F(BandwidthLimiterTest, AcquireAsync_ReturnsValidFuture) {
    bandwidth_limiter limiter(10 * MB);

    auto future = limiter.acquire_async(1 * MB);
    EXPECT_TRUE(future.valid());

    future.wait();
    // Should complete without exception
}

// Scoped acquire tests

TEST_F(BandwidthLimiterTest, ScopedAcquire_AcquiresOnConstruction) {
    bandwidth_limiter limiter(10 * MB);

    {
        scoped_bandwidth_acquire guard(limiter, 5 * MB);
        // Tokens should be reduced (with small tolerance for refill)
        EXPECT_LE(limiter.available_tokens(), 5 * MB + 100 * KB);
    }
}

// Move semantics tests

TEST_F(BandwidthLimiterTest, MoveConstruction_TransfersState) {
    bandwidth_limiter limiter1(10 * MB);
    limiter1.acquire(5 * MB);

    bandwidth_limiter limiter2(std::move(limiter1));

    EXPECT_EQ(limiter2.get_limit(), 10 * MB);
    EXPECT_TRUE(limiter2.is_enabled());
}

TEST_F(BandwidthLimiterTest, MoveAssignment_TransfersState) {
    bandwidth_limiter limiter1(10 * MB);
    bandwidth_limiter limiter2(5 * MB);

    limiter2 = std::move(limiter1);

    EXPECT_EQ(limiter2.get_limit(), 10 * MB);
    EXPECT_TRUE(limiter2.is_enabled());
}

// Edge cases

TEST_F(BandwidthLimiterTest, EdgeCase_VerySmallLimit) {
    bandwidth_limiter limiter(100);  // 100 bytes/sec

    auto start = std::chrono::steady_clock::now();
    limiter.acquire(100);
    limiter.acquire(100);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    // Second acquire should have blocked
    EXPECT_GE(elapsed.count(), 800);
}

TEST_F(BandwidthLimiterTest, EdgeCase_VeryLargeLimit) {
    bandwidth_limiter limiter(1024ULL * 1024 * 1024 * 10);  // 10 GB/s

    auto start = std::chrono::steady_clock::now();
    limiter.acquire(1 * MB);
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Should be nearly instant
    EXPECT_LT(elapsed, std::chrono::milliseconds(100));
}

TEST_F(BandwidthLimiterTest, EdgeCase_AcquireExactBucketCapacity) {
    bandwidth_limiter limiter(10 * MB);

    auto start = std::chrono::steady_clock::now();
    limiter.acquire(10 * MB);  // Exactly bucket capacity
    auto elapsed = std::chrono::steady_clock::now() - start;

    // First acquire should be immediate
    EXPECT_LT(elapsed, std::chrono::milliseconds(100));
}

}  // namespace kcenon::file_transfer::test
