/**
 * @file file_transfer_client.cpp
 * @brief File transfer client implementation
 */

#include "kcenon/file_transfer/client/file_transfer_client.h"

#include <kcenon/file_transfer/core/checksum.h>
#include <kcenon/file_transfer/core/chunk_assembler.h>
#include <kcenon/file_transfer/core/protocol_types.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <thread>

#ifdef FILE_TRANS_ENABLE_LZ4
#include <lz4.h>
#endif

#ifdef BUILD_WITH_NETWORK_SYSTEM
#include <kcenon/network/core/messaging_client.h>
#endif

namespace kcenon::file_transfer {

/**
 * @brief Internal download state enum
 */
enum class download_state {
    idle,
    initializing,
    transferring,
    verifying,
    completing,
    completed,
    failed,
    cancelled
};

/**
 * @brief Download context for tracking download state
 */
struct download_context {
    transfer_id tid;
    std::string remote_name;
    std::filesystem::path local_path;
    std::filesystem::path temp_path;
    download_options options;
    download_state state{download_state::idle};

    // Metadata from server
    uint64_t file_size{0};
    uint64_t total_chunks{0};
    uint32_t chunk_size{0};
    std::string expected_sha256;

    // Progress tracking
    uint64_t received_chunks{0};
    uint64_t bytes_received{0};
    std::chrono::steady_clock::time_point start_time;

    // Synchronization
    std::mutex mutex;
    std::condition_variable cv;
    bool cancelled{false};
    std::string error_message;
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

#ifdef BUILD_WITH_NETWORK_SYSTEM
    std::shared_ptr<network_system::core::messaging_client> network_client;
#endif

    // Callbacks
    std::function<void(const transfer_progress&)> progress_callback;
    std::function<void(const transfer_result&)> complete_callback;
    std::function<void(connection_state)> state_callback;

    // Statistics
    mutable std::mutex stats_mutex;
    client_statistics statistics;
    compression_statistics compression_stats;

    // Active transfers
    std::mutex transfers_mutex;
    std::unordered_map<uint64_t, transfer_handle> active_transfers;
    std::atomic<uint64_t> next_transfer_id{1};

    // Download contexts
    std::mutex download_mutex;
    std::unordered_map<uint64_t, std::unique_ptr<download_context>> download_contexts;

    explicit impl(client_config cfg) : config(std::move(cfg)) {}

    void set_state(connection_state new_state) {
        auto old_state = current_state.exchange(new_state);
        if (old_state != new_state && state_callback) {
            state_callback(new_state);
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
#ifdef BUILD_WITH_NETWORK_SYSTEM
    impl_->network_client = std::make_shared<network_system::core::messaging_client>(
        "file_transfer_client");
#endif
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
        return unexpected{error{error_code::already_initialized,
                               "Client is already connected or connecting"}};
    }

    impl_->set_state(connection_state::connecting);
    impl_->server_endpoint = server_addr;

#ifdef BUILD_WITH_NETWORK_SYSTEM
    auto result = impl_->network_client->start_client(server_addr.host, server_addr.port);
    if (!result) {
        impl_->set_state(connection_state::disconnected);
        return unexpected{error{error_code::internal_error,
                               "Failed to connect: " + result.error().message}};
    }
#endif

    impl_->set_state(connection_state::connected);
    return {};
}

auto file_transfer_client::disconnect() -> result<void> {
    if (impl_->current_state != connection_state::connected) {
        return unexpected{error{error_code::not_initialized,
                               "Client is not connected"}};
    }

#ifdef BUILD_WITH_NETWORK_SYSTEM
    auto result = impl_->network_client->stop_client();
    if (!result) {
        return unexpected{error{error_code::internal_error,
                               "Failed to disconnect: " + result.error().message}};
    }
#endif

    impl_->set_state(connection_state::disconnected);
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
    [[maybe_unused]] const std::string& remote_name,
    [[maybe_unused]] const upload_options& options) -> result<transfer_handle> {

    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized,
                               "Client is not connected"}};
    }

    if (!std::filesystem::exists(local_path)) {
        return unexpected{error{error_code::file_not_found,
                               "Local file not found: " + local_path.string()}};
    }

    auto transfer_id = impl_->next_transfer_id++;
    transfer_handle handle{transfer_id};

    {
        std::lock_guard lock(impl_->transfers_mutex);
        impl_->active_transfers[transfer_id] = handle;
    }

    // TODO: Implement actual file upload logic
    // For now, just return the handle

    return handle;
}

