/**
 * @file quota_manager.cpp
 * @brief Server storage quota management implementation
 */

#include "kcenon/file_transfer/server/quota_manager.h"

#include "kcenon/file_transfer/core/logging.h"

#include <algorithm>
#include <mutex>
#include <shared_mutex>

namespace kcenon::file_transfer {

struct quota_manager::impl {
    std::filesystem::path storage_path;
    std::atomic<uint64_t> total_quota{0};
    std::atomic<uint64_t> max_file_size{0};

    mutable std::shared_mutex usage_mutex;
    quota_usage current_usage;

    mutable std::mutex thresholds_mutex;
    std::vector<warning_threshold> warning_thresholds;

    std::mutex callbacks_mutex;
    std::function<void(const quota_usage&)> warning_callback;
    std::function<void(const quota_usage&)> exceeded_callback;

    mutable std::mutex policy_mutex;
    cleanup_policy current_policy;

    explicit impl(std::filesystem::path path, uint64_t quota)
        : storage_path(std::move(path)), total_quota(quota) {
        // Set default warning thresholds
        warning_thresholds.emplace_back(80.0);
        warning_thresholds.emplace_back(90.0);
        warning_thresholds.emplace_back(95.0);
    }

    auto calculate_usage() -> quota_usage {
        quota_usage usage;
        usage.total_quota = total_quota.load();
        usage.used_bytes = 0;
        usage.file_count = 0;

        std::error_code ec;
        if (std::filesystem::exists(storage_path, ec) && !ec) {
            for (const auto& entry :
                 std::filesystem::directory_iterator(storage_path, ec)) {
                if (ec) break;
                if (entry.is_regular_file(ec) && !ec) {
                    auto size = entry.file_size(ec);
                    if (!ec) {
                        usage.used_bytes += size;
                        usage.file_count++;
                    }
                }
            }
        }

        if (usage.total_quota > 0) {
            usage.available_bytes = (usage.used_bytes < usage.total_quota)
                                        ? (usage.total_quota - usage.used_bytes)
                                        : 0;
            usage.usage_percent =
                static_cast<double>(usage.used_bytes) /
                static_cast<double>(usage.total_quota) * 100.0;
        } else {
            // Unlimited quota - try to get filesystem space
            auto space_info = std::filesystem::space(storage_path, ec);
            if (!ec) {
                usage.available_bytes = space_info.available;
            } else {
                usage.available_bytes = UINT64_MAX;
            }
            usage.usage_percent = 0.0;
        }

        return usage;
    }

    auto get_files_sorted_by_time() -> std::vector<std::filesystem::path> {
        std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> files;

        std::error_code ec;
        for (const auto& entry :
             std::filesystem::directory_iterator(storage_path, ec)) {
            if (ec) break;
            if (entry.is_regular_file(ec) && !ec) {
                auto last_write = entry.last_write_time(ec);
                if (!ec) {
                    files.emplace_back(entry.path(), last_write);
                }
            }
        }

        // Sort by last write time (oldest first for deletion)
        std::sort(files.begin(), files.end(),
                  [](const auto& a, const auto& b) {
                      return a.second < b.second;
                  });

        std::vector<std::filesystem::path> result;
        result.reserve(files.size());
        for (auto& [path, _] : files) {
            result.push_back(std::move(path));
        }
        return result;
    }

