/**
 * @file test_transport_interface.cpp
 * @brief Unit tests for transport abstraction layer
 */

#include <gtest/gtest.h>

#include <mutex>

#include "kcenon/file_transfer/transport/transport_interface.h"
#include "kcenon/file_transfer/transport/transport_config.h"
#include "kcenon/file_transfer/transport/tcp_transport.h"

namespace kcenon::file_transfer {
namespace {

// ============================================================================
// Transport Config Tests
// ============================================================================

class TransportConfigTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(TransportConfigTest, DefaultTcpConfig) {
    tcp_transport_config config;

    EXPECT_EQ(config.type, transport_type::tcp);
    EXPECT_EQ(config.connect_timeout, std::chrono::milliseconds{30000});
    EXPECT_TRUE(config.tcp_nodelay);
    EXPECT_TRUE(config.reuse_address);
    EXPECT_FALSE(config.reuse_port);
    EXPECT_TRUE(config.keep_alive);
}

TEST_F(TransportConfigTest, DefaultQuicConfig) {
    quic_transport_config config;

    EXPECT_EQ(config.type, transport_type::quic);
    EXPECT_TRUE(config.enable_0rtt);
    EXPECT_EQ(config.max_idle_timeout, std::chrono::seconds{30});
    EXPECT_EQ(config.max_bidi_streams, 100);
}

TEST_F(TransportConfigTest, TcpConfigBuilder) {
    auto config = transport_config_builder::tcp()
        .with_connect_timeout(std::chrono::seconds{5})
        .with_tcp_nodelay(false)
        .with_reuse_address(false)
        .with_keep_alive(true, std::chrono::seconds{30})
        .with_retry(5, std::chrono::milliseconds{500})
        .build_tcp();

    EXPECT_EQ(config.type, transport_type::tcp);
    EXPECT_EQ(config.connect_timeout, std::chrono::seconds{5});
    EXPECT_FALSE(config.tcp_nodelay);
    EXPECT_FALSE(config.reuse_address);
    EXPECT_TRUE(config.keep_alive);
    EXPECT_EQ(config.keep_alive_interval, std::chrono::seconds{30});
    EXPECT_EQ(config.max_retry_attempts, 5);
    EXPECT_EQ(config.retry_delay, std::chrono::milliseconds{500});
}

TEST_F(TransportConfigTest, QuicConfigBuilder) {
    auto config = transport_config_builder::quic()
        .with_connect_timeout(std::chrono::seconds{10})
        .with_0rtt(false)
        .with_max_idle_timeout(std::chrono::seconds{60})
        .build_quic();

    EXPECT_EQ(config.type, transport_type::quic);
    EXPECT_EQ(config.connect_timeout, std::chrono::seconds{10});
    EXPECT_FALSE(config.enable_0rtt);
    EXPECT_EQ(config.max_idle_timeout, std::chrono::seconds{60});
}

TEST_F(TransportConfigTest, BufferSizeConfiguration) {
    auto config = transport_config_builder::tcp()
        .with_buffer_sizes(64 * 1024, 128 * 1024)
        .build_tcp();

    EXPECT_EQ(config.send_buffer_size, 64 * 1024);
    EXPECT_EQ(config.receive_buffer_size, 128 * 1024);
}

// ============================================================================
// Transport State Tests
// ============================================================================

class TransportStateTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(TransportStateTest, StateToString) {
    EXPECT_STREQ(to_string(transport_state::disconnected), "disconnected");
    EXPECT_STREQ(to_string(transport_state::connecting), "connecting");
    EXPECT_STREQ(to_string(transport_state::connected), "connected");
    EXPECT_STREQ(to_string(transport_state::disconnecting), "disconnecting");
    EXPECT_STREQ(to_string(transport_state::error), "error");
}

TEST_F(TransportStateTest, TransportTypeToString) {
    EXPECT_STREQ(to_string(transport_type::tcp), "tcp");
    EXPECT_STREQ(to_string(transport_type::quic), "quic");
}

// ============================================================================
// TCP Transport Tests
// ============================================================================

class TcpTransportTest : public ::testing::Test {
protected:
    void SetUp() override {
        transport_ = tcp_transport::create();
    }

    void TearDown() override {
        transport_.reset();
    }

