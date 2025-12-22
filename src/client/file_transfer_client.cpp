/**
 * @file file_transfer_client.cpp
 * @brief File transfer client implementation
 */

#include "kcenon/file_transfer/client/file_transfer_client.h"

#include <kcenon/file_transfer/core/bandwidth_limiter.h>
#include <kcenon/file_transfer/core/checksum.h>
#include <kcenon/file_transfer/core/chunk_assembler.h>
#include <kcenon/file_transfer/core/protocol_types.h>
#include <kcenon/file_transfer/core/logging.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <thread>

#ifdef FILE_TRANS_ENABLE_LZ4
#include <lz4.h>
#endif

#include <kcenon/network/core/messaging_client.h>

// Namespace alias for network_system
namespace network = kcenon::network;

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

    // Network client for server communication
    std::shared_ptr<network::core::messaging_client> network_client;

    // Callbacks
    std::function<void(const transfer_progress&)> progress_callback;
    std::function<void(const transfer_result&)> complete_callback;
    std::function<void(connection_state)> state_callback;

    // Statistics
    mutable std::mutex stats_mutex;
    client_statistics statistics;
    compression_statistics compression_stats;

    // Active transfers
    mutable std::mutex transfers_mutex;
    std::atomic<uint64_t> next_transfer_id{1};

    // Upload contexts
    mutable std::mutex upload_mutex;
    std::unordered_map<uint64_t, std::unique_ptr<upload_context>> upload_contexts;

    // Download contexts
    mutable std::mutex download_mutex;
    std::unordered_map<uint64_t, std::unique_ptr<download_context>> download_contexts;

    // Batch contexts
    mutable std::mutex batch_mutex;
    std::atomic<uint64_t> next_batch_id{1};
    std::unordered_map<uint64_t, std::unique_ptr<batch_context>> batch_contexts;

    // Bandwidth limiters
    std::unique_ptr<bandwidth_limiter> upload_limiter;
    std::unique_ptr<bandwidth_limiter> download_limiter;

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

            if (state_callback) {
                state_callback(new_state);
            }
        }
    }
};

// Builder implementation
file_transfer_client::builder::builder() = default;

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

