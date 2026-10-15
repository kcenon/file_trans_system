#include "protocol_peer.h"
#include <kcenon/file_transfer/client/file_transfer_client.h>
#include <kcenon/file_transfer/client/detail/reconnect_sequence.h>
#include <kcenon/file_transfer/server/file_transfer_server.h>
#include <fstream>
namespace kcenon::file_transfer::test {
TEST(ReconnectSequenceTest, VirtualClockChecksAttemptLimitBackoffCapAndConnectionDeadline) {
    reconnect_policy policy; policy.initial_delay=10ms; policy.max_delay=35ms; policy.max_attempts=5; policy.backoff_multiplier=2;
    std::vector<std::chrono::milliseconds> waits; std::chrono::milliseconds time{}; size_t attempts=0;
    bool result=detail::reconnect_sequence(policy,77ms,[&](auto delay) { waits.push_back(delay); time+=delay; return true; },
        [&](auto deadline) { EXPECT_EQ(deadline,77ms); ++attempts; return false; });
    EXPECT_FALSE(result); EXPECT_EQ(attempts,5); EXPECT_EQ(waits,(std::vector{10ms,20ms,35ms,35ms,35ms})); EXPECT_EQ(time,135ms);
}
TEST(ReconnectSequenceTest, SuccessCancellationAndZeroLimitStopTheSequence) {
    reconnect_policy policy; policy.initial_delay=1ms; policy.max_attempts=10; size_t attempts=0, waits=0;
    EXPECT_TRUE(detail::reconnect_sequence(policy,10ms,[&](auto) { ++waits; return true; },[&](auto) { return ++attempts==3; }));
    EXPECT_EQ(attempts,3); EXPECT_EQ(waits,3);
    attempts=0;
    EXPECT_FALSE(detail::reconnect_sequence(policy,10ms,[](auto) { return false; },[&](auto) { ++attempts; return true; })); EXPECT_EQ(attempts,0);
    policy.max_attempts=0;
    EXPECT_FALSE(detail::reconnect_sequence(policy,10ms,[](auto) { ADD_FAILURE(); return true; },[](auto) { ADD_FAILURE(); return true; }));
}
class PublicReconnectTest : public ::testing::Test {
protected:
    std::filesystem::path root=std::filesystem::temp_directory_path() / ("fts-reconnect-"+transfer_id::generate().to_string());
    std::unique_ptr<file_transfer_server> server;
    std::unique_ptr<file_transfer_client> client;
    uint16_t port{};
    std::mutex mutex; std::condition_variable cv; std::vector<connection_state> events;
    void setup(bool enable=true, size_t attempts=20, std::chrono::milliseconds delay=20ms) {
        auto s=file_transfer_server::builder().with_storage_directory(root).build(); ASSERT_TRUE(s);
        server=std::make_unique<file_transfer_server>(std::move(s.value())); port=available_port(); ASSERT_TRUE(server->start({"127.0.0.1",port}));
        reconnect_policy policy; policy.initial_delay=delay; policy.max_delay=delay*4; policy.max_attempts=attempts;
        auto c=file_transfer_client::builder().with_auto_reconnect(enable,policy).with_connect_timeout(100ms).build(); ASSERT_TRUE(c);
        client=std::make_unique<file_transfer_client>(std::move(c.value()));
        client->on_connection_state_changed([this](auto state) { std::lock_guard lock(mutex); events.push_back(state); cv.notify_all(); });
        ASSERT_TRUE(client->connect({"127.0.0.1",port})); ASSERT_TRUE(wait(connection_state::connected));
    }
    bool wait(connection_state state, size_t count=1) {
        std::unique_lock lock(mutex); return cv.wait_for(lock,3s,[&] { return std::count(events.begin(),events.end(),state)>=static_cast<long>(count); });
    }
    void TearDown() override { client.reset(); if(server && server->is_running()) (void)server->stop(); server.reset(); std::error_code ec; std::filesystem::remove_all(root,ec); }
};
TEST_F(PublicReconnectTest, ServerRestartReconnectsAndPublicRequestsWork) {
    setup(); ASSERT_TRUE(server->stop()); ASSERT_TRUE(wait(connection_state::reconnecting));
    ASSERT_TRUE(server->start({"127.0.0.1",port})); ASSERT_TRUE(wait(connection_state::connected,2)); ASSERT_TRUE(client->is_connected());
    { std::ofstream f(root / "known"); f << "after reconnect"; }
    auto files=client->list_files(); ASSERT_TRUE(files); ASSERT_EQ(files.value().size(),1); EXPECT_EQ(files.value()[0].filename,"known");
    EXPECT_GE(client->get_statistics().total_retries,1);
}
TEST_F(PublicReconnectTest, DisabledReconnectDoesNotRetryAfterServerReturns) {
    setup(false); ASSERT_TRUE(server->stop()); ASSERT_TRUE(wait(connection_state::disconnected)); ASSERT_TRUE(server->start({"127.0.0.1",port}));
    std::unique_lock lock(mutex); EXPECT_FALSE(cv.wait_for(lock,150ms,[&] { return std::count(events.begin(),events.end(),connection_state::connected)>1; }));
    EXPECT_EQ(client->get_statistics().total_retries,0); EXPECT_FALSE(client->is_connected());
}
TEST_F(PublicReconnectTest, AttemptLimitTerminatesWithoutAnotherSequence) {
    setup(true,3,1ms);
    // Stop drains existing sessions before closing the listener. A retry in
    // that interval can connect successfully and legitimately begin a new
    // sequence when the draining server closes it. Exercise one unavailable
    // endpoint sequence by releasing retries only after stop has completed.
    std::promise<void> stopped;
    auto ready = stopped.get_future().share();
    client->on_connection_state_changed([this, ready](auto state) {
        if (state == connection_state::reconnecting)
            EXPECT_EQ(ready.wait_for(3s), std::future_status::ready);
        std::lock_guard lock(mutex); events.push_back(state); cv.notify_all();
    });
    auto result = server->stop(); stopped.set_value(); ASSERT_TRUE(result);
    ASSERT_TRUE(wait(connection_state::disconnected));
    std::unique_lock lock(mutex);
    EXPECT_FALSE(cv.wait_for(lock, 30ms, [&] {
        return std::count(events.begin(), events.end(), connection_state::reconnecting) > 1;
    }));
    EXPECT_EQ(client->get_statistics().total_retries,3); EXPECT_FALSE(client->is_connected());
}
TEST_F(PublicReconnectTest, ExplicitDisconnectCancelsBackoffBeforeAttempt) {
    setup(true,20,2s); ASSERT_TRUE(server->stop()); ASSERT_TRUE(wait(connection_state::reconnecting));
    auto start=std::chrono::steady_clock::now(); ASSERT_TRUE(client->disconnect()); EXPECT_LT(std::chrono::steady_clock::now()-start,200ms);
    ASSERT_TRUE(server->start({"127.0.0.1",port})); EXPECT_EQ(client->get_statistics().total_retries,0); EXPECT_FALSE(client->is_connected());
}
TEST_F(PublicReconnectTest, DestructionCancelsBackoffAndJoinsCallbacks) {
    setup(true,20,2s); ASSERT_TRUE(server->stop()); ASSERT_TRUE(wait(connection_state::reconnecting));
    auto start=std::chrono::steady_clock::now(); client.reset(); EXPECT_LT(std::chrono::steady_clock::now()-start,200ms);
    ASSERT_TRUE(server->start({"127.0.0.1",port}));
}
TEST_F(PublicReconnectTest, StateCallbackCanExplicitlyDisconnect) {
    setup(); client->on_connection_state_changed([&](auto state) {
        if(state==connection_state::reconnecting) EXPECT_TRUE(client->disconnect());
        std::lock_guard lock(mutex); events.push_back(state); cv.notify_all();
    });
    ASSERT_TRUE(server->stop()); ASSERT_TRUE(wait(connection_state::disconnected)); EXPECT_FALSE(client->is_connected());
}
TEST_F(PublicReconnectTest, ExplicitConnectReplacesAnEndpointDuringBackoff) {
    setup(true,20,2s); ASSERT_TRUE(server->stop()); ASSERT_TRUE(wait(connection_state::reconnecting));
    auto new_port=available_port(); ASSERT_TRUE(server->start({"127.0.0.1",new_port}));
    ASSERT_TRUE(client->connect({"127.0.0.1",new_port})); ASSERT_TRUE(wait(connection_state::connected,2));
    EXPECT_TRUE(client->is_connected()); EXPECT_EQ(client->get_statistics().total_retries,0);
}
TEST(ReconnectSequenceTest, InvalidPolicyIsRejected) {
    reconnect_policy policy; policy.backoff_multiplier=0.5; EXPECT_FALSE(file_transfer_client::builder().with_auto_reconnect(true,policy).build());
    policy.backoff_multiplier=2; policy.initial_delay=-1ms; EXPECT_FALSE(file_transfer_client::builder().with_auto_reconnect(true,policy).build());
}
} // namespace kcenon::file_transfer::test