    auto is_excluded(const std::filesystem::path& path) const -> bool {
        std::lock_guard lock(policy_mutex);
        auto filename = path.filename().string();
        for (const auto& pattern : current_policy.exclusions) {
            // Simple pattern matching - check if filename contains pattern
            if (filename.find(pattern) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    auto is_file_old_enough(const std::filesystem::path& path) const -> bool {
        std::lock_guard lock(policy_mutex);
        if (current_policy.min_file_age.count() == 0) {
            return true;  // No age requirement
        }

        std::error_code ec;
        auto last_write = std::filesystem::last_write_time(path, ec);
        if (ec) return false;

        auto now = std::filesystem::file_time_type::clock::now();
        auto age = std::chrono::duration_cast<std::chrono::hours>(now - last_write);
        return age >= current_policy.min_file_age;
    }
};

auto quota_manager::create(
    const std::filesystem::path& storage_path,
    uint64_t total_quota) -> result<quota_manager> {
    std::error_code ec;

    // Verify path exists or can be created
    if (!std::filesystem::exists(storage_path, ec)) {
        std::filesystem::create_directories(storage_path, ec);
        if (ec) {
            return unexpected{error{error_code::file_access_denied,
                                   "Failed to create storage directory: " + ec.message()}};
        }
    }

    if (!std::filesystem::is_directory(storage_path, ec) || ec) {
        return unexpected{error{error_code::invalid_file_path,
                               "Storage path is not a directory"}};
    }

    return quota_manager{storage_path, total_quota};
}

quota_manager::quota_manager(const std::filesystem::path& storage_path, uint64_t total_quota)
    : impl_(std::make_unique<impl>(storage_path, total_quota)) {
    // Initial usage calculation
    refresh_usage();
    FT_LOG_INFO(log_category::server,
        "Quota manager initialized: path=" + storage_path.string() +
        ", quota=" + std::to_string(total_quota) + " bytes");
}

quota_manager::quota_manager(quota_manager&&) noexcept = default;
auto quota_manager::operator=(quota_manager&&) noexcept -> quota_manager& = default;
quota_manager::~quota_manager() = default;

auto quota_manager::set_total_quota(uint64_t bytes) -> void {
    impl_->total_quota.store(bytes);
    refresh_usage();
    check_thresholds();
    FT_LOG_DEBUG(log_category::server,
        "Total quota set to " + std::to_string(bytes) + " bytes");
}

auto quota_manager::get_total_quota() const -> uint64_t {
    return impl_->total_quota.load();
}

auto quota_manager::set_max_file_size(uint64_t bytes) -> void {
    impl_->max_file_size.store(bytes);
    FT_LOG_DEBUG(log_category::server,
        "Max file size set to " + std::to_string(bytes) + " bytes");
}

auto quota_manager::get_max_file_size() const -> uint64_t {
    return impl_->max_file_size.load();
}

auto quota_manager::check_quota(uint64_t required_bytes) -> result<void> {
    auto usage = get_usage();

    if (usage.total_quota == 0) {
        return {};  // Unlimited quota
    }

    if (usage.used_bytes + required_bytes > usage.total_quota) {
        FT_LOG_WARN(log_category::server,
            "Quota check failed: required=" + std::to_string(required_bytes) +
            ", available=" + std::to_string(usage.available_bytes));
        return unexpected{error{error_code::quota_exceeded,
                               "Storage quota would be exceeded"}};
    }

    return {};
}

auto quota_manager::check_file_size(uint64_t file_size) -> result<void> {
    auto max_size = impl_->max_file_size.load();
    if (max_size > 0 && file_size > max_size) {
        FT_LOG_WARN(log_category::server,
            "File size check failed: size=" + std::to_string(file_size) +
            ", max=" + std::to_string(max_size));
        return unexpected{error{error_code::file_too_large,
                               "File size exceeds maximum allowed"}};
    }
    return {};
}

auto quota_manager::get_available_space() const -> uint64_t {
    std::shared_lock lock(impl_->usage_mutex);
    return impl_->current_usage.available_bytes;
}

auto quota_manager::get_usage() const -> quota_usage {
    std::shared_lock lock(impl_->usage_mutex);
    return impl_->current_usage;
}

auto quota_manager::refresh_usage() -> void {
    auto usage = impl_->calculate_usage();
    {
        std::unique_lock lock(impl_->usage_mutex);
        impl_->current_usage = usage;
    }
    check_thresholds();
}

auto quota_manager::record_bytes_added(uint64_t bytes) -> void {
    {
        std::unique_lock lock(impl_->usage_mutex);
        impl_->current_usage.used_bytes += bytes;
        auto total = impl_->total_quota.load();
        if (total > 0) {
            impl_->current_usage.available_bytes =
                (impl_->current_usage.used_bytes < total)
                    ? (total - impl_->current_usage.used_bytes)
                    : 0;
            impl_->current_usage.usage_percent =
                static_cast<double>(impl_->current_usage.used_bytes) /
                static_cast<double>(total) * 100.0;
        }
    }
    check_thresholds();
}

auto quota_manager::record_bytes_removed(uint64_t bytes) -> void {
    std::unique_lock lock(impl_->usage_mutex);
    if (bytes > impl_->current_usage.used_bytes) {
        impl_->current_usage.used_bytes = 0;
    } else {
        impl_->current_usage.used_bytes -= bytes;
    }
    auto total = impl_->total_quota.load();
    if (total > 0) {
        impl_->current_usage.available_bytes = total - impl_->current_usage.used_bytes;
        impl_->current_usage.usage_percent =
            static_cast<double>(impl_->current_usage.used_bytes) /
            static_cast<double>(total) * 100.0;
    }
}

auto quota_manager::record_file_added() -> void {
    std::unique_lock lock(impl_->usage_mutex);
    impl_->current_usage.file_count++;
}

auto quota_manager::record_file_removed() -> void {
    std::unique_lock lock(impl_->usage_mutex);
    if (impl_->current_usage.file_count > 0) {
        impl_->current_usage.file_count--;
    }
}

auto quota_manager::set_warning_thresholds(const std::vector<double>& percentages) -> void {
    std::lock_guard lock(impl_->thresholds_mutex);
    impl_->warning_thresholds.clear();
    for (double pct : percentages) {
        if (pct > 0.0 && pct <= 100.0) {
            impl_->warning_thresholds.emplace_back(pct);
        }
    }
    // Sort thresholds ascending
    std::sort(impl_->warning_thresholds.begin(), impl_->warning_thresholds.end(),
              [](const warning_threshold& a, const warning_threshold& b) {
                  return a.percentage < b.percentage;
              });
    FT_LOG_DEBUG(log_category::server,
        "Warning thresholds updated: " + std::to_string(percentages.size()) + " thresholds");
}

auto quota_manager::get_warning_thresholds() const -> std::vector<warning_threshold> {
    std::lock_guard lock(impl_->thresholds_mutex);
    return impl_->warning_thresholds;
}

auto quota_manager::reset_threshold_triggers() -> void {
    std::lock_guard lock(impl_->thresholds_mutex);
    for (auto& threshold : impl_->warning_thresholds) {
        threshold.triggered = false;
    }
}

auto quota_manager::on_quota_warning(std::function<void(const quota_usage&)> callback) -> void {
    std::lock_guard lock(impl_->callbacks_mutex);
    impl_->warning_callback = std::move(callback);
}

auto quota_manager::on_quota_exceeded(std::function<void(const quota_usage&)> callback) -> void {
    std::lock_guard lock(impl_->callbacks_mutex);
    impl_->exceeded_callback = std::move(callback);
}

auto quota_manager::set_cleanup_policy(const cleanup_policy& policy) -> void {
    std::lock_guard lock(impl_->policy_mutex);
    impl_->current_policy = policy;
    FT_LOG_INFO(log_category::server,
        "Cleanup policy updated: enabled=" + std::string(policy.enabled ? "true" : "false") +
        ", trigger=" + std::to_string(policy.trigger_threshold) + "%");
}

auto quota_manager::get_cleanup_policy() const -> cleanup_policy {
    std::lock_guard lock(impl_->policy_mutex);
    return impl_->current_policy;
}

auto quota_manager::execute_cleanup() -> uint64_t {
    cleanup_policy policy;
    {
        std::lock_guard lock(impl_->policy_mutex);
        policy = impl_->current_policy;
    }

    if (!policy.enabled) {
        return 0;
    }

    auto usage = get_usage();
    if (usage.usage_percent < policy.trigger_threshold) {
        return 0;  // No cleanup needed
    }

    FT_LOG_INFO(log_category::server,
        "Starting cleanup: current usage=" + std::to_string(usage.usage_percent) +
        "%, target=" + std::to_string(policy.target_threshold) + "%");

    uint64_t bytes_freed = 0;
    auto files = impl_->get_files_sorted_by_time();

    if (!policy.delete_oldest_first) {
        std::reverse(files.begin(), files.end());
    }

    for (const auto& file_path : files) {
        // Re-check usage
        auto current = get_usage();
        if (current.usage_percent <= policy.target_threshold) {
            break;  // Target reached
        }

        // Check exclusions
        if (impl_->is_excluded(file_path)) {
            continue;
        }

        // Check file age
        if (!impl_->is_file_old_enough(file_path)) {
            continue;
        }

        // Delete the file
        std::error_code ec;
        auto file_size = std::filesystem::file_size(file_path, ec);
        if (ec) continue;

        if (std::filesystem::remove(file_path, ec) && !ec) {
            bytes_freed += file_size;
            record_bytes_removed(file_size);
            record_file_removed();
            FT_LOG_DEBUG(log_category::server,
                "Cleanup deleted: " + file_path.filename().string() +
                " (" + std::to_string(file_size) + " bytes)");
        }
    }

    FT_LOG_INFO(log_category::server,
        "Cleanup completed: freed " + std::to_string(bytes_freed) + " bytes");

    return bytes_freed;
}

auto quota_manager::should_cleanup() const -> bool {
    cleanup_policy policy;
    {
        std::lock_guard lock(impl_->policy_mutex);
        policy = impl_->current_policy;
    }

    if (!policy.enabled) {
        return false;
    }

    auto usage = get_usage();
    return usage.usage_percent >= policy.trigger_threshold;
}

auto quota_manager::storage_path() const -> const std::filesystem::path& {
    return impl_->storage_path;
}

auto quota_manager::check_thresholds() -> void {
    auto usage = get_usage();

    // Check if quota is exceeded
    if (usage.is_exceeded()) {
        std::function<void(const quota_usage&)> callback;
        {
            std::lock_guard lock(impl_->callbacks_mutex);
            callback = impl_->exceeded_callback;
        }
        if (callback) {
            FT_LOG_WARN(log_category::server,
                "Quota exceeded: " + std::to_string(usage.usage_percent) + "% used");
            callback(usage);
        }
        return;
    }

    // Check warning thresholds
    std::function<void(const quota_usage&)> callback;
    {
        std::lock_guard lock(impl_->callbacks_mutex);
        callback = impl_->warning_callback;
    }

    if (!callback) {
        return;
    }

    std::lock_guard lock(impl_->thresholds_mutex);
    for (auto& threshold : impl_->warning_thresholds) {
        if (!threshold.triggered && usage.usage_percent >= threshold.percentage) {
            threshold.triggered = true;
            FT_LOG_WARN(log_category::server,
                "Quota warning: " + std::to_string(threshold.percentage) +
                "% threshold reached (current: " + std::to_string(usage.usage_percent) + "%)");
            callback(usage);
        }
    }
}

}  // namespace kcenon::file_transfer
