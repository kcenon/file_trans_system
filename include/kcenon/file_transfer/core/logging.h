// BSD 3-Clause License
//
// Copyright (c) 2025, kcenon
// All rights reserved.

#pragma once

#include <string>
#include <string_view>
#include <chrono>
#include <optional>
#include <memory>
#include <functional>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <mutex>

#ifdef BUILD_WITH_LOGGER_SYSTEM
#include <kcenon/logger/core/logger.h>
#include <kcenon/logger/core/logger_builder.h>
#include <kcenon/logger/writers/console_writer.h>
#endif

namespace kcenon::file_transfer {

/**
 * @brief Log categories for file transfer system
 */
struct log_category {
    static constexpr std::string_view server = "file_transfer.server";
    static constexpr std::string_view client = "file_transfer.client";
    static constexpr std::string_view pipeline = "file_transfer.pipeline";
    static constexpr std::string_view compression = "file_transfer.compression";
    static constexpr std::string_view resume = "file_transfer.resume";
    static constexpr std::string_view transfer = "file_transfer.transfer";
    static constexpr std::string_view chunk = "file_transfer.chunk";
};

/**
 * @brief Log levels for file transfer system
 */
enum class log_level {
    trace = 0,
    debug = 1,
    info = 2,
    warn = 3,
    error = 4,
    fatal = 5
};

/**
 * @brief Convert log level to string
 */
inline std::string_view log_level_to_string(log_level level) {
    switch (level) {
        case log_level::trace: return "TRACE";
        case log_level::debug: return "DEBUG";
        case log_level::info: return "INFO";
        case log_level::warn: return "WARN";
        case log_level::error: return "ERROR";
        case log_level::fatal: return "FATAL";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Structured log context for file transfer operations
 */
struct transfer_log_context {
    std::string transfer_id;
    std::string filename;
    std::optional<uint64_t> file_size;
    std::optional<uint64_t> bytes_transferred;
    std::optional<uint32_t> chunk_index;
    std::optional<uint32_t> total_chunks;
    std::optional<double> progress_percent;
    std::optional<double> rate_mbps;
    std::optional<uint64_t> duration_ms;
    std::optional<std::string> error_message;
    std::optional<std::string> client_id;
    std::optional<std::string> server_address;

    /**
     * @brief Convert context to JSON string
     */
    std::string to_json() const {
        std::ostringstream oss;
        oss << "{";

        bool first = true;
        auto add_field = [&](const char* name, const std::string& value) {
            if (!first) oss << ",";
            oss << "\"" << name << "\":\"" << value << "\"";
            first = false;
        };
        auto add_numeric = [&](const char* name, auto value) {
            if (!first) oss << ",";
            oss << "\"" << name << "\":" << value;
            first = false;
        };

        if (!transfer_id.empty()) add_field("transfer_id", transfer_id);
        if (!filename.empty()) add_field("filename", filename);
        if (file_size) add_numeric("file_size", *file_size);
        if (bytes_transferred) add_numeric("bytes_transferred", *bytes_transferred);
        if (chunk_index) add_numeric("chunk_index", *chunk_index);
        if (total_chunks) add_numeric("total_chunks", *total_chunks);
        if (progress_percent) add_numeric("progress_percent", std::fixed << std::setprecision(2) << *progress_percent);
        if (rate_mbps) add_numeric("rate_mbps", std::fixed << std::setprecision(2) << *rate_mbps);
        if (duration_ms) add_numeric("duration_ms", *duration_ms);
        if (error_message) add_field("error_message", *error_message);
        if (client_id) add_field("client_id", *client_id);
        if (server_address) add_field("server_address", *server_address);

        oss << "}";
        return oss.str();
    }
};

// Forward declaration
class file_transfer_logger;

/**
 * @brief Global logger accessor
 */
file_transfer_logger& get_logger();

/**
 * @brief File transfer logging interface
 */
class file_transfer_logger {
public:
    using log_callback = std::function<void(log_level, std::string_view, std::string_view, const transfer_log_context*)>;

    file_transfer_logger() = default;
    ~file_transfer_logger() = default;

    file_transfer_logger(const file_transfer_logger&) = delete;
    file_transfer_logger& operator=(const file_transfer_logger&) = delete;

    /**
     * @brief Initialize the logger
     */
    void initialize() {
#ifdef BUILD_WITH_LOGGER_SYSTEM
        auto result = kcenon::logger::logger_builder()
            .with_async(true)
            .with_min_level(kcenon::logger::log_level::info)
            .add_writer("console", std::make_unique<kcenon::logger::console_writer>())
            .build();

        if (result) {
            logger_ = std::move(result.value());
        }
#endif
        initialized_ = true;
    }

    /**
     * @brief Shutdown the logger
     */
    void shutdown() {
#ifdef BUILD_WITH_LOGGER_SYSTEM
        if (logger_) {
            logger_->flush();
            logger_->stop();
            logger_.reset();
        }
#endif
        initialized_ = false;
    }

    /**
     * @brief Check if logger is initialized
     */
    bool is_initialized() const { return initialized_; }

    /**
     * @brief Set minimum log level
     */
    void set_level(log_level level) {
        min_level_ = level;
#ifdef BUILD_WITH_LOGGER_SYSTEM
        if (logger_) {
            logger_->set_min_level(to_logger_level(level));
        }
#endif
    }

    /**
     * @brief Get current log level
     */
    log_level get_level() const { return min_level_; }

    /**
     * @brief Set custom log callback
     */
    void set_callback(log_callback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback_ = std::move(callback);
    }

    /**
     * @brief Check if logging is enabled for level
     */
    bool is_enabled(log_level level) const {
        return static_cast<int>(level) >= static_cast<int>(min_level_);
    }

    /**
     * @brief Log a message
     */
    void log(log_level level,
             std::string_view category,
             std::string_view message,
             const transfer_log_context* context = nullptr,
             const char* file = nullptr,
             int line = 0,
             const char* function = nullptr) {

        if (!is_enabled(level)) return;

        // Call custom callback if set
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (callback_) {
                callback_(level, category, message, context);
            }
        }

#ifdef BUILD_WITH_LOGGER_SYSTEM
        if (logger_) {
            std::string full_message = format_message(category, message, context);
            if (file && line > 0 && function) {
                logger_->log(to_logger_level(level), full_message, file, line, function);
            } else {
                logger_->log(to_logger_level(level), full_message);
            }
        }
#else
        // Fallback: print to stderr
        std::string timestamp = get_timestamp();
        std::string level_str(log_level_to_string(level));
        std::string cat_str(category);
        std::string msg_str(message);

        std::ostringstream oss;
        oss << timestamp << " [" << level_str << "] [" << cat_str << "] " << msg_str;
        if (context) {
            oss << " " << context->to_json();
        }
        oss << "\n";

        // Thread-safe write
        static std::mutex stderr_mutex;
        std::lock_guard<std::mutex> lock(stderr_mutex);
        std::cerr << oss.str();
#endif
    }

    /**
     * @brief Flush pending logs
     */
    void flush() {
#ifdef BUILD_WITH_LOGGER_SYSTEM
        if (logger_) {
            logger_->flush();
        }
#endif
    }

private:
#ifdef BUILD_WITH_LOGGER_SYSTEM
    static kcenon::logger::log_level to_logger_level(log_level level) {
        switch (level) {
            case log_level::trace: return kcenon::logger::log_level::trace;
            case log_level::debug: return kcenon::logger::log_level::debug;
            case log_level::info: return kcenon::logger::log_level::info;
            case log_level::warn: return kcenon::logger::log_level::warning;
            case log_level::error: return kcenon::logger::log_level::error;
            case log_level::fatal: return kcenon::logger::log_level::critical;
            default: return kcenon::logger::log_level::info;
        }
    }

    std::string format_message(std::string_view category,
                               std::string_view message,
                               const transfer_log_context* context) {
        std::ostringstream oss;
        oss << "[" << category << "] " << message;
        if (context) {
            oss << " " << context->to_json();
        }
        return oss.str();
    }

    std::unique_ptr<kcenon::logger::logger> logger_;
#endif

    static std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

    std::atomic<log_level> min_level_{log_level::info};
    std::atomic<bool> initialized_{false};
    log_callback callback_;
    std::mutex callback_mutex_;
};

/**
 * @brief Get global logger instance
 */
inline file_transfer_logger& get_logger() {
    static file_transfer_logger instance;
    return instance;
}

// Logging macros for convenience
#define FT_LOG(level, category, message) \
    kcenon::file_transfer::get_logger().log( \
        level, category, message, nullptr, __FILE__, __LINE__, __FUNCTION__)

#define FT_LOG_CTX(level, category, message, context) \
    kcenon::file_transfer::get_logger().log( \
        level, category, message, &context, __FILE__, __LINE__, __FUNCTION__)

#define FT_LOG_TRACE(category, message) \
    FT_LOG(kcenon::file_transfer::log_level::trace, category, message)

#define FT_LOG_DEBUG(category, message) \
    FT_LOG(kcenon::file_transfer::log_level::debug, category, message)

#define FT_LOG_INFO(category, message) \
    FT_LOG(kcenon::file_transfer::log_level::info, category, message)

#define FT_LOG_WARN(category, message) \
    FT_LOG(kcenon::file_transfer::log_level::warn, category, message)

#define FT_LOG_ERROR(category, message) \
    FT_LOG(kcenon::file_transfer::log_level::error, category, message)

#define FT_LOG_FATAL(category, message) \
    FT_LOG(kcenon::file_transfer::log_level::fatal, category, message)

#define FT_LOG_TRACE_CTX(category, message, ctx) \
    FT_LOG_CTX(kcenon::file_transfer::log_level::trace, category, message, ctx)

#define FT_LOG_DEBUG_CTX(category, message, ctx) \
    FT_LOG_CTX(kcenon::file_transfer::log_level::debug, category, message, ctx)

#define FT_LOG_INFO_CTX(category, message, ctx) \
    FT_LOG_CTX(kcenon::file_transfer::log_level::info, category, message, ctx)

#define FT_LOG_WARN_CTX(category, message, ctx) \
    FT_LOG_CTX(kcenon::file_transfer::log_level::warn, category, message, ctx)

#define FT_LOG_ERROR_CTX(category, message, ctx) \
    FT_LOG_CTX(kcenon::file_transfer::log_level::error, category, message, ctx)

#define FT_LOG_FATAL_CTX(category, message, ctx) \
    FT_LOG_CTX(kcenon::file_transfer::log_level::fatal, category, message, ctx)

} // namespace kcenon::file_transfer
