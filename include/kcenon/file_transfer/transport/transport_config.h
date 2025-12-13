/**
 * @file transport_config.h
 * @brief Transport configuration types
 * @version 0.1.0
 *
 * This file defines configuration structures for transport implementations.
 */

#ifndef KCENON_FILE_TRANSFER_TRANSPORT_TRANSPORT_CONFIG_H
#define KCENON_FILE_TRANSFER_TRANSPORT_TRANSPORT_CONFIG_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace kcenon::file_transfer {

/**
 * @brief Transport type enumeration
 */
enum class transport_type {
    tcp,    ///< TCP transport
    quic    ///< QUIC transport (future)
};

/**
 * @brief Convert transport_type to string
 */
[[nodiscard]] constexpr auto to_string(transport_type type) -> const char* {
    switch (type) {
        case transport_type::tcp: return "tcp";
        case transport_type::quic: return "quic";
        default: return "unknown";
    }
}

/**
 * @brief Base transport configuration
 */
struct transport_config {
    transport_type type = transport_type::tcp;

    /// Connection timeout
    std::chrono::milliseconds connect_timeout{30000};

    /// Read timeout (0 = no timeout)
    std::chrono::milliseconds read_timeout{0};

    /// Write timeout (0 = no timeout)
    std::chrono::milliseconds write_timeout{0};

    /// Send buffer size (0 = system default)
    std::size_t send_buffer_size = 0;

    /// Receive buffer size (0 = system default)
    std::size_t receive_buffer_size = 0;

    /// Keep-alive enabled
    bool keep_alive = true;

    /// Keep-alive interval
    std::chrono::seconds keep_alive_interval{60};

    /// Maximum retry attempts for connection
    std::size_t max_retry_attempts = 3;

    /// Delay between retry attempts
    std::chrono::milliseconds retry_delay{1000};

    virtual ~transport_config() = default;
};

/**
 * @brief TCP-specific transport configuration
 */
struct tcp_transport_config : transport_config {
    tcp_transport_config() {
        type = transport_type::tcp;
    }

    /// Enable TCP_NODELAY (disable Nagle's algorithm)
    bool tcp_nodelay = true;

    /// Enable SO_REUSEADDR
    bool reuse_address = true;

    /// Enable SO_REUSEPORT (if supported)
    bool reuse_port = false;

    /// Linger timeout (-1 = disabled, 0 = immediate close, >0 = linger seconds)
    int linger_timeout = -1;

    /// TCP keep-alive probe count
    int keep_alive_probes = 9;

    /// TCP keep-alive probe interval
    std::chrono::seconds keep_alive_probe_interval{75};
};

/**
 * @brief QUIC-specific transport configuration (for future use)
 */
struct quic_transport_config : transport_config {
    quic_transport_config() {
        type = transport_type::quic;
    }

    /// Enable 0-RTT connection resumption
    bool enable_0rtt = true;

    /// Maximum idle timeout
    std::chrono::seconds max_idle_timeout{30};

    /// Maximum bidirectional streams
    uint64_t max_bidi_streams = 100;

    /// Maximum unidirectional streams
    uint64_t max_uni_streams = 100;

    /// Initial maximum data
    uint64_t initial_max_data = 10 * 1024 * 1024;  // 10MB

    /// Initial maximum stream data
    uint64_t initial_max_stream_data = 1 * 1024 * 1024;  // 1MB

    /// ALPN protocol identifiers
    std::string alpn = "file-transfer/1";

    /// Path to TLS certificate file
    std::optional<std::string> cert_path;

    /// Path to TLS private key file
    std::optional<std::string> key_path;

    /// Path to CA certificate file for verification
    std::optional<std::string> ca_path;

    /// Skip certificate verification (for testing only)
    bool skip_cert_verify = false;

    /// Server name for SNI
    std::optional<std::string> server_name;
};

/**
 * @brief Transport configuration builder
 */
class transport_config_builder {
public:
    /**
     * @brief Start building TCP configuration
     */
    static auto tcp() -> transport_config_builder {
        transport_config_builder builder;
        builder.tcp_config_ = tcp_transport_config{};
        return builder;
    }

    /**
     * @brief Start building QUIC configuration
     */
    static auto quic() -> transport_config_builder {
        transport_config_builder builder;
        builder.quic_config_ = quic_transport_config{};
        return builder;
    }

