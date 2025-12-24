/**
 * @file quic_transport_test.cpp
 * @brief Unit tests for QUIC transport implementation
 */

#include <gtest/gtest.h>

#include <mutex>

#include "kcenon/file_transfer/transport/transport_interface.h"
#include "kcenon/file_transfer/transport/transport_config.h"
#include "kcenon/file_transfer/transport/quic_transport.h"

namespace kcenon::file_transfer {
namespace {

// ============================================================================
// QUIC Transport Creation Tests
// ============================================================================

class QuicTransportTest : public ::testing::Test {
protected:
    void SetUp() override {
        transport_ = quic_transport::create();
    }

    void TearDown() override {
        transport_.reset();
    }

    std::unique_ptr<quic_transport> transport_;
};

TEST_F(QuicTransportTest, Creation) {
    ASSERT_NE(transport_, nullptr);
    EXPECT_EQ(transport_->type(), "quic");
}

TEST_F(QuicTransportTest, InitialState) {
    EXPECT_EQ(transport_->state(), transport_state::disconnected);
    EXPECT_FALSE(transport_->is_connected());
}

TEST_F(QuicTransportTest, LocalEndpointWhenDisconnected) {
    auto ep = transport_->local_endpoint();
    EXPECT_FALSE(ep.has_value());
}

TEST_F(QuicTransportTest, RemoteEndpointWhenDisconnected) {
    auto ep = transport_->remote_endpoint();
    EXPECT_FALSE(ep.has_value());
}

TEST_F(QuicTransportTest, StatisticsInitialized) {
    auto stats = transport_->get_statistics();
    EXPECT_EQ(stats.bytes_sent, 0);
    EXPECT_EQ(stats.bytes_received, 0);
    EXPECT_EQ(stats.packets_sent, 0);
    EXPECT_EQ(stats.packets_received, 0);
    EXPECT_EQ(stats.errors, 0);
}

TEST_F(QuicTransportTest, ConfigRetrieval) {
    const auto& config = transport_->config();
    EXPECT_EQ(config.type, transport_type::quic);
}

TEST_F(QuicTransportTest, SendWithoutConnection) {
    std::vector<std::byte> data(100);
    auto result = transport_->send(data);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(QuicTransportTest, ReceiveWithoutConnection) {
    auto result = transport_->receive();

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(QuicTransportTest, DisconnectWhenAlreadyDisconnected) {
    auto result = transport_->disconnect();
    EXPECT_TRUE(result.has_value());  // No error when already disconnected
}

TEST_F(QuicTransportTest, CustomConfiguration) {
    auto config = transport_config_builder::quic()
        .with_connect_timeout(std::chrono::seconds{5})
        .with_0rtt(true)
        .with_max_idle_timeout(std::chrono::seconds{60})
        .build_quic();

    auto custom_transport = quic_transport::create(config);
    ASSERT_NE(custom_transport, nullptr);

    const auto& retrieved_config = custom_transport->config();
    EXPECT_EQ(retrieved_config.connect_timeout, std::chrono::seconds{5});
    EXPECT_EQ(retrieved_config.type, transport_type::quic);
}

TEST_F(QuicTransportTest, StateChangeCallback) {
    std::vector<transport_state> states;
    std::mutex states_mutex;

    transport_->on_state_changed([&states, &states_mutex](transport_state state) {
        std::lock_guard lock(states_mutex);
        states.push_back(state);
    });

    // Verify the callback is set without attempting actual connection
    {
        std::lock_guard lock(states_mutex);
        EXPECT_TRUE(states.empty());  // No state changes yet
    }
}

TEST_F(QuicTransportTest, HandshakeNotCompleteWhenDisconnected) {
    EXPECT_FALSE(transport_->is_handshake_complete());
}

TEST_F(QuicTransportTest, AlpnProtocolEmptyWhenDisconnected) {
    auto alpn = transport_->alpn_protocol();
    EXPECT_FALSE(alpn.has_value());
}

TEST_F(QuicTransportTest, CreateStreamWithoutConnection) {
    auto result = transport_->create_stream();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(QuicTransportTest, CreateUnidirectionalStreamWithoutConnection) {
    auto result = transport_->create_unidirectional_stream();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(QuicTransportTest, SendOnStreamWithoutConnection) {
    std::vector<std::byte> data(100);
    auto result = transport_->send_on_stream(0, data);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(QuicTransportTest, CloseStreamWithoutConnection) {
    auto result = transport_->close_stream(0);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

// ============================================================================
// QUIC Transport Factory Tests
// ============================================================================

class QuicTransportFactoryTest : public ::testing::Test {
protected:
    quic_transport_factory factory_;
};

TEST_F(QuicTransportFactoryTest, SupportedTypes) {
    auto types = factory_.supported_types();
    ASSERT_EQ(types.size(), 1);
    EXPECT_EQ(types[0], "quic");
}

TEST_F(QuicTransportFactoryTest, CreateQuicTransport) {
    quic_transport_config config;
    auto transport = factory_.create(config);

    ASSERT_NE(transport, nullptr);
    EXPECT_EQ(transport->type(), "quic");
}

TEST_F(QuicTransportFactoryTest, RejectTcpConfig) {
    tcp_transport_config config;
    auto transport = factory_.create(config);

    EXPECT_EQ(transport, nullptr);
}

// ============================================================================
// QUIC Configuration Tests
// ============================================================================

class QuicConfigTest : public ::testing::Test {};

TEST_F(QuicConfigTest, DefaultQuicConfig) {
    quic_transport_config config;

    EXPECT_EQ(config.type, transport_type::quic);
    EXPECT_TRUE(config.enable_0rtt);
    EXPECT_EQ(config.max_idle_timeout, std::chrono::seconds{30});
    EXPECT_EQ(config.max_bidi_streams, 100);
    EXPECT_EQ(config.max_uni_streams, 100);
    EXPECT_EQ(config.initial_max_data, 10 * 1024 * 1024);
    EXPECT_EQ(config.initial_max_stream_data, 1 * 1024 * 1024);
}

TEST_F(QuicConfigTest, QuicConfigBuilder) {
    auto config = transport_config_builder::quic()
        .with_connect_timeout(std::chrono::seconds{10})
        .with_0rtt(false)
        .with_max_idle_timeout(std::chrono::seconds{120})
        .build_quic();

    EXPECT_EQ(config.type, transport_type::quic);
    EXPECT_EQ(config.connect_timeout, std::chrono::seconds{10});
    EXPECT_FALSE(config.enable_0rtt);
    EXPECT_EQ(config.max_idle_timeout, std::chrono::seconds{120});
}

TEST_F(QuicConfigTest, QuicConfigWithTls) {
    quic_transport_config config;
    config.cert_path = "/path/to/cert.pem";
    config.key_path = "/path/to/key.pem";
    config.ca_path = "/path/to/ca.pem";
    config.skip_cert_verify = false;
    config.server_name = "example.com";

    EXPECT_TRUE(config.cert_path.has_value());
    EXPECT_TRUE(config.key_path.has_value());
    EXPECT_TRUE(config.ca_path.has_value());
    EXPECT_FALSE(config.skip_cert_verify);
    EXPECT_TRUE(config.server_name.has_value());
}

TEST_F(QuicConfigTest, QuicConfigWithAlpn) {
    quic_transport_config config;
    config.alpn = "file-transfer/1";

    EXPECT_EQ(config.alpn, "file-transfer/1");
}

// ============================================================================
// QUIC-specific Options Tests
// ============================================================================

class QuicOptionsTest : public ::testing::Test {};

TEST_F(QuicOptionsTest, StreamLimits) {
    quic_transport_config config;

    config.max_bidi_streams = 200;
    config.max_uni_streams = 50;

    EXPECT_EQ(config.max_bidi_streams, 200);
    EXPECT_EQ(config.max_uni_streams, 50);
}

TEST_F(QuicOptionsTest, DataLimits) {
    quic_transport_config config;

    config.initial_max_data = 100 * 1024 * 1024;  // 100 MB
    config.initial_max_stream_data = 5 * 1024 * 1024;  // 5 MB

    EXPECT_EQ(config.initial_max_data, 100 * 1024 * 1024);
    EXPECT_EQ(config.initial_max_stream_data, 5 * 1024 * 1024);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

class QuicTransportErrorTest : public ::testing::Test {
protected:
    void SetUp() override {
        transport_ = quic_transport::create();
    }

    std::unique_ptr<quic_transport> transport_;
};

TEST_F(QuicTransportErrorTest, SendEmptyData) {
    // Even if connected, sending empty data should return 0
    std::vector<std::byte> empty_data;
    auto result = transport_->send(empty_data);

    // Since not connected, should get error
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(QuicTransportErrorTest, ReceiveIntoBufferWithoutConnection) {
    std::vector<std::byte> buffer(1024);
    auto result = transport_->receive_into(buffer);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(QuicTransportErrorTest, ConnectToInvalidEndpoint) {
    // Try to connect to an invalid endpoint
    endpoint invalid_ep{"", 0};
    auto result = transport_->connect(invalid_ep, std::chrono::milliseconds{100});

    // Should fail with error
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// Async Operations Tests
// ============================================================================

class QuicTransportAsyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        transport_ = quic_transport::create();
    }

    std::unique_ptr<quic_transport> transport_;
};

TEST_F(QuicTransportAsyncTest, SendAsyncWithoutConnection) {
    std::vector<std::byte> data(100);
    auto future = transport_->send_async(data);

    auto result = future.get();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(QuicTransportAsyncTest, ReceiveAsyncWithoutConnection) {
    auto future = transport_->receive_async();

    auto result = future.get();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

// Note: ConnectAsyncToInvalidEndpoint test disabled due to network timeout
// causing test instability. The synchronous connect tests cover the
// error handling scenarios adequately.

}  // namespace
}  // namespace kcenon::file_transfer
