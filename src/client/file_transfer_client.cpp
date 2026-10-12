/**
 * @file file_transfer_client.cpp
 * @brief File transfer client implementation
 */

#include "kcenon/file_transfer/client/file_transfer_client.h"

#include <kcenon/file_transfer/core/bandwidth_limiter.h>
#include <kcenon/file_transfer/core/checksum.h>
#include <kcenon/file_transfer/core/chunk_assembler.h>
#include <kcenon/file_transfer/core/protocol_types.h>
#include <kcenon/file_transfer/core/protocol_codec.h>
#include <kcenon/file_transfer/core/chunk_config.h>
#include <kcenon/file_transfer/core/compression_engine.h>
#include <kcenon/file_transfer/core/logging.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <thread>
#include <deque>
#include <future>

#ifdef FILE_TRANS_ENABLE_LZ4
#include <lz4.h>
#endif

#include <kcenon/network/core/messaging_client.h>

// Windows compatibility: undefine macros from Windows headers that conflict
// with std::ofstream::write() method
#ifdef _WIN32
#ifdef write
#undef write
#endif
#ifdef read
#undef read
#endif
#endif

namespace kcenon::file_transfer {

/**
 * @brief Internal transfer state enum
 */
enum class internal_transfer_state {
    idle,
    initializing,
    transferring,
    paused,
    verifying,
    completing,
    completed,
    failed,
    cancelled
};

/**
 * @brief Convert internal state to transfer_status
 */
[[nodiscard]] auto to_transfer_status(internal_transfer_state state) noexcept
    -> transfer_status {
    switch (state) {
        case internal_transfer_state::idle:
        case internal_transfer_state::initializing:
            return transfer_status::pending;
        case internal_transfer_state::transferring:
            return transfer_status::in_progress;
        case internal_transfer_state::paused:
            return transfer_status::paused;
        case internal_transfer_state::verifying:
        case internal_transfer_state::completing:
            return transfer_status::completing;
        case internal_transfer_state::completed:
            return transfer_status::completed;
        case internal_transfer_state::failed:
            return transfer_status::failed;
        case internal_transfer_state::cancelled:
            return transfer_status::cancelled;
        default:
            return transfer_status::failed;
    }
}

/**
 * @brief Check if state transition is valid
 */
[[nodiscard]] auto is_valid_transition(
    internal_transfer_state from,
    internal_transfer_state to) noexcept -> bool {
    // Terminal states cannot transition
    if (from == internal_transfer_state::completed ||
        from == internal_transfer_state::failed ||
        from == internal_transfer_state::cancelled) {
        return false;
    }

    // Any non-terminal state can transition to cancelled
    if (to == internal_transfer_state::cancelled) {
        return true;
    }

    // Specific transitions
    switch (from) {
        case internal_transfer_state::idle:
            return to == internal_transfer_state::initializing;
        case internal_transfer_state::initializing:
            return to == internal_transfer_state::transferring ||
                   to == internal_transfer_state::failed;
        case internal_transfer_state::transferring:
            return to == internal_transfer_state::paused ||
                   to == internal_transfer_state::verifying ||
                   to == internal_transfer_state::completing ||
                   to == internal_transfer_state::failed;
        case internal_transfer_state::paused:
            return to == internal_transfer_state::transferring ||
                   to == internal_transfer_state::failed;
        case internal_transfer_state::verifying:
            return to == internal_transfer_state::completing ||
                   to == internal_transfer_state::failed;
        case internal_transfer_state::completing:
            return to == internal_transfer_state::completed ||
                   to == internal_transfer_state::failed;
        default:
            return false;
    }
}

// Legacy alias for download_state (for backward compatibility)
using download_state = internal_transfer_state;

/**
 * @brief Base transfer context for common state
 */
struct transfer_context_base {
    transfer_id tid;
    internal_transfer_state state{internal_transfer_state::idle};

    // Progress tracking
    uint64_t file_size{0};
    uint64_t total_chunks{0};
    uint64_t transferred_chunks{0};
    uint64_t transferred_bytes{0};
    std::chrono::steady_clock::time_point start_time;

    // Synchronization
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::string error_message;

    [[nodiscard]] auto get_progress_info() const -> transfer_progress_info {
        transfer_progress_info info;
        info.bytes_transferred = transferred_bytes;
        info.total_bytes = file_size;
        info.chunks_transferred = transferred_chunks;
        info.total_chunks = total_chunks;

        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start_time);
        info.elapsed = elapsed_ms;

        if (elapsed_ms.count() > 0) {
            info.transfer_rate = static_cast<double>(transferred_bytes) /
                                 (static_cast<double>(elapsed_ms.count()) / 1000.0);
        }

        return info;
    }

    [[nodiscard]] auto get_result_info() const -> transfer_result_info {
        transfer_result_info info;
        info.success = (state == internal_transfer_state::completed);
        info.bytes_transferred = transferred_bytes;

        auto now = std::chrono::steady_clock::now();
        info.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start_time);

        if (!error_message.empty()) {
            info.error_message = error_message;
        }

        return info;
    }
};

/**
 * @brief Upload context for tracking upload state
 */
struct upload_context : transfer_context_base {
    std::filesystem::path local_path;
    std::string remote_name;
    upload_options options;
    uint32_t chunk_size{0};
    std::string file_hash;
};

/**
 * @brief Download context for tracking download state
 */
struct download_context : transfer_context_base {
    std::string remote_name;
    std::filesystem::path local_path;
    std::filesystem::path temp_path;
    download_options options;

    // Metadata from server
    uint32_t chunk_size{0};
    std::string expected_sha256;

    // Legacy compatibility (keeping for existing code)
    uint64_t& received_chunks{transferred_chunks};
    uint64_t& bytes_received{transferred_bytes};
    bool cancelled{false};
};

/**
 * @brief Batch transfer direction
 */
enum class batch_direction {
    upload,
    download
};

/**
 * @brief Batch transfer context
 */
struct batch_context {
    batch_direction direction;
    batch_options options;
    std::chrono::steady_clock::time_point start_time;

    // Transfer tracking
    std::vector<uint64_t> transfer_ids;      // Individual transfer handle IDs
    std::vector<std::string> filenames;      // Filenames for result reporting
    std::atomic<std::size_t> completed_count{0};
    std::atomic<std::size_t> failed_count{0};
    std::atomic<std::size_t> in_progress_count{0};
    std::atomic<uint64_t> total_bytes{0};
    std::atomic<uint64_t> transferred_bytes{0};

    // Synchronization
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> paused{false};

    // Results
    std::vector<batch_file_result> file_results;

    [[nodiscard]] auto get_progress() const -> batch_progress {
        batch_progress progress;
        progress.total_files = transfer_ids.size();
        progress.completed_files = completed_count.load();
        progress.failed_files = failed_count.load();
        progress.in_progress_files = in_progress_count.load();
        progress.total_bytes = total_bytes.load();
        progress.transferred_bytes = transferred_bytes.load();

        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start_time);
        if (elapsed_ms.count() > 0) {
            progress.overall_rate = static_cast<double>(progress.transferred_bytes) /
                                   (static_cast<double>(elapsed_ms.count()) / 1000.0);
        }

        return progress;
    }

    [[nodiscard]] auto get_result() const -> batch_result {
        std::lock_guard lock(mutex);
        batch_result result;
        result.total_files = transfer_ids.size();
        result.succeeded = completed_count.load();
        result.failed = failed_count.load();
        result.total_bytes = transferred_bytes.load();

        auto now = std::chrono::steady_clock::now();
        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start_time);

        result.file_results = file_results;
        return result;
    }

    [[nodiscard]] auto is_complete() const -> bool {
        return (completed_count.load() + failed_count.load()) >= transfer_ids.size();
    }
};

/**
 * @brief LZ4 decompression helper function
 */
[[nodiscard]] inline auto decompress_lz4(
    std::span<const std::byte> input,
    std::size_t original_size) -> result<std::vector<std::byte>> {
#ifdef FILE_TRANS_ENABLE_LZ4
    if (input.empty()) {
        return std::vector<std::byte>{};
    }

    std::vector<std::byte> output(original_size);

    const int decompressed_size = LZ4_decompress_safe(
        reinterpret_cast<const char*>(input.data()),
        reinterpret_cast<char*>(output.data()),
        static_cast<int>(input.size()),
        static_cast<int>(original_size));

    if (decompressed_size < 0) {
        return unexpected{error{error_code::internal_error,
                               "LZ4 decompression failed: corrupted data"}};
    }

    if (static_cast<std::size_t>(decompressed_size) != original_size) {
        return unexpected{error{error_code::internal_error,
                               "LZ4 decompression size mismatch"}};
    }

    return output;
#else
    (void)input;
    (void)original_size;
    return unexpected{error{error_code::internal_error,
                           "LZ4 compression not enabled"}};
#endif
}

