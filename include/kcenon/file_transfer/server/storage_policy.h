/**
 * @file storage_policy.h
 * @brief Storage tiering and lifecycle policy definitions
 */

#ifndef KCENON_FILE_TRANSFER_SERVER_STORAGE_POLICY_H
#define KCENON_FILE_TRANSFER_SERVER_STORAGE_POLICY_H

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "kcenon/file_transfer/core/types.h"
#include "kcenon/file_transfer/server/storage_manager.h"

namespace kcenon::file_transfer {

/**
 * @brief Tiering trigger type
 */
enum class tiering_trigger {
    access_pattern,     ///< Based on access frequency
    age,               ///< Based on object age
    size,              ///< Based on object size
    manual             ///< Manual trigger only
};

/**
 * @brief Convert tiering_trigger to string
 */
[[nodiscard]] constexpr auto to_string(tiering_trigger trigger) -> const char* {
    switch (trigger) {
        case tiering_trigger::access_pattern: return "access_pattern";
        case tiering_trigger::age: return "age";
        case tiering_trigger::size: return "size";
        case tiering_trigger::manual: return "manual";
        default: return "unknown";
    }
}

/**
 * @brief Tiering action type
 */
enum class tiering_action {
    move,       ///< Move to new tier (delete from source)
    copy,       ///< Copy to new tier (keep source)
    archive,    ///< Archive (move to archive tier)
    delete_obj  ///< Delete object
};

/**
 * @brief Convert tiering_action to string
 */
[[nodiscard]] constexpr auto to_string(tiering_action action) -> const char* {
    switch (action) {
        case tiering_action::move: return "move";
        case tiering_action::copy: return "copy";
        case tiering_action::archive: return "archive";
        case tiering_action::delete_obj: return "delete";
        default: return "unknown";
    }
}

/**
 * @brief Access pattern configuration for auto-tiering
 */
struct access_pattern_config {
    /// Minimum access count to stay in hot tier
    uint64_t hot_min_access_count = 10;

    /// Minimum access count to stay in warm tier
    uint64_t warm_min_access_count = 2;

    /// Time window for counting accesses
    std::chrono::hours access_window{24 * 7};  // 7 days

    /// Minimum time in tier before eligible for demotion
    std::chrono::hours min_time_in_tier{24};  // 24 hours
};

/**
 * @brief Age-based tiering configuration
 */
struct age_tiering_config {
    /// Age to move from hot to warm tier
    std::chrono::hours hot_to_warm_age{24 * 30};  // 30 days

    /// Age to move from warm to cold tier
    std::chrono::hours warm_to_cold_age{24 * 90};  // 90 days

    /// Age to move from cold to archive tier
    std::chrono::hours cold_to_archive_age{24 * 365};  // 365 days

    /// Age to delete (0 = never delete)
    std::chrono::hours delete_after{0};
};

/**
 * @brief Size-based tiering configuration
 */
struct size_tiering_config {
    /// Files smaller than this go to hot tier
    uint64_t hot_max_size = 10ULL * 1024 * 1024;  // 10MB

    /// Files smaller than this go to warm tier
    uint64_t warm_max_size = 100ULL * 1024 * 1024;  // 100MB

    /// Files larger go to cold tier
};

/**
 * @brief Tiering rule definition
 */
struct tiering_rule {
    /// Rule name
    std::string name;

    /// Rule priority (higher = evaluated first)
    int priority = 0;

    /// Trigger type
    tiering_trigger trigger = tiering_trigger::age;

    /// Source tier (nullopt = any tier)
    std::optional<storage_tier> source_tier;

    /// Target tier
    storage_tier target_tier = storage_tier::cold;

    /// Action to take
    tiering_action action = tiering_action::move;

    /// Object key pattern filter (glob pattern)
    std::optional<std::string> key_pattern;

    /// Minimum object age for rule to apply
    std::optional<std::chrono::hours> min_age;

    /// Maximum object age for rule to apply
    std::optional<std::chrono::hours> max_age;

    /// Minimum object size for rule to apply
    std::optional<uint64_t> min_size;

    /// Maximum object size for rule to apply
    std::optional<uint64_t> max_size;

    /// Maximum access count in window for rule to apply
    std::optional<uint64_t> max_access_count;

    /// Rule enabled
    bool enabled = true;
};

/**
 * @brief Retention policy configuration
 */
struct retention_policy {
    /// Minimum retention period (cannot delete before this)
    std::chrono::hours min_retention{0};

