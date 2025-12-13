// BSD 3-Clause License
//
// Copyright (c) 2025, kcenon
// All rights reserved.

#pragma once

#include <string>
#include <string_view>
#include <chrono>
#include <ctime>
#include <optional>
#include <memory>
#include <functional>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <mutex>
#include <iostream>
#include <regex>
#include <algorithm>

// logger_system integration requires common_system
#if defined(BUILD_WITH_LOGGER_SYSTEM) && defined(BUILD_WITH_COMMON_SYSTEM)
#define FILE_TRANSFER_USE_LOGGER_SYSTEM 1
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
 * @brief Configuration for sensitive information masking
 */
struct masking_config {
    bool mask_paths = false;
    bool mask_ips = false;
    bool mask_filenames = false;
    std::string mask_char = "*";
    size_t visible_chars = 4;

    /**
     * @brief Create config with all masking enabled
     */
    static masking_config all_masked() {
        return {true, true, true, "*", 4};
    }

    /**
     * @brief Create config with no masking
     */
    static masking_config none() {
        return {false, false, false, "*", 4};
    }
};

/**
 * @brief Utility class for masking sensitive information in log messages
 */
class sensitive_info_masker {
public:
    explicit sensitive_info_masker(masking_config config = masking_config::none())
        : config_(std::move(config)) {}

    /**
     * @brief Mask sensitive information in a string
     */
    [[nodiscard]] auto mask(const std::string& input) const -> std::string {
        if (!config_.mask_paths && !config_.mask_ips && !config_.mask_filenames) {
            return input;
        }

        std::string result = input;

        if (config_.mask_ips) {
            result = mask_ip_addresses(result);
        }

        if (config_.mask_paths) {
            result = mask_file_paths(result);
        }

        return result;
    }

    /**
     * @brief Mask a file path
     */
    [[nodiscard]] auto mask_path(const std::string& path) const -> std::string {
        if (!config_.mask_paths || path.empty()) {
            return path;
        }

        auto last_sep = path.find_last_of("/\\");
        if (last_sep == std::string::npos) {
            return mask_filename(path);
        }

        std::string masked_dir(last_sep, config_.mask_char[0]);
        std::string filename = path.substr(last_sep + 1);

        if (config_.mask_filenames) {
            filename = mask_filename(filename);
        }

        return masked_dir + "/" + filename;
    }

    /**
     * @brief Mask an IP address
     */
    [[nodiscard]] auto mask_ip(const std::string& ip) const -> std::string {
        if (!config_.mask_ips || ip.empty()) {
            return ip;
        }

        auto last_dot = ip.find_last_of('.');
        if (last_dot == std::string::npos) {
            return std::string(ip.size(), config_.mask_char[0]);
        }

        std::string masked_prefix(last_dot, config_.mask_char[0]);
        return masked_prefix + ip.substr(last_dot);
    }

    /**
     * @brief Get current masking configuration
     */
    [[nodiscard]] auto get_config() const -> const masking_config& {
        return config_;
    }

    /**
     * @brief Update masking configuration
     */
    void set_config(masking_config config) {
        config_ = std::move(config);
    }

private:
    [[nodiscard]] auto mask_filename(const std::string& filename) const -> std::string {
        if (filename.size() <= config_.visible_chars) {
            return filename;
        }

        auto dot_pos = filename.find_last_of('.');
        if (dot_pos != std::string::npos && dot_pos > 0) {
            std::string name = filename.substr(0, dot_pos);
            std::string ext = filename.substr(dot_pos);

            if (name.size() <= config_.visible_chars) {
                return filename;
            }

            std::string visible = name.substr(0, config_.visible_chars);
            std::string masked(name.size() - config_.visible_chars, config_.mask_char[0]);
            return visible + masked + ext;
        }

        std::string visible = filename.substr(0, config_.visible_chars);
        std::string masked(filename.size() - config_.visible_chars, config_.mask_char[0]);
        return visible + masked;
    }

