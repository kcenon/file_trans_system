/**
 * @file transport_interface.h
 * @brief Transport abstraction layer interface
 * @version 0.1.0
 *
 * This file defines the transport abstraction interface that allows
 * seamless switching between TCP and QUIC transports.
 */

#ifndef KCENON_FILE_TRANSFER_TRANSPORT_TRANSPORT_INTERFACE_H
#define KCENON_FILE_TRANSFER_TRANSPORT_TRANSPORT_INTERFACE_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "kcenon/file_transfer/core/types.h"
#include "kcenon/file_transfer/server/server_types.h"
#include "transport_config.h"

namespace kcenon::file_transfer {

/**
 * @brief Transport state enumeration
 */
enum class transport_state {
    disconnected,   ///< Not connected
    connecting,     ///< Connection in progress
    connected,      ///< Connected and ready
    disconnecting,  ///< Disconnection in progress
    error           ///< Error state
};

/**
 * @brief Convert transport_state to string
 */
[[nodiscard]] constexpr auto to_string(transport_state state) -> const char* {
    switch (state) {
        case transport_state::disconnected: return "disconnected";
        case transport_state::connecting: return "connecting";
        case transport_state::connected: return "connected";
        case transport_state::disconnecting: return "disconnecting";
        case transport_state::error: return "error";
        default: return "unknown";
    }
}

/**
 * @brief Transport event types
 */
enum class transport_event {
    connected,      ///< Connection established
    disconnected,   ///< Connection closed
    data_received,  ///< Data received
    error           ///< Error occurred
};

/**
 * @brief Transport event data
 */
struct transport_event_data {
    transport_event event;
    std::string error_message;
    std::vector<std::byte> data;
};

/**
 * @brief Transport statistics
 */
struct transport_statistics {
    uint64_t bytes_sent = 0;           ///< Total bytes sent
    uint64_t bytes_received = 0;       ///< Total bytes received
    uint64_t packets_sent = 0;         ///< Total packets sent
    uint64_t packets_received = 0;     ///< Total packets received
    uint64_t errors = 0;               ///< Total errors
    std::chrono::milliseconds rtt{0};  ///< Round-trip time (if available)
    std::chrono::steady_clock::time_point connected_at;  ///< Connection time
};

/**
 * @brief Send options for transport operations
 */
struct send_options {
    bool reliable = true;                                      ///< Reliable delivery
    std::chrono::milliseconds timeout{30000};                  ///< Send timeout
    std::function<void(uint64_t)> on_progress = nullptr;       ///< Progress callback
};

/**
 * @brief Receive options for transport operations
 */
struct receive_options {
    std::size_t max_size = 1024 * 1024;                        ///< Maximum receive size
    std::chrono::milliseconds timeout{30000};                  ///< Receive timeout
};

/**
 * @brief Connection result containing connection details
 */
struct connection_result {
    bool success;
    std::string local_address;
    uint16_t local_port;
    std::string remote_address;
    uint16_t remote_port;
    std::string error_message;
};

/**
 * @brief Transport interface base class
 *
 * Provides an abstraction layer for different transport protocols (TCP, QUIC).
 * All implementations must support both synchronous and asynchronous operations.
 *
 * @code
 * // Example usage with TCP transport
 * auto transport = tcp_transport::create(tcp_config{});
 * if (transport) {
 *     auto result = transport->connect(endpoint{"localhost", 8080});
 *     if (result.has_value()) {
 *         transport->send(data);
 *     }
 * }
 * @endcode
 */
class transport_interface {
public:
    virtual ~transport_interface() = default;

    // Non-copyable
    transport_interface(const transport_interface&) = delete;
    auto operator=(const transport_interface&) -> transport_interface& = delete;

    // Movable
    transport_interface(transport_interface&&) noexcept = default;
    auto operator=(transport_interface&&) noexcept -> transport_interface& = default;

    /**
     * @brief Get the transport type identifier
     * @return Transport type string (e.g., "tcp", "quic")
     */
    [[nodiscard]] virtual auto type() const -> std::string_view = 0;

    // ========================================================================
    // Connection Management
    // ========================================================================

    /**
     * @brief Connect to a remote endpoint (synchronous)
     * @param remote Remote endpoint to connect to
     * @return Result containing connection details or error
     */
    [[nodiscard]] virtual auto connect(const endpoint& remote) -> result<connection_result> = 0;