auto file_transfer_client::builder::build() -> result<file_transfer_client> {
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

    impl_->network_client = std::make_shared<network::core::messaging_client>(
        "file_transfer_client");
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

    auto result = impl_->network_client->start_client(server_addr.host, server_addr.port);
    if (result.is_err()) {
        impl_->set_state(connection_state::disconnected);
        FT_LOG_ERROR(log_category::client,
            "Failed to connect: " + result.error().message);
        return unexpected{error{error_code::internal_error,
                               "Failed to connect: " + result.error().message}};
    }

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
    const std::filesystem::path& local_path,
    const std::string& remote_name,
    const upload_options& options) -> result<transfer_handle> {

    if (!is_connected()) {
        FT_LOG_ERROR(log_category::client, "Upload failed: client is not connected");
        return unexpected{error{error_code::not_initialized,
                               "Client is not connected"}};
    }

    if (!std::filesystem::exists(local_path)) {
        FT_LOG_ERROR(log_category::client,
            "Upload failed: local file not found: " + local_path.string());
        return unexpected{error{error_code::file_not_found,
                               "Local file not found: " + local_path.string()}};
    }

    // Get file info
    std::error_code ec;
    auto file_size = std::filesystem::file_size(local_path, ec);
    if (ec) {
        FT_LOG_ERROR(log_category::client,
            "Upload failed: cannot get file size: " + ec.message());
        return unexpected{error{error_code::file_read_error,
                               "Cannot get file size: " + ec.message()}};
    }

    // Generate handle ID
    auto handle_id = impl_->next_transfer_id++;
    transfer_handle handle{handle_id, this};

    // Create upload context
    auto ctx = std::make_unique<upload_context>();
    ctx->tid = transfer_id::generate();
    ctx->local_path = local_path;
    ctx->remote_name = remote_name;
    ctx->options = options;
    ctx->state = internal_transfer_state::initializing;
    ctx->file_size = file_size;
    ctx->chunk_size = static_cast<uint32_t>(impl_->config.chunk_size);
    ctx->total_chunks = (file_size + ctx->chunk_size - 1) / ctx->chunk_size;
    ctx->start_time = std::chrono::steady_clock::now();

    // Log upload start with context
    transfer_log_context log_ctx;
    log_ctx.transfer_id = ctx->tid.to_string();
    log_ctx.filename = remote_name;
    log_ctx.file_size = file_size;
    log_ctx.total_chunks = static_cast<uint32_t>(ctx->total_chunks);
    FT_LOG_INFO_CTX(log_category::client, "Starting file upload", log_ctx);

    // Store upload context
    {
        std::lock_guard lock(impl_->upload_mutex);
        impl_->upload_contexts[handle_id] = std::move(ctx);
    }

    // Update statistics
    {
        std::lock_guard lock(impl_->stats_mutex);
        impl_->statistics.total_files_uploaded++;
    }

    // TODO: Implement actual file upload logic with network layer
    // For now, mark as transferring
    {
        std::lock_guard lock(impl_->upload_mutex);
        auto it = impl_->upload_contexts.find(handle_id);
        if (it != impl_->upload_contexts.end()) {
            std::lock_guard ctx_lock(it->second->mutex);
            it->second->state = internal_transfer_state::transferring;
        }
    }

    return handle;
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
    auto ctx = std::make_unique<download_context>();
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
    download_context* dl_ctx = nullptr;
    {
        std::lock_guard lock(impl_->download_mutex);
        dl_ctx = impl_->download_contexts[handle_id].get();
    }

    // Build and send DOWNLOAD_REQUEST message
    msg_download_request request;
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
    uint64_t handle_id,
    const chunk& received_chunk) -> result<void> {

    // Apply download bandwidth limit if configured
    if (impl_->download_limiter) {
        impl_->download_limiter->acquire(received_chunk.data.size());
    }

    download_context* ctx = nullptr;
    {
        std::lock_guard lock(impl_->download_mutex);
        auto it = impl_->download_contexts.find(handle_id);
        if (it == impl_->download_contexts.end()) {
            return unexpected{error{error_code::not_initialized,
                                   "Download context not found"}};
        }
        ctx = it->second.get();
    }

    std::lock_guard ctx_lock(ctx->mutex);

    // Check if download was cancelled
    if (ctx->cancelled) {
        return unexpected{error{error_code::internal_error,
                               "Download was cancelled"}};
    }

    // Verify chunk checksum (CRC32)
    if (!checksum::verify_crc32(
            std::span<const std::byte>(received_chunk.data),
            received_chunk.header.checksum)) {
        return unexpected{error{error_code::chunk_checksum_error,
                               "Chunk CRC32 verification failed"}};
    }

    // Decompress if needed
    std::vector<std::byte> write_data;
    if (received_chunk.is_compressed()) {
        auto decompress_result = decompress_lz4(
            std::span<const std::byte>(received_chunk.data),
            received_chunk.header.original_size);
        if (!decompress_result.has_value()) {
            return unexpected{error{error_code::internal_error,
                                   "Decompression failed"}};
        }
        write_data = std::move(decompress_result.value());
    } else {
        write_data = received_chunk.data;
    }

    // Write to temp file
    std::ofstream file(ctx->temp_path,
                       std::ios::binary | std::ios::in | std::ios::out);
    if (!file) {
        // Create file if it doesn't exist
        file.open(ctx->temp_path, std::ios::binary | std::ios::out);
        if (!file) {
            return unexpected{error{error_code::file_write_error,
                                   "Cannot open temp file: " + ctx->temp_path.string()}};
        }
    }

    // Seek to correct offset
    file.seekp(static_cast<std::streamoff>(received_chunk.header.chunk_offset));
    if (!file.good()) {
        return unexpected{error{error_code::file_write_error, "Seek failed"}};
    }

    // Write data
    file.write(reinterpret_cast<const char*>(write_data.data()),
               static_cast<std::streamsize>(write_data.size()));
    if (!file.good()) {
        return unexpected{error{error_code::file_write_error, "Write failed"}};
    }

    file.close();

    // Update progress
    ctx->received_chunks++;
    ctx->bytes_received += write_data.size();
    ctx->state = download_state::transferring;

    // Invoke progress callback
    if (impl_->progress_callback) {
        transfer_progress progress;
        progress.filename = ctx->remote_name;
        progress.bytes_transferred = ctx->bytes_received;
        progress.total_bytes = ctx->file_size;
        progress.percentage = (ctx->file_size > 0)
                                  ? static_cast<double>(ctx->bytes_received) /
                                        static_cast<double>(ctx->file_size) * 100.0
                                  : 0.0;
        impl_->progress_callback(progress);
    }

    // Check if download is complete
    if (ctx->received_chunks >= ctx->total_chunks) {
        return finalize_download(handle_id);
    }

    return {};
}