    [[nodiscard]] auto mask_ip_addresses(const std::string& input) const -> std::string {
        static const std::regex ip_pattern(
            R"((\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3}))");

        std::string result;
        std::sregex_iterator it(input.begin(), input.end(), ip_pattern);
        std::sregex_iterator end;

        size_t last_pos = 0;
        for (; it != end; ++it) {
            result += input.substr(last_pos, it->position() - last_pos);
            result += mask_ip(it->str());
            last_pos = it->position() + it->length();
        }
        result += input.substr(last_pos);

        return result;
    }

    [[nodiscard]] auto mask_file_paths(const std::string& input) const -> std::string {
        static const std::regex path_pattern(
            R"((?:\/[a-zA-Z0-9._-]+)+|(?:[a-zA-Z]:\\(?:[a-zA-Z0-9._-]+\\?)+))");

        std::string result;
        std::sregex_iterator it(input.begin(), input.end(), path_pattern);
        std::sregex_iterator end;

        size_t last_pos = 0;
        for (; it != end; ++it) {
            result += input.substr(last_pos, it->position() - last_pos);
            result += mask_path(it->str());
            last_pos = it->position() + it->length();
        }
        result += input.substr(last_pos);

        return result;
    }

    masking_config config_;
};

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
    [[nodiscard]] auto to_json() const -> std::string {
        return to_json_with_masking(nullptr);
    }

    /**
     * @brief Convert context to JSON string with optional masking
     */
    [[nodiscard]] auto to_json_with_masking(const sensitive_info_masker* masker) const -> std::string {
        std::ostringstream oss;
        oss << "{";

        bool first = true;
        auto add_field = [&](const char* name, const std::string& value) {
            if (!first) oss << ",";
            std::string escaped = escape_json_string(value);
            oss << "\"" << name << "\":\"" << escaped << "\"";
            first = false;
        };
        auto add_uint = [&](const char* name, uint64_t value) {
            if (!first) oss << ",";
            oss << "\"" << name << "\":" << value;
            first = false;
        };
        auto add_double = [&](const char* name, double value) {
            if (!first) oss << ",";
            oss << std::fixed << std::setprecision(2);
            oss << "\"" << name << "\":" << value;
            first = false;
        };

        if (!transfer_id.empty()) add_field("transfer_id", transfer_id);
        if (!filename.empty()) {
            std::string fname = filename;
            if (masker && masker->get_config().mask_filenames) {
                fname = masker->mask_path(filename);
            }
            add_field("filename", fname);
        }
        if (file_size) add_uint("size", *file_size);
        if (bytes_transferred) add_uint("bytes_transferred", *bytes_transferred);
        if (chunk_index) add_uint("chunk_index", *chunk_index);
        if (total_chunks) add_uint("total_chunks", *total_chunks);
        if (progress_percent) add_double("progress_percent", *progress_percent);
        if (rate_mbps) add_double("rate_mbps", *rate_mbps);
        if (duration_ms) add_uint("duration_ms", *duration_ms);
        if (error_message) {
            std::string msg = *error_message;
            if (masker) {
                msg = masker->mask(msg);
            }
            add_field("error_message", msg);
        }
        if (client_id) add_field("client_id", *client_id);
        if (server_address) {
            std::string addr = *server_address;
            if (masker && masker->get_config().mask_ips) {
                addr = masker->mask_ip(addr);
            }
            add_field("server_address", addr);
        }

        oss << "}";
        return oss.str();
    }

private:
    [[nodiscard]] static auto escape_json_string(const std::string& input) -> std::string {
        std::string output;
        output.reserve(input.size() + 16);
        for (char c : input) {
            switch (c) {
                case '"':  output += "\\\""; break;
                case '\\': output += "\\\\"; break;
                case '\b': output += "\\b";  break;
                case '\f': output += "\\f";  break;
                case '\n': output += "\\n";  break;
                case '\r': output += "\\r";  break;
                case '\t': output += "\\t";  break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                        output += buf;
                    } else {
                        output += c;
                    }
            }
        }
        return output;
    }
};

/**
 * @brief Complete structured log entry with all metadata
 */
struct structured_log_entry {
    std::string timestamp;
    log_level level = log_level::info;
    std::string category;
    std::string message;
    std::optional<transfer_log_context> context;
    std::optional<std::string> source_file;
    std::optional<int> source_line;
    std::optional<std::string> function_name;

