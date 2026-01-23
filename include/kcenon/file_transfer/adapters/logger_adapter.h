// BSD 3-Clause License
//
// Copyright (c) 2025, kcenon
// All rights reserved.

/**
 * @file logger_adapter.h
 * @brief ILogger adapter for file_trans_system
 *
 * This adapter bridges file_transfer logging to the common::interfaces::ILogger
 * interface, enabling standardized logging across the kcenon ecosystem.
 *
 * Features enabled when logger_system is available:
 * - OpenTelemetry trace/span correlation
 * - Structured JSON logging
 * - Log sampling for high-throughput transfers
 * - Dynamic log routing
 *
 * @since 0.3.0
 */

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <optional>
#include <atomic>
#include <mutex>

#include "../config/feature_flags.h"
#include "../core/types.h"

// Include common_system ILogger interface
#if KCENON_WITH_COMMON_SYSTEM
#include <kcenon/common/interfaces/logger_interface.h>
#include <kcenon/common/utils/source_location.h>
#endif

// Include logger_system when available
#if FILE_TRANSFER_USE_LOGGER_SYSTEM
#include <kcenon/logger/core/logger.h>
#include <kcenon/logger/core/logger_builder.h>
#include <kcenon/logger/writers/console_writer.h>
#include <kcenon/logger/otlp/otel_context.h>
#endif

namespace kcenon::file_transfer::adapters {

/**
 * @brief Transfer context for structured logging
 *
 * Contains metadata about the current file transfer operation
 * for inclusion in log entries.
 */
struct transfer_context {
    std::string transfer_id;
    std::string filename;
    std::optional<uint64_t> file_size;
    std::optional<std::string> client_id;
    std::optional<std::string> server_address;

    /**
     * @brief Check if context has any data set
     */
    [[nodiscard]] bool is_empty() const {
        return transfer_id.empty() && filename.empty();
    }

    /**
     * @brief Clear all context data
     */
    void clear() {
        transfer_id.clear();
        filename.clear();
        file_size.reset();
        client_id.reset();
        server_address.reset();
    }
};

#if KCENON_WITH_COMMON_SYSTEM

/**
 * @brief Adapter that exposes file_transfer logging through ILogger interface
 *
 * This adapter implements common::interfaces::ILogger and provides:
 * - Standardized logging interface across the ecosystem
 * - OpenTelemetry context propagation (when logger_system available)
 * - Structured logging support for file transfers
 * - Backward compatibility with existing FT_LOG_* macros
 *
 * @note Thread-safe: All public methods are safe to call from multiple threads.
 *
 * @example Basic usage:
 * @code
 * auto adapter = file_transfer_logger_adapter::create();
 * if (adapter) {
 *     adapter->log(log_level::info, "Transfer started");
 *     adapter->set_transfer_context({"txn-123", "file.dat", 1024});
 * }
 * @endcode
 *
 * @since 0.3.0
 */
class file_transfer_logger_adapter : public common::interfaces::ILogger {
public:
    /**
     * @brief Factory method to create an adapter instance
     * @return Shared pointer to the adapter, or nullptr on failure
     */
    [[nodiscard]] static std::shared_ptr<file_transfer_logger_adapter> create();

    /**
     * @brief Default constructor
     */
    file_transfer_logger_adapter();

    /**
     * @brief Destructor
     */
    ~file_transfer_logger_adapter() override;

    // Non-copyable
    file_transfer_logger_adapter(const file_transfer_logger_adapter&) = delete;
    file_transfer_logger_adapter& operator=(const file_transfer_logger_adapter&) = delete;

    // Movable
    file_transfer_logger_adapter(file_transfer_logger_adapter&&) noexcept;
    file_transfer_logger_adapter& operator=(file_transfer_logger_adapter&&) noexcept;

    // =========================================================================
    // ILogger interface implementation
    // =========================================================================

    /**
     * @brief Log a message with specified level
     * @param level Log level
     * @param message Log message
     * @return VoidResult indicating success or error
     */
    common::VoidResult log(common::interfaces::log_level level,
                           const std::string& message) override;

    /**
     * @brief Log a message with source location (C++20)
     * @param level Log level
     * @param message Log message
     * @param loc Source location
     * @return VoidResult indicating success or error
     */
    common::VoidResult log(common::interfaces::log_level level,
                           std::string_view message,
                           const common::source_location& loc = common::source_location::current()) override;

    /**
     * @brief Log a structured entry
     * @param entry Log entry containing all information
     * @return VoidResult indicating success or error
     */
    common::VoidResult log(const common::interfaces::log_entry& entry) override;

    /**
     * @brief Check if logging is enabled for the specified level
     * @param level Log level to check
     * @return true if logging is enabled for this level
     */
    [[nodiscard]] bool is_enabled(common::interfaces::log_level level) const override;