struct file_transfer_client::impl {
    client_config config;
    std::atomic<connection_state> current_state{connection_state::disconnected};
    endpoint server_endpoint;

    std::shared_ptr<kcenon::network::core::messaging_client> network_client;

    // Callbacks
    std::function<void(const transfer_progress&)> progress_callback;
    std::function<void(const transfer_result&)> complete_callback;
    std::function<void(connection_state)> state_callback;
    mutable std::mutex callbacks_mutex;

    auto completion_callback() const -> std::function<void(const transfer_result&)> {
        std::lock_guard lock(callbacks_mutex);
        return complete_callback;
    }
    auto progress_observer() const -> std::function<void(const transfer_progress&)> {
        std::lock_guard lock(callbacks_mutex);
        return progress_callback;
    }

    // Statistics
    mutable std::mutex stats_mutex;
    client_statistics statistics;
    compression_statistics compression_stats;

    // Active transfers
    mutable std::mutex transfers_mutex;
    std::atomic<uint64_t> next_transfer_id{1};

    // Upload contexts
    mutable std::mutex upload_mutex;
    std::unordered_map<uint64_t, std::shared_ptr<upload_context>> upload_contexts;

    // Download contexts
    mutable std::mutex download_mutex;
    std::unordered_map<uint64_t, std::shared_ptr<download_context>> download_contexts;

    // Batch contexts
    mutable std::mutex batch_mutex;
    std::atomic<uint64_t> next_batch_id{1};
    std::unordered_map<uint64_t, std::unique_ptr<batch_context>> batch_contexts;

    // Bandwidth limiters
    std::unique_ptr<bandwidth_limiter> upload_limiter;
    std::unique_ptr<bandwidth_limiter> download_limiter;

    struct channel {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<wire::frame> frames;
        std::optional<error> failure;
    };
    std::mutex channels_mutex;
    std::unordered_map<transfer_id, std::shared_ptr<channel>> channels;
    std::mutex jobs_mutex;
    std::vector<std::future<void>> jobs;
    std::atomic<bool> closing{false};
    std::mutex connection_mutex;
    std::condition_variable connection_cv;
    bool transport_connected{false};
    std::string connection_error;
    wire::stream_decoder decoder;