    /**
     * @brief Convert to complete JSON format
     */
    [[nodiscard]] auto to_json() const -> std::string {
        return to_json_with_masking(nullptr);
    }

    /**
     * @brief Convert to complete JSON format with optional masking
     */
    [[nodiscard]] auto to_json_with_masking(const sensitive_info_masker* masker) const -> std::string {
        std::ostringstream oss;
        oss << "{";

        oss << "\"timestamp\":\"" << timestamp << "\"";
        oss << ",\"level\":\"" << log_level_to_string(level) << "\"";
        oss << ",\"category\":\"" << category << "\"";

        std::string msg = message;
        if (masker) {
            msg = masker->mask(msg);
        }
        oss << ",\"message\":\"" << escape_json_string(msg) << "\"";

        if (context) {
            std::string ctx_json = context->to_json_with_masking(masker);
            if (ctx_json.size() > 2) {
                oss << "," << ctx_json.substr(1, ctx_json.size() - 2);
            }
        }

        if (source_file) {
            std::string file = *source_file;
            if (masker && masker->get_config().mask_paths) {
                file = masker->mask_path(file);
            }
            oss << ",\"source\":{";
            oss << "\"file\":\"" << file << "\"";
            if (source_line) {
                oss << ",\"line\":" << *source_line;
            }
            if (function_name) {
                oss << ",\"function\":\"" << *function_name << "\"";
            }
            oss << "}";
        }

        oss << "}";
        return oss.str();
    }

private:
    [[nodiscard]] static auto escape_json_string(const std::string& input) -> std::string {
        std::string output;
        output.reserve(input.size() + 16);
        for (char c : input) {
            switch (c) {
                case '"':  output += "\\\""; break;
                case '\\': output += "\\\\"; break;
                case '\b': output += "\\b";  break;
                case '\f': output += "\\f";  break;
                case '\n': output += "\\n";  break;
                case '\r': output += "\\r";  break;
                case '\t': output += "\\t";  break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                        output += buf;
                    } else {
                        output += c;
                    }
            }
        }
        return output;
    }
};

/**
 * @brief Builder class for creating structured log entries
 *
 * Example usage:
 * @code
 * auto entry = log_entry_builder()
 *     .with_level(log_level::info)
 *     .with_category(log_category::client)
 *     .with_message("Upload completed")
 *     .with_transfer_id("abc-123")
 *     .with_filename("data.zip")
 *     .with_file_size(1048576)
 *     .with_duration_ms(500)
 *     .with_rate_mbps(2.0)
 *     .build();
 *
 * std::string json = entry.to_json();
 * @endcode
 */
class log_entry_builder {
public:
    log_entry_builder() {
        entry_.timestamp = get_iso8601_timestamp();
    }

    /**
     * @brief Set the log level
     */
    auto with_level(log_level level) -> log_entry_builder& {
        entry_.level = level;
        return *this;
    }

    /**
     * @brief Set the log category
     */
    auto with_category(std::string_view category) -> log_entry_builder& {
        entry_.category = std::string(category);
        return *this;
    }

    /**
     * @brief Set the log message
     */
    auto with_message(std::string_view message) -> log_entry_builder& {
        entry_.message = std::string(message);
        return *this;
    }

    /**
     * @brief Set the transfer ID
     */
    auto with_transfer_id(std::string_view id) -> log_entry_builder& {
        ensure_context();
        entry_.context->transfer_id = std::string(id);
        return *this;
    }

    /**
     * @brief Set the filename
     */
    auto with_filename(std::string_view filename) -> log_entry_builder& {
        ensure_context();
        entry_.context->filename = std::string(filename);
        return *this;
    }

    /**
     * @brief Set the file size
     */
    auto with_file_size(uint64_t size) -> log_entry_builder& {
        ensure_context();
        entry_.context->file_size = size;
        return *this;
    }

    /**
     * @brief Set the bytes transferred
     */
    auto with_bytes_transferred(uint64_t bytes) -> log_entry_builder& {
        ensure_context();
        entry_.context->bytes_transferred = bytes;
        return *this;
    }

    /**
     * @brief Set the chunk index
     */
    auto with_chunk_index(uint32_t index) -> log_entry_builder& {
        ensure_context();
        entry_.context->chunk_index = index;
        return *this;
    }

