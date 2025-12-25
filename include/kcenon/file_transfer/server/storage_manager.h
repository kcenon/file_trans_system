/**
 * @file storage_manager.h
 * @brief Unified storage manager with local and cloud backend support
 */

#ifndef KCENON_FILE_TRANSFER_SERVER_STORAGE_MANAGER_H
#define KCENON_FILE_TRANSFER_SERVER_STORAGE_MANAGER_H

#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "kcenon/file_transfer/cloud/cloud_storage_interface.h"
#include "kcenon/file_transfer/core/types.h"

namespace kcenon::file_transfer {

/**
 * @brief Storage backend type enumeration
 */
enum class storage_backend_type {
    local,      ///< Local filesystem storage
    cloud_s3,   ///< AWS S3 or S3-compatible storage
    cloud_azure,///< Azure Blob Storage
    cloud_gcs   ///< Google Cloud Storage
};

/**
 * @brief Convert storage_backend_type to string
 */
[[nodiscard]] constexpr auto to_string(storage_backend_type type) -> const char* {
    switch (type) {
        case storage_backend_type::local: return "local";
        case storage_backend_type::cloud_s3: return "cloud_s3";
        case storage_backend_type::cloud_azure: return "cloud_azure";
        case storage_backend_type::cloud_gcs: return "cloud_gcs";
        default: return "unknown";
    }
}

/**
 * @brief Storage tier enumeration for tiered storage
 */
enum class storage_tier {
    hot,        ///< Frequently accessed data (local or high-performance cloud)
    warm,       ///< Occasionally accessed data
    cold,       ///< Rarely accessed data (archive storage)
    archive     ///< Long-term archive storage
};

/**
 * @brief Convert storage_tier to string
 */
[[nodiscard]] constexpr auto to_string(storage_tier tier) -> const char* {
    switch (tier) {
        case storage_tier::hot: return "hot";
        case storage_tier::warm: return "warm";
        case storage_tier::cold: return "cold";
        case storage_tier::archive: return "archive";
        default: return "unknown";
    }
}

/**
 * @brief Storage operation type
 */
enum class storage_operation {
    store,      ///< Store file
    retrieve,   ///< Retrieve file
    remove,     ///< Delete file
    list,       ///< List files
    metadata    ///< Get metadata
};

/**
 * @brief Stored object metadata
 */
struct stored_object_metadata {
    /// Object key/filename
    std::string key;

    /// Object size in bytes
    uint64_t size = 0;

    /// Last modified timestamp
    std::chrono::system_clock::time_point last_modified;

    /// Content hash (SHA256)
    std::optional<std::string> content_hash;

    /// Storage backend where object is stored
    storage_backend_type backend = storage_backend_type::local;

    /// Storage tier
    storage_tier tier = storage_tier::hot;

    /// ETag (for cloud objects)
    std::optional<std::string> etag;

    /// Content type
    std::optional<std::string> content_type;

    /// Custom metadata
    std::vector<std::pair<std::string, std::string>> custom_metadata;

    /// Access count (for auto-tiering)
    uint64_t access_count = 0;

    /// Last accessed timestamp
    std::optional<std::chrono::system_clock::time_point> last_accessed;
};

/**
 * @brief Store operation options
 */
struct store_options {
    /// Target storage tier
    storage_tier tier = storage_tier::hot;

    /// Content type
    std::optional<std::string> content_type;

    /// Custom metadata
    std::vector<std::pair<std::string, std::string>> custom_metadata;

    /// Content hash (SHA256) for verification
    std::optional<std::string> content_hash;

    /// Overwrite if exists
    bool overwrite = false;

    /// Cloud storage class (e.g., "STANDARD", "STANDARD_IA", "GLACIER")
    std::optional<std::string> storage_class;
};

/**
 * @brief Retrieve operation options
 */
struct retrieve_options {
    /// Update last accessed timestamp
    bool update_access_time = true;

    /// Verify content hash
    bool verify_hash = false;

    /// Expected content hash
    std::optional<std::string> expected_hash;
};

/**
 * @brief List operation options
 */
struct list_storage_options {
    /// Prefix to filter objects
    std::optional<std::string> prefix;

