/**
 * @file connection_migration.h
 * @brief QUIC connection migration support for seamless network transitions
 * @version 0.1.0
 *
 * This file provides connection migration functionality for QUIC transport,
 * allowing active connections to survive network changes without interruption.
 */

#ifndef KCENON_FILE_TRANSFER_TRANSPORT_CONNECTION_MIGRATION_H
#define KCENON_FILE_TRANSFER_TRANSPORT_CONNECTION_MIGRATION_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "kcenon/file_transfer/core/types.h"

namespace kcenon::file_transfer {

/**
 * @brief Network path information for connection migration
 */
struct network_path {
    /// Local IP address
    std::string local_address;

    /// Local port
    uint16_t local_port = 0;

    /// Remote IP address
    std::string remote_address;

    /// Remote port
    uint16_t remote_port = 0;

    /// Network interface name (e.g., "en0", "eth0", "wlan0")
    std::string interface_name;

    /// Whether this path is validated
    bool validated = false;

    /// Round-trip time for this path
    std::chrono::milliseconds rtt{0};

    /// Path creation timestamp
    std::chrono::steady_clock::time_point created_at;

    /**
     * @brief Check if path is equal to another
     */
    [[nodiscard]] auto operator==(const network_path& other) const -> bool {
        return local_address == other.local_address &&
               local_port == other.local_port &&
               remote_address == other.remote_address &&
               remote_port == other.remote_port;
    }

    /**
     * @brief Get path identifier string
     */
    [[nodiscard]] auto to_string() const -> std::string {
        return local_address + ":" + std::to_string(local_port) + " -> " +
               remote_address + ":" + std::to_string(remote_port);
    }
};

/**
 * @brief Migration state enumeration
 */
enum class migration_state {
    idle,              ///< No migration in progress
    detecting,         ///< Detecting network changes
    probing,           ///< Probing new path
    validating,        ///< Validating new path
    migrating,         ///< Migration in progress
    completed,         ///< Migration completed successfully
    failed             ///< Migration failed
};

/**
 * @brief Convert migration_state to string
 */
[[nodiscard]] constexpr auto to_string(migration_state state) -> const char* {
    switch (state) {
        case migration_state::idle: return "idle";
        case migration_state::detecting: return "detecting";
        case migration_state::probing: return "probing";
        case migration_state::validating: return "validating";
        case migration_state::migrating: return "migrating";
        case migration_state::completed: return "completed";
        case migration_state::failed: return "failed";
        default: return "unknown";
    }
}

/**
 * @brief Migration event types
 */
enum class migration_event {
    network_change_detected,  ///< Network interface change detected
    path_probe_started,       ///< Started probing new path
    path_probe_succeeded,     ///< Path probe succeeded
    path_probe_failed,        ///< Path probe failed
    migration_started,        ///< Migration started
    migration_completed,      ///< Migration completed successfully
    migration_failed,         ///< Migration failed
    path_validated,           ///< Path validation completed
    path_degraded,            ///< Current path quality degraded
    fallback_triggered        ///< Fallback to previous path triggered
};

/**
 * @brief Convert migration_event to string
 */
[[nodiscard]] constexpr auto to_string(migration_event event) -> const char* {
    switch (event) {
        case migration_event::network_change_detected: return "network_change_detected";
        case migration_event::path_probe_started: return "path_probe_started";
        case migration_event::path_probe_succeeded: return "path_probe_succeeded";
        case migration_event::path_probe_failed: return "path_probe_failed";
        case migration_event::migration_started: return "migration_started";
        case migration_event::migration_completed: return "migration_completed";
        case migration_event::migration_failed: return "migration_failed";
        case migration_event::path_validated: return "path_validated";
        case migration_event::path_degraded: return "path_degraded";
        case migration_event::fallback_triggered: return "fallback_triggered";
        default: return "unknown";
    }
}

/**
 * @brief Migration event data passed to callbacks
 */
struct migration_event_data {
    /// Event type
    migration_event event;