    /**
     * @brief Set the total chunks
     */
    auto with_total_chunks(uint32_t total) -> log_entry_builder& {
        ensure_context();
        entry_.context->total_chunks = total;
        return *this;
    }

    /**
     * @brief Set the progress percentage
     */
    auto with_progress_percent(double percent) -> log_entry_builder& {
        ensure_context();
        entry_.context->progress_percent = percent;
        return *this;
    }

    /**
     * @brief Set the transfer rate in MB/s
     */
    auto with_rate_mbps(double rate) -> log_entry_builder& {
        ensure_context();
        entry_.context->rate_mbps = rate;
        return *this;
    }

    /**
     * @brief Set the duration in milliseconds
     */
    auto with_duration_ms(uint64_t duration) -> log_entry_builder& {
        ensure_context();
        entry_.context->duration_ms = duration;
        return *this;
    }

    /**
     * @brief Set the error message
     */
    auto with_error_message(std::string_view error) -> log_entry_builder& {
        ensure_context();
        entry_.context->error_message = std::string(error);
        return *this;
    }

    /**
     * @brief Set the client ID
     */
    auto with_client_id(std::string_view id) -> log_entry_builder& {
        ensure_context();
        entry_.context->client_id = std::string(id);
        return *this;
    }

    /**
     * @brief Set the server address
     */
    auto with_server_address(std::string_view address) -> log_entry_builder& {
        ensure_context();
        entry_.context->server_address = std::string(address);
        return *this;
    }

    /**
     * @brief Set the source file location
     */
    auto with_source_location(const char* file, int line, const char* function) -> log_entry_builder& {
        if (file) entry_.source_file = file;
        if (line > 0) entry_.source_line = line;
        if (function) entry_.function_name = function;
        return *this;
    }

    /**
     * @brief Set the context directly
     */
    auto with_context(const transfer_log_context& ctx) -> log_entry_builder& {
        entry_.context = ctx;
        return *this;
    }

    /**
     * @brief Build and return the log entry
     */
    [[nodiscard]] auto build() const -> structured_log_entry {
        return entry_;
    }

    /**
     * @brief Build and return JSON string
     */
    [[nodiscard]] auto build_json() const -> std::string {
        return entry_.to_json();
    }

    /**
     * @brief Build and return JSON string with masking
     */
    [[nodiscard]] auto build_json_masked(const sensitive_info_masker& masker) const -> std::string {
        return entry_.to_json_with_masking(&masker);
    }

private:
    void ensure_context() {
        if (!entry_.context) {
            entry_.context = transfer_log_context{};
        }
    }

    [[nodiscard]] static auto get_iso8601_timestamp() -> std::string {
        auto now = std::chrono::system_clock::now();
        auto time_t_val = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm tm_buf{};
#if defined(_WIN32)
        gmtime_s(&tm_buf, &time_t_val);
#else
        gmtime_r(&time_t_val, &tm_buf);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count()
            << 'Z';
        return oss.str();
    }

    structured_log_entry entry_;
};

// Forward declaration
class file_transfer_logger;

/**
 * @brief Global logger accessor
 */
file_transfer_logger& get_logger();

/**
 * @brief Output format for log messages
 */
enum class log_output_format {
    text,   ///< Traditional text format
    json    ///< JSON format for structured logging
};

/**
 * @brief File transfer logging interface
 */
class file_transfer_logger {
public:
    using log_callback = std::function<void(log_level, std::string_view, std::string_view, const transfer_log_context*)>;
    using json_log_callback = std::function<void(const structured_log_entry&, const std::string&)>;

    file_transfer_logger() = default;
    ~file_transfer_logger() = default;

    file_transfer_logger(const file_transfer_logger&) = delete;
    file_transfer_logger& operator=(const file_transfer_logger&) = delete;

    /**
     * @brief Initialize the logger
     *
     * Safe to call multiple times - subsequent calls are no-ops.
     * Automatically called when creating server or client instances.
     */
    void initialize() {
        // Prevent multiple initializations
        bool expected = false;
        if (!initialized_.compare_exchange_strong(expected, true)) {
            return;  // Already initialized
        }

#ifdef FILE_TRANSFER_USE_LOGGER_SYSTEM
        auto result = kcenon::logger::logger_builder()
            .with_async(true)
            .with_min_level(kcenon::logger::log_level::info)
            .add_writer("console", std::make_unique<kcenon::logger::console_writer>())
            .build();

        if (result) {
            logger_ = std::move(result.value());
        }
#endif
    }

