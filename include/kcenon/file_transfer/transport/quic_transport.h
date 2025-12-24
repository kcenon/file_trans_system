/**
 * @file quic_transport.h
 * @brief QUIC transport implementation
 * @version 0.1.0
 *
 * This file implements the transport_interface for QUIC connections.
 */

#ifndef KCENON_FILE_TRANSFER_TRANSPORT_QUIC_TRANSPORT_H
#define KCENON_FILE_TRANSFER_TRANSPORT_QUIC_TRANSPORT_H

#include <atomic>
#include <memory>
#include <mutex>

#include "transport_interface.h"
#include "transport_config.h"

namespace kcenon::file_transfer {

/**
 * @brief QUIC transport implementation
 *
 * Provides QUIC-based transport using the network_system's messaging_quic_client.
 * QUIC offers several advantages over TCP:
 * - Reduced connection latency (0-RTT)
 * - Built-in encryption (TLS 1.3)
 * - Multiplexed streams without head-of-line blocking
 * - Connection migration
 *
 * @code
 * auto config = transport_config_builder::quic()
 *     .with_0rtt(true)
 *     .with_max_idle_timeout(std::chrono::seconds{60})
 *     .with_connect_timeout(std::chrono::seconds{10})
 *     .build_quic();
 *
 * auto transport = quic_transport::create(config);
 * if (transport) {
 *     auto result = transport->connect(endpoint{"localhost", 8080});
 *     if (result.has_value()) {
 *         // Send data
 *         std::vector<std::byte> data = ...;
 *         transport->send(data);
 *     }
 * }
 * @endcode
 */
class quic_transport : public transport_interface {
public:
    /**
     * @brief Create a QUIC transport instance
     * @param config QUIC configuration
     * @return Transport instance or nullptr on failure
     */
    [[nodiscard]] static auto create(const quic_transport_config& config = {})
        -> std::unique_ptr<quic_transport>;

    ~quic_transport() override;

    // Non-copyable
    quic_transport(const quic_transport&) = delete;
    auto operator=(const quic_transport&) -> quic_transport& = delete;

    // Movable
    quic_transport(quic_transport&&) noexcept;
    auto operator=(quic_transport&&) noexcept -> quic_transport&;

    // ========================================================================
    // transport_interface implementation
    // ========================================================================

    [[nodiscard]] auto type() const -> std::string_view override;

    // Connection Management
    [[nodiscard]] auto connect(const endpoint& remote) -> result<connection_result> override;
    [[nodiscard]] auto connect(
        const endpoint& remote,
        std::chrono::milliseconds timeout) -> result<connection_result> override;
    [[nodiscard]] auto connect_async(const endpoint& remote)
        -> std::future<result<connection_result>> override;
    [[nodiscard]] auto disconnect() -> result<void> override;
    [[nodiscard]] auto is_connected() const -> bool override;
    [[nodiscard]] auto state() const -> transport_state override;

    // Data Transfer - Synchronous
    [[nodiscard]] auto send(
        std::span<const std::byte> data,
        const send_options& options = {}) -> result<std::size_t> override;
    [[nodiscard]] auto receive(
        const receive_options& options = {}) -> result<std::vector<std::byte>> override;
    [[nodiscard]] auto receive_into(
        std::span<std::byte> buffer,
        const receive_options& options = {}) -> result<std::size_t> override;

    // Data Transfer - Asynchronous
    [[nodiscard]] auto send_async(
        std::span<const std::byte> data,
        const send_options& options = {}) -> std::future<result<std::size_t>> override;
    [[nodiscard]] auto receive_async(
        const receive_options& options = {}) -> std::future<result<std::vector<std::byte>>> override;

    // Event Handling
    void on_event(std::function<void(const transport_event_data&)> callback) override;
    void on_state_changed(std::function<void(transport_state)> callback) override;

    // Statistics and Information
    [[nodiscard]] auto get_statistics() const -> transport_statistics override;
    [[nodiscard]] auto local_endpoint() const -> std::optional<endpoint> override;
    [[nodiscard]] auto remote_endpoint() const -> std::optional<endpoint> override;
    [[nodiscard]] auto config() const -> const transport_config& override;

    // ========================================================================
    // QUIC-specific features
    // ========================================================================

    /**
     * @brief Create a new bidirectional stream
     * @return Stream ID or error
     */
    [[nodiscard]] auto create_stream() -> result<uint64_t>;

    /**
     * @brief Create a new unidirectional stream
     * @return Stream ID or error
     */
    [[nodiscard]] auto create_unidirectional_stream() -> result<uint64_t>;

    /**
     * @brief Send data on a specific stream
     * @param stream_id Target stream ID
     * @param data Data to send
     * @param fin True if this is the final data on the stream
     * @return Result indicating success or failure
     */
    [[nodiscard]] auto send_on_stream(
        uint64_t stream_id,
        std::span<const std::byte> data,
        bool fin = false) -> result<std::size_t>;

    /**
     * @brief Close a specific stream
     * @param stream_id Stream ID to close
     * @return Result indicating success or failure
     */
    [[nodiscard]] auto close_stream(uint64_t stream_id) -> result<void>;

    /**
     * @brief Check if TLS handshake is complete
     * @return true if handshake is complete
     */
    [[nodiscard]] auto is_handshake_complete() const -> bool;

    /**
     * @brief Get the negotiated ALPN protocol
     * @return Protocol string if negotiated
     */
    [[nodiscard]] auto alpn_protocol() const -> std::optional<std::string>;

private:
    explicit quic_transport(quic_transport_config config);

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief QUIC transport factory
 */
class quic_transport_factory : public transport_factory {
public:
    [[nodiscard]] auto create(
        const transport_config& config) -> std::unique_ptr<transport_interface> override;

    [[nodiscard]] auto supported_types() const -> std::vector<std::string> override;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_TRANSPORT_QUIC_TRANSPORT_H