    ~impl() {
        closing = true;
        fail_channels(error{error_code::connection_lost, "Client shut down"});
        if (network_client) (void)network_client->stop_client();
        for (auto& job : jobs) if (job.valid()) job.wait();
    }
    auto open_channel(const transfer_id& id) -> std::shared_ptr<channel> {
        auto response = std::make_shared<channel>();
        std::lock_guard lock(channels_mutex); channels[id] = response; return response;
    }
    void close_channel(const transfer_id& id) {
        std::lock_guard lock(channels_mutex); channels.erase(id);
    }
    void fail_channels(error err) {
        std::lock_guard lock(channels_mutex);
        for (auto& [id, response] : channels) {
            std::lock_guard response_lock(response->mutex);
            response->failure = err; response->cv.notify_all();
        }
    }
    void fail_channel(const transfer_id& id, error err) {
        std::shared_ptr<channel> response;
        { std::lock_guard lock(channels_mutex); auto it = channels.find(id); if (it != channels.end()) response = it->second; }
        if (response) {
            std::lock_guard lock(response->mutex); response->failure = std::move(err); response->cv.notify_all();
        }
    }
    auto receive(const std::shared_ptr<channel>& response) -> result<wire::frame> {
        std::unique_lock lock(response->mutex);
        if (!response->cv.wait_for(lock, config.transfer_timeout,
                [&] { return closing || response->failure || !response->frames.empty(); }))
            return unexpected{error{error_code::transfer_timeout, "Protocol response deadline exceeded"}};
        if (response->failure) return unexpected{*response->failure};
        if (closing) return unexpected{error{error_code::connection_lost, "Client shut down"}};
        auto frame = std::move(response->frames.front()); response->frames.pop_front();
        return frame;
    }
    template<class T> auto send(message_type type, const T& message) -> result<void> {
        auto bytes = wire::encode(type, message);
        if (!bytes) return unexpected{bytes.error()};
        auto sent = network_client->send_packet(std::move(bytes.value()));
        if (sent.is_err()) return unexpected{error{error_code::connection_lost, sent.error().message}};
        return {};
    }
    void receive_bytes(const std::vector<uint8_t>& bytes) {
        auto frames = decoder.feed(bytes);
        if (!frames) { fail_channels(frames.error()); return; }
        for (const auto& bytes : frames.value()) {
            auto decoded = wire::decode_frame(bytes);
            if (!decoded) { fail_channels(decoded.error()); continue; }
            auto& frame = decoded.value();
            transfer_id id;
            if (frame.type == message_type::error) {
                auto err = wire::decode<msg_error>(frame.payload);
                if (!err) { fail_channels(err.error()); continue; }
                id.bytes = err.value().related_id;
                error failure{static_cast<error_code>(err.value().error_code), err.value().message};
                if (id.is_null()) fail_channels(failure); else fail_channel(id, failure);
                continue;
            }
            if (frame.payload.size() < id.bytes.size()) { fail_channels(error{error_code::invalid_configuration, "Uncorrelated response"}); continue; }
            std::copy_n(frame.payload.begin(), id.bytes.size(), id.bytes.begin());
            std::shared_ptr<channel> response;
            { std::lock_guard lock(channels_mutex); auto it = channels.find(id); if (it != channels.end()) response = it->second; }
            if (!response) continue; // A late response to a terminated transfer.
            std::lock_guard lock(response->mutex);
            if (response->frames.size() >= 4) response->failure = error{error_code::invalid_configuration, "Peer exceeded response window"};
            else response->frames.push_back(std::move(frame));
            response->cv.notify_all();
        }
    }
    void launch(std::function<void()> task) {
        std::lock_guard lock(jobs_mutex);
        std::erase_if(jobs, [](auto& job) { return job.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
        jobs.push_back(std::async(std::launch::async, std::move(task)));
    }
    auto wait_active(const std::shared_ptr<transfer_context_base>& ctx) -> bool {
        std::unique_lock lock(ctx->mutex);
        while (ctx->state == internal_transfer_state::paused && !closing && current_state == connection_state::connected)
            ctx->cv.wait_for(lock, std::chrono::milliseconds(20));
        return !closing && current_state == connection_state::connected && !is_terminal_status(to_transfer_status(ctx->state));
    }
    auto throttle(const std::shared_ptr<transfer_context_base>& ctx, bandwidth_limiter* limiter, std::size_t bytes) -> bool {
        if (!limiter || !limiter->is_enabled()) return wait_active(ctx);
        while (bytes) {
            if (!wait_active(ctx)) return false;
            auto step = std::min(bytes, std::max<std::size_t>(1, limiter->bucket_capacity()));
            if (limiter->try_acquire(step)) bytes -= step;
            else { std::unique_lock lock(ctx->mutex); ctx->cv.wait_for(lock, std::chrono::milliseconds(5)); }
        }
        return true;
    }
    void finish_upload(const std::shared_ptr<upload_context>& ctx, bool success, std::string message = {}) {
        transfer_result outcome;
        {
            std::lock_guard lock(ctx->mutex);
            if (is_terminal_status(to_transfer_status(ctx->state))) return;
            if (success) {
                std::lock_guard stats_lock(stats_mutex);
                ++statistics.total_files_uploaded; statistics.total_bytes_uploaded += ctx->transferred_bytes;
            }
            ctx->state = success ? internal_transfer_state::completed : internal_transfer_state::failed;
            ctx->error_message = message;
            outcome = {success, ctx->remote_name, ctx->transferred_bytes, message};
            ctx->cv.notify_all();
        }
        if (auto callback = completion_callback()) { try { callback(outcome); } catch (...) {} }
    }

    explicit impl(client_config cfg) : config(std::move(cfg)) {
        // Initialize bandwidth limiters if limits are configured
        if (config.upload_bandwidth_limit.has_value()) {
            upload_limiter = std::make_unique<bandwidth_limiter>(
                config.upload_bandwidth_limit.value());
        }
        if (config.download_bandwidth_limit.has_value()) {
            download_limiter = std::make_unique<bandwidth_limiter>(
                config.download_bandwidth_limit.value());
        }
    }

    void set_state(connection_state new_state) {
        auto old_state = current_state.exchange(new_state);
        if (old_state != new_state) {
            // Log state transitions
            if (new_state == connection_state::reconnecting) {
                FT_LOG_INFO(log_category::client, "Attempting to reconnect to server");
            } else if (new_state == connection_state::connected &&
                       old_state == connection_state::reconnecting) {
                FT_LOG_INFO(log_category::client, "Reconnection successful");
            } else if (new_state == connection_state::disconnected &&
                       old_state == connection_state::reconnecting) {
                FT_LOG_WARN(log_category::client, "Reconnection failed");
            }

            std::function<void(connection_state)> callback;
            { std::lock_guard lock(callbacks_mutex); callback = state_callback; }
            if (callback) { try { callback(new_state); } catch (...) {} }
        }
    }
};

// Builder implementation
file_transfer_client::builder::builder() = default;

auto file_transfer_client::builder::with_transfer_timeout(std::chrono::milliseconds timeout) -> builder& {
    config_.transfer_timeout = timeout;
    return *this;
}

auto file_transfer_client::builder::with_compression(compression_mode mode) -> builder& {
    config_.compression = mode;
    return *this;
}

auto file_transfer_client::builder::with_compression_level(compression_level level) -> builder& {
    config_.comp_level = level;
    return *this;
}

auto file_transfer_client::builder::with_chunk_size(std::size_t size) -> builder& {
    config_.chunk_size = size;
    return *this;
}

auto file_transfer_client::builder::with_auto_reconnect(
    bool enable, reconnect_policy policy) -> builder& {
    config_.auto_reconnect = enable;
    config_.reconnect = policy;
    return *this;
}

auto file_transfer_client::builder::with_upload_bandwidth_limit(
    std::size_t bytes_per_second) -> builder& {
    config_.upload_bandwidth_limit = bytes_per_second;
    return *this;
}

auto file_transfer_client::builder::with_download_bandwidth_limit(
    std::size_t bytes_per_second) -> builder& {
    config_.download_bandwidth_limit = bytes_per_second;
    return *this;
}

auto file_transfer_client::builder::with_connect_timeout(
    std::chrono::milliseconds timeout) -> builder& {
    config_.connect_timeout = timeout;
    return *this;
}

auto file_transfer_client::builder::with_progress_interval(
    std::chrono::milliseconds interval) -> builder& {
    config_.progress.callback_interval = interval;
    return *this;
}

auto file_transfer_client::builder::with_progress_config(
    const progress_config& config) -> builder& {
    config_.progress = config;
    return *this;
}

auto file_transfer_client::builder::with_encryption(bool enable) -> builder& {
    config_.enable_encryption = enable;
    return *this;
}

auto file_transfer_client::builder::with_encryption_password(
    const std::string& password) -> builder& {
    config_.encryption_password = password;
    config_.enable_encryption = true;
    return *this;
}

auto file_transfer_client::builder::with_encryption_key(
    std::span<const std::byte> key) -> builder& {
    config_.encryption_key.assign(key.begin(), key.end());
    config_.enable_encryption = true;
    return *this;
}

auto file_transfer_client::builder::build() -> result<file_transfer_client> {
    if (config_.connect_timeout <= std::chrono::milliseconds::zero() ||
        config_.transfer_timeout <= std::chrono::milliseconds::zero())
        return unexpected{error{error_code::invalid_configuration, "Connection and transfer deadlines must be positive"}};
    if (config_.chunk_size < 64 * 1024 || config_.chunk_size > 1024 * 1024) {
        return unexpected{error{error_code::invalid_chunk_size,
                               "Chunk size must be between 64KB and 1MB"}};
    }

    return file_transfer_client{std::move(config_)};
}

// file_transfer_client implementation
file_transfer_client::file_transfer_client(client_config config)
    : impl_(std::make_unique<impl>(std::move(config))) {
    // Initialize logger (safe to call multiple times)
    get_logger().initialize();

    impl_->network_client = std::make_shared<kcenon::network::core::messaging_client>(
        "file_transfer_client");
    auto* state = impl_.get();
    impl_->network_client->set_receive_callback([state](const auto& bytes) { state->receive_bytes(bytes); });
    impl_->network_client->set_connected_callback([state] {
        std::lock_guard lock(state->connection_mutex);
        state->transport_connected = true; state->connection_cv.notify_all();
    });
    impl_->network_client->set_disconnected_callback([state] {
        {
            std::lock_guard lock(state->connection_mutex);
            state->transport_connected = false; state->connection_error = "Connection lost";
            state->connection_cv.notify_all();
        }
        state->fail_channels(error{error_code::connection_lost, "Connection lost"});
        state->set_state(connection_state::disconnected);
    });
    impl_->network_client->set_error_callback([state](std::error_code error) {
        std::lock_guard lock(state->connection_mutex);
        state->connection_error = error.message(); state->connection_cv.notify_all();
    });
}

file_transfer_client::file_transfer_client(file_transfer_client&&) noexcept = default;
auto file_transfer_client::operator=(file_transfer_client&&) noexcept
    -> file_transfer_client& = default;
file_transfer_client::~file_transfer_client() {
    if (impl_ && is_connected()) {
        (void)disconnect();
    }
}

auto file_transfer_client::connect(const endpoint& server_addr) -> result<void> {
    if (impl_->current_state == connection_state::connected ||
        impl_->current_state == connection_state::connecting) {
        FT_LOG_WARN(log_category::client, "Client is already connected or connecting");
        return unexpected{error{error_code::already_initialized,
                               "Client is already connected or connecting"}};
    }

    FT_LOG_INFO(log_category::client,
        "Connecting to server " + server_addr.host + ":" + std::to_string(server_addr.port));

    impl_->set_state(connection_state::connecting);
    impl_->server_endpoint = server_addr;
    {
        std::lock_guard lock(impl_->connection_mutex);
        impl_->transport_connected = false; impl_->connection_error.clear();
        impl_->decoder = wire::stream_decoder{};
    }

    auto result = impl_->network_client->start_client(server_addr.host, server_addr.port);
    if (result.is_err()) {
        impl_->set_state(connection_state::disconnected);
        FT_LOG_ERROR(log_category::client,
            "Failed to connect: " + result.error().message);
        return unexpected{error{error_code::internal_error,
                               "Failed to connect: " + result.error().message}};
    }

    std::unique_lock lock(impl_->connection_mutex);
    bool ready = impl_->connection_cv.wait_for(lock, impl_->config.connect_timeout, [&] {
        return impl_->transport_connected || !impl_->connection_error.empty();
    });
    if (!ready || !impl_->transport_connected) {
        auto message = ready ? impl_->connection_error : "Connection deadline exceeded";
        lock.unlock();
        (void)impl_->network_client->stop_client();
        impl_->set_state(connection_state::disconnected);
        return unexpected{error{ready ? error_code::connection_failed : error_code::connection_timeout, message}};
    }
    lock.unlock();
    impl_->set_state(connection_state::connected);
    FT_LOG_INFO(log_category::client, "Connected to server successfully");
    return {};
}

auto file_transfer_client::disconnect() -> result<void> {
    if (impl_->current_state != connection_state::connected) {
        FT_LOG_WARN(log_category::client, "Disconnect called but client is not connected");
        return unexpected{error{error_code::not_initialized,
                               "Client is not connected"}};
    }

    FT_LOG_INFO(log_category::client, "Disconnecting from server");
    impl_->fail_channels(error{error_code::connection_lost, "Client disconnected"});

    auto result = impl_->network_client->stop_client();
    if (result.is_err()) {
        FT_LOG_ERROR(log_category::client,
            "Failed to disconnect: " + result.error().message);
        return unexpected{error{error_code::internal_error,
                               "Failed to disconnect: " + result.error().message}};
    }

    impl_->set_state(connection_state::disconnected);
    FT_LOG_INFO(log_category::client, "Disconnected from server");
    return {};
}

auto file_transfer_client::is_connected() const -> bool {
    return impl_->current_state == connection_state::connected;
}

auto file_transfer_client::state() const -> connection_state {
    return impl_->current_state;
}

auto file_transfer_client::upload_file(
    const std::filesystem::path& local_path, const std::string& remote_name,
    const upload_options& options) -> result<transfer_handle> {
    if (!is_connected()) return unexpected{error{error_code::not_initialized, "Client is not connected"}};
    if (options.encrypt.value_or(impl_->config.enable_encryption))
        return unexpected{error{error_code::invalid_configuration, "Transfer encryption has not been configured"}};
    std::error_code ec;
    if (!std::filesystem::is_regular_file(local_path, ec))
        return unexpected{error{error_code::file_not_found, "Local file not found: " + local_path.string()}};
    auto size = std::filesystem::file_size(local_path, ec);
    if (ec) return unexpected{error{error_code::file_read_error, ec.message()}};
    auto handle_id = impl_->next_transfer_id++;
    auto ctx = std::make_shared<upload_context>();
    ctx->tid = transfer_id::generate(); ctx->local_path = local_path;
    ctx->remote_name = remote_name.empty() ? local_path.filename().string() : remote_name;
    ctx->options = options; ctx->state = internal_transfer_state::initializing;
    ctx->file_size = size; ctx->start_time = std::chrono::steady_clock::now();
    { std::lock_guard lock(impl_->upload_mutex); impl_->upload_contexts[handle_id] = ctx; }
    auto response = impl_->open_channel(ctx->tid);
    auto* state = impl_.get();
    state->launch([state, ctx, response] {
        auto send = [&](message_type type, const auto& value) {
            auto sent = state->send(type, value);
            if (!sent) throw std::runtime_error(sent.error().message);
        };
        auto receive = [&]<class T>(message_type expected) -> T {
            auto frame = state->receive(response);
            if (!frame) throw std::runtime_error(frame.error().message);
            if (frame.value().type == message_type::upload_reject) {
                auto reject = wire::decode<msg_upload_reject>(frame.value().payload);
                throw std::runtime_error(reject ? reject.value().message : reject.error().message);
            }
            if (frame.value().type != expected) throw std::runtime_error("Unexpected upload response");
            auto value = wire::decode<T>(frame.value().payload);
            if (!value) throw std::runtime_error(value.error().message);
            return std::move(value.value());
        };
        try {
            auto hash = checksum::sha256_file(ctx->local_path);
            if (!hash) throw std::runtime_error(hash.error().message);
            msg_upload_request request{};
            request.transfer_id = ctx->tid.bytes; request.filename = ctx->remote_name; request.file_size = ctx->file_size;
            for (std::size_t i = 0; i < 32; ++i)
                request.sha256_hash[i] = static_cast<uint8_t>(std::stoul(hash.value().substr(i * 2, 2), nullptr, 16));
            const auto compression = ctx->options.compression.value_or(state->config.compression);
            request.compression = compression == compression_mode::none ? wire_compression_mode::none :
                (compression == compression_mode::always ? wire_compression_mode::lz4 : wire_compression_mode::adaptive);
            request.options = transfer_options::verify_checksum;
            if (ctx->options.overwrite) request.options = request.options | transfer_options::overwrite_existing;
            if (!state->wait_active(ctx)) throw std::runtime_error("Upload interrupted");
            send(message_type::upload_request, request);
            auto accepted = receive.template operator()<msg_upload_accept>(message_type::upload_accept);
            if (!chunk_config{accepted.chunk_size}.validate() || accepted.resume_offset ||
                accepted.encryption != wire_encryption_algorithm::none || accepted.compression != request.compression)
                throw std::runtime_error("Invalid upload negotiation response");
            {
                std::lock_guard lock(ctx->mutex);
                if (is_terminal_status(to_transfer_status(ctx->state))) throw std::runtime_error("Upload interrupted");
                ctx->chunk_size = accepted.chunk_size;
                ctx->total_chunks = ctx->file_size / accepted.chunk_size + (ctx->file_size % accepted.chunk_size != 0);
                ctx->file_hash = hash.value(); ctx->state = internal_transfer_state::transferring;
            }
            std::ifstream file(ctx->local_path, std::ios::binary);
            if (!file) throw std::runtime_error("Unable to open upload source");
            compression_engine compressor(state->config.comp_level);
            uint64_t wire_bytes = 0;
            for (uint64_t index = 0; index < ctx->total_chunks; ++index) {
                if (!state->wait_active(ctx)) throw std::runtime_error("Upload interrupted");
                chunk data; auto& h = data.header;
                h.id = ctx->tid; h.chunk_index = index; h.chunk_offset = index * accepted.chunk_size;
                h.original_size = static_cast<uint32_t>(std::min<uint64_t>(accepted.chunk_size, ctx->file_size - h.chunk_offset));
                data.data.resize(h.original_size);
                file.read(reinterpret_cast<char*>(data.data.data()), h.original_size);
                if (!file || file.gcount() != h.original_size) throw std::runtime_error("Upload source changed or could not be read");
                h.checksum = checksum::crc32(data.data);
                if (index == 0) h.flags |= chunk_flags::first_chunk;
                if (index + 1 == ctx->total_chunks) h.flags |= chunk_flags::last_chunk;
                if (compression != compression_mode::none) {
                    auto encoded = compressor.compress(data.data);
                    if (!encoded) throw std::runtime_error(encoded.error().message);
                    if (encoded.value().size() < data.data.size()) { data.data = std::move(encoded.value()); h.flags |= chunk_flags::compressed; }
                }
                h.compressed_size = static_cast<uint32_t>(data.data.size());
                if (!state->throttle(ctx, state->upload_limiter.get(), data.data.size())) throw std::runtime_error("Upload interrupted");
                send(message_type::chunk_data, data);
                auto ack = receive.template operator()<msg_chunk_ack>(message_type::chunk_ack);
                if (ack.chunk_index != index) throw std::runtime_error("Invalid upload chunk acknowledgement");
                wire_bytes += data.data.size();
                transfer_progress progress;
                {
                    std::lock_guard lock(ctx->mutex);
                    if (is_terminal_status(to_transfer_status(ctx->state))) throw std::runtime_error("Upload interrupted");
                    ++ctx->transferred_chunks; ctx->transferred_bytes += h.original_size;
                    progress = {ctx->remote_name, ctx->transferred_bytes, ctx->file_size, ctx->file_size ? 100.0 * ctx->transferred_bytes / ctx->file_size : 100.0};
                }
                if (auto callback = state->progress_observer()) { try { callback(progress); } catch (...) {} }
            }
            if (!state->wait_active(ctx)) throw std::runtime_error("Upload interrupted");
            { std::lock_guard lock(ctx->mutex); ctx->state = internal_transfer_state::completing; }
            send(message_type::upload_complete, msg_upload_complete{ctx->tid.bytes, ctx->total_chunks, ctx->file_size, wire_bytes});
            auto ack = receive.template operator()<msg_upload_ack>(message_type::upload_ack);
            if (ack.verified != 1) throw std::runtime_error("Server did not verify uploaded file");
            state->finish_upload(ctx, true);
        } catch (const std::exception& err) {
            (void)state->send(message_type::transfer_cancel, msg_transfer_cancel{ctx->tid.bytes, 0, err.what()});
            state->finish_upload(ctx, false, err.what());
        }
        state->close_channel(ctx->tid);
    });
    return transfer_handle{handle_id, this};
}

auto file_transfer_client::download_file(
    const std::string& remote_name,
    const std::filesystem::path& local_path,
    const download_options& options) -> result<transfer_handle> {

    // Validate connection state
    if (!is_connected()) {
        FT_LOG_ERROR(log_category::client, "Download failed: client is not connected");
        return unexpected{error{error_code::not_initialized,
                               "Client is not connected"}};
    }

    // Validate remote filename
    if (remote_name.empty()) {
        FT_LOG_ERROR(log_category::client, "Download failed: remote filename is empty");
        return unexpected{error{error_code::invalid_file_path,
                               "Remote filename is empty"}};
    }

    // Validate local path
    if (local_path.empty()) {
        FT_LOG_ERROR(log_category::client, "Download failed: local path is empty");
        return unexpected{error{error_code::invalid_file_path,
                               "Local path is empty"}};
    }

    // Check if file already exists (unless overwrite is enabled)
    if (!options.overwrite && std::filesystem::exists(local_path)) {
        FT_LOG_ERROR(log_category::client,
            "Download failed: file already exists: " + local_path.string());
        return unexpected{error{error_code::file_already_exists,
                               "File already exists: " + local_path.string()}};
    }

    // Ensure parent directory exists
    auto parent_path = local_path.parent_path();
    if (!parent_path.empty() && !std::filesystem::exists(parent_path)) {
        std::error_code ec;
        std::filesystem::create_directories(parent_path, ec);
        if (ec) {
            FT_LOG_ERROR(log_category::client,
                "Download failed: cannot create directory: " + parent_path.string());
            return unexpected{error{error_code::file_access_denied,
                                   "Cannot create directory: " + parent_path.string()}};
        }
    }

    // Generate transfer ID and handle
    auto handle_id = impl_->next_transfer_id++;
    transfer_handle handle{handle_id, this};

    // Create download context
    auto ctx = std::make_shared<download_context>();
    ctx->tid = transfer_id::generate();
    ctx->remote_name = remote_name;
    ctx->local_path = local_path;
    ctx->options = options;
    ctx->state = internal_transfer_state::initializing;
    ctx->start_time = std::chrono::steady_clock::now();

    // Create temp file path
    ctx->temp_path = local_path;
    ctx->temp_path += ".tmp";

    // Log download start with context
    transfer_log_context log_ctx;
    log_ctx.transfer_id = ctx->tid.to_string();
    log_ctx.filename = remote_name;
    FT_LOG_INFO_CTX(log_category::client, "Starting file download", log_ctx);

    // Store download context
    {
        std::lock_guard lock(impl_->download_mutex);
        impl_->download_contexts[handle_id] = std::move(ctx);
    }

    // Get context pointer for operations
    std::shared_ptr<download_context> dl_ctx;
    {
        std::lock_guard lock(impl_->download_mutex);
        dl_ctx = impl_->download_contexts[handle_id];
    }

    // Build and send DOWNLOAD_REQUEST message
    msg_download_request request{};
    std::copy(dl_ctx->tid.bytes.begin(), dl_ctx->tid.bytes.end(),
              request.transfer_id.begin());
    request.filename = remote_name;
    request.compression = (impl_->config.compression == compression_mode::none)
                              ? wire_compression_mode::none
                              : wire_compression_mode::lz4;
    request.resume_from = 0;  // New download, no resume

    // Send request via network client
    // Note: Actual network send implementation depends on messaging_client API
    // This is a placeholder for the protocol flow

    // Wait for DOWNLOAD_ACCEPT or DOWNLOAD_REJECT
    // The actual implementation would use async message handling

    // Update statistics
    {
        std::lock_guard lock(impl_->stats_mutex);
        impl_->statistics.total_files_downloaded++;
    }

    return handle;
}

auto file_transfer_client::process_download_chunk(
    uint64_t handle_id, const chunk& received_chunk) -> result<void> {
    if (impl_->download_limiter) impl_->download_limiter->acquire(received_chunk.data.size());
    std::shared_ptr<download_context> ctx;
    {
        std::lock_guard lock(impl_->download_mutex);
        auto it = impl_->download_contexts.find(handle_id);
        if (it == impl_->download_contexts.end())
            return unexpected{error{error_code::transfer_not_found, "Download context not found"}};
        ctx = it->second;
    }
    std::unique_lock lock(ctx->mutex);
    if (is_terminal_status(to_transfer_status(ctx->state)))
        return unexpected{error{error_code::transfer_already_completed, "Download is already terminal"}};
    std::vector<std::byte> data;
    if (received_chunk.is_compressed()) {
        auto decoded = decompress_lz4(received_chunk.data, received_chunk.header.original_size);
        if (!decoded) return unexpected{decoded.error()};
        data = std::move(decoded.value());
    } else data = received_chunk.data;
    // CRC32 describes the original data, not its compressed wire encoding.
    if (!checksum::verify_crc32(data, received_chunk.header.checksum))
        return unexpected{error{error_code::chunk_checksum_error, "Chunk CRC32 verification failed"}};
    std::fstream file(ctx->temp_path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) return unexpected{error{error_code::file_write_error, "Cannot open download temporary file"}};
    file.seekp(static_cast<std::streamoff>(received_chunk.header.chunk_offset));
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    file.close();
    if (!file) return unexpected{error{error_code::file_write_error, "Failed to write download chunk"}};
    ++ctx->received_chunks;
    ctx->bytes_received += data.size();
    ctx->state = download_state::transferring;
    const bool complete = ctx->received_chunks >= ctx->total_chunks;
    transfer_progress progress{ctx->remote_name, ctx->bytes_received, ctx->file_size,
        ctx->file_size ? 100.0 * ctx->bytes_received / ctx->file_size : 100.0};
    lock.unlock();
    if (auto callback = impl_->progress_observer()) callback(progress);
    // finalize_download owns the context lock. Never call it while holding it.
    return complete ? finalize_download(handle_id) : result<void>{};
}

auto file_transfer_client::finalize_download(uint64_t handle_id) -> result<void> {
    std::shared_ptr<download_context> ctx;
    {
        std::lock_guard lock(impl_->download_mutex);
        auto it = impl_->download_contexts.find(handle_id);
        if (it == impl_->download_contexts.end())
            return unexpected{error{error_code::transfer_not_found, "Download context not found"}};
        ctx = it->second;
    }
    std::unique_lock lock(ctx->mutex);
    if (ctx->state == download_state::completed) return {};
    if (is_terminal_status(to_transfer_status(ctx->state)))
        return unexpected{error{error_code::transfer_already_completed, "Download is already terminal"}};
    error failure;
    ctx->state = download_state::verifying;
    if (ctx->received_chunks != ctx->total_chunks || ctx->bytes_received != ctx->file_size)
        failure = error{error_code::missing_chunks, "Download is incomplete"};
    else if (ctx->options.verify_hash && !ctx->expected_sha256.empty() &&
             !checksum::verify_sha256(ctx->temp_path, ctx->expected_sha256))
        failure = error{error_code::file_hash_mismatch, "SHA-256 verification failed"};
    else {
        ctx->state = download_state::completing;
        std::error_code ec;
        if (!ctx->options.overwrite && std::filesystem::exists(ctx->local_path, ec))
            failure = error{error_code::file_already_exists, "Destination already exists"};
        else {
            std::filesystem::rename(ctx->temp_path, ctx->local_path, ec);
            if (ec) failure = error{error_code::file_write_error, "Cannot commit download: " + ec.message()};
        }
    }
    ctx->state = failure ? download_state::failed : download_state::completed;
    ctx->error_message = failure.message;
    if (failure) {
        std::error_code ec;
        std::filesystem::remove(ctx->temp_path, ec);
    }
    transfer_result outcome{!failure, ctx->remote_name, ctx->bytes_received, failure.message};
    ctx->cv.notify_all();
    lock.unlock();
    if (!failure) {
        std::lock_guard stats_lock(impl_->stats_mutex);
        impl_->statistics.total_bytes_downloaded += outcome.bytes_transferred;
    }
    if (auto callback = impl_->completion_callback()) callback(outcome);
    return failure ? result<void>{unexpected{failure}} : result<void>{};
}

auto file_transfer_client::cancel_download(uint64_t handle_id) -> result<void> {
    std::shared_ptr<download_context> ctx;
    {
        std::lock_guard lock(impl_->download_mutex);
        auto it = impl_->download_contexts.find(handle_id);
        if (it == impl_->download_contexts.end())
            return unexpected{error{error_code::transfer_not_found, "Download context not found"}};
        ctx = it->second;
    }
    std::unique_lock lock(ctx->mutex);
    if (ctx->state == download_state::cancelled) return {};
    if (is_terminal_status(to_transfer_status(ctx->state)))
        return unexpected{error{error_code::transfer_already_completed, "Download is already terminal"}};
    ctx->cancelled = true;
    ctx->state = download_state::cancelled;
    ctx->error_message = "Download cancelled";
    // Cleanup is serialized with chunk writes/finalization, then waiters and
    // observers see one fully established terminal outcome.
    std::error_code ec;
    std::filesystem::remove(ctx->temp_path, ec);
    transfer_result outcome{false, ctx->remote_name, ctx->bytes_received, ctx->error_message};
    ctx->cv.notify_all();
    lock.unlock();
    if (auto callback = impl_->completion_callback()) callback(outcome);
    return {};
}

auto file_transfer_client::set_download_metadata(
    uint64_t handle_id,
    uint64_t file_size,
    uint64_t total_chunks,
    uint32_t chunk_size,
    const std::string& sha256_hash) -> result<void> {

    std::shared_ptr<download_context> ctx;
    {
        std::lock_guard lock(impl_->download_mutex);
        auto it = impl_->download_contexts.find(handle_id);
        if (it == impl_->download_contexts.end()) {
            return unexpected{error{error_code::not_initialized,
                                   "Download context not found"}};
        }
        ctx = it->second;
    }

    std::lock_guard ctx_lock(ctx->mutex);
    ctx->file_size = file_size;
    ctx->total_chunks = total_chunks;
    ctx->chunk_size = chunk_size;
    ctx->expected_sha256 = sha256_hash;
    ctx->state = download_state::transferring;

    // Pre-allocate temp file for better performance
    std::ofstream file(ctx->temp_path, std::ios::binary);
    if (file && file_size > 0) {
        file.seekp(static_cast<std::streamoff>(file_size - 1));
        file.put('\0');
    }
    file.close();

    return {};
}

auto file_transfer_client::list_files(
    [[maybe_unused]] const list_options& options) -> result<std::vector<file_info>> {

    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized,
                               "Client is not connected"}};
    }

    // TODO: Implement actual file listing logic
    // For now, return empty list
    return std::vector<file_info>{};
}