    /**
     * @brief Connect to a remote endpoint with timeout (synchronous)
     * @param remote Remote endpoint to connect to
     * @param timeout Connection timeout
     * @return Result containing connection details or error
     */
    [[nodiscard]] virtual auto connect(
        const endpoint& remote,
        std::chrono::milliseconds timeout) -> result<connection_result> = 0;

    /**
     * @brief Connect to a remote endpoint (asynchronous)
     * @param remote Remote endpoint to connect to
     * @return Future containing connection result
     */
    [[nodiscard]] virtual auto connect_async(const endpoint& remote)
        -> std::future<result<connection_result>> = 0;

    /**
     * @brief Disconnect from the remote endpoint
     * @return Result indicating success or failure
     */
    [[nodiscard]] virtual auto disconnect() -> result<void> = 0;

    /**
     * @brief Check if connected
     * @return true if connected, false otherwise
     */
    [[nodiscard]] virtual auto is_connected() const -> bool = 0;

    /**
     * @brief Get current transport state
     * @return Current state
     */
    [[nodiscard]] virtual auto state() const -> transport_state = 0;

    // ========================================================================
    // Data Transfer - Synchronous
    // ========================================================================

    /**
     * @brief Send data (synchronous)
     * @param data Data to send
     * @param options Send options
     * @return Result containing bytes sent or error
     */
    [[nodiscard]] virtual auto send(
        std::span<const std::byte> data,
        const send_options& options = {}) -> result<std::size_t> = 0;

    /**
     * @brief Receive data (synchronous)
     * @param options Receive options
     * @return Result containing received data or error
     */
    [[nodiscard]] virtual auto receive(
        const receive_options& options = {}) -> result<std::vector<std::byte>> = 0;

    /**
     * @brief Receive data into buffer (synchronous)
     * @param buffer Buffer to receive into
     * @param options Receive options
     * @return Result containing bytes received or error
     */
    [[nodiscard]] virtual auto receive_into(
        std::span<std::byte> buffer,
        const receive_options& options = {}) -> result<std::size_t> = 0;

    // ========================================================================
    // Data Transfer - Asynchronous
    // ========================================================================

    /**
     * @brief Send data (asynchronous)
     * @param data Data to send
     * @param options Send options
     * @return Future containing bytes sent or error
     */
    [[nodiscard]] virtual auto send_async(
        std::span<const std::byte> data,
        const send_options& options = {}) -> std::future<result<std::size_t>> = 0;

    /**
     * @brief Receive data (asynchronous)
     * @param options Receive options
     * @return Future containing received data or error
     */
    [[nodiscard]] virtual auto receive_async(
        const receive_options& options = {}) -> std::future<result<std::vector<std::byte>>> = 0;

    // ========================================================================
    // Event Handling
    // ========================================================================

    /**
     * @brief Set callback for transport events
     * @param callback Function called when events occur
     */
    virtual void on_event(std::function<void(const transport_event_data&)> callback) = 0;

    /**
     * @brief Set callback for state changes
     * @param callback Function called when state changes
     */
    virtual void on_state_changed(std::function<void(transport_state)> callback) = 0;

    // ========================================================================
    // Statistics and Information
    // ========================================================================

    /**
     * @brief Get transport statistics
     * @return Current statistics
     */
    [[nodiscard]] virtual auto get_statistics() const -> transport_statistics = 0;

    /**
     * @brief Get local endpoint
     * @return Local endpoint if connected
     */
    [[nodiscard]] virtual auto local_endpoint() const -> std::optional<endpoint> = 0;

    /**
     * @brief Get remote endpoint
     * @return Remote endpoint if connected
     */
    [[nodiscard]] virtual auto remote_endpoint() const -> std::optional<endpoint> = 0;

    /**
     * @brief Get transport configuration
     * @return Configuration reference
     */
    [[nodiscard]] virtual auto config() const -> const transport_config& = 0;

protected:
    transport_interface() = default;
};

/**
 * @brief Transport factory interface
 *
 * Creates transport instances based on configuration.
 */
class transport_factory {
public:
    virtual ~transport_factory() = default;

    /**
     * @brief Create a transport instance
     * @param config Transport configuration
     * @return Transport instance or nullptr on failure
     */
    [[nodiscard]] virtual auto create(
        const transport_config& config) -> std::unique_ptr<transport_interface> = 0;

    /**
     * @brief Get supported transport types
     * @return Vector of supported type identifiers
     */
    [[nodiscard]] virtual auto supported_types() const -> std::vector<std::string> = 0;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_TRANSPORT_TRANSPORT_INTERFACE_H
