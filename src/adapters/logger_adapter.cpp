// BSD 3-Clause License
//
// Copyright (c) 2025, kcenon
// All rights reserved.

#include <kcenon/file_transfer/adapters/logger_adapter.h>

#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>

namespace kcenon::file_transfer::adapters {

#if KCENON_WITH_COMMON_SYSTEM

// ============================================================================
// Factory method
// ============================================================================

std::shared_ptr<file_transfer_logger_adapter> file_transfer_logger_adapter::create() {
    auto adapter = std::make_shared<file_transfer_logger_adapter>();
    if (!adapter->initialize()) {
        return nullptr;
    }
    return adapter;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

file_transfer_logger_adapter::file_transfer_logger_adapter() = default;

file_transfer_logger_adapter::~file_transfer_logger_adapter() {
    shutdown();
}

file_transfer_logger_adapter::file_transfer_logger_adapter(file_transfer_logger_adapter&& other) noexcept
    : min_level_(other.min_level_.load())
    , initialized_(other.initialized_.load())
#if FILE_TRANSFER_USE_LOGGER_SYSTEM
    , logger_(std::move(other.logger_))
#endif
{
    std::lock_guard<std::mutex> lock(other.context_mutex_);
    current_context_ = std::move(other.current_context_);
    other.initialized_ = false;
}

file_transfer_logger_adapter& file_transfer_logger_adapter::operator=(file_transfer_logger_adapter&& other) noexcept {
    if (this != &other) {
        shutdown();

        min_level_ = other.min_level_.load();
        initialized_ = other.initialized_.load();

#if FILE_TRANSFER_USE_LOGGER_SYSTEM
        logger_ = std::move(other.logger_);
#endif

        {
            std::lock_guard<std::mutex> lock(other.context_mutex_);
            current_context_ = std::move(other.current_context_);
        }

        other.initialized_ = false;
    }
    return *this;
}

// ============================================================================
// Initialization and lifecycle
// ============================================================================

bool file_transfer_logger_adapter::initialize() {
    bool expected = false;
    if (!initialized_.compare_exchange_strong(expected, true)) {
        return true;  // Already initialized
    }

#if FILE_TRANSFER_USE_LOGGER_SYSTEM
    auto result = kcenon::logger::logger_builder()
        .with_async(true)
        .with_min_level(kcenon::logger::log_level::info)
        .add_writer("console", std::make_unique<kcenon::logger::console_writer>())
        .build();

    if (result) {
        logger_ = std::move(result.value());
        return true;
    }

    initialized_ = false;
    return false;
#else
    return true;
#endif
}

void file_transfer_logger_adapter::shutdown() {
#if FILE_TRANSFER_USE_LOGGER_SYSTEM
    if (logger_) {
        logger_->flush();
        logger_->stop();
        logger_.reset();
    }
#endif

    {
        std::lock_guard<std::mutex> lock(context_mutex_);
        current_context_.clear();
    }

    initialized_ = false;
}

bool file_transfer_logger_adapter::is_initialized() const {
    return initialized_.load();
}

// ============================================================================
// ILogger interface implementation
// ============================================================================

common::VoidResult file_transfer_logger_adapter::log(
    common::interfaces::log_level level,
    const std::string& message) {

    if (!is_enabled(level)) {
        return common::VoidResult::ok({});
    }

#if FILE_TRANSFER_USE_LOGGER_SYSTEM
    if (logger_) {
        return logger_->log(level, message);
    }
#endif

    // Fallback: output to stderr
    auto now = std::chrono::system_clock::now();
    auto time_t_val = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &time_t_val);
#else
    localtime_r(&time_t_val, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count()
        << " [" << common::interfaces::to_string(level) << "] "
        << "[file_transfer] " << message;

    // Add transfer context if available
    {
        std::lock_guard<std::mutex> lock(context_mutex_);
        if (!current_context_.is_empty()) {
            oss << " {transfer_id=" << current_context_.transfer_id;
            if (!current_context_.filename.empty()) {
                oss << ", filename=" << current_context_.filename;
            }
            if (current_context_.file_size) {
                oss << ", size=" << *current_context_.file_size;
            }
            oss << "}";
        }
    }

    std::cerr << oss.str() << "\n";
    return common::VoidResult::ok({});
}

common::VoidResult file_transfer_logger_adapter::log(
    common::interfaces::log_level level,
    std::string_view message,
    const common::source_location& loc) {

    if (!is_enabled(level)) {
        return common::VoidResult::ok({});
    }

#if FILE_TRANSFER_USE_LOGGER_SYSTEM
    if (logger_) {
        // Apply transfer context before logging
        std::lock_guard<std::mutex> lock(context_mutex_);
        if (!current_context_.is_empty()) {
            logger_->set_context("transfer_id", current_context_.transfer_id);
            if (!current_context_.filename.empty()) {
                logger_->set_context("filename", current_context_.filename);
            }
            if (current_context_.file_size) {
                logger_->set_context("file_size", static_cast<int64_t>(*current_context_.file_size));
            }
            if (current_context_.client_id) {
                logger_->set_context("client_id", *current_context_.client_id);
            }
            if (current_context_.server_address) {
                logger_->set_context("server_address", *current_context_.server_address);
            }
        }

        return logger_->log(level, message, loc);
    }
#endif

    // Fallback: use the simple log method
    return log(level, std::string(message));
}

common::VoidResult file_transfer_logger_adapter::log(
    const common::interfaces::log_entry& entry) {

    if (!is_enabled(entry.level)) {
        return common::VoidResult::ok({});
    }

#if FILE_TRANSFER_USE_LOGGER_SYSTEM
    if (logger_) {
        return logger_->log(entry);
    }
#endif

    // Fallback: convert to simple log
    std::ostringstream oss;
    oss << entry.message;
    if (!entry.file.empty()) {
        oss << " [" << entry.file << ":" << entry.line << "]";
    }
    if (!entry.function.empty()) {
        oss << " in " << entry.function;
    }

    return log(entry.level, oss.str());
}

bool file_transfer_logger_adapter::is_enabled(common::interfaces::log_level level) const {
#if FILE_TRANSFER_USE_LOGGER_SYSTEM
    if (logger_) {
        return logger_->is_enabled(level);
    }
#endif

    return static_cast<int>(level) >= static_cast<int>(min_level_.load());
}

common::VoidResult file_transfer_logger_adapter::set_level(common::interfaces::log_level level) {
    min_level_.store(level);

#if FILE_TRANSFER_USE_LOGGER_SYSTEM
    if (logger_) {
        return logger_->set_level(level);
    }
#endif

    return common::VoidResult::ok({});
}

common::interfaces::log_level file_transfer_logger_adapter::get_level() const {
#if FILE_TRANSFER_USE_LOGGER_SYSTEM
    if (logger_) {
        return logger_->get_level();
    }
#endif

    return min_level_.load();
}

common::VoidResult file_transfer_logger_adapter::flush() {
#if FILE_TRANSFER_USE_LOGGER_SYSTEM
    if (logger_) {
        return logger_->flush();
    }
#endif

    std::cerr.flush();
    return common::VoidResult::ok({});
}

// ============================================================================
// File transfer specific extensions
// ============================================================================

void file_transfer_logger_adapter::set_transfer_context(const transfer_context& ctx) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    current_context_ = ctx;

#if FILE_TRANSFER_USE_LOGGER_SYSTEM
    if (logger_) {
        logger_->set_context("transfer_id", ctx.transfer_id);
        if (!ctx.filename.empty()) {
            logger_->set_context("filename", ctx.filename);
        }
        if (ctx.file_size) {
            logger_->set_context("file_size", static_cast<int64_t>(*ctx.file_size));
        }
        if (ctx.client_id) {
            logger_->set_context("client_id", *ctx.client_id);
        }
        if (ctx.server_address) {
            logger_->set_context("server_address", *ctx.server_address);
        }
    }
#endif
}

transfer_context file_transfer_logger_adapter::get_transfer_context() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    return current_context_;
}