auto file_transfer_client::download_file(
    const std::string& remote_name,
    const std::filesystem::path& local_path,
    const download_options& options) -> result<transfer_handle> {

    // Validate connection state
    if (!is_connected()) {
        return unexpected{error{error_code::not_initialized,
                               "Client is not connected"}};
    }

    // Validate remote filename
    if (remote_name.empty()) {
        return unexpected{error{error_code::invalid_file_path,
                               "Remote filename is empty"}};
    }

    // Validate local path
    if (local_path.empty()) {
        return unexpected{error{error_code::invalid_file_path,
                               "Local path is empty"}};
    }

    // Check if file already exists (unless overwrite is enabled)
    if (!options.overwrite && std::filesystem::exists(local_path)) {
        return unexpected{error{error_code::file_already_exists,
                               "File already exists: " + local_path.string()}};
    }

    // Ensure parent directory exists
    auto parent_path = local_path.parent_path();
    if (!parent_path.empty() && !std::filesystem::exists(parent_path)) {
        std::error_code ec;
        std::filesystem::create_directories(parent_path, ec);
        if (ec) {
            return unexpected{error{error_code::file_access_denied,
                                   "Cannot create directory: " + parent_path.string()}};
        }
    }

    // Generate transfer ID and handle
    auto handle_id = impl_->next_transfer_id++;
    transfer_handle handle{handle_id};

    // Create download context
    auto ctx = std::make_unique<download_context>();
    ctx->tid = transfer_id::generate();
    ctx->remote_name = remote_name;
    ctx->local_path = local_path;
    ctx->options = options;
    ctx->state = download_state::initializing;
    ctx->start_time = std::chrono::steady_clock::now();

    // Create temp file path
    ctx->temp_path = local_path;
    ctx->temp_path += ".tmp";

    // Register active transfer
    {
        std::lock_guard lock(impl_->transfers_mutex);
        impl_->active_transfers[handle_id] = handle;
    }

    // Store download context
    {
        std::lock_guard lock(impl_->download_mutex);
        impl_->download_contexts[handle_id] = std::move(ctx);
    }

#ifdef BUILD_WITH_NETWORK_SYSTEM
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
#endif

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
            return unexpected{error{error_code::not_initialized,
                                   "Download context not found"}};
        }
        ctx = it->second.get();
    }

    std::lock_guard ctx_lock(ctx->mutex);
    ctx->state = download_state::verifying;

    // Verify SHA-256 hash if enabled
    if (ctx->options.verify_hash && !ctx->expected_sha256.empty()) {
        if (!checksum::verify_sha256(ctx->temp_path, ctx->expected_sha256)) {
            ctx->state = download_state::failed;
            ctx->error_message = "SHA-256 hash mismatch";

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

    // Remove from active transfers
    {
        std::lock_guard lock(impl_->transfers_mutex);
        impl_->active_transfers.erase(handle_id);
    }

    return {};
}

auto file_transfer_client::cancel_download(uint64_t handle_id) -> result<void> {
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

    {
        std::lock_guard ctx_lock(ctx->mutex);
        ctx->cancelled = true;
        ctx->state = download_state::cancelled;
        ctx->cv.notify_all();
    }

    // Clean up temp file
    std::error_code ec;
    std::filesystem::remove(ctx->temp_path, ec);

    // Remove from active transfers
    {
        std::lock_guard lock(impl_->transfers_mutex);
        impl_->active_transfers.erase(handle_id);
    }

    // Remove download context
    {
        std::lock_guard lock(impl_->download_mutex);
        impl_->download_contexts.erase(handle_id);
    }

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
    std::lock_guard transfers_lock(impl_->transfers_mutex);
    stats.active_transfers = impl_->active_transfers.size();
    return stats;
}

auto file_transfer_client::get_compression_stats() const -> compression_statistics {
    std::lock_guard lock(impl_->stats_mutex);
    return impl_->compression_stats;
}

auto file_transfer_client::config() const -> const client_config& {
    return impl_->config;
}

}  // namespace kcenon::file_transfer