    /// Previous path (if applicable)
    std::optional<network_path> old_path;

    /// New path (if applicable)
    std::optional<network_path> new_path;

    /// Error message (for failure events)
    std::string error_message;

    /// Event timestamp
    std::chrono::steady_clock::time_point timestamp;

    migration_event_data()
        : event(migration_event::network_change_detected),
          timestamp(std::chrono::steady_clock::now()) {}

    explicit migration_event_data(migration_event e)
        : event(e), timestamp(std::chrono::steady_clock::now()) {}
};

/**
 * @brief Migration result containing migration details
 */
struct migration_result {
    bool success = false;
    network_path old_path;
    network_path new_path;
    std::chrono::milliseconds duration{0};
    std::string error_message;

    /**
     * @brief Create a successful migration result
     */
    static auto succeeded(
        const network_path& old_p,
        const network_path& new_p,
        std::chrono::milliseconds dur) -> migration_result {
        migration_result result;
        result.success = true;
        result.old_path = old_p;
        result.new_path = new_p;
        result.duration = dur;
        return result;
    }

    /**
     * @brief Create a failed migration result
     */
    static auto failed(
        const network_path& old_p,
        const std::string& error) -> migration_result {
        migration_result result;
        result.success = false;
        result.old_path = old_p;
        result.error_message = error;
        return result;
    }
};

/**
 * @brief Migration statistics
 */
struct migration_statistics {
    uint64_t total_migrations = 0;           ///< Total migration attempts
    uint64_t successful_migrations = 0;      ///< Successful migrations
    uint64_t failed_migrations = 0;          ///< Failed migrations
    uint64_t path_probes = 0;                ///< Total path probes
    uint64_t network_changes_detected = 0;   ///< Network changes detected
    std::chrono::milliseconds total_downtime{0};  ///< Cumulative downtime
    std::chrono::milliseconds avg_migration_time{0};  ///< Average migration time
};

/**
 * @brief Configuration for connection migration
 */
struct migration_config {
    /// Enable automatic migration on network changes
    bool auto_migrate = true;

    /// Enable path probing for new paths
    bool enable_path_probing = true;

    /// Path probe interval
    std::chrono::milliseconds probe_interval{1000};

    /// Path probe timeout
    std::chrono::milliseconds probe_timeout{5000};

    /// Maximum number of probe retries
    std::size_t max_probe_retries = 3;

    /// Path validation timeout
    std::chrono::milliseconds validation_timeout{10000};

    /// Enable fallback to previous path on migration failure
    bool enable_fallback = true;

    /// Minimum RTT improvement to trigger migration (in percentage)
    double min_rtt_improvement_percent = 20.0;

    /// Network change detection interval
    std::chrono::milliseconds detection_interval{500};

    /// Keep previous paths for fallback
    bool keep_previous_paths = true;

    /// Maximum number of previous paths to keep
    std::size_t max_previous_paths = 3;
};

/**
 * @brief Network interface information
 */
struct network_interface {
    std::string name;            ///< Interface name
    std::string address;         ///< IP address
    bool is_up = false;          ///< Interface is up
    bool is_wireless = false;    ///< Is wireless interface
    int priority = 0;            ///< Interface priority (higher = preferred)
};

/**
 * @brief Connection migration manager
 *
 * Manages QUIC connection migration during network changes.
 * Supports both client-initiated and server-initiated migration.
 *
 * @code
 * auto config = migration_config{};
 * config.auto_migrate = true;
 * config.enable_path_probing = true;
 *
 * auto manager = connection_migration_manager::create(config);
 *
 * manager->on_migration_event([](const migration_event_data& event) {
 *     std::cout << "Migration event: " << to_string(event.event) << "\n";
 * });
 *
 * // Start monitoring network changes
 * manager->start_monitoring();
 *
 * // Later, when network changes are detected
 * auto result = manager->migrate_to_path(new_path);
 * if (result.has_value()) {
 *     std::cout << "Migration successful: " << result.value().duration.count() << "ms\n";
 * }
 * @endcode
 */
class connection_migration_manager {
public:
    using event_callback = std::function<void(const migration_event_data&)>;
    using network_change_callback = std::function<void(const std::vector<network_interface>&)>;