    /// Maximum results
    std::size_t max_results = 1000;

    /// Include only specific tier
    std::optional<storage_tier> tier_filter;

    /// Include only specific backend
    std::optional<storage_backend_type> backend_filter;

    /// Continuation token for pagination
    std::optional<std::string> continuation_token;
};

/**
 * @brief List operation result
 */
struct list_storage_result {
    /// Objects in the result
    std::vector<stored_object_metadata> objects;

    /// Is the result truncated
    bool is_truncated = false;

    /// Continuation token for pagination
    std::optional<std::string> continuation_token;

    /// Total count (if available)
    std::optional<uint64_t> total_count;
};

/**
 * @brief Store operation result
 */
struct store_result {
    /// Object key
    std::string key;

    /// Bytes stored
    uint64_t bytes_stored = 0;

    /// Backend used
    storage_backend_type backend = storage_backend_type::local;

    /// Storage tier
    storage_tier tier = storage_tier::hot;

    /// ETag (for cloud storage)
    std::optional<std::string> etag;

    /// Duration of operation
    std::chrono::milliseconds duration{0};
};

/**
 * @brief Retrieve operation result
 */
struct retrieve_result {
    /// Object key
    std::string key;

    /// Bytes retrieved
    uint64_t bytes_retrieved = 0;

    /// Backend source
    storage_backend_type backend = storage_backend_type::local;

    /// Object metadata
    stored_object_metadata metadata;

    /// Duration of operation
    std::chrono::milliseconds duration{0};
};

/**
 * @brief Storage operation progress
 */
struct storage_progress {
    /// Operation type
    storage_operation operation = storage_operation::store;

    /// Object key
    std::string key;

    /// Bytes transferred
    uint64_t bytes_transferred = 0;

    /// Total bytes
    uint64_t total_bytes = 0;

    /// Current backend
    storage_backend_type backend = storage_backend_type::local;

    [[nodiscard]] auto percentage() const -> double {
        if (total_bytes == 0) return 0.0;
        return static_cast<double>(bytes_transferred) /
               static_cast<double>(total_bytes) * 100.0;
    }
};

/**
 * @brief Storage manager statistics
 */
struct storage_manager_statistics {
    /// Bytes stored total
    uint64_t bytes_stored = 0;

    /// Bytes retrieved total
    uint64_t bytes_retrieved = 0;

    /// Store operations count
    uint64_t store_count = 0;

    /// Retrieve operations count
    uint64_t retrieve_count = 0;

    /// Delete operations count
    uint64_t delete_count = 0;

    /// Error count
    uint64_t error_count = 0;

    /// Files on local storage
    uint64_t local_file_count = 0;

    /// Bytes on local storage
    uint64_t local_bytes = 0;

    /// Files on cloud storage
    uint64_t cloud_file_count = 0;

    /// Bytes on cloud storage
    uint64_t cloud_bytes = 0;

    /// Tier operations count
    uint64_t tier_change_count = 0;
};

/**
 * @brief Storage backend interface
 *
 * Abstract interface for storage backends (local filesystem, cloud storage).
 */
class storage_backend {
public:
    virtual ~storage_backend() = default;

    // Non-copyable
    storage_backend(const storage_backend&) = delete;
    auto operator=(const storage_backend&) -> storage_backend& = delete;

    // Movable
    storage_backend(storage_backend&&) noexcept = default;
    auto operator=(storage_backend&&) noexcept -> storage_backend& = default;

    /**
     * @brief Get backend type
     */
    [[nodiscard]] virtual auto type() const -> storage_backend_type = 0;

    /**
     * @brief Get backend name
     */
    [[nodiscard]] virtual auto name() const -> std::string_view = 0;

    /**
     * @brief Check if backend is available
     */
    [[nodiscard]] virtual auto is_available() const -> bool = 0;

    /**
     * @brief Connect/initialize backend
     */
    [[nodiscard]] virtual auto connect() -> result<void> = 0;

