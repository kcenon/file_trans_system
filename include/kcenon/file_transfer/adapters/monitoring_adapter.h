// BSD 3-Clause License
//
// Copyright (c) 2025, kcenon
// All rights reserved.

/**
 * @file monitoring_adapter.h
 * @brief IMonitor adapter for file_trans_system
 *
 * This adapter bridges file_transfer monitoring to the common::interfaces::IMonitor
 * interface, enabling standardized metrics collection across the kcenon ecosystem.
 *
 * Collected metrics when monitoring is enabled:
 * - file_transfer.bytes_sent (counter) - Total bytes sent to clients
 * - file_transfer.bytes_received (counter) - Total bytes received from clients
 * - file_transfer.active_transfers (gauge) - Current active transfers
 * - file_transfer.active_uploads (gauge) - Current active uploads
 * - file_transfer.active_downloads (gauge) - Current active downloads
 * - file_transfer.active_connections (gauge) - Connected clients
 * - file_transfer.completed_uploads (counter) - Total completed uploads
 * - file_transfer.completed_downloads (counter) - Total completed downloads
 * - file_transfer.quota_usage_percent (gauge) - Storage quota usage
 * - file_transfer.quota_used_bytes (gauge) - Storage bytes used
 * - file_transfer.quota_available_bytes (gauge) - Storage bytes available
 * - file_transfer.uptime_ms (counter) - Server uptime in milliseconds
 *
 * @since 0.3.0
 */

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "../config/feature_flags.h"
#include "../core/types.h"

#if KCENON_WITH_COMMON_SYSTEM
#include <kcenon/common/interfaces/monitoring_interface.h>
#endif

namespace kcenon::file_transfer {

// Forward declarations
class file_transfer_server;

}  // namespace kcenon::file_transfer

namespace kcenon::file_transfer::adapters {

#if KCENON_WITH_COMMON_SYSTEM

/**
 * @brief Adapter that exposes file_transfer metrics through IMonitor interface
 *
 * This adapter implements common::interfaces::IMonitor and provides:
 * - Standardized metrics interface across the ecosystem
 * - File transfer statistics (bytes, transfers, connections)
 * - Storage quota monitoring
 * - Health checks for server status
 *
 * @note Thread-safe: All public methods are safe to call from multiple threads.
 *
 * @example Basic usage:
 * @code
 * auto server = file_transfer_server::builder()
 *     .with_storage_directory("/data")
 *     .build()
 *     .value();
 *
 * auto monitor = file_transfer_monitor_adapter::create(
 *     std::make_shared<file_transfer_server>(std::move(server)));
 *
 * // Get metrics snapshot
 * auto metrics_result = monitor->get_metrics();
 * if (metrics_result.has_value()) {
 *     for (const auto& metric : metrics_result.value().metrics) {
 *         std::cout << metric.name << ": " << metric.value << "\n";
 *     }
 * }
 *
 * // Perform health check
 * auto health_result = monitor->check_health();
 * if (health_result.has_value()) {
 *     std::cout << "Status: " << to_string(health_result.value().status) << "\n";
 * }
 * @endcode
 *
 * @since 0.3.0
 */
class file_transfer_monitor_adapter : public common::interfaces::IMonitor {
public:
    /**
     * @brief Factory method to create an adapter instance
     * @param server Shared pointer to the file transfer server
     * @param source_id Optional identifier for the metrics source
     * @return Shared pointer to the adapter
     */
    [[nodiscard]] static std::shared_ptr<file_transfer_monitor_adapter> create(
        std::shared_ptr<file_transfer_server> server,
        const std::string& source_id = "file_transfer");

    /**
     * @brief Constructor
     * @param server Shared pointer to the file transfer server
     * @param source_id Identifier for the metrics source
     */
    explicit file_transfer_monitor_adapter(
        std::shared_ptr<file_transfer_server> server,
        const std::string& source_id = "file_transfer");

