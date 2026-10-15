#include <gtest/gtest.h>
#include <kcenon/file_transfer/adapters/thread_pool_adapter.h>

namespace kcenon::file_transfer::adapters {
using namespace std::chrono_literals;
class ThreadPoolAdapterTest : public ::testing::TestWithParam<int> {
protected:
    auto create() -> std::shared_ptr<transfer_thread_pool_interface> {
        switch (GetParam()) {
        case 0: return std::make_shared<async_transfer_pool>();
        case 1: return network_pool_transfer_adapter::create_basic(1, "adapter-test");
        default: return transfer_pool_factory::create(1, "adapter-test");
        }
    }
};
TEST_P(ThreadPoolAdapterTest, TasksExceptionsAndDelayedWorkHaveObservableOutcomes) {
    auto pool = create(); ASSERT_TRUE(pool); EXPECT_TRUE(pool->is_running()); EXPECT_GT(pool->worker_count(), 0);
    int value = 0; auto task = pool->submit([&] { value = 17; }); task.get(); EXPECT_EQ(value, 17);
    auto failed = pool->submit([] { throw std::runtime_error("task failed"); }); EXPECT_THROW(failed.get(), std::runtime_error);
    auto began = std::chrono::steady_clock::now();
    pool->submit_delayed([&] { value = 42; }, 5ms).get();
    EXPECT_GE(std::chrono::steady_clock::now() - began, 5ms); EXPECT_EQ(value, 42);
    auto delayed_error = pool->submit_delayed([] { throw std::runtime_error("delayed failure"); }, 1ms);
    EXPECT_THROW(delayed_error.get(), std::runtime_error);
}
TEST_P(ThreadPoolAdapterTest, StageCountsReleaseOnSuccessAndFailure) {
    auto pool = create(); std::promise<void> entered, release;
    auto ready = entered.get_future(); auto gate = release.get_future().share();
    auto task = pool->submit_to_stage([&] { entered.set_value(); gate.wait(); }, "write");
    ready.wait(); EXPECT_EQ(pool->pending_tasks("write"), 1); EXPECT_EQ(pool->pending_tasks("unknown"), 0);
    (void)pool->pending_tasks(); release.set_value(); task.get();
    EXPECT_EQ(pool->pending_tasks("write"), 0);
    auto failed = pool->submit_to_stage([] { throw std::runtime_error("write failed"); }, "write");
    EXPECT_THROW(failed.get(), std::runtime_error); EXPECT_EQ(pool->pending_tasks("write"), 0);
}
INSTANTIATE_TEST_SUITE_P(AllBackends, ThreadPoolAdapterTest, ::testing::Values(0, 1, 2));

TEST(ThreadPoolAdapterLifetimeTest, AsyncWorkOwnsCountersAfterAdapterDestruction) {
    auto pool = std::make_unique<async_transfer_pool>();
    std::promise<void> entered, release; auto ready = entered.get_future(); auto gate = release.get_future().share();
    auto task = pool->submit_to_stage([&] { entered.set_value(); gate.wait(); }, "write");
    ready.wait(); pool.reset(); release.set_value(); EXPECT_NO_THROW(task.get());
}
TEST(ThreadPoolAdapterLifetimeTest, ExternallyOwnedNetworkPoolOutlivesStageAdapter) {
    auto underlying = std::make_shared<network::integration::basic_thread_pool>(1);
    auto adapter = std::make_unique<network_pool_transfer_adapter>(underlying);
    std::promise<void> entered, release; auto ready = entered.get_future(); auto gate = release.get_future().share();
    auto task = adapter->submit_to_stage([&] { entered.set_value(); gate.wait(); }, "write");
    ready.wait(); adapter.reset(); release.set_value(); EXPECT_NO_THROW(task.get());
}
} // namespace kcenon::file_transfer::adapters