void file_transfer_client::on_progress(
    std::function<void(const transfer_progress&)> callback) {
    std::lock_guard lock(impl_->callbacks_mutex);
    impl_->progress_callback = std::move(callback);
}

void file_transfer_client::on_complete(
    std::function<void(const transfer_result&)> callback) {
    std::lock_guard lock(impl_->callbacks_mutex);
    impl_->complete_callback = std::move(callback);
}

void file_transfer_client::on_connection_state_changed(
    std::function<void(connection_state)> callback) {
    std::lock_guard lock(impl_->callbacks_mutex);
    impl_->state_callback = std::move(callback);
}

auto file_transfer_client::get_statistics() const -> client_statistics {
    client_statistics stats;
    {
        std::lock_guard lock(impl_->stats_mutex);
        stats = impl_->statistics;
    }

    // Count active transfers from upload and download contexts
    std::size_t active_count = 0;
    {
        std::lock_guard upload_lock(impl_->upload_mutex);
        for (const auto& [id, ctx] : impl_->upload_contexts) {
            if (!ctx) continue;
            std::lock_guard context_lock(ctx->mutex);
            if (!is_terminal_status(to_transfer_status(ctx->state))) {
                ++active_count;
            }
        }
    }
    {
        std::lock_guard download_lock(impl_->download_mutex);
        for (const auto& [id, ctx] : impl_->download_contexts) {
            if (!ctx) continue;
            std::lock_guard context_lock(ctx->mutex);
            if (!is_terminal_status(to_transfer_status(ctx->state))) {
                ++active_count;
            }
        }
    }
    stats.active_transfers = active_count;

    return stats;
}