    /**
     * @brief Set the minimum log level
     * @param level Minimum level for messages to be logged
     * @return VoidResult indicating success or error
     */
    common::VoidResult set_level(common::interfaces::log_level level) override;

    /**
     * @brief Get the current minimum log level
     * @return Current minimum log level
     */
    [[nodiscard]] common::interfaces::log_level get_level() const override;

    /**
     * @brief Flush any buffered log messages
     * @return VoidResult indicating success or error
     */
    common::VoidResult flush() override;

    // =========================================================================
    // File transfer specific extensions
    // =========================================================================

    /**
     * @brief Set transfer context for structured logging
     * @param ctx Transfer context containing transfer metadata
     *
     * @details When set, all subsequent log messages will include
     * the transfer context information in structured log output.
     */
    void set_transfer_context(const transfer_context& ctx);

    /**
     * @brief Get the current transfer context
     * @return Current transfer context
     */
    [[nodiscard]] transfer_context get_transfer_context() const;

    /**
     * @brief Clear the transfer context
     */
    void clear_transfer_context();

    /**
     * @brief Check if transfer context is set
     * @return true if context is set
     */
    [[nodiscard]] bool has_transfer_context() const;

    // =========================================================================
    // OpenTelemetry integration (when logger_system available)
    // =========================================================================

#if FILE_TRANSFER_USE_LOGGER_SYSTEM
    /**
     * @brief Set OpenTelemetry context for trace correlation
     * @param ctx OpenTelemetry context (trace_id, span_id, etc.)
     *
     * @details When set, all log messages will include trace/span IDs
     * for distributed tracing correlation.
     */
    void set_otel_context(const kcenon::logger::otlp::otel_context& ctx);

    /**
     * @brief Get the current OpenTelemetry context
     * @return Optional context, empty if not set
     */
    [[nodiscard]] std::optional<kcenon::logger::otlp::otel_context> get_otel_context() const;

    /**
     * @brief Clear the OpenTelemetry context
     */
    void clear_otel_context();

    /**
     * @brief Check if OpenTelemetry context is set
     * @return true if context is set
     */
    [[nodiscard]] bool has_otel_context() const;

    /**
     * @brief Access underlying logger_system logger
     * @return Reference to the underlying logger
     *
     * @note Use this for advanced logger_system features not exposed
     * through the ILogger interface.
     */
    [[nodiscard]] kcenon::logger::logger& underlying_logger();

    /**
     * @brief Access underlying logger_system logger (const version)
     * @return Const reference to the underlying logger
     */
    [[nodiscard]] const kcenon::logger::logger& underlying_logger() const;
#endif

    // =========================================================================
    // Initialization and lifecycle
    // =========================================================================

    /**
     * @brief Initialize the adapter
     * @return true if initialization succeeded
     *
     * @note Automatically called by create(), but can be called manually
     * if using the default constructor.
     */
    bool initialize();

    /**
     * @brief Shutdown the adapter
     *
     * @details Flushes all pending logs and releases resources.
     */
    void shutdown();

    /**
     * @brief Check if adapter is initialized
     * @return true if initialized
     */
    [[nodiscard]] bool is_initialized() const;

private:
    std::atomic<common::interfaces::log_level> min_level_{common::interfaces::log_level::info};
    std::atomic<bool> initialized_{false};
    transfer_context current_context_;
    mutable std::mutex context_mutex_;

#if FILE_TRANSFER_USE_LOGGER_SYSTEM
    std::unique_ptr<kcenon::logger::logger> logger_;
#endif
};

#else // !KCENON_WITH_COMMON_SYSTEM

/**
 * @brief Stub adapter when common_system is not available
 *
 * Provides a minimal implementation that logs to stderr.
 */
class file_transfer_logger_adapter {
public:
    static std::shared_ptr<file_transfer_logger_adapter> create() {
        return std::make_shared<file_transfer_logger_adapter>();
    }

    void set_transfer_context(const transfer_context&) {}
    void clear_transfer_context() {}
    [[nodiscard]] bool has_transfer_context() const { return false; }
    [[nodiscard]] transfer_context get_transfer_context() const { return {}; }

    bool initialize() { return true; }
    void shutdown() {}
    [[nodiscard]] bool is_initialized() const { return true; }
};

#endif // KCENON_WITH_COMMON_SYSTEM

/**
 * @brief Get the global file transfer logger adapter instance
 * @return Reference to the singleton adapter
 *
 * @note Thread-safe: First call initializes the adapter.
 */
file_transfer_logger_adapter& get_logger_adapter();

} // namespace kcenon::file_transfer::adapters
