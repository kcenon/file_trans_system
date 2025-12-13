/**
 * @file quota_manager.h
 * @brief Server storage quota management
 */

#ifndef KCENON_FILE_TRANSFER_SERVER_QUOTA_MANAGER_H
#define KCENON_FILE_TRANSFER_SERVER_QUOTA_MANAGER_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "kcenon/file_transfer/core/types.h"

namespace kcenon::file_transfer {

/**
 * @brief Storage quota usage information
 */
struct quota_usage {
    uint64_t total_quota = 0;
    uint64_t used_bytes = 0;
    uint64_t available_bytes = 0;
    double usage_percent = 0.0;
    std::size_t file_count = 0;

    /**
     * @brief Check if quota is exceeded
     * @return true if used_bytes >= total_quota
     */
    [[nodiscard]] auto is_exceeded() const -> bool {
        return total_quota > 0 && used_bytes >= total_quota;
    }

    /**
     * @brief Check if a specific threshold is reached
     * @param threshold Threshold percentage (0.0 to 100.0)
     * @return true if usage_percent >= threshold
     */
    [[nodiscard]] auto is_threshold_reached(double threshold) const -> bool {
        return usage_percent >= threshold;
    }
};

/**
 * @brief Warning threshold configuration
 */
struct warning_threshold {
    double percentage;
    bool triggered = false;

    explicit warning_threshold(double pct) : percentage(pct) {}
};

/**
 * @brief Cleanup policy for automatic storage cleanup
 */
struct cleanup_policy {
    bool enabled = false;
    double trigger_threshold = 90.0;      // Start cleanup when usage reaches this %
    double target_threshold = 80.0;       // Cleanup until usage drops to this %
    bool delete_oldest_first = true;      // Delete oldest files first
    std::chrono::hours min_file_age{0};   // Only delete files older than this
    std::vector<std::string> exclusions;  // Patterns to exclude from cleanup
};

/**
 * @brief Quota manager for server storage
 *
 * Manages storage quota, tracks usage, and provides warnings when
 * thresholds are reached. Optionally supports automatic cleanup policies.
 *
 * @code
 * auto manager_result = quota_manager::create(storage_path, 100ULL * 1024 * 1024 * 1024);
 * if (manager_result.has_value()) {
 *     auto& manager = manager_result.value();
 *     manager.set_warning_thresholds({80.0, 90.0, 95.0});
 *     manager.on_quota_warning([](const quota_usage& usage) {
 *         std::cout << "Warning: " << usage.usage_percent << "% used\n";
 *     });
 * }
 * @endcode
 */
class quota_manager {
public:
    /**
     * @brief Create a quota manager
     * @param storage_path Path to storage directory
     * @param total_quota Total quota in bytes (0 = unlimited)
     * @return Result containing the manager or an error
     */
    [[nodiscard]] static auto create(
        const std::filesystem::path& storage_path,
        uint64_t total_quota = 0) -> result<quota_manager>;

    // Non-copyable, movable
    quota_manager(const quota_manager&) = delete;
    auto operator=(const quota_manager&) -> quota_manager& = delete;
    quota_manager(quota_manager&&) noexcept;
    auto operator=(quota_manager&&) noexcept -> quota_manager&;
    ~quota_manager();

    /**
     * @brief Set total quota
     * @param bytes Total quota in bytes (0 = unlimited)
     */
    auto set_total_quota(uint64_t bytes) -> void;

    /**
     * @brief Get total quota
     * @return Total quota in bytes
     */
    [[nodiscard]] auto get_total_quota() const -> uint64_t;

    /**
     * @brief Set maximum file size limit
     * @param bytes Maximum size per file (0 = unlimited)
     */
    auto set_max_file_size(uint64_t bytes) -> void;

    /**
     * @brief Get maximum file size limit
     * @return Maximum file size in bytes
     */
    [[nodiscard]] auto get_max_file_size() const -> uint64_t;

    /**
     * @brief Check if storage can accommodate required bytes
     * @param required_bytes Number of bytes to store
     * @return Result<void> on success, error if quota would be exceeded
     */
    [[nodiscard]] auto check_quota(uint64_t required_bytes) -> result<void>;

    /**
     * @brief Check if a file size is within limits
     * @param file_size Size of the file in bytes
     * @return Result<void> on success, error if file is too large
     */
    [[nodiscard]] auto check_file_size(uint64_t file_size) -> result<void>;

    /**
     * @brief Get available space
     * @return Available bytes within quota
     */
    [[nodiscard]] auto get_available_space() const -> uint64_t;

    /**
     * @brief Get current quota usage
     * @return Current usage information
     */
    [[nodiscard]] auto get_usage() const -> quota_usage;

    /**
     * @brief Refresh usage statistics from filesystem
     *
     * Scans the storage directory and updates usage statistics.
     * This is called automatically but can be triggered manually.
     */
    auto refresh_usage() -> void;

    /**
     * @brief Record bytes being added to storage
     * @param bytes Number of bytes added
     */
    auto record_bytes_added(uint64_t bytes) -> void;

    /**
     * @brief Record bytes being removed from storage
     * @param bytes Number of bytes removed
     */
    auto record_bytes_removed(uint64_t bytes) -> void;

    /**
     * @brief Record a file being added
     */
    auto record_file_added() -> void;

    /**
     * @brief Record a file being removed
     */
    auto record_file_removed() -> void;

    // Warning thresholds

    /**
     * @brief Set warning thresholds
     * @param percentages List of percentage thresholds (e.g., {80.0, 90.0, 95.0})
     */
    auto set_warning_thresholds(const std::vector<double>& percentages) -> void;

    /**
     * @brief Get current warning thresholds
     * @return List of thresholds with their triggered state
     */
    [[nodiscard]] auto get_warning_thresholds() const -> std::vector<warning_threshold>;

    /**
     * @brief Reset all threshold triggers
     *
     * Call this after taking action on warnings to allow re-triggering.
     */
    auto reset_threshold_triggers() -> void;

    // Callbacks

    /**
     * @brief Set callback for quota warning events
     * @param callback Function called when a warning threshold is reached
     */
    auto on_quota_warning(std::function<void(const quota_usage&)> callback) -> void;

    /**
     * @brief Set callback for quota exceeded events
     * @param callback Function called when quota is exceeded
     */
    auto on_quota_exceeded(std::function<void(const quota_usage&)> callback) -> void;

    // Cleanup policy

    /**
     * @brief Set cleanup policy
     * @param policy Cleanup policy configuration
     */
    auto set_cleanup_policy(const cleanup_policy& policy) -> void;

    /**
     * @brief Get current cleanup policy
     * @return Current cleanup policy
     */
    [[nodiscard]] auto get_cleanup_policy() const -> cleanup_policy;

    /**
     * @brief Execute cleanup according to policy
     * @return Number of bytes freed
     */
    auto execute_cleanup() -> uint64_t;

    /**
     * @brief Check if cleanup should be executed
     * @return true if cleanup should run based on current usage and policy
     */
    [[nodiscard]] auto should_cleanup() const -> bool;

    // Storage path

    /**
     * @brief Get storage directory path
     * @return Path to storage directory
     */
    [[nodiscard]] auto storage_path() const -> const std::filesystem::path&;

private:
    explicit quota_manager(const std::filesystem::path& storage_path, uint64_t total_quota);

    auto check_thresholds() -> void;

    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_SERVER_QUOTA_MANAGER_H