    /// Maximum retention period (auto-delete after this)
    std::optional<std::chrono::hours> max_retention;

    /// Legal hold (overrides retention)
    bool legal_hold = false;

    /// Governance mode (admin can override)
    bool governance_mode = false;

    /// Compliance mode (no override possible)
    bool compliance_mode = false;

    /// Key patterns to exclude from retention
    std::vector<std::string> exclusions;
};

/**
 * @brief Storage policy evaluation result
 */
struct policy_evaluation_result {
    /// Object key
    std::string key;

    /// Matched rule name
    std::string matched_rule;

    /// Recommended action
    tiering_action recommended_action = tiering_action::move;

    /// Current tier
    storage_tier current_tier = storage_tier::hot;

    /// Target tier
    storage_tier target_tier = storage_tier::hot;

    /// Reason for recommendation
    std::string reason;

    /// Is action blocked by retention
    bool blocked_by_retention = false;
};

/**
 * @brief Tiering statistics
 */
struct tiering_statistics {
    /// Objects evaluated
    uint64_t objects_evaluated = 0;

    /// Objects moved
    uint64_t objects_moved = 0;

    /// Objects copied
    uint64_t objects_copied = 0;

    /// Objects archived
    uint64_t objects_archived = 0;

    /// Objects deleted
    uint64_t objects_deleted = 0;

    /// Bytes moved
    uint64_t bytes_moved = 0;

    /// Errors encountered
    uint64_t errors = 0;

    /// Last evaluation time
    std::chrono::system_clock::time_point last_evaluation;

    /// Last execution time
    std::chrono::system_clock::time_point last_execution;
};

/**
 * @brief Storage policy manager
 *
 * Manages storage tiering policies and automatic lifecycle management.
 *
 * @code
 * auto policy = storage_policy::builder()
 *     .with_access_pattern_tiering(access_pattern_config{})
 *     .with_rule(tiering_rule{
 *         .name = "archive_old_logs",
 *         .trigger = tiering_trigger::age,
 *         .key_pattern = "logs/*",
 *         .min_age = std::chrono::hours{24 * 90},
 *         .target_tier = storage_tier::archive
 *     })
 *     .with_retention(retention_policy{
 *         .min_retention = std::chrono::hours{24 * 30}
 *     })
 *     .build();
 *
 * policy->attach(storage_manager);
 * policy->evaluate_all();
 * policy->execute_pending();
 * @endcode
 */
class storage_policy {
public:
    /**
     * @brief Builder for storage_policy
     */
    class builder {
    public:
        builder();

        /**
         * @brief Enable access pattern based tiering
         */
        auto with_access_pattern_tiering(
            const access_pattern_config& config) -> builder&;

        /**
         * @brief Enable age based tiering
         */
        auto with_age_tiering(const age_tiering_config& config) -> builder&;

        /**
         * @brief Enable size based tiering
         */
        auto with_size_tiering(const size_tiering_config& config) -> builder&;

        /**
         * @brief Add a tiering rule
         */
        auto with_rule(tiering_rule rule) -> builder&;

        /**
         * @brief Set retention policy
         */
        auto with_retention(retention_policy policy) -> builder&;

        /**
         * @brief Enable automatic evaluation
         * @param interval Evaluation interval
         */
        auto with_auto_evaluation(std::chrono::seconds interval) -> builder&;

        /**
         * @brief Enable automatic execution of tiering actions
         */
        auto with_auto_execution(bool enable) -> builder&;

        /**
         * @brief Enable dry run mode (no actual changes)
         */
        auto with_dry_run(bool enable) -> builder&;

        /**
         * @brief Build the storage policy
         */
        [[nodiscard]] auto build() -> std::unique_ptr<storage_policy>;

    private:
        struct builder_data;
        std::unique_ptr<builder_data> data_;
    };

    // Non-copyable, movable
    storage_policy(const storage_policy&) = delete;
    auto operator=(const storage_policy&) -> storage_policy& = delete;
    storage_policy(storage_policy&&) noexcept;
    auto operator=(storage_policy&&) noexcept -> storage_policy&;
    ~storage_policy();

    /**
     * @brief Attach to a storage manager
     * @param manager Storage manager to manage
     */
    void attach(storage_manager& manager);

    /**
     * @brief Detach from storage manager
     */
    void detach();