    /**
     * @brief Disconnect/cleanup backend
     */
    [[nodiscard]] virtual auto disconnect() -> result<void> = 0;

    // ========================================================================
    // Storage Operations
    // ========================================================================

    /**
     * @brief Store data
     * @param key Object key
     * @param data Data to store
     * @param options Store options
     * @return Store result or error
     */
    [[nodiscard]] virtual auto store(
        const std::string& key,
        std::span<const std::byte> data,
        const store_options& options = {}) -> result<store_result> = 0;

    /**
     * @brief Store file
     * @param key Object key
     * @param file_path Local file path
     * @param options Store options
     * @return Store result or error
     */
    [[nodiscard]] virtual auto store_file(
        const std::string& key,
        const std::filesystem::path& file_path,
        const store_options& options = {}) -> result<store_result> = 0;

    /**
     * @brief Retrieve data
     * @param key Object key
     * @param options Retrieve options
     * @return Data or error
     */
    [[nodiscard]] virtual auto retrieve(
        const std::string& key,
        const retrieve_options& options = {}) -> result<std::vector<std::byte>> = 0;

    /**
     * @brief Retrieve file
     * @param key Object key
     * @param file_path Local file path to save to
     * @param options Retrieve options
     * @return Retrieve result or error
     */
    [[nodiscard]] virtual auto retrieve_file(
        const std::string& key,
        const std::filesystem::path& file_path,
        const retrieve_options& options = {}) -> result<retrieve_result> = 0;

    /**
     * @brief Remove object
     * @param key Object key
     * @return Success or error
     */
    [[nodiscard]] virtual auto remove(const std::string& key) -> result<void> = 0;

    /**
     * @brief Check if object exists
     * @param key Object key
     * @return true if exists
     */
    [[nodiscard]] virtual auto exists(const std::string& key) -> result<bool> = 0;

    /**
     * @brief Get object metadata
     * @param key Object key
     * @return Metadata or error
     */
    [[nodiscard]] virtual auto get_metadata(
        const std::string& key) -> result<stored_object_metadata> = 0;

    /**
     * @brief List objects
     * @param options List options
     * @return List result or error
     */
    [[nodiscard]] virtual auto list(
        const list_storage_options& options = {}) -> result<list_storage_result> = 0;

    // ========================================================================
    // Async Operations
    // ========================================================================

    /**
     * @brief Store data asynchronously
     */
    [[nodiscard]] virtual auto store_async(
        const std::string& key,
        std::span<const std::byte> data,
        const store_options& options = {}) -> std::future<result<store_result>> = 0;

    /**
     * @brief Store file asynchronously
     */
    [[nodiscard]] virtual auto store_file_async(
        const std::string& key,
        const std::filesystem::path& file_path,
        const store_options& options = {}) -> std::future<result<store_result>> = 0;

    /**
     * @brief Retrieve data asynchronously
     */
    [[nodiscard]] virtual auto retrieve_async(
        const std::string& key,
        const retrieve_options& options = {}) -> std::future<result<std::vector<std::byte>>> = 0;

    /**
     * @brief Retrieve file asynchronously
     */
    [[nodiscard]] virtual auto retrieve_file_async(
        const std::string& key,
        const std::filesystem::path& file_path,
        const retrieve_options& options = {}) -> std::future<result<retrieve_result>> = 0;

    // ========================================================================
    // Progress Callbacks
    // ========================================================================

    /**
     * @brief Set progress callback
     */
    virtual void on_progress(std::function<void(const storage_progress&)> callback) = 0;

protected:
    storage_backend() = default;
};

/**
 * @brief Local filesystem storage backend
 */
class local_storage_backend : public storage_backend {
public:
    /**
     * @brief Create local storage backend
     * @param base_path Base directory for storage
     * @return Backend instance or nullptr
     */
    [[nodiscard]] static auto create(
        const std::filesystem::path& base_path) -> std::unique_ptr<local_storage_backend>;

    [[nodiscard]] auto type() const -> storage_backend_type override;
    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto is_available() const -> bool override;

    [[nodiscard]] auto connect() -> result<void> override;
    [[nodiscard]] auto disconnect() -> result<void> override;

