// BSD 3-Clause License
//
// Copyright (c) 2025, kcenon
// All rights reserved.

/**
 * @file monitorable_adapter.h
 * @brief IMonitorable adapter for file_trans_system components
 *
 * This adapter makes file_transfer_server and file_transfer_client observable
 * through the common::interfaces::IMonitorable interface, enabling integration
 * with the kcenon monitoring ecosystem.
 *
 * @since 0.3.0
 */

#pragma once

#include <memory>
#include <string>

#include "../config/feature_flags.h"
#include "../core/types.h"

#if KCENON_WITH_COMMON_SYSTEM
#include <kcenon/common/interfaces/monitoring_interface.h>
#endif

namespace kcenon::file_transfer {

// Forward declarations
class file_transfer_server;
class file_transfer_client;

}  // namespace kcenon::file_transfer

namespace kcenon::file_transfer::adapters {

#if KCENON_WITH_COMMON_SYSTEM

/**
 * @brief Makes file_transfer_server observable through IMonitorable interface
 *
 * This adapter implements common::interfaces::IMonitorable, allowing the
 * file transfer server to be registered with monitoring registries and
 * health check systems.
 *
 * @note Thread-safe: All public methods are safe to call from multiple threads.
 *
 * @example Basic usage:
 * @code
 * auto server = std::make_shared<file_transfer_server>(
 *     file_transfer_server::builder()
 *         .with_storage_directory("/data")
 *         .build()
 *         .value());
 *
 * auto monitorable = std::make_shared<file_transfer_server_monitorable>(
 *     server, "file_server_01");
 *
 * // Register with monitoring system
 * monitoring_registry::instance().register_component(monitorable);
 *
 * // Get monitoring data
 * auto data = monitorable->get_monitoring_data();
 * if (data.has_value()) {
 *     for (const auto& metric : data.value().metrics) {
 *         std::cout << metric.name << ": " << metric.value << "\n";
 *     }
 * }
 * @endcode
 *
 * @since 0.3.0
 */
class file_transfer_server_monitorable : public common::interfaces::IMonitorable {
public:
    /**
     * @brief Factory method to create a monitorable wrapper
     * @param server Shared pointer to the file transfer server
     * @param name Component name for identification
     * @return Shared pointer to the monitorable
     */
    [[nodiscard]] static std::shared_ptr<file_transfer_server_monitorable> create(
        std::shared_ptr<file_transfer_server> server,
        const std::string& name = "file_transfer_server");

    /**
     * @brief Constructor
     * @param server Shared pointer to the file transfer server
     * @param name Component name for identification
     */
    explicit file_transfer_server_monitorable(
        std::shared_ptr<file_transfer_server> server,
        const std::string& name = "file_transfer_server");

    /**
     * @brief Destructor
     */
    ~file_transfer_server_monitorable() override;

    // Non-copyable
    file_transfer_server_monitorable(const file_transfer_server_monitorable&) = delete;
    file_transfer_server_monitorable& operator=(const file_transfer_server_monitorable&) = delete;

    // Movable
    file_transfer_server_monitorable(file_transfer_server_monitorable&&) noexcept;
    file_transfer_server_monitorable& operator=(file_transfer_server_monitorable&&) noexcept;

    // =========================================================================
    // IMonitorable interface implementation
    // =========================================================================

    /**
     * @brief Get monitoring data from the server
     * @return Result containing metrics snapshot or error
     *
     * @details Collects comprehensive metrics including:
     * - Transfer statistics (bytes, counts)
     * - Connection statistics
     * - Quota usage
     * - Server uptime
     */
    common::Result<common::interfaces::metrics_snapshot> get_monitoring_data() override;

    /**
     * @brief Perform health check on the server
     * @return Result containing health check result or error
     *
     * @details Evaluates:
     * - Server running status
     * - Storage quota status
     * - Connection capacity
     * - Storage accessibility
     */
    common::Result<common::interfaces::health_check_result> health_check() override;

    /**
     * @brief Get component name for monitoring identification
     * @return Component identifier string
     */
    [[nodiscard]] std::string get_component_name() const override;

    // =========================================================================
    // Additional methods
    // =========================================================================

    /**
     * @brief Check if the server is still available
     * @return true if the server reference is valid
     */
    [[nodiscard]] bool is_server_available() const;