auto file_transfer_client::get_compression_stats() const -> compression_statistics {
    std::lock_guard lock(impl_->stats_mutex);
    return impl_->compression_stats;
}

auto file_transfer_client::config() const -> const client_config& {
    return impl_->config;
}

// ============================================================================
// Transfer control methods
// ============================================================================

auto file_transfer_client::get_transfer_status(uint64_t handle_id) const
    -> transfer_status {
    // Check upload contexts
    {
        std::lock_guard lock(impl_->upload_mutex);
        auto it = impl_->upload_contexts.find(handle_id);
        if (it != impl_->upload_contexts.end() && it->second) {
            std::lock_guard ctx_lock(it->second->mutex);
            return to_transfer_status(it->second->state);
        }
    }

    // Check download contexts
    {
        std::lock_guard lock(impl_->download_mutex);
        auto it = impl_->download_contexts.find(handle_id);
        if (it != impl_->download_contexts.end() && it->second) {
            std::lock_guard ctx_lock(it->second->mutex);
            return to_transfer_status(it->second->state);
        }
    }

    return transfer_status::failed;
}

auto file_transfer_client::get_transfer_progress(uint64_t handle_id) const
    -> transfer_progress_info {
    // Check upload contexts
    {
        std::lock_guard lock(impl_->upload_mutex);
        auto it = impl_->upload_contexts.find(handle_id);
        if (it != impl_->upload_contexts.end() && it->second) {
            std::lock_guard ctx_lock(it->second->mutex);
            return it->second->get_progress_info();
        }
    }

    // Check download contexts
    {
        std::lock_guard lock(impl_->download_mutex);
        auto it = impl_->download_contexts.find(handle_id);
        if (it != impl_->download_contexts.end() && it->second) {
            std::lock_guard ctx_lock(it->second->mutex);
            return it->second->get_progress_info();
        }
    }

    return transfer_progress_info{};
}