auto file_transfer_client::finalize_download(uint64_t handle_id) -> result<void> {
    download_context* ctx = nullptr;
    {
        std::lock_guard lock(impl_->download_mutex);
        auto it = impl_->download_contexts.find(handle_id);
        if (it == impl_->download_contexts.end()) {
            FT_LOG_ERROR(log_category::client, "Download finalization failed: context not found");
            return unexpected{error{error_code::not_initialized,
                                   "Download context not found"}};
        }
        ctx = it->second.get();
    }

    std::lock_guard ctx_lock(ctx->mutex);
    ctx->state = download_state::verifying;
    FT_LOG_DEBUG(log_category::client, "Download verification started for: " + ctx->remote_name);

    // Verify SHA-256 hash if enabled
    if (ctx->options.verify_hash && !ctx->expected_sha256.empty()) {
        if (!checksum::verify_sha256(ctx->temp_path, ctx->expected_sha256)) {
            ctx->state = download_state::failed;
            ctx->error_message = "SHA-256 hash mismatch";

            transfer_log_context log_ctx;
            log_ctx.transfer_id = ctx->tid.to_string();
            log_ctx.filename = ctx->remote_name;
            log_ctx.error_message = ctx->error_message;
            FT_LOG_ERROR_CTX(log_category::client, "Download failed: hash verification error", log_ctx);

            // Clean up temp file
            std::error_code ec;
            std::filesystem::remove(ctx->temp_path, ec);

            // Invoke completion callback with error
            if (impl_->complete_callback) {
                transfer_result result;
                result.success = false;
                result.filename = ctx->remote_name;
                result.bytes_transferred = ctx->bytes_received;
                result.error_message = ctx->error_message;
                impl_->complete_callback(result);
            }

            return unexpected{error{error_code::file_hash_mismatch,
                                   "SHA-256 verification failed"}};
        }
    }

    ctx->state = download_state::completing;

    // Move temp file to final location
    std::error_code ec;

    // Remove existing file if overwrite is enabled
    if (ctx->options.overwrite && std::filesystem::exists(ctx->local_path, ec)) {
        std::filesystem::remove(ctx->local_path, ec);
    }

    // Rename temp file to final path
    std::filesystem::rename(ctx->temp_path, ctx->local_path, ec);
    if (ec) {
        ctx->state = download_state::failed;
        ctx->error_message = "Cannot move file: " + ec.message();

        transfer_log_context log_ctx;
        log_ctx.transfer_id = ctx->tid.to_string();
        log_ctx.filename = ctx->remote_name;
        log_ctx.error_message = ctx->error_message;
        FT_LOG_ERROR_CTX(log_category::client, "Download failed: file move error", log_ctx);

        // Invoke completion callback with error
        if (impl_->complete_callback) {
            transfer_result result;
            result.success = false;
            result.filename = ctx->remote_name;
            result.bytes_transferred = ctx->bytes_received;
            result.error_message = ctx->error_message;
            impl_->complete_callback(result);
        }

        return unexpected{error{error_code::file_write_error,
                               "Cannot move temp file: " + ec.message()}};
    }

    ctx->state = download_state::completed;

    // Log download completion
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - ctx->start_time).count();
        double rate_mbps = (elapsed_ms > 0)
            ? (static_cast<double>(ctx->bytes_received) * 8.0 / 1000000.0) /
              (static_cast<double>(elapsed_ms) / 1000.0)
            : 0.0;

        transfer_log_context log_ctx;
        log_ctx.transfer_id = ctx->tid.to_string();
        log_ctx.filename = ctx->remote_name;
        log_ctx.file_size = ctx->file_size;
        log_ctx.bytes_transferred = ctx->bytes_received;
        log_ctx.duration_ms = static_cast<uint64_t>(elapsed_ms);
        log_ctx.rate_mbps = rate_mbps;
        FT_LOG_INFO_CTX(log_category::client, "Download completed successfully", log_ctx);
    }

    // Update statistics
    {
        std::lock_guard stats_lock(impl_->stats_mutex);
        impl_->statistics.total_bytes_downloaded += ctx->bytes_received;
    }

    // Invoke completion callback
    if (impl_->complete_callback) {
        transfer_result result;
        result.success = true;
        result.filename = ctx->remote_name;
        result.bytes_transferred = ctx->bytes_received;
        impl_->complete_callback(result);
    }

    // Notify waiting threads
    ctx->cv.notify_all();

    return {};
}