    [[nodiscard]] auto store(
        const std::string& key,
        std::span<const std::byte> data,
        const store_options& options = {}) -> result<store_result> override;

    [[nodiscard]] auto store_file(
        const std::string& key,
        const std::filesystem::path& file_path,
        const store_options& options = {}) -> result<store_result> override;

    [[nodiscard]] auto retrieve(
        const std::string& key,
        const retrieve_options& options = {}) -> result<std::vector<std::byte>> override;

    [[nodiscard]] auto retrieve_file(
        const std::string& key,
        const std::filesystem::path& file_path,
        const retrieve_options& options = {}) -> result<retrieve_result> override;

    [[nodiscard]] auto remove(const std::string& key) -> result<void> override;
    [[nodiscard]] auto exists(const std::string& key) -> result<bool> override;
    [[nodiscard]] auto get_metadata(
        const std::string& key) -> result<stored_object_metadata> override;
    [[nodiscard]] auto list(
        const list_storage_options& options = {}) -> result<list_storage_result> override;

    [[nodiscard]] auto store_async(
        const std::string& key,
        std::span<const std::byte> data,
        const store_options& options = {}) -> std::future<result<store_result>> override;

    [[nodiscard]] auto store_file_async(
        const std::string& key,
        const std::filesystem::path& file_path,
        const store_options& options = {}) -> std::future<result<store_result>> override;

    [[nodiscard]] auto retrieve_async(
        const std::string& key,
        const retrieve_options& options = {}) -> std::future<result<std::vector<std::byte>>> override;

    [[nodiscard]] auto retrieve_file_async(
        const std::string& key,
        const std::filesystem::path& file_path,
        const retrieve_options& options = {}) -> std::future<result<retrieve_result>> override;

    void on_progress(std::function<void(const storage_progress&)> callback) override;

    /**
     * @brief Get base path
     */
    [[nodiscard]] auto base_path() const -> const std::filesystem::path&;

    /**
     * @brief Get full path for a key
     */
    [[nodiscard]] auto full_path(const std::string& key) const -> std::filesystem::path;

private:
    explicit local_storage_backend(const std::filesystem::path& base_path);

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief Cloud storage backend adapter
 *
 * Wraps cloud_storage_interface to provide storage_backend interface.
 */
class cloud_storage_backend : public storage_backend {
public:
    /**
     * @brief Create cloud storage backend
     * @param storage Cloud storage interface instance
     * @param backend_type Backend type
     * @return Backend instance or nullptr
     */
    [[nodiscard]] static auto create(
        std::unique_ptr<cloud_storage_interface> storage,
        storage_backend_type backend_type = storage_backend_type::cloud_s3)
        -> std::unique_ptr<cloud_storage_backend>;

    [[nodiscard]] auto type() const -> storage_backend_type override;
    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto is_available() const -> bool override;

    [[nodiscard]] auto connect() -> result<void> override;
    [[nodiscard]] auto disconnect() -> result<void> override;

    [[nodiscard]] auto store(
        const std::string& key,
        std::span<const std::byte> data,
        const store_options& options = {}) -> result<store_result> override;

    [[nodiscard]] auto store_file(
        const std::string& key,
        const std::filesystem::path& file_path,
        const store_options& options = {}) -> result<store_result> override;

    [[nodiscard]] auto retrieve(
        const std::string& key,
        const retrieve_options& options = {}) -> result<std::vector<std::byte>> override;

    [[nodiscard]] auto retrieve_file(
        const std::string& key,
        const std::filesystem::path& file_path,
        const retrieve_options& options = {}) -> result<retrieve_result> override;

    [[nodiscard]] auto remove(const std::string& key) -> result<void> override;
    [[nodiscard]] auto exists(const std::string& key) -> result<bool> override;
    [[nodiscard]] auto get_metadata(
        const std::string& key) -> result<stored_object_metadata> override;
    [[nodiscard]] auto list(
        const list_storage_options& options = {}) -> result<list_storage_result> override;

    [[nodiscard]] auto store_async(
        const std::string& key,
        std::span<const std::byte> data,
        const store_options& options = {}) -> std::future<result<store_result>> override;