auto file_transfer_client::pause_transfer(uint64_t handle_id) -> result<void> {
    // Try upload context first
    {
        std::lock_guard lock(impl_->upload_mutex);
        auto it = impl_->upload_contexts.find(handle_id);
        if (it != impl_->upload_contexts.end() && it->second) {
            std::lock_guard ctx_lock(it->second->mutex);
            auto& ctx = it->second;

            // Validate state transition
            if (!is_valid_transition(ctx->state,
                                     internal_transfer_state::paused)) {
                return unexpected{error{error_code::invalid_state_transition,
                    "Cannot pause transfer in current state: " +
                    std::string(to_string(to_transfer_status(ctx->state)))}};
            }

            ctx->state = internal_transfer_state::paused;
            return {};
        }
    }

    // Try download context
    {
        std::lock_guard lock(impl_->download_mutex);
        auto it = impl_->download_contexts.find(handle_id);
        if (it != impl_->download_contexts.end() && it->second) {
            std::lock_guard ctx_lock(it->second->mutex);
            auto& ctx = it->second;

            // Validate state transition
            if (!is_valid_transition(ctx->state,
                                     internal_transfer_state::paused)) {
                return unexpected{error{error_code::invalid_state_transition,
                    "Cannot pause transfer in current state: " +
                    std::string(to_string(to_transfer_status(ctx->state)))}};
            }

            ctx->state = internal_transfer_state::paused;
            return {};
        }
    }

    return unexpected{error{error_code::transfer_not_found,
                           "Transfer not found: " + std::to_string(handle_id)}};
}

auto file_transfer_client::resume_transfer(uint64_t handle_id) -> result<void> {
    // Try upload context first
    {
        std::lock_guard lock(impl_->upload_mutex);
        auto it = impl_->upload_contexts.find(handle_id);
        if (it != impl_->upload_contexts.end() && it->second) {
            std::lock_guard ctx_lock(it->second->mutex);
            auto& ctx = it->second;

            // Validate state transition
            if (!is_valid_transition(ctx->state,
                                     internal_transfer_state::transferring)) {
                return unexpected{error{error_code::invalid_state_transition,
                    "Cannot resume transfer in current state: " +
                    std::string(to_string(to_transfer_status(ctx->state)))}};
            }

            ctx->state = internal_transfer_state::transferring;
            ctx->cv.notify_all();  // Wake up any paused workers
            return {};
        }
    }

    // Try download context
    {
        std::lock_guard lock(impl_->download_mutex);
        auto it = impl_->download_contexts.find(handle_id);
        if (it != impl_->download_contexts.end() && it->second) {
            std::lock_guard ctx_lock(it->second->mutex);
            auto& ctx = it->second;

            // Validate state transition
            if (!is_valid_transition(ctx->state,
                                     internal_transfer_state::transferring)) {
                return unexpected{error{error_code::invalid_state_transition,
                    "Cannot resume transfer in current state: " +
                    std::string(to_string(to_transfer_status(ctx->state)))}};
            }

            ctx->state = internal_transfer_state::transferring;
            ctx->cv.notify_all();  // Wake up any paused workers
            return {};
        }
    }

    return unexpected{error{error_code::transfer_not_found,
                           "Transfer not found: " + std::to_string(handle_id)}};
}

auto file_transfer_client::cancel_transfer(uint64_t handle_id) -> result<void> {
    std::shared_ptr<upload_context> upload;
    {
        std::lock_guard lock(impl_->upload_mutex);
        auto it = impl_->upload_contexts.find(handle_id);
        if (it != impl_->upload_contexts.end()) upload = it->second;
    }
    if (upload) {
        transfer_result outcome;
        {
            std::lock_guard lock(upload->mutex);
            if (upload->state == internal_transfer_state::cancelled) return {};
            if (is_terminal_status(to_transfer_status(upload->state)))
                return unexpected{error{error_code::transfer_already_completed, "Upload already terminal"}};
            upload->state = internal_transfer_state::cancelled;
            upload->error_message = "Upload cancelled"; upload->cv.notify_all();
            outcome = {false, upload->remote_name, upload->transferred_bytes, upload->error_message};
        }
        impl_->fail_channel(upload->tid, error{error_code::invalid_state_transition, "Upload cancelled"});
        if (auto callback = impl_->completion_callback()) { try { callback(outcome); } catch (...) {} }
        return {};
    }

    // cancel_download performs its own map lookup and retains shared ownership.
    bool download_exists;
    {
        std::lock_guard lock(impl_->download_mutex);
        download_exists = impl_->download_contexts.contains(handle_id);
    }
    if (download_exists) return cancel_download(handle_id);

    return unexpected{error{error_code::transfer_not_found,
                           "Transfer not found: " + std::to_string(handle_id)}};
}

auto file_transfer_client::wait_for_transfer(uint64_t handle_id)
    -> result<transfer_result_info> {
    // Use maximum timeout
    return wait_for_transfer(handle_id,
                             std::chrono::milliseconds::max());
}

auto file_transfer_client::wait_for_transfer(
    uint64_t handle_id,
    std::chrono::milliseconds timeout) -> result<transfer_result_info> {

    auto deadline = timeout == std::chrono::milliseconds::max()
        ? std::chrono::steady_clock::time_point::max()
        : std::chrono::steady_clock::now() + timeout;

    // Try upload context first
    {
        std::unique_lock lock(impl_->upload_mutex);
        auto it = impl_->upload_contexts.find(handle_id);
        if (it != impl_->upload_contexts.end() && it->second) {
            auto ctx = it->second;
            lock.unlock();  // Don't hold upload_mutex while waiting

            std::unique_lock ctx_lock(ctx->mutex);
            auto status = to_transfer_status(ctx->state);

            // Wait until terminal state or timeout
            while (!is_terminal_status(status)) {
                if (timeout == std::chrono::milliseconds::max()) {
                    ctx->cv.wait(ctx_lock);
                } else {
                    auto remaining = deadline - std::chrono::steady_clock::now();
                    if (remaining <= std::chrono::milliseconds::zero()) {
                        return unexpected{error{error_code::transfer_timeout,
                                               "Wait timed out"}};
                    }
                    ctx->cv.wait_for(ctx_lock, remaining);
                }
                status = to_transfer_status(ctx->state);
            }

            return ctx->get_result_info();
        }
    }

    // Try download context
    {
        std::unique_lock lock(impl_->download_mutex);
        auto it = impl_->download_contexts.find(handle_id);
        if (it != impl_->download_contexts.end() && it->second) {
            auto ctx = it->second;
            lock.unlock();  // Don't hold download_mutex while waiting

            std::unique_lock ctx_lock(ctx->mutex);
            auto status = to_transfer_status(ctx->state);

            // Wait until terminal state or timeout
            while (!is_terminal_status(status)) {
                if (timeout == std::chrono::milliseconds::max()) {
                    ctx->cv.wait(ctx_lock);
                } else {
                    auto remaining = deadline - std::chrono::steady_clock::now();
                    if (remaining <= std::chrono::milliseconds::zero()) {
                        return unexpected{error{error_code::transfer_timeout,
                                               "Wait timed out"}};
                    }
                    ctx->cv.wait_for(ctx_lock, remaining);
                }
                status = to_transfer_status(ctx->state);
            }

            return ctx->get_result_info();
        }
    }

    return unexpected{error{error_code::transfer_not_found,
                           "Transfer not found: " + std::to_string(handle_id)}};
}