auto file_transfer_client::cancel_download(uint64_t handle_id) -> result<void> {
    download_context* ctx = nullptr;
    {
        std::lock_guard lock(impl_->download_mutex);
        auto it = impl_->download_contexts.find(handle_id);
        if (it == impl_->download_contexts.end()) {
            FT_LOG_WARN(log_category::client, "Cancel download failed: context not found");
            return unexpected{error{error_code::not_initialized,
                                   "Download context not found"}};
        }
        ctx = it->second.get();
    }

    {
        std::lock_guard ctx_lock(ctx->mutex);
        ctx->cancelled = true;
        ctx->state = internal_transfer_state::cancelled;

        transfer_log_context log_ctx;
        log_ctx.transfer_id = ctx->tid.to_string();
        log_ctx.filename = ctx->remote_name;
        log_ctx.bytes_transferred = ctx->bytes_received;
        FT_LOG_INFO_CTX(log_category::client, "Download cancelled by user", log_ctx);

        ctx->cv.notify_all();
    }

    // Clean up temp file
    std::error_code ec;
    std::filesystem::remove(ctx->temp_path, ec);

    return {};
}

auto file_transfer_client::set_download_metadata(
    uint64_t handle_id,
    uint64_t file_size,
    uint64_t total_chunks,
    uint32_t chunk_size,
    const std::string& sha256_hash) -> result<void> {

    download_context* ctx = nullptr;
    {
        std::lock_guard lock(impl_->download_mutex);
        auto it = impl_->download_contexts.find(handle_id);
        if (it == impl_->download_contexts.end()) {
            return unexpected{error{error_code::not_initialized,
                                   "Download context not found"}};
        }
        ctx = it->second.get();
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
    impl_->progress_callback = std::move(callback);
}

void file_transfer_client::on_complete(
    std::function<void(const transfer_result&)> callback) {
    impl_->complete_callback = std::move(callback);
}

void file_transfer_client::on_connection_state_changed(
    std::function<void(connection_state)> callback) {
    impl_->state_callback = std::move(callback);
}

auto file_transfer_client::get_statistics() const -> client_statistics {
    std::lock_guard lock(impl_->stats_mutex);
    auto stats = impl_->statistics;

    // Count active transfers from upload and download contexts
    std::size_t active_count = 0;
    {
        std::lock_guard upload_lock(impl_->upload_mutex);
        for (const auto& [id, ctx] : impl_->upload_contexts) {
            if (ctx && !is_terminal_status(to_transfer_status(ctx->state))) {
                ++active_count;
            }
        }
    }
    {
        std::lock_guard download_lock(impl_->download_mutex);
        for (const auto& [id, ctx] : impl_->download_contexts) {
            if (ctx && !is_terminal_status(to_transfer_status(ctx->state))) {
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
    // Try upload context first
    {
        std::lock_guard lock(impl_->upload_mutex);
        auto it = impl_->upload_contexts.find(handle_id);
        if (it != impl_->upload_contexts.end() && it->second) {
            std::lock_guard ctx_lock(it->second->mutex);
            auto& ctx = it->second;

            // Validate state transition
            if (!is_valid_transition(ctx->state,
                                     internal_transfer_state::cancelled)) {
                return unexpected{error{error_code::transfer_already_completed,
                    "Cannot cancel transfer in current state: " +
                    std::string(to_string(to_transfer_status(ctx->state)))}};
            }

            ctx->state = internal_transfer_state::cancelled;
            ctx->cv.notify_all();
            return {};
        }
    }

    // Try download context - delegate to existing cancel_download
    {
        bool found = false;
        {
            std::lock_guard lock(impl_->download_mutex);
            auto it = impl_->download_contexts.find(handle_id);
            found = (it != impl_->download_contexts.end());
        }  // lock released here safely
        if (found) {
            return cancel_download(handle_id);
        }
    }

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

    auto deadline = std::chrono::steady_clock::now() + timeout;

    // Try upload context first
    {
        std::unique_lock lock(impl_->upload_mutex);
        auto it = impl_->upload_contexts.find(handle_id);
        if (it != impl_->upload_contexts.end() && it->second) {
            auto& ctx = it->second;
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
            auto& ctx = it->second;
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

    auto deadline = std::chrono::steady_clock::now() + timeout;

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