    std::unique_ptr<tcp_transport> transport_;
};

TEST_F(TcpTransportTest, Creation) {
    ASSERT_NE(transport_, nullptr);
    EXPECT_EQ(transport_->type(), "tcp");
}

TEST_F(TcpTransportTest, InitialState) {
    EXPECT_EQ(transport_->state(), transport_state::disconnected);
    EXPECT_FALSE(transport_->is_connected());
}

TEST_F(TcpTransportTest, LocalEndpointWhenDisconnected) {
    auto ep = transport_->local_endpoint();
    EXPECT_FALSE(ep.has_value());
}

TEST_F(TcpTransportTest, RemoteEndpointWhenDisconnected) {
    auto ep = transport_->remote_endpoint();
    EXPECT_FALSE(ep.has_value());
}

TEST_F(TcpTransportTest, StatisticsInitialized) {
    auto stats = transport_->get_statistics();
    EXPECT_EQ(stats.bytes_sent, 0);
    EXPECT_EQ(stats.bytes_received, 0);
    EXPECT_EQ(stats.packets_sent, 0);
    EXPECT_EQ(stats.packets_received, 0);
    EXPECT_EQ(stats.errors, 0);
}

TEST_F(TcpTransportTest, ConfigRetrieval) {
    const auto& config = transport_->config();
    EXPECT_EQ(config.type, transport_type::tcp);
}

TEST_F(TcpTransportTest, SendWithoutConnection) {
    std::vector<std::byte> data(100);
    auto result = transport_->send(data);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(TcpTransportTest, ReceiveWithoutConnection) {
    auto result = transport_->receive();

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(TcpTransportTest, DisconnectWhenAlreadyDisconnected) {
    auto result = transport_->disconnect();
    EXPECT_TRUE(result.has_value());  // No error when already disconnected
}

TEST_F(TcpTransportTest, CustomConfiguration) {
    auto config = transport_config_builder::tcp()
        .with_connect_timeout(std::chrono::seconds{5})
        .with_tcp_nodelay(true)
        .build_tcp();

    auto custom_transport = tcp_transport::create(config);
    ASSERT_NE(custom_transport, nullptr);

    const auto& retrieved_config = custom_transport->config();
    EXPECT_EQ(retrieved_config.connect_timeout, std::chrono::seconds{5});
}

TEST_F(TcpTransportTest, StateChangeCallback) {
    std::vector<transport_state> states;
    std::mutex states_mutex;

    transport_->on_state_changed([&states, &states_mutex](transport_state state) {
        std::lock_guard lock(states_mutex);
        states.push_back(state);
    });

    // Just verify the callback is set without attempting actual connection
    // Actual connection tests would require a running server
    {
        std::lock_guard lock(states_mutex);
        EXPECT_TRUE(states.empty());  // No state changes yet
    }
}

// ============================================================================
// TCP Transport Factory Tests
// ============================================================================

class TcpTransportFactoryTest : public ::testing::Test {
protected:
    tcp_transport_factory factory_;
};

TEST_F(TcpTransportFactoryTest, SupportedTypes) {
    auto types = factory_.supported_types();
    ASSERT_EQ(types.size(), 1);
    EXPECT_EQ(types[0], "tcp");
}

TEST_F(TcpTransportFactoryTest, CreateTcpTransport) {
    tcp_transport_config config;
    auto transport = factory_.create(config);

    ASSERT_NE(transport, nullptr);
    EXPECT_EQ(transport->type(), "tcp");
}

TEST_F(TcpTransportFactoryTest, RejectQuicConfig) {
    quic_transport_config config;
    auto transport = factory_.create(config);

    EXPECT_EQ(transport, nullptr);
}

// ============================================================================
// Send/Receive Options Tests
// ============================================================================

class TransportOptionsTest : public ::testing::Test {};

TEST_F(TransportOptionsTest, DefaultSendOptions) {
    send_options options;

    EXPECT_TRUE(options.reliable);
    EXPECT_EQ(options.timeout, std::chrono::milliseconds{30000});
    EXPECT_EQ(options.on_progress, nullptr);
}

TEST_F(TransportOptionsTest, DefaultReceiveOptions) {
    receive_options options;

    EXPECT_EQ(options.max_size, 1024 * 1024);
    EXPECT_EQ(options.timeout, std::chrono::milliseconds{30000});
}

TEST_F(TransportOptionsTest, CustomSendOptions) {
    send_options options;
    options.reliable = false;
    options.timeout = std::chrono::seconds{10};
    bool progress_called = false;
    options.on_progress = [&progress_called](uint64_t) {
        progress_called = true;
    };

    EXPECT_FALSE(options.reliable);
    EXPECT_EQ(options.timeout, std::chrono::seconds{10});
    EXPECT_NE(options.on_progress, nullptr);

    options.on_progress(100);
    EXPECT_TRUE(progress_called);
}

// ============================================================================
// Transport Statistics Tests
// ============================================================================

class TransportStatisticsTest : public ::testing::Test {};

TEST_F(TransportStatisticsTest, DefaultValues) {
    transport_statistics stats;

    EXPECT_EQ(stats.bytes_sent, 0);
    EXPECT_EQ(stats.bytes_received, 0);
    EXPECT_EQ(stats.packets_sent, 0);
    EXPECT_EQ(stats.packets_received, 0);
    EXPECT_EQ(stats.errors, 0);
    EXPECT_EQ(stats.rtt, std::chrono::milliseconds{0});
}

// ============================================================================
// Connection Result Tests
// ============================================================================

class ConnectionResultTest : public ::testing::Test {};

TEST_F(ConnectionResultTest, SuccessfulConnection) {
    connection_result result;
    result.success = true;
    result.local_address = "192.168.1.100";
    result.local_port = 12345;
    result.remote_address = "192.168.1.1";
    result.remote_port = 8080;

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.local_address, "192.168.1.100");
    EXPECT_EQ(result.local_port, 12345);
    EXPECT_EQ(result.remote_address, "192.168.1.1");
    EXPECT_EQ(result.remote_port, 8080);
    EXPECT_TRUE(result.error_message.empty());
}

TEST_F(ConnectionResultTest, FailedConnection) {
    connection_result result;
    result.success = false;
    result.error_message = "Connection refused";

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_message, "Connection refused");
}

// ============================================================================
// Transport Event Tests
// ============================================================================

class TransportEventTest : public ::testing::Test {};

TEST_F(TransportEventTest, EventDataConstruction) {
    transport_event_data event;
    event.event = transport_event::connected;
    event.error_message = "";

    EXPECT_EQ(event.event, transport_event::connected);
    EXPECT_TRUE(event.error_message.empty());
    EXPECT_TRUE(event.data.empty());
}

TEST_F(TransportEventTest, EventWithData) {
    transport_event_data event;
    event.event = transport_event::data_received;
    event.data = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    EXPECT_EQ(event.event, transport_event::data_received);
    EXPECT_EQ(event.data.size(), 3);
}

TEST_F(TransportEventTest, ErrorEvent) {
    transport_event_data event;
    event.event = transport_event::error;
    event.error_message = "Connection timeout";

    EXPECT_EQ(event.event, transport_event::error);
    EXPECT_EQ(event.error_message, "Connection timeout");
}

}  // namespace
}  // namespace kcenon::file_transfer