// ============================================================================
// transfer_handle implementation
// ============================================================================

transfer_handle::transfer_handle() : id_(0), client_(nullptr) {}

transfer_handle::transfer_handle(uint64_t handle_id, file_transfer_client* client)
    : id_(handle_id), client_(client) {}

auto transfer_handle::get_id() const noexcept -> uint64_t {
    return id_;
}

auto transfer_handle::is_valid() const noexcept -> bool {
    return id_ != 0 && client_ != nullptr;
}

auto transfer_handle::get_status() const -> transfer_status {
    if (!is_valid()) {
        return transfer_status::failed;
    }
    return client_->get_transfer_status(id_);
}

auto transfer_handle::get_progress() const -> transfer_progress_info {
    if (!is_valid()) {
        return transfer_progress_info{};
    }
    return client_->get_transfer_progress(id_);
}

auto transfer_handle::pause() -> result<void> {
    if (!is_valid()) {
        return unexpected{error{error_code::not_initialized,
                               "Invalid transfer handle"}};
    }
    return client_->pause_transfer(id_);
}

auto transfer_handle::resume() -> result<void> {
    if (!is_valid()) {
        return unexpected{error{error_code::not_initialized,
                               "Invalid transfer handle"}};
    }
    return client_->resume_transfer(id_);
}

auto transfer_handle::cancel() -> result<void> {
    if (!is_valid()) {
        return unexpected{error{error_code::not_initialized,
                               "Invalid transfer handle"}};
    }
    return client_->cancel_transfer(id_);
}

auto transfer_handle::wait() -> result<transfer_result_info> {
    if (!is_valid()) {
        return unexpected{error{error_code::not_initialized,
                               "Invalid transfer handle"}};
    }
    return client_->wait_for_transfer(id_);
}

auto transfer_handle::wait_for(std::chrono::milliseconds timeout)
    -> result<transfer_result_info> {
    if (!is_valid()) {
        return unexpected{error{error_code::not_initialized,
                               "Invalid transfer handle"}};
    }
    return client_->wait_for_transfer(id_, timeout);
}

// ============================================================================
// Batch transfer methods
// ============================================================================

auto file_transfer_client::upload_files(
    std::span<const upload_entry> files,
    const batch_options& options) -> result<batch_transfer_handle> {

    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized,
                               "Client is not connected"}};
    }

    if (files.empty()) {
        return unexpected{error{error_code::invalid_file_path,
                               "No files specified for upload"}};
    }

    // Validate all files exist before starting
    for (const auto& entry : files) {
        if (!std::filesystem::exists(entry.local_path)) {
            return unexpected{error{error_code::file_not_found,
                "File not found: " + entry.local_path.string()}};
        }
    }

    // Create batch context
    auto batch_id = impl_->next_batch_id++;
    auto ctx = std::make_unique<batch_context>();
    ctx->direction = batch_direction::upload;
    ctx->options = options;
    ctx->start_time = std::chrono::steady_clock::now();
    ctx->file_results.resize(files.size());

    // Calculate total bytes and prepare filenames
    uint64_t total = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
        std::error_code ec;
        auto size = std::filesystem::file_size(files[i].local_path, ec);
        if (!ec) {
            total += size;
        }
        ctx->filenames.push_back(files[i].remote_name.empty()
            ? files[i].local_path.filename().string()
            : files[i].remote_name);
    }
    ctx->total_bytes = total;

    // Start transfers up to max_concurrent
    std::size_t started = 0;
    for (std::size_t i = 0; i < files.size() && started < options.max_concurrent; ++i) {
        const auto& entry = files[i];
        std::string remote_name = entry.remote_name.empty()
            ? entry.local_path.filename().string()
            : entry.remote_name;

        upload_options upload_opts;
        upload_opts.overwrite = options.overwrite;
        if (options.compression.has_value()) {
            upload_opts.compression = options.compression;
        }

        auto handle_result = upload_file(entry.local_path, remote_name, upload_opts);
        if (handle_result.has_value()) {
            ctx->transfer_ids.push_back(handle_result.value().get_id());
            ctx->in_progress_count++;
            started++;
        } else {
            // Record failure immediately
            ctx->transfer_ids.push_back(0);  // Invalid ID
            ctx->failed_count++;

            batch_file_result file_result;
            file_result.filename = remote_name;
            file_result.success = false;
            file_result.error_message = handle_result.error().message;
            ctx->file_results[i] = file_result;

            if (!options.continue_on_error) {
                return unexpected{error{error_code::internal_error,
                    "Failed to start upload: " + handle_result.error().message}};
            }
        }
    }

    // Queue remaining files (they'll be started as others complete)
    for (std::size_t i = started; i < files.size(); ++i) {
        ctx->transfer_ids.push_back(0);  // Pending, not started yet
    }

    // Store pending files info for later processing
    // (In a real implementation, we'd have a worker thread to manage the queue)

    // Store batch context
    {
        std::lock_guard lock(impl_->batch_mutex);
        impl_->batch_contexts[batch_id] = std::move(ctx);
    }

    return batch_transfer_handle{batch_id, this};
}

auto file_transfer_client::download_files(
    std::span<const download_entry> files,
    const batch_options& options) -> result<batch_transfer_handle> {

    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized,
                               "Client is not connected"}};
    }

    if (files.empty()) {
        return unexpected{error{error_code::invalid_file_path,
                               "No files specified for download"}};
    }

    // Create batch context
    auto batch_id = impl_->next_batch_id++;
    auto ctx = std::make_unique<batch_context>();
    ctx->direction = batch_direction::download;
    ctx->options = options;
    ctx->start_time = std::chrono::steady_clock::now();
    ctx->file_results.resize(files.size());

    // Prepare filenames
    for (const auto& entry : files) {
        ctx->filenames.push_back(entry.remote_name);
    }

    // Start transfers up to max_concurrent
    std::size_t started = 0;
    for (std::size_t i = 0; i < files.size() && started < options.max_concurrent; ++i) {
        const auto& entry = files[i];

        download_options download_opts;
        download_opts.overwrite = options.overwrite;

        auto handle_result = download_file(entry.remote_name, entry.local_path, download_opts);
        if (handle_result.has_value()) {
            ctx->transfer_ids.push_back(handle_result.value().get_id());
            ctx->in_progress_count++;
            started++;
        } else {
            // Record failure immediately
            ctx->transfer_ids.push_back(0);  // Invalid ID
            ctx->failed_count++;

            batch_file_result file_result;
            file_result.filename = entry.remote_name;
            file_result.success = false;
            file_result.error_message = handle_result.error().message;
            ctx->file_results[i] = file_result;

            if (!options.continue_on_error) {
                return unexpected{error{error_code::internal_error,
                    "Failed to start download: " + handle_result.error().message}};
            }
        }
    }

    // Queue remaining files
    for (std::size_t i = started; i < files.size(); ++i) {
        ctx->transfer_ids.push_back(0);  // Pending, not started yet
    }

    // Store batch context
    {
        std::lock_guard lock(impl_->batch_mutex);
        impl_->batch_contexts[batch_id] = std::move(ctx);
    }

    return batch_transfer_handle{batch_id, this};
}

auto file_transfer_client::get_batch_progress(uint64_t batch_id) const
    -> batch_progress {
    std::lock_guard lock(impl_->batch_mutex);
    auto it = impl_->batch_contexts.find(batch_id);
    if (it == impl_->batch_contexts.end() || !it->second) {
        return batch_progress{};
    }

    auto progress = it->second->get_progress();

    // Update transferred bytes from individual transfers
    uint64_t transferred = 0;
    for (auto transfer_id : it->second->transfer_ids) {
        if (transfer_id != 0) {
            auto info = get_transfer_progress(transfer_id);
            transferred += info.bytes_transferred;
        }
    }
    progress.transferred_bytes = transferred;

    return progress;
}

auto file_transfer_client::get_batch_total_files(uint64_t batch_id) const
    -> std::size_t {
    std::lock_guard lock(impl_->batch_mutex);
    auto it = impl_->batch_contexts.find(batch_id);
    if (it == impl_->batch_contexts.end() || !it->second) {
        return 0;
    }
    return it->second->transfer_ids.size();
}