    /**
     * @brief Set component name
     * @param name New component name
     */
    void set_component_name(const std::string& name);

private:
    std::weak_ptr<file_transfer_server> server_;
    std::string component_name_;
};

/**
 * @brief Makes file_transfer_client observable through IMonitorable interface
 *
 * This adapter implements common::interfaces::IMonitorable, allowing the
 * file transfer client to be registered with monitoring registries and
 * health check systems.
 *
 * @note Thread-safe: All public methods are safe to call from multiple threads.
 *
 * @example Basic usage:
 * @code
 * auto client = std::make_shared<file_transfer_client>(
 *     file_transfer_client::builder().build().value());
 *
 * auto monitorable = std::make_shared<file_transfer_client_monitorable>(
 *     client, "file_client_01");
 *
 * // Check client health
 * auto health = monitorable->health_check();
 * if (health.has_value()) {
 *     std::cout << "Client status: " << to_string(health.value().status) << "\n";
 * }
 * @endcode
 *
 * @since 0.3.0
 */
class file_transfer_client_monitorable : public common::interfaces::IMonitorable {
public:
    /**
     * @brief Factory method to create a monitorable wrapper
     * @param client Shared pointer to the file transfer client
     * @param name Component name for identification
     * @return Shared pointer to the monitorable
     */
    [[nodiscard]] static std::shared_ptr<file_transfer_client_monitorable> create(
        std::shared_ptr<file_transfer_client> client,
        const std::string& name = "file_transfer_client");

    /**
     * @brief Constructor
     * @param client Shared pointer to the file transfer client
     * @param name Component name for identification
     */
    explicit file_transfer_client_monitorable(
        std::shared_ptr<file_transfer_client> client,
        const std::string& name = "file_transfer_client");

    /**
     * @brief Destructor
     */
    ~file_transfer_client_monitorable() override;

    // Non-copyable
    file_transfer_client_monitorable(const file_transfer_client_monitorable&) = delete;
    file_transfer_client_monitorable& operator=(const file_transfer_client_monitorable&) = delete;

    // Movable
    file_transfer_client_monitorable(file_transfer_client_monitorable&&) noexcept;
    file_transfer_client_monitorable& operator=(file_transfer_client_monitorable&&) noexcept;

    // =========================================================================
    // IMonitorable interface implementation
    // =========================================================================

    /**
     * @brief Get monitoring data from the client
     * @return Result containing metrics snapshot or error
     *
     * @details Collects metrics including:
     * - Transfer statistics (bytes, counts)
     * - Connection status
     * - Active transfer progress
     */
    common::Result<common::interfaces::metrics_snapshot> get_monitoring_data() override;

    /**
     * @brief Perform health check on the client
     * @return Result containing health check result or error
     *
     * @details Evaluates:
     * - Client connection status
     * - Active transfer health
     */
    common::Result<common::interfaces::health_check_result> health_check() override;

    /**
     * @brief Get component name for monitoring identification
     * @return Component identifier string
     */
    [[nodiscard]] std::string get_component_name() const override;

    // =========================================================================
    // Additional methods
    // =========================================================================

    /**
     * @brief Check if the client is still available
     * @return true if the client reference is valid
     */
    [[nodiscard]] bool is_client_available() const;

    /**
     * @brief Set component name
     * @param name New component name
     */
    void set_component_name(const std::string& name);

private:
    std::weak_ptr<file_transfer_client> client_;
    std::string component_name_;
};

#else  // !KCENON_WITH_COMMON_SYSTEM

/**
 * @brief Stub server monitorable when common_system is not available
 */
class file_transfer_server_monitorable {
public:
    static std::shared_ptr<file_transfer_server_monitorable> create(
        std::shared_ptr<file_transfer_server> /* server */,
        const std::string& name = "file_transfer_server") {
        return std::make_shared<file_transfer_server_monitorable>(name);
    }

    explicit file_transfer_server_monitorable(const std::string& name = "file_transfer_server")
        : component_name_(name) {}

    [[nodiscard]] std::string get_component_name() const { return component_name_; }
    [[nodiscard]] bool is_server_available() const { return false; }
    void set_component_name(const std::string& name) { component_name_ = name; }

private:
    std::string component_name_;
};

/**
 * @brief Stub client monitorable when common_system is not available
 */
class file_transfer_client_monitorable {
public:
    static std::shared_ptr<file_transfer_client_monitorable> create(
        std::shared_ptr<file_transfer_client> /* client */,
        const std::string& name = "file_transfer_client") {
        return std::make_shared<file_transfer_client_monitorable>(name);
    }

    explicit file_transfer_client_monitorable(const std::string& name = "file_transfer_client")
        : component_name_(name) {}

    [[nodiscard]] std::string get_component_name() const { return component_name_; }
    [[nodiscard]] bool is_client_available() const { return false; }
    void set_component_name(const std::string& name) { component_name_ = name; }

private:
    std::string component_name_;
};

#endif  // KCENON_WITH_COMMON_SYSTEM

}  // namespace kcenon::file_transfer::adapters
