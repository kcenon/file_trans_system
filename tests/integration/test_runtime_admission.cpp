#include "protocol_peer.h"
#include "../../src/server/server_runtime.h"

namespace kcenon::file_transfer::test {
class RuntimeAdmissionTest : public ::testing::Test {
protected:
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("fts-admission-" + transfer_id::generate().to_string());
    std::shared_ptr<detail::server_runtime> runtime;
    void SetUp() override {
        server_config config; config.storage_directory = root;
        runtime = std::make_shared<detail::server_runtime>(config, [] { return detail::server_callbacks{}; });
        ASSERT_TRUE(runtime->start());
    }
    void TearDown() override { runtime->stop(); runtime.reset(); std::error_code ec; std::filesystem::remove_all(root, ec); }
    auto wait_for_producer() -> bool {
        auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!runtime->receive_statistics().waiting_producers && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        return runtime->receive_statistics().waiting_producers == 1;
    }
};

TEST_F(RuntimeAdmissionTest, SaturationRetainsExecutingClosureUntilDestructionThenMakesProgress) {
    std::promise<void> entered, release; auto ready = entered.get_future(); auto gate = release.get_future().share();
    auto buffer = std::make_shared<std::vector<uint8_t>>(4 * 1024 * 1024);
    std::weak_ptr<std::vector<uint8_t>> retained = buffer;
    ASSERT_TRUE(runtime->post([&, buffer, gate] { entered.set_value(); gate.wait(); }, buffer->size()));
    buffer.reset(); ready.wait();
    EXPECT_FALSE(retained.expired());
    EXPECT_TRUE(runtime->post([] { throw std::runtime_error("injected consumer error"); }, 4 * 1024 * 1024));
    auto producer = std::async(std::launch::async, [&] {
        auto accepted = runtime->post([] {}, 1024);
        return accepted && retained.expired();
    });
    EXPECT_TRUE(wait_for_producer());
    auto stats = runtime->receive_statistics();
    EXPECT_EQ(stats.reserved_bytes, runtime->receive_budget);
    EXPECT_EQ(stats.admitted_frames, 2); EXPECT_EQ(stats.backpressure_events, 1);
    release.set_value(); EXPECT_TRUE(producer.get());
    runtime->stop(); stats = runtime->receive_statistics();
    EXPECT_EQ(stats.reserved_bytes, 0); EXPECT_EQ(stats.admitted_frames, 0);
    EXPECT_LE(stats.peak_bytes, runtime->receive_budget);
}

TEST_F(RuntimeAdmissionTest, StopWakesBlockedAdmissionBeforeWaitingForRunningConsumer) {
    std::promise<void> entered, release; auto ready = entered.get_future(); auto gate = release.get_future().share();
    ASSERT_TRUE(runtime->post([&] { entered.set_value(); gate.wait(); }, runtime->receive_budget)); ready.wait();
    auto producer = std::async(std::launch::async, [&] { return runtime->post([] {}, 1); });
    EXPECT_TRUE(wait_for_producer());
    auto stopped = std::async(std::launch::async, [&] { runtime->stop(); });
    EXPECT_EQ(producer.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(stopped.wait_for(20ms), std::future_status::timeout);
    release.set_value(); EXPECT_FALSE(producer.get()); stopped.get();
    EXPECT_EQ(runtime->receive_statistics().reserved_bytes, 0);
    EXPECT_FALSE(runtime->post([] {}, 1));
}

class admission_peer final : public accepted_transport {
public:
    std::function<void(const std::vector<uint8_t>&)> receive;
    std::function<void()> disconnected;
    auto send(std::vector<uint8_t>) -> result<void> override { return {}; }
    void close() override { if (disconnected) disconnected(); }
    void on_receive(std::function<void(const std::vector<uint8_t>&)> cb) override { receive = std::move(cb); }
    void on_disconnect(std::function<void()> cb) override { disconnected = std::move(cb); }
};

TEST_F(RuntimeAdmissionTest, DisconnectWakesSaturatedReceiveWithoutNeedingControlExecutor) {
    auto peer = std::make_shared<admission_peer>(); runtime->connect(peer);
    std::promise<void> entered, release; auto ready = entered.get_future(); auto gate = release.get_future().share();
    ASSERT_TRUE(runtime->post([&] { entered.set_value(); gate.wait(); }, runtime->receive_budget)); ready.wait();
    auto frame = wire::encode(message_type::heartbeat, msg_heartbeat{}); ASSERT_TRUE(frame);
    auto receiver = std::async(std::launch::async, [&] { peer->receive(frame.value()); });
    EXPECT_TRUE(wait_for_producer()); peer->close();
    EXPECT_EQ(receiver.wait_for(2s), std::future_status::ready);
    release.set_value(); receiver.get(); runtime->stop();
    EXPECT_EQ(runtime->statistics().active_connections, 0);
    EXPECT_EQ(runtime->receive_statistics().reserved_bytes, 0);
}

TEST_F(RuntimeAdmissionTest, ImpossibleAndReentrantAdmissionsFailWithoutWaiting) {
    EXPECT_FALSE(runtime->post([] {}, runtime->receive_budget + 1));
    std::promise<bool> nested; auto result = nested.get_future();
    ASSERT_TRUE(runtime->post([&] { nested.set_value(runtime->post([] {}, 1)); }));
    EXPECT_EQ(result.wait_for(2s), std::future_status::ready); EXPECT_FALSE(result.get());
}

TEST(RuntimeDecoderTest, OversizedHeaderFailsBeforeBodyAndConsumerStopsCoalescedFrames) {
    auto frame = wire::encode(message_type::heartbeat, msg_heartbeat{}); ASSERT_TRUE(frame);
    wire::stream_decoder bounded(frame_header::total_overhead);
    auto prefix = std::span<const uint8_t>(frame.value()).first(frame_header::size + 1);
    EXPECT_FALSE(bounded.feed_each(prefix, [](auto&&) { return true; }));
    auto coalesced = frame.value(); coalesced.insert(coalesced.end(), frame.value().begin(), frame.value().end());
    wire::stream_decoder decoder; unsigned delivered = 0;
    EXPECT_FALSE(decoder.feed_each(coalesced, [&](auto&&) { ++delivered; return false; }));
    EXPECT_EQ(delivered, 1);
}
} // namespace kcenon::file_transfer::test