void file_transfer_logger_adapter::clear_transfer_context() {
    std::lock_guard<std::mutex> lock(context_mutex_);
    current_context_.clear();

#if FILE_TRANSFER_USE_LOGGER_SYSTEM
    if (logger_) {
        logger_->remove_context("transfer_id");
        logger_->remove_context("filename");
        logger_->remove_context("file_size");
        logger_->remove_context("client_id");
        logger_->remove_context("server_address");
    }
#endif
}

bool file_transfer_logger_adapter::has_transfer_context() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    return !current_context_.is_empty();
}

// ============================================================================
// OpenTelemetry integration
// ============================================================================

#if FILE_TRANSFER_USE_LOGGER_SYSTEM
void file_transfer_logger_adapter::set_otel_context(
    const kcenon::logger::otlp::otel_context& ctx) {
    if (logger_) {
        logger_->set_otel_context(ctx);
    }
}

std::optional<kcenon::logger::otlp::otel_context>
file_transfer_logger_adapter::get_otel_context() const {
    if (logger_) {
        return logger_->get_otel_context();
    }
    return std::nullopt;
}

void file_transfer_logger_adapter::clear_otel_context() {
    if (logger_) {
        logger_->clear_otel_context();
    }
}

bool file_transfer_logger_adapter::has_otel_context() const {
    if (logger_) {
        return logger_->has_otel_context();
    }
    return false;
}

kcenon::logger::logger& file_transfer_logger_adapter::underlying_logger() {
    if (!logger_) {
        throw std::runtime_error("Logger not initialized");
    }
    return *logger_;
}

const kcenon::logger::logger& file_transfer_logger_adapter::underlying_logger() const {
    if (!logger_) {
        throw std::runtime_error("Logger not initialized");
    }
    return *logger_;
}
#endif // FILE_TRANSFER_USE_LOGGER_SYSTEM

#endif // KCENON_WITH_COMMON_SYSTEM

// ============================================================================
// Global accessor
// ============================================================================

file_transfer_logger_adapter& get_logger_adapter() {
    static file_transfer_logger_adapter instance;
    static std::once_flag init_flag;

    std::call_once(init_flag, []() {
        instance.initialize();
    });

    return instance;
}

} // namespace kcenon::file_transfer::adapters