    [[nodiscard]] auto store_file_async(
        const std::string& key,
        const std::filesystem::path& file_path,
        const store_options& options = {}) -> std::future<result<store_result>> override;

    [[nodiscard]] auto retrieve_async(
        const std::string& key,
        const retrieve_options& options = {}) -> std::future<result<std::vector<std::byte>>> override;

    [[nodiscard]] auto retrieve_file_async(
        const std::string& key,
        const std::filesystem::path& file_path,
        const retrieve_options& options = {}) -> std::future<result<retrieve_result>> override;

    void on_progress(std::function<void(const storage_progress&)> callback) override;

    /**
     * @brief Get underlying cloud storage interface
     */
    [[nodiscard]] auto cloud_storage() -> cloud_storage_interface&;
    [[nodiscard]] auto cloud_storage() const -> const cloud_storage_interface&;

private:
    cloud_storage_backend(
        std::unique_ptr<cloud_storage_interface> storage,
        storage_backend_type backend_type);

    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief Storage manager configuration
 */
struct storage_manager_config {
    /// Primary storage backend
    std::shared_ptr<storage_backend> primary_backend;

    /// Secondary/fallback storage backend
    std::shared_ptr<storage_backend> secondary_backend;

    /// Enable hybrid storage (store to both backends)
    bool hybrid_storage = false;

    /// Use secondary as read fallback
    bool fallback_reads = true;

    /// Replicate writes to secondary
    bool replicate_writes = false;

    /// Local cache for cloud objects
    std::optional<std::filesystem::path> cache_directory;

    /// Maximum cache size in bytes
    uint64_t max_cache_size = 1ULL * 1024 * 1024 * 1024;  // 1GB

    /// Enable access tracking for auto-tiering
    bool track_access = true;

    [[nodiscard]] auto is_valid() const -> bool {
        return primary_backend != nullptr;
    }
};

/**
 * @brief Unified storage manager
 *
 * Manages multiple storage backends with hybrid storage support.
 *
 * @code
 * // Create storage manager with local and cloud backends
 * auto local = local_storage_backend::create("/data/storage");
 * auto cloud = cloud_storage_backend::create(s3_storage_instance);
 *
 * storage_manager_config config;
 * config.primary_backend = std::move(local);
 * config.secondary_backend = std::move(cloud);
 * config.hybrid_storage = true;
 *
 * auto manager = storage_manager::create(config);
 * if (manager) {
 *     auto result = manager->store("file.txt", data);
 * }
 * @endcode
 */
class storage_manager {
public:
    /**
     * @brief Create storage manager
     * @param config Manager configuration
     * @return Manager instance or nullptr
     */
    [[nodiscard]] static auto create(
        const storage_manager_config& config) -> std::unique_ptr<storage_manager>;

    // Non-copyable, movable
    storage_manager(const storage_manager&) = delete;
    auto operator=(const storage_manager&) -> storage_manager& = delete;
    storage_manager(storage_manager&&) noexcept;
    auto operator=(storage_manager&&) noexcept -> storage_manager&;
    ~storage_manager();

    /**
     * @brief Initialize storage manager
     */
    [[nodiscard]] auto initialize() -> result<void>;

    /**
     * @brief Shutdown storage manager
     */
    [[nodiscard]] auto shutdown() -> result<void>;

    // ========================================================================
    // Storage Operations
    // ========================================================================

    /**
     * @brief Store data
     * @param key Object key
     * @param data Data to store
     * @param options Store options
     * @return Store result or error
     */
    [[nodiscard]] auto store(
        const std::string& key,
        std::span<const std::byte> data,
        const store_options& options = {}) -> result<store_result>;

    /**
     * @brief Store file
     * @param key Object key
     * @param file_path Local file path
     * @param options Store options
     * @return Store result or error
     */
    [[nodiscard]] auto store_file(
        const std::string& key,
        const std::filesystem::path& file_path,
        const store_options& options = {}) -> result<store_result>;

