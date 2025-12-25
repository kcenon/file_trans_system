/**
 * @file session_resumption.h
 * @brief QUIC session ticket management for 0-RTT connection resumption
 * @version 0.1.0
 *
 * This file provides session ticket storage and management for QUIC 0-RTT
 * connection resumption. Session tickets allow clients to reconnect to
 * previously visited servers with reduced latency.
 */

#ifndef KCENON_FILE_TRANSFER_TRANSPORT_SESSION_RESUMPTION_H
#define KCENON_FILE_TRANSFER_TRANSPORT_SESSION_RESUMPTION_H

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "kcenon/file_transfer/core/types.h"

namespace kcenon::file_transfer {

/**
 * @brief Session ticket data structure
 *
 * Contains the encrypted session ticket and associated metadata
 * for QUIC 0-RTT connection resumption.
 */
struct session_ticket {
    /// Unique identifier for the server (host:port)
    std::string server_id;

    /// Encrypted session ticket data from TLS
    std::vector<uint8_t> ticket_data;

    /// When the ticket was issued
    std::chrono::system_clock::time_point issued_at;

    /// When the ticket expires
    std::chrono::system_clock::time_point expires_at;

    /// Maximum early data size allowed (0 = no early data)
    uint32_t max_early_data_size = 0;

    /// ALPN protocol used for this session
    std::string alpn_protocol;

    /// Server name indication (SNI) used
    std::string server_name;

    /**
     * @brief Check if the ticket is still valid
     * @return true if ticket has not expired
     */
    [[nodiscard]] auto is_valid() const -> bool {
        return std::chrono::system_clock::now() < expires_at;
    }

    /**
     * @brief Check if 0-RTT early data is allowed
     * @return true if early data can be sent
     */
    [[nodiscard]] auto allows_early_data() const -> bool {
        return max_early_data_size > 0 && is_valid();
    }

    /**
     * @brief Get remaining validity duration
     * @return Duration until expiration (negative if expired)
     */
    [[nodiscard]] auto time_until_expiry() const -> std::chrono::seconds {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(expires_at - now);
    }
};

/**
 * @brief Session store configuration
 */
struct session_store_config {
    /// Maximum number of tickets to store (0 = unlimited)
    std::size_t max_tickets = 1000;

    /// Default ticket lifetime if not specified by server
    std::chrono::seconds default_lifetime{7 * 24 * 3600};  // 7 days

    /// Minimum remaining lifetime to consider ticket valid
    std::chrono::seconds min_remaining_lifetime{60};  // 1 minute

    /// Path to persistent storage file (empty = in-memory only)
    std::filesystem::path storage_path;

    /// Enable automatic cleanup of expired tickets
    bool auto_cleanup = true;

    /// Cleanup interval for expired tickets
    std::chrono::seconds cleanup_interval{3600};  // 1 hour
};

/**
 * @brief Session ticket storage interface
 *
 * Provides thread-safe storage and retrieval of session tickets
 * for QUIC 0-RTT connection resumption.
 */
class session_store {
public:
    virtual ~session_store() = default;

    /**
     * @brief Store a session ticket
     * @param ticket Session ticket to store
     * @return Result indicating success or failure
     */
    [[nodiscard]] virtual auto store(const session_ticket& ticket) -> result<void> = 0;

    /**
     * @brief Retrieve a session ticket for a server
     * @param server_id Server identifier (host:port)
     * @return Session ticket if found and valid
     */
    [[nodiscard]] virtual auto retrieve(const std::string& server_id)
        -> std::optional<session_ticket> = 0;

    /**
     * @brief Remove a session ticket
     * @param server_id Server identifier
     * @return true if ticket was removed
     */
    virtual auto remove(const std::string& server_id) -> bool = 0;

    /**
     * @brief Remove all expired tickets
     * @return Number of tickets removed
     */
    virtual auto cleanup_expired() -> std::size_t = 0;

    /**
     * @brief Clear all stored tickets
     */
    virtual void clear() = 0;

    /**
     * @brief Get the number of stored tickets
     * @return Current ticket count
     */
    [[nodiscard]] virtual auto size() const -> std::size_t = 0;

