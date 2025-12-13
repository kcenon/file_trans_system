/**
 * @file tcp_transport.h
 * @brief TCP transport implementation
 * @version 0.1.0
 *
 * This file implements the transport_interface for TCP connections.
 */

#ifndef KCENON_FILE_TRANSFER_TRANSPORT_TCP_TRANSPORT_H
#define KCENON_FILE_TRANSFER_TRANSPORT_TCP_TRANSPORT_H

#include <atomic>
#include <memory>
#include <mutex>

#include "transport_interface.h"
#include "transport_config.h"

namespace kcenon::file_transfer {

/**
 * @brief TCP transport implementation
 *
 * Provides TCP-based transport using the existing network_system infrastructure.
 *
 * @code
 * auto config = transport_config_builder::tcp()
 *     .with_tcp_nodelay(true)
 *     .with_connect_timeout(std::chrono::seconds{10})
 *     .build_tcp();
 *
 * auto transport = tcp_transport::create(config);
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
class tcp_transport : public transport_interface {
public:
    /**
     * @brief Create a TCP transport instance
     * @param config TCP configuration
     * @return Transport instance or nullptr on failure
     */
    [[nodiscard]] static auto create(const tcp_transport_config& config = {})
        -> std::unique_ptr<tcp_transport>;

    ~tcp_transport() override;

    // Non-copyable
    tcp_transport(const tcp_transport&) = delete;
    auto operator=(const tcp_transport&) -> tcp_transport& = delete;

    // Movable
    tcp_transport(tcp_transport&&) noexcept;
    auto operator=(tcp_transport&&) noexcept -> tcp_transport&;

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

private:
    explicit tcp_transport(tcp_transport_config config);

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief TCP transport factory
 */
class tcp_transport_factory : public transport_factory {
public:
    [[nodiscard]] auto create(
        const transport_config& config) -> std::unique_ptr<transport_interface> override;

    [[nodiscard]] auto supported_types() const -> std::vector<std::string> override;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_TRANSPORT_TCP_TRANSPORT_H