    /**
     * @brief Shutdown the logger
     */
    void shutdown() {
#ifdef FILE_TRANSFER_USE_LOGGER_SYSTEM
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
    [[nodiscard]] auto is_initialized() const -> bool { return initialized_.load(); }

    /**
     * @brief Set minimum log level
     */
    void set_level(log_level level) {
        min_level_.store(level);
#ifdef FILE_TRANSFER_USE_LOGGER_SYSTEM
        if (logger_) {
            logger_->set_min_level(to_logger_level(level));
        }
#endif
    }

    /**
     * @brief Get current log level
     */
    [[nodiscard]] auto get_level() const -> log_level { return min_level_.load(); }

    /**
     * @brief Set output format (text or JSON)
     */
    void set_output_format(log_output_format format) {
        std::lock_guard<std::mutex> lock(config_mutex_);
        output_format_ = format;
    }

    /**
     * @brief Get current output format
     */
    [[nodiscard]] auto get_output_format() const -> log_output_format {
        std::lock_guard<std::mutex> lock(config_mutex_);
        return output_format_;
    }

    /**
     * @brief Enable JSON output format
     */
    void enable_json_output(bool enable = true) {
        set_output_format(enable ? log_output_format::json : log_output_format::text);
    }

    /**
     * @brief Check if JSON output is enabled
     */
    [[nodiscard]] auto is_json_output_enabled() const -> bool {
        return get_output_format() == log_output_format::json;
    }

    /**
     * @brief Set masking configuration for sensitive information
     */
    void set_masking_config(masking_config config) {
        std::lock_guard<std::mutex> lock(config_mutex_);
        masker_.set_config(std::move(config));
    }

    /**
     * @brief Get current masking configuration
     */
    [[nodiscard]] auto get_masking_config() const -> masking_config {
        std::lock_guard<std::mutex> lock(config_mutex_);
        return masker_.get_config();
    }

    /**
     * @brief Enable masking for all sensitive information
     */
    void enable_masking(bool enable = true) {
        set_masking_config(enable ? masking_config::all_masked() : masking_config::none());
    }

    /**
     * @brief Set custom log callback
     */
    void set_callback(log_callback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback_ = std::move(callback);
    }

    /**
     * @brief Set JSON log callback
     */
    void set_json_callback(json_log_callback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        json_callback_ = std::move(callback);
    }

    /**
     * @brief Check if logging is enabled for level
     */
    [[nodiscard]] auto is_enabled(log_level level) const -> bool {
        return static_cast<int>(level) >= static_cast<int>(min_level_.load());
    }

    /**
     * @brief Log a message
     */
    void log(log_level level,
             std::string_view category,
             std::string_view message,
             const transfer_log_context* context = nullptr,
             [[maybe_unused]] const char* file = nullptr,
             [[maybe_unused]] int line = 0,
             [[maybe_unused]] const char* function = nullptr) {

        if (!is_enabled(level)) return;

        // Call custom callback if set
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (callback_) {
                callback_(level, category, message, context);
            }
        }

        // Get current format and masker
        log_output_format format;
        sensitive_info_masker current_masker;
        {
            std::lock_guard<std::mutex> lock(config_mutex_);
            format = output_format_;
            current_masker = masker_;
        }

        if (format == log_output_format::json) {
            log_json(level, category, message, context, file, line, function, current_masker);
        } else {
            log_text(level, category, message, context, file, line, function, current_masker);
        }
    }