    /**
     * @brief Check if a valid ticket exists for a server
     * @param server_id Server identifier
     * @return true if a valid ticket exists
     */
    [[nodiscard]] virtual auto has_ticket(const std::string& server_id) const -> bool = 0;
};

/**
 * @brief In-memory session ticket store
 *
 * Thread-safe implementation of session_store that keeps
 * all tickets in memory. Suitable for short-lived applications.
 *
 * @code
 * auto store = memory_session_store::create();
 *
 * session_ticket ticket;
 * ticket.server_id = "example.com:443";
 * ticket.ticket_data = received_ticket;
 * ticket.expires_at = std::chrono::system_clock::now() + std::chrono::hours{24};
 *
 * store->store(ticket);
 *
 * // Later, when reconnecting
 * if (auto ticket = store->retrieve("example.com:443")) {
 *     // Use ticket for 0-RTT
 * }
 * @endcode
 */
class memory_session_store : public session_store {
public:
    /**
     * @brief Create a memory session store
     * @param config Store configuration
     * @return Session store instance
     */
    [[nodiscard]] static auto create(const session_store_config& config = {})
        -> std::unique_ptr<memory_session_store>;

    ~memory_session_store() override = default;

    // session_store interface
    [[nodiscard]] auto store(const session_ticket& ticket) -> result<void> override;
    [[nodiscard]] auto retrieve(const std::string& server_id)
        -> std::optional<session_ticket> override;
    auto remove(const std::string& server_id) -> bool override;
    auto cleanup_expired() -> std::size_t override;
    void clear() override;
    [[nodiscard]] auto size() const -> std::size_t override;
    [[nodiscard]] auto has_ticket(const std::string& server_id) const -> bool override;

private:
    explicit memory_session_store(session_store_config config);

    session_store_config config_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, session_ticket> tickets_;
};

/**
 * @brief File-based session ticket store
 *
 * Persists session tickets to disk for use across application restarts.
 * Uses secure storage practices to protect ticket data.
 *
 * @code
 * auto config = session_store_config{};
 * config.storage_path = "/path/to/session_tickets.dat";
 *
 * auto store = file_session_store::create(config);
 *
 * // Tickets are automatically loaded from disk
 * if (auto ticket = store->retrieve("example.com:443")) {
 *     // Use ticket for 0-RTT
 * }
 * @endcode
 */
class file_session_store : public session_store {
public:
    /**
     * @brief Create a file session store
     * @param config Store configuration (storage_path must be set)
     * @return Session store instance or nullptr on failure
     */
    [[nodiscard]] static auto create(const session_store_config& config)
        -> std::unique_ptr<file_session_store>;

    ~file_session_store() override;

    // session_store interface
    [[nodiscard]] auto store(const session_ticket& ticket) -> result<void> override;
    [[nodiscard]] auto retrieve(const std::string& server_id)
        -> std::optional<session_ticket> override;
    auto remove(const std::string& server_id) -> bool override;
    auto cleanup_expired() -> std::size_t override;
    void clear() override;
    [[nodiscard]] auto size() const -> std::size_t override;
    [[nodiscard]] auto has_ticket(const std::string& server_id) const -> bool override;

    /**
     * @brief Force save all tickets to disk
     * @return Result indicating success or failure
     */
    [[nodiscard]] auto save() -> result<void>;

    /**
     * @brief Reload tickets from disk
     * @return Result indicating success or failure
     */
    [[nodiscard]] auto load() -> result<void>;

private:
    explicit file_session_store(session_store_config config);

    [[nodiscard]] auto save_internal() -> result<void>;
    [[nodiscard]] auto load_internal() -> result<void>;

    session_store_config config_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, session_ticket> tickets_;
    bool dirty_ = false;
};

/**
 * @brief Session resumption manager
 *
 * High-level interface for managing QUIC session resumption with
 * automatic ticket storage, retrieval, and lifecycle management.
 *
 * @code
 * auto config = session_resumption_config{};
 * config.enable_0rtt = true;
 * config.store_config.storage_path = "sessions.dat";
 *
 * auto manager = session_resumption_manager::create(config);
 *
 * // Get ticket for connection
 * auto ticket = manager->get_ticket_for_server("example.com", 443);
 *
 * // After connection, save new ticket
 * manager->on_new_ticket(new_ticket_data);
 * @endcode
 */
struct session_resumption_config {
    /// Enable 0-RTT connection resumption
    bool enable_0rtt = true;

    /// Session store configuration
    session_store_config store_config;