    // Common options
    auto with_connect_timeout(std::chrono::milliseconds timeout) -> transport_config_builder& {
        if (tcp_config_.has_value()) {
            tcp_config_->connect_timeout = timeout;
        } else if (quic_config_.has_value()) {
            quic_config_->connect_timeout = timeout;
        }
        return *this;
    }

    auto with_read_timeout(std::chrono::milliseconds timeout) -> transport_config_builder& {
        if (tcp_config_.has_value()) {
            tcp_config_->read_timeout = timeout;
        } else if (quic_config_.has_value()) {
            quic_config_->read_timeout = timeout;
        }
        return *this;
    }

    auto with_write_timeout(std::chrono::milliseconds timeout) -> transport_config_builder& {
        if (tcp_config_.has_value()) {
            tcp_config_->write_timeout = timeout;
        } else if (quic_config_.has_value()) {
            quic_config_->write_timeout = timeout;
        }
        return *this;
    }

    auto with_buffer_sizes(std::size_t send, std::size_t recv) -> transport_config_builder& {
        if (tcp_config_.has_value()) {
            tcp_config_->send_buffer_size = send;
            tcp_config_->receive_buffer_size = recv;
        } else if (quic_config_.has_value()) {
            quic_config_->send_buffer_size = send;
            quic_config_->receive_buffer_size = recv;
        }
        return *this;
    }

    auto with_keep_alive(bool enable, std::chrono::seconds interval = std::chrono::seconds{60})
        -> transport_config_builder& {
        if (tcp_config_.has_value()) {
            tcp_config_->keep_alive = enable;
            tcp_config_->keep_alive_interval = interval;
        } else if (quic_config_.has_value()) {
            quic_config_->keep_alive = enable;
            quic_config_->keep_alive_interval = interval;
        }
        return *this;
    }

    auto with_retry(std::size_t max_attempts, std::chrono::milliseconds delay)
        -> transport_config_builder& {
        if (tcp_config_.has_value()) {
            tcp_config_->max_retry_attempts = max_attempts;
            tcp_config_->retry_delay = delay;
        } else if (quic_config_.has_value()) {
            quic_config_->max_retry_attempts = max_attempts;
            quic_config_->retry_delay = delay;
        }
        return *this;
    }

    // TCP-specific options
    auto with_tcp_nodelay(bool enable) -> transport_config_builder& {
        if (tcp_config_.has_value()) {
            tcp_config_->tcp_nodelay = enable;
        }
        return *this;
    }

    auto with_reuse_address(bool enable) -> transport_config_builder& {
        if (tcp_config_.has_value()) {
            tcp_config_->reuse_address = enable;
        }
        return *this;
    }

    auto with_linger(int timeout) -> transport_config_builder& {
        if (tcp_config_.has_value()) {
            tcp_config_->linger_timeout = timeout;
        }
        return *this;
    }

    // QUIC-specific options
    auto with_0rtt(bool enable) -> transport_config_builder& {
        if (quic_config_.has_value()) {
            quic_config_->enable_0rtt = enable;
        }
        return *this;
    }

    auto with_max_idle_timeout(std::chrono::seconds timeout) -> transport_config_builder& {
        if (quic_config_.has_value()) {
            quic_config_->max_idle_timeout = timeout;
        }
        return *this;
    }

    auto with_tls_config(
        const std::string& cert_path,
        const std::string& key_path,
        const std::optional<std::string>& ca_path = std::nullopt) -> transport_config_builder& {
        if (quic_config_.has_value()) {
            quic_config_->cert_path = cert_path;
            quic_config_->key_path = key_path;
            quic_config_->ca_path = ca_path;
        }
        return *this;
    }

    /**
     * @brief Build TCP configuration
     * @return TCP transport configuration
     */
    [[nodiscard]] auto build_tcp() const -> tcp_transport_config {
        if (tcp_config_.has_value()) {
            return tcp_config_.value();
        }
        return tcp_transport_config{};
    }

    /**
     * @brief Build QUIC configuration
     * @return QUIC transport configuration
     */
    [[nodiscard]] auto build_quic() const -> quic_transport_config {
        if (quic_config_.has_value()) {
            return quic_config_.value();
        }
        return quic_transport_config{};
    }

private:
    std::optional<tcp_transport_config> tcp_config_;
    std::optional<quic_transport_config> quic_config_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_TRANSPORT_TRANSPORT_CONFIG_H