    /**
     * @brief Log a structured entry directly
     */
    void log(const structured_log_entry& entry) {
        if (!is_enabled(entry.level)) return;

        // Get current masker
        sensitive_info_masker current_masker;
        {
            std::lock_guard<std::mutex> lock(config_mutex_);
            current_masker = masker_;
        }

        std::string json_str = entry.to_json_with_masking(&current_masker);

        // Call JSON callback if set
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (json_callback_) {
                json_callback_(entry, json_str);
            }
        }

#ifdef FILE_TRANSFER_USE_LOGGER_SYSTEM
        if (logger_) {
            if (entry.source_file && entry.source_line && entry.function_name) {
                logger_->log(to_logger_level(entry.level), json_str,
                           entry.source_file->c_str(), *entry.source_line,
                           entry.function_name->c_str());
            } else {
                logger_->log(to_logger_level(entry.level), json_str);
            }
        }
#else
        output_to_stderr(json_str);
#endif
    }

    /**
     * @brief Flush pending logs
     */
    void flush() {
#ifdef FILE_TRANSFER_USE_LOGGER_SYSTEM
        if (logger_) {
            logger_->flush();
        }
#endif
    }

private:
    void log_json(log_level level,
                  std::string_view category,
                  std::string_view message,
                  const transfer_log_context* context,
                  const char* file,
                  int line,
                  const char* function,
                  const sensitive_info_masker& masker) {

        auto builder = log_entry_builder()
            .with_level(level)
            .with_category(category)
            .with_message(message);

        if (file || line > 0 || function) {
            builder.with_source_location(file, line, function);
        }

        if (context) {
            builder.with_context(*context);
        }

        auto entry = builder.build();
        std::string json_str = entry.to_json_with_masking(&masker);

        // Call JSON callback if set
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (json_callback_) {
                json_callback_(entry, json_str);
            }
        }

#ifdef FILE_TRANSFER_USE_LOGGER_SYSTEM
        if (logger_) {
            if (file && line > 0 && function) {
                logger_->log(to_logger_level(level), json_str, file, line, function);
            } else {
                logger_->log(to_logger_level(level), json_str);
            }
        }
#else
        output_to_stderr(json_str);
#endif
    }

    void log_text(log_level level,
                  std::string_view category,
                  std::string_view message,
                  const transfer_log_context* context,
                  [[maybe_unused]] const char* file,
                  [[maybe_unused]] int line,
                  [[maybe_unused]] const char* function,
                  const sensitive_info_masker& masker) {

#ifdef FILE_TRANSFER_USE_LOGGER_SYSTEM
        if (logger_) {
            std::string full_message = format_message(category, message, context, &masker);
            if (file && line > 0 && function) {
                logger_->log(to_logger_level(level), full_message, file, line, function);
            } else {
                logger_->log(to_logger_level(level), full_message);
            }
        }
#else
        std::string timestamp = get_timestamp();
        std::string level_str(log_level_to_string(level));
        std::string cat_str(category);
        std::string msg_str = masker.mask(std::string(message));

        std::ostringstream oss;
        oss << timestamp << " [" << level_str << "] [" << cat_str << "] " << msg_str;
        if (context) {
            oss << " " << context->to_json_with_masking(&masker);
        }
        oss << "\n";

        output_to_stderr(oss.str());
#endif
    }

    static void output_to_stderr(const std::string& msg) {
        static std::mutex stderr_mutex;
        std::lock_guard<std::mutex> lock(stderr_mutex);
        std::cerr << msg << "\n";
    }

#ifdef FILE_TRANSFER_USE_LOGGER_SYSTEM
    static auto to_logger_level(log_level level) -> kcenon::logger::log_level {
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

    static auto format_message(std::string_view category,
                               std::string_view message,
                               const transfer_log_context* context,
                               const sensitive_info_masker* masker = nullptr) -> std::string {
        std::ostringstream oss;
        oss << "[" << category << "] ";

        if (masker) {
            oss << masker->mask(std::string(message));
        } else {
            oss << message;
        }

        if (context) {
            oss << " " << context->to_json_with_masking(masker);
        }
        return oss.str();
    }

    std::unique_ptr<kcenon::logger::logger> logger_;
#endif

    static auto get_timestamp() -> std::string {
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
            << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

    std::atomic<log_level> min_level_{log_level::info};
    std::atomic<bool> initialized_{false};
    log_callback callback_;
    json_log_callback json_callback_;
    std::mutex callback_mutex_;

    log_output_format output_format_{log_output_format::text};
    sensitive_info_masker masker_;
    mutable std::mutex config_mutex_;
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