auto file_transfer_client::get_batch_completed_files(uint64_t batch_id) const
    -> std::size_t {
    std::lock_guard lock(impl_->batch_mutex);
    auto it = impl_->batch_contexts.find(batch_id);
    if (it == impl_->batch_contexts.end() || !it->second) {
        return 0;
    }
    return it->second->completed_count.load();
}

auto file_transfer_client::get_batch_failed_files(uint64_t batch_id) const
    -> std::size_t {
    std::lock_guard lock(impl_->batch_mutex);
    auto it = impl_->batch_contexts.find(batch_id);
    if (it == impl_->batch_contexts.end() || !it->second) {
        return 0;
    }
    return it->second->failed_count.load();
}

auto file_transfer_client::get_batch_individual_handles(uint64_t batch_id) const
    -> std::vector<transfer_handle> {
    std::vector<transfer_handle> handles;

    std::lock_guard lock(impl_->batch_mutex);
    auto it = impl_->batch_contexts.find(batch_id);
    if (it == impl_->batch_contexts.end() || !it->second) {
        return handles;
    }

    handles.reserve(it->second->transfer_ids.size());
    for (auto transfer_id : it->second->transfer_ids) {
        if (transfer_id != 0) {
            handles.emplace_back(transfer_id,
                const_cast<file_transfer_client*>(this));
        }
    }

    return handles;
}

auto file_transfer_client::pause_batch(uint64_t batch_id) -> result<void> {
    batch_context* ctx = nullptr;
    {
        std::lock_guard lock(impl_->batch_mutex);
        auto it = impl_->batch_contexts.find(batch_id);
        if (it == impl_->batch_contexts.end() || !it->second) {
            return unexpected{error{error_code::transfer_not_found,
                "Batch not found: " + std::to_string(batch_id)}};
        }
        ctx = it->second.get();
    }

    ctx->paused = true;

    // Pause all active transfers
    for (auto transfer_id : ctx->transfer_ids) {
        if (transfer_id != 0) {
            auto status = get_transfer_status(transfer_id);
            if (status == transfer_status::in_progress) {
                (void)pause_transfer(transfer_id);
            }
        }
    }

    return {};
}

auto file_transfer_client::resume_batch(uint64_t batch_id) -> result<void> {
    batch_context* ctx = nullptr;
    {
        std::lock_guard lock(impl_->batch_mutex);
        auto it = impl_->batch_contexts.find(batch_id);
        if (it == impl_->batch_contexts.end() || !it->second) {
            return unexpected{error{error_code::transfer_not_found,
                "Batch not found: " + std::to_string(batch_id)}};
        }
        ctx = it->second.get();
    }

    ctx->paused = false;

    // Resume all paused transfers
    for (auto transfer_id : ctx->transfer_ids) {
        if (transfer_id != 0) {
            auto status = get_transfer_status(transfer_id);
            if (status == transfer_status::paused) {
                (void)resume_transfer(transfer_id);
            }
        }
    }

    ctx->cv.notify_all();
    return {};
}

auto file_transfer_client::cancel_batch(uint64_t batch_id) -> result<void> {
    batch_context* ctx = nullptr;
    {
        std::lock_guard lock(impl_->batch_mutex);
        auto it = impl_->batch_contexts.find(batch_id);
        if (it == impl_->batch_contexts.end() || !it->second) {
            return unexpected{error{error_code::transfer_not_found,
                "Batch not found: " + std::to_string(batch_id)}};
        }
        ctx = it->second.get();
    }

    ctx->cancelled = true;

    // Cancel all active transfers
    for (auto transfer_id : ctx->transfer_ids) {
        if (transfer_id != 0) {
            auto status = get_transfer_status(transfer_id);
            if (!is_terminal_status(status)) {
                (void)cancel_transfer(transfer_id);
            }
        }
    }

    ctx->cv.notify_all();
    return {};
}

auto file_transfer_client::wait_for_batch(uint64_t batch_id)
    -> result<batch_result> {
    return wait_for_batch(batch_id, std::chrono::milliseconds::max());
}

auto file_transfer_client::wait_for_batch(
    uint64_t batch_id,
    std::chrono::milliseconds timeout) -> result<batch_result> {

    batch_context* ctx = nullptr;
    {
        std::lock_guard lock(impl_->batch_mutex);
        auto it = impl_->batch_contexts.find(batch_id);
        if (it == impl_->batch_contexts.end() || !it->second) {
            return unexpected{error{error_code::transfer_not_found,
                "Batch not found: " + std::to_string(batch_id)}};
        }
        ctx = it->second.get();
    }

    auto deadline = timeout == std::chrono::milliseconds::max()
        ? std::chrono::steady_clock::time_point::max()
        : std::chrono::steady_clock::now() + timeout;

    // Wait for all transfers to complete
    for (std::size_t i = 0; i < ctx->transfer_ids.size(); ++i) {
        auto transfer_id = ctx->transfer_ids[i];
        if (transfer_id == 0) {
            continue;  // Already failed or not started
        }

        std::chrono::milliseconds remaining = timeout;
        if (timeout != std::chrono::milliseconds::max()) {
            remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining <= std::chrono::milliseconds::zero()) {
                return unexpected{error{error_code::transfer_timeout,
                    "Batch wait timed out"}};
            }
        }

        auto result = wait_for_transfer(transfer_id, remaining);

        batch_file_result file_result;
        file_result.filename = ctx->filenames[i];

        if (result.has_value()) {
            file_result.success = result.value().success;
            file_result.bytes_transferred = result.value().bytes_transferred;
            file_result.elapsed = result.value().elapsed;
            if (result.value().error_message.has_value()) {
                file_result.error_message = result.value().error_message;
            }

            if (result.value().success) {
                ctx->completed_count++;
            } else {
                ctx->failed_count++;
            }
        } else {
            file_result.success = false;
            file_result.error_message = result.error().message;
            ctx->failed_count++;
        }

        {
            std::lock_guard lock(ctx->mutex);
            ctx->file_results[i] = file_result;
        }
        ctx->in_progress_count--;
    }

    return ctx->get_result();
}

// ============================================================================
// batch_transfer_handle implementation
// ============================================================================

batch_transfer_handle::batch_transfer_handle() : id_(0), client_(nullptr) {}

batch_transfer_handle::batch_transfer_handle(
    uint64_t batch_id, file_transfer_client* client)
    : id_(batch_id), client_(client) {}

auto batch_transfer_handle::get_id() const noexcept -> uint64_t {
    return id_;
}

auto batch_transfer_handle::is_valid() const noexcept -> bool {
    return id_ != 0 && client_ != nullptr;
}

auto batch_transfer_handle::get_total_files() const -> std::size_t {
    if (!is_valid()) {
        return 0;
    }
    return client_->get_batch_total_files(id_);
}

auto batch_transfer_handle::get_completed_files() const -> std::size_t {
    if (!is_valid()) {
        return 0;
    }
    return client_->get_batch_completed_files(id_);
}

auto batch_transfer_handle::get_failed_files() const -> std::size_t {
    if (!is_valid()) {
        return 0;
    }
    return client_->get_batch_failed_files(id_);
}

auto batch_transfer_handle::get_individual_handles() const
    -> std::vector<transfer_handle> {
    if (!is_valid()) {
        return {};
    }
    return client_->get_batch_individual_handles(id_);
}

auto batch_transfer_handle::get_batch_progress() const -> batch_progress {
    if (!is_valid()) {
        return batch_progress{};
    }
    return client_->get_batch_progress(id_);
}

auto batch_transfer_handle::pause_all() -> result<void> {
    if (!is_valid()) {
        return unexpected{error{error_code::not_initialized,
                               "Invalid batch handle"}};
    }
    return client_->pause_batch(id_);
}

auto batch_transfer_handle::resume_all() -> result<void> {
    if (!is_valid()) {
        return unexpected{error{error_code::not_initialized,
                               "Invalid batch handle"}};
    }
    return client_->resume_batch(id_);
}

auto batch_transfer_handle::cancel_all() -> result<void> {
    if (!is_valid()) {
        return unexpected{error{error_code::not_initialized,
                               "Invalid batch handle"}};
    }
    return client_->cancel_batch(id_);
}

auto batch_transfer_handle::wait() -> result<batch_result> {
    if (!is_valid()) {
        return unexpected{error{error_code::not_initialized,
                               "Invalid batch handle"}};
    }
    return client_->wait_for_batch(id_);
}

auto batch_transfer_handle::wait_for(std::chrono::milliseconds timeout)
    -> result<batch_result> {
    if (!is_valid()) {
        return unexpected{error{error_code::not_initialized,
                               "Invalid batch handle"}};
    }
    return client_->wait_for_batch(id_, timeout);
}

}  // namespace kcenon::file_transfer