    /**
     * @brief Destructor
     */
    ~file_transfer_monitor_adapter() override;

    // Non-copyable
    file_transfer_monitor_adapter(const file_transfer_monitor_adapter&) = delete;
    file_transfer_monitor_adapter& operator=(const file_transfer_monitor_adapter&) = delete;

    // Movable
    file_transfer_monitor_adapter(file_transfer_monitor_adapter&&) noexcept;
    file_transfer_monitor_adapter& operator=(file_transfer_monitor_adapter&&) noexcept;

    // =========================================================================
    // IMonitor interface implementation
    // =========================================================================

    /**
     * @brief Record a metric value
     * @param name Metric name
     * @param value Metric value
     * @return VoidResult indicating success or error
     *
     * @note This method stores custom metrics that will be included in get_metrics()
     */
    common::VoidResult record_metric(const std::string& name, double value) override;

    /**
     * @brief Record a metric with tags
     * @param name Metric name
     * @param value Metric value
     * @param tags Additional metadata tags
     * @return VoidResult indicating success or error
     */
    common::VoidResult record_metric(
        const std::string& name,
        double value,
        const std::unordered_map<std::string, std::string>& tags) override;

    /**
     * @brief Get current metrics snapshot
     * @return Result containing metrics snapshot or error
     *
     * @details Collects metrics from the server including:
     * - Transfer statistics (bytes sent/received, active transfers)
     * - Connection statistics
     * - Quota usage information
     * - Custom recorded metrics
     */
    common::Result<common::interfaces::metrics_snapshot> get_metrics() override;

    /**
     * @brief Perform health check
     * @return Result containing health check result or error
     *
     * @details Checks the following conditions:
     * - Server running status
     * - Storage quota status (critical > 95%, warning > 80%)
     * - Connection capacity (warning if > 90% of max)
     */
    common::Result<common::interfaces::health_check_result> check_health() override;

    /**
     * @brief Reset all custom metrics
     * @return VoidResult indicating success or error
     *
     * @note This only resets custom metrics recorded via record_metric().
     *       Server statistics are not affected.
     */
    common::VoidResult reset() override;

    // =========================================================================
    // Additional methods
    // =========================================================================

    /**
     * @brief Get the source identifier
     * @return Source ID string
     */
    [[nodiscard]] std::string get_source_id() const;

    /**
     * @brief Check if the server is still available
     * @return true if the server reference is valid
     */
    [[nodiscard]] bool is_server_available() const;

private:
    std::weak_ptr<file_transfer_server> server_;
    std::string source_id_;

    // Custom metrics storage
    struct custom_metric {
        double value;
        common::interfaces::metric_type type;
        std::unordered_map<std::string, std::string> tags;
    };
    std::unordered_map<std::string, custom_metric> custom_metrics_;
    mutable std::mutex metrics_mutex_;

    // Helper methods
    [[nodiscard]] common::interfaces::metrics_snapshot collect_server_metrics() const;
    [[nodiscard]] common::interfaces::health_check_result check_server_health() const;
};

#else  // !KCENON_WITH_COMMON_SYSTEM

/**
 * @brief Stub adapter when common_system is not available
 *
 * Provides a minimal implementation that can be used for type compatibility
 * when common_system is not linked.
 */
class file_transfer_monitor_adapter {
public:
    static std::shared_ptr<file_transfer_monitor_adapter> create(
        std::shared_ptr<file_transfer_server> /* server */,
        const std::string& /* source_id */ = "file_transfer") {
        return std::make_shared<file_transfer_monitor_adapter>();
    }

    file_transfer_monitor_adapter() = default;
    ~file_transfer_monitor_adapter() = default;

    [[nodiscard]] std::string get_source_id() const { return "file_transfer"; }
    [[nodiscard]] bool is_server_available() const { return false; }
};

#endif  // KCENON_WITH_COMMON_SYSTEM

}  // namespace kcenon::file_transfer::adapters