    /**
     * @brief Create a connection migration manager
     * @param config Migration configuration
     * @return Manager instance
     */
    [[nodiscard]] static auto create(const migration_config& config = {})
        -> std::unique_ptr<connection_migration_manager>;

    ~connection_migration_manager();

    // Non-copyable
    connection_migration_manager(const connection_migration_manager&) = delete;
    auto operator=(const connection_migration_manager&) -> connection_migration_manager& = delete;

    // Movable
    connection_migration_manager(connection_migration_manager&&) noexcept;
    auto operator=(connection_migration_manager&&) noexcept -> connection_migration_manager&;

    /**
     * @brief Start monitoring network changes
     * @return Result indicating success or failure
     */
    [[nodiscard]] auto start_monitoring() -> result<void>;

    /**
     * @brief Stop monitoring network changes
     */
    void stop_monitoring();

    /**
     * @brief Check if monitoring is active
     */
    [[nodiscard]] auto is_monitoring() const -> bool;

    /**
     * @brief Get current migration state
     */
    [[nodiscard]] auto state() const -> migration_state;

    /**
     * @brief Get current network path
     */
    [[nodiscard]] auto current_path() const -> std::optional<network_path>;

    /**
     * @brief Set current network path
     * @param path The current active path
     */
    void set_current_path(const network_path& path);

    /**
     * @brief Get previous network paths
     */
    [[nodiscard]] auto previous_paths() const -> std::vector<network_path>;

    /**
     * @brief Migrate connection to a new path
     * @param new_path Target path for migration
     * @return Migration result
     */
    [[nodiscard]] auto migrate_to_path(const network_path& new_path)
        -> result<migration_result>;

    /**
     * @brief Probe a new path for viability
     * @param path Path to probe
     * @return Result indicating if path is viable
     */
    [[nodiscard]] auto probe_path(const network_path& path) -> result<bool>;

    /**
     * @brief Validate a path after migration
     * @param path Path to validate
     * @return Validation result
     */
    [[nodiscard]] auto validate_path(const network_path& path) -> result<bool>;

    /**
     * @brief Trigger fallback to previous path
     * @return Fallback result
     */
    [[nodiscard]] auto fallback_to_previous() -> result<migration_result>;

    /**
     * @brief Get available network interfaces
     */
    [[nodiscard]] auto get_available_interfaces() const -> std::vector<network_interface>;

    /**
     * @brief Detect network changes
     * @return List of detected changes
     */
    [[nodiscard]] auto detect_network_changes() -> std::vector<network_interface>;

    /**
     * @brief Set callback for migration events
     * @param callback Event callback function
     */
    void on_migration_event(event_callback callback);

    /**
     * @brief Set callback for network changes
     * @param callback Network change callback function
     */
    void on_network_change(network_change_callback callback);

    /**
     * @brief Get migration statistics
     */
    [[nodiscard]] auto get_statistics() const -> migration_statistics;

    /**
     * @brief Reset migration statistics
     */
    void reset_statistics();

    /**
     * @brief Get current configuration
     */
    [[nodiscard]] auto config() const -> const migration_config&;

    /**
     * @brief Update configuration
     * @param config New configuration
     */
    void set_config(const migration_config& config);

    /**
     * @brief Check if migration is available
     * @return true if migration can be performed
     */
    [[nodiscard]] auto is_migration_available() const -> bool;

    /**
     * @brief Cancel ongoing migration
     */
    void cancel_migration();

private:
    explicit connection_migration_manager(migration_config config);

    void emit_event(const migration_event_data& event);
    void update_statistics(const migration_result& result);

    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_TRANSPORT_CONNECTION_MIGRATION_H