    /// Callback when 0-RTT is rejected (for metrics/logging)
    std::function<void(const std::string& server_id)> on_0rtt_rejected;

    /// Callback when 0-RTT succeeds (for metrics/logging)
    std::function<void(const std::string& server_id)> on_0rtt_accepted;

    /// Callback when new ticket is received
    std::function<void(const session_ticket&)> on_ticket_received;
};

/**
 * @brief Session resumption manager implementation
 */
class session_resumption_manager {
public:
    /**
     * @brief Create a session resumption manager
     * @param config Configuration options
     * @return Manager instance
     */
    [[nodiscard]] static auto create(const session_resumption_config& config = {})
        -> std::unique_ptr<session_resumption_manager>;

    ~session_resumption_manager();

    // Non-copyable
    session_resumption_manager(const session_resumption_manager&) = delete;
    auto operator=(const session_resumption_manager&) -> session_resumption_manager& = delete;

    // Movable
    session_resumption_manager(session_resumption_manager&&) noexcept;
    auto operator=(session_resumption_manager&&) noexcept -> session_resumption_manager&;

    /**
     * @brief Get session ticket data for a server
     * @param host Server hostname
     * @param port Server port
     * @return Ticket data if available and valid
     */
    [[nodiscard]] auto get_ticket_for_server(const std::string& host, uint16_t port)
        -> std::optional<std::vector<uint8_t>>;

    /**
     * @brief Get full session ticket for a server
     * @param host Server hostname
     * @param port Server port
     * @return Session ticket if available and valid
     */
    [[nodiscard]] auto get_session(const std::string& host, uint16_t port)
        -> std::optional<session_ticket>;

    /**
     * @brief Store a new session ticket
     * @param host Server hostname
     * @param port Server port
     * @param ticket_data Ticket data from TLS
     * @param lifetime Ticket lifetime (optional, uses server-provided if available)
     * @param max_early_data Maximum early data size
     * @param alpn ALPN protocol
     * @return Result indicating success or failure
     */
    [[nodiscard]] auto store_ticket(
        const std::string& host,
        uint16_t port,
        std::vector<uint8_t> ticket_data,
        std::optional<std::chrono::seconds> lifetime = std::nullopt,
        uint32_t max_early_data = 0,
        const std::string& alpn = "") -> result<void>;

    /**
     * @brief Handle 0-RTT rejection
     * @param host Server hostname
     * @param port Server port
     *
     * Called when server rejects 0-RTT. The ticket will be invalidated
     * and the rejection callback will be invoked.
     */
    void on_0rtt_rejected(const std::string& host, uint16_t port);

    /**
     * @brief Handle 0-RTT acceptance
     * @param host Server hostname
     * @param port Server port
     *
     * Called when server accepts 0-RTT. The acceptance callback will be invoked.
     */
    void on_0rtt_accepted(const std::string& host, uint16_t port);

    /**
     * @brief Check if 0-RTT is available for a server
     * @param host Server hostname
     * @param port Server port
     * @return true if valid ticket with early data support exists
     */
    [[nodiscard]] auto can_use_0rtt(const std::string& host, uint16_t port) const -> bool;

    /**
     * @brief Remove ticket for a server
     * @param host Server hostname
     * @param port Server port
     * @return true if ticket was removed
     */
    auto remove_ticket(const std::string& host, uint16_t port) -> bool;

    /**
     * @brief Clear all stored tickets
     */
    void clear_all_tickets();

    /**
     * @brief Get current configuration
     * @return Configuration reference
     */
    [[nodiscard]] auto config() const -> const session_resumption_config&;

    /**
     * @brief Get the underlying session store
     * @return Session store reference
     */
    [[nodiscard]] auto store() -> session_store&;
    [[nodiscard]] auto store() const -> const session_store&;

private:
    explicit session_resumption_manager(session_resumption_config config);

    [[nodiscard]] static auto make_server_id(const std::string& host, uint16_t port)
        -> std::string;

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief Helper to create server identifier string
 * @param host Server hostname
 * @param port Server port
 * @return Server identifier in "host:port" format
 */
[[nodiscard]] inline auto make_server_id(const std::string& host, uint16_t port) -> std::string {
    return host + ":" + std::to_string(port);
}

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_TRANSPORT_SESSION_RESUMPTION_H