    /**
     * @brief Check if attached to a storage manager
     */
    [[nodiscard]] auto is_attached() const -> bool;

    // ========================================================================
    // Evaluation Operations
    // ========================================================================

    /**
     * @brief Evaluate policies for a single object
     * @param key Object key
     * @return Evaluation result
     */
    [[nodiscard]] auto evaluate(
        const std::string& key) -> result<policy_evaluation_result>;

    /**
     * @brief Evaluate policies for all objects
     * @return Vector of evaluation results
     */
    [[nodiscard]] auto evaluate_all() -> result<std::vector<policy_evaluation_result>>;

    /**
     * @brief Evaluate policies for objects matching a prefix
     * @param prefix Key prefix
     * @return Vector of evaluation results
     */
    [[nodiscard]] auto evaluate_prefix(
        const std::string& prefix) -> result<std::vector<policy_evaluation_result>>;

    // ========================================================================
    // Execution Operations
    // ========================================================================

    /**
     * @brief Execute tiering action for a single object
     * @param key Object key
     * @return Success or error
     */
    [[nodiscard]] auto execute(const std::string& key) -> result<void>;

    /**
     * @brief Execute all pending tiering actions
     * @return Number of actions executed
     */
    [[nodiscard]] auto execute_pending() -> result<std::size_t>;

    /**
     * @brief Execute tiering action with specific parameters
     * @param key Object key
     * @param target_tier Target tier
     * @param action Action to perform
     * @return Success or error
     */
    [[nodiscard]] auto execute_action(
        const std::string& key,
        storage_tier target_tier,
        tiering_action action) -> result<void>;

    // ========================================================================
    // Rule Management
    // ========================================================================

    /**
     * @brief Add a tiering rule
     * @param rule Rule to add
     */
    void add_rule(tiering_rule rule);

    /**
     * @brief Remove a rule by name
     * @param name Rule name
     * @return true if rule was removed
     */
    auto remove_rule(const std::string& name) -> bool;

    /**
     * @brief Get all rules
     * @return Vector of rules
     */
    [[nodiscard]] auto rules() const -> const std::vector<tiering_rule>&;

    /**
     * @brief Enable/disable a rule
     * @param name Rule name
     * @param enable Enable or disable
     */
    void set_rule_enabled(const std::string& name, bool enable);

    // ========================================================================
    // Retention Management
    // ========================================================================

    /**
     * @brief Check if object can be deleted
     * @param key Object key
     * @return true if deletion is allowed
     */
    [[nodiscard]] auto can_delete(const std::string& key) -> result<bool>;

    /**
     * @brief Check if object can be modified
     * @param key Object key
     * @return true if modification is allowed
     */
    [[nodiscard]] auto can_modify(const std::string& key) -> result<bool>;

    /**
     * @brief Get retention policy
     */
    [[nodiscard]] auto retention() const -> const retention_policy&;

    /**
     * @brief Update retention policy
     */
    void set_retention(retention_policy policy);

    // ========================================================================
    // Statistics and Monitoring
    // ========================================================================

    /**
     * @brief Get tiering statistics
     */
    [[nodiscard]] auto get_statistics() const -> tiering_statistics;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

    /**
     * @brief Set callback for policy evaluation results
     */
    void on_evaluation(
        std::function<void(const policy_evaluation_result&)> callback);

    /**
     * @brief Set callback for tiering actions
     */
    void on_action(
        std::function<void(const std::string& key, tiering_action action,
                          storage_tier from, storage_tier to)> callback);

    /**
     * @brief Set callback for errors
     */
    void on_error(
        std::function<void(const std::string& key, const error& err)> callback);

    // ========================================================================
    // Configuration Access
    // ========================================================================

    /**
     * @brief Check if access pattern tiering is enabled
     */
    [[nodiscard]] auto has_access_pattern_tiering() const -> bool;

    /**
     * @brief Check if age tiering is enabled
     */
    [[nodiscard]] auto has_age_tiering() const -> bool;

    /**
     * @brief Check if size tiering is enabled
     */
    [[nodiscard]] auto has_size_tiering() const -> bool;

    /**
     * @brief Check if dry run mode is enabled
     */
    [[nodiscard]] auto is_dry_run() const -> bool;

    /**
     * @brief Enable/disable dry run mode
     */
    void set_dry_run(bool enable);

private:
    storage_policy();

    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_SERVER_STORAGE_POLICY_H