    /**
     * @brief Retrieve data
     * @param key Object key
     * @param options Retrieve options
     * @return Data or error
     */
    [[nodiscard]] auto retrieve(
        const std::string& key,
        const retrieve_options& options = {}) -> result<std::vector<std::byte>>;

    /**
     * @brief Retrieve file
     * @param key Object key
     * @param file_path Local file path to save to
     * @param options Retrieve options
     * @return Retrieve result or error
     */
    [[nodiscard]] auto retrieve_file(
        const std::string& key,
        const std::filesystem::path& file_path,
        const retrieve_options& options = {}) -> result<retrieve_result>;

    /**
     * @brief Remove object
     * @param key Object key
     * @return Success or error
     */
    [[nodiscard]] auto remove(const std::string& key) -> result<void>;

    /**
     * @brief Check if object exists
     * @param key Object key
     * @return true if exists in any backend
     */
    [[nodiscard]] auto exists(const std::string& key) -> result<bool>;

    /**
     * @brief Get object metadata
     * @param key Object key
     * @return Metadata or error
     */
    [[nodiscard]] auto get_metadata(
        const std::string& key) -> result<stored_object_metadata>;

    /**
     * @brief List objects
     * @param options List options
     * @return List result or error
     */
    [[nodiscard]] auto list(
        const list_storage_options& options = {}) -> result<list_storage_result>;

    // ========================================================================
    // Async Operations
    // ========================================================================

    /**
     * @brief Store data asynchronously
     */
    [[nodiscard]] auto store_async(
        const std::string& key,
        std::span<const std::byte> data,
        const store_options& options = {}) -> std::future<result<store_result>>;

    /**
     * @brief Store file asynchronously
     */
    [[nodiscard]] auto store_file_async(
        const std::string& key,
        const std::filesystem::path& file_path,
        const store_options& options = {}) -> std::future<result<store_result>>;

    /**
     * @brief Retrieve data asynchronously
     */
    [[nodiscard]] auto retrieve_async(
        const std::string& key,
        const retrieve_options& options = {}) -> std::future<result<std::vector<std::byte>>>;

    /**
     * @brief Retrieve file asynchronously
     */
    [[nodiscard]] auto retrieve_file_async(
        const std::string& key,
        const std::filesystem::path& file_path,
        const retrieve_options& options = {}) -> std::future<result<retrieve_result>>;

    // ========================================================================
    // Tiering Operations
    // ========================================================================

    /**
     * @brief Change object storage tier
     * @param key Object key
     * @param target_tier Target storage tier
     * @return Success or error
     */
    [[nodiscard]] auto change_tier(
        const std::string& key,
        storage_tier target_tier) -> result<void>;

    /**
     * @brief Copy object between backends
     * @param key Object key
     * @param source Source backend
     * @param destination Destination backend
     * @return Success or error
     */
    [[nodiscard]] auto copy_between_backends(
        const std::string& key,
        storage_backend_type source,
        storage_backend_type destination) -> result<void>;

    // ========================================================================
    // Statistics and Configuration
    // ========================================================================

    /**
     * @brief Get storage manager statistics
     */
    [[nodiscard]] auto get_statistics() const -> storage_manager_statistics;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

    /**
     * @brief Get configuration
     */
    [[nodiscard]] auto config() const -> const storage_manager_config&;

    /**
     * @brief Get primary backend
     */
    [[nodiscard]] auto primary_backend() -> storage_backend&;
    [[nodiscard]] auto primary_backend() const -> const storage_backend&;

    /**
     * @brief Get secondary backend (if configured)
     */
    [[nodiscard]] auto secondary_backend() -> storage_backend*;
    [[nodiscard]] auto secondary_backend() const -> const storage_backend*;

    // ========================================================================
    // Callbacks
    // ========================================================================

    /**
     * @brief Set progress callback
     */
    void on_progress(std::function<void(const storage_progress&)> callback);

    /**
     * @brief Set error callback
     */
    void on_error(std::function<void(const std::string& key, const error&)> callback);

private:
    explicit storage_manager(const storage_manager_config& config);

    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_SERVER_STORAGE_MANAGER_H
