/**
 * @file storage_policy.cpp
 * @brief Storage tiering and lifecycle policy implementation
 */

#include "kcenon/file_transfer/server/storage_policy.h"

#include <algorithm>
#include <mutex>
#include <regex>
#include <shared_mutex>
#include <unordered_set>

namespace kcenon::file_transfer {

// ============================================================================
// Helper functions
// ============================================================================

namespace {

auto matches_glob_pattern(const std::string& pattern, const std::string& key) -> bool {
    if (pattern.empty()) return true;

    // Convert glob pattern to regex
    std::string regex_pattern;
    for (char c : pattern) {
        switch (c) {
            case '*':
                regex_pattern += ".*";
                break;
            case '?':
                regex_pattern += ".";
                break;
            case '.':
            case '+':
            case '^':
            case '$':
            case '(':
            case ')':
            case '[':
            case ']':
            case '{':
            case '}':
            case '|':
            case '\\':
                regex_pattern += '\\';
                regex_pattern += c;
                break;
            default:
                regex_pattern += c;
                break;
        }
    }

    try {
        std::regex re(regex_pattern);
        return std::regex_match(key, re);
    } catch (...) {
        return false;
    }
}

auto needs_action(const policy_evaluation_result& evaluation) -> bool {
    return evaluation.recommended_action == tiering_action::delete_obj ||
           evaluation.target_tier != evaluation.current_tier;
}

}  // namespace

// ============================================================================
// storage_policy::builder implementation
// ============================================================================

struct storage_policy::builder::builder_data {
    std::optional<access_pattern_config> access_config;
    std::optional<age_tiering_config> age_config;
    std::optional<size_tiering_config> size_config;
    std::vector<tiering_rule> rules;
    std::optional<retention_policy> retention;
    std::chrono::seconds auto_eval_interval{0};
    bool auto_execution = false;
    bool dry_run = false;
};

// ============================================================================

struct storage_policy::impl {
    storage_manager* manager = nullptr;

    std::optional<access_pattern_config> access_config;
    std::optional<age_tiering_config> age_config;
    std::optional<size_tiering_config> size_config;

    mutable std::shared_mutex rules_mutex;
    std::vector<tiering_rule> rules;

    mutable std::mutex retention_mutex;
    retention_policy retention_policy_;

    std::chrono::seconds auto_eval_interval{0};
    bool auto_execution = false;
    bool dry_run = false;

    mutable std::shared_mutex stats_mutex;
    tiering_statistics stats;

    mutable std::mutex callbacks_mutex;
    std::function<void(const policy_evaluation_result&)> eval_callback;
    std::function<void(const std::string&, tiering_action,
                      storage_tier, storage_tier)> action_callback;
    std::function<void(const std::string&, const error&)> error_callback;

    // Pending actions from evaluation
    mutable std::mutex pending_mutex;
    std::vector<policy_evaluation_result> pending_actions;

    void report_eval(const policy_evaluation_result& result) {
        decltype(eval_callback) callback;
        {
            std::lock_guard lock(callbacks_mutex);
            callback = eval_callback;
        }
        if (callback) callback(result);
    }

    void report_action(const std::string& key, tiering_action action,
                      storage_tier from, storage_tier to) {
        decltype(action_callback) callback;
        {
            std::lock_guard lock(callbacks_mutex);
            callback = action_callback;
        }
        if (callback) callback(key, action, from, to);
    }

    void report_error(const std::string& key, const error& err) {
        decltype(error_callback) callback;
        {
            std::lock_guard lock(callbacks_mutex);
            callback = error_callback;
        }
        if (callback) callback(key, err);
    }

    void forget_pending(const std::string& key) {
        std::lock_guard lock(pending_mutex);
        std::erase_if(pending_actions, [&](const auto& pending) { return pending.key == key; });
    }

    void remember_evaluation(const policy_evaluation_result& evaluation) {
        std::lock_guard lock(pending_mutex);
        std::erase_if(pending_actions, [&](const auto& pending) { return pending.key == evaluation.key; });
        if (needs_action(evaluation) && !evaluation.blocked_by_retention) {
            pending_actions.push_back(evaluation);
        }
    }

    void record_evaluation() {
        std::unique_lock lock(stats_mutex);
        stats.objects_evaluated++;
        stats.last_evaluation = std::chrono::system_clock::now();
    }

    void record_action(tiering_action action, uint64_t bytes) {
        std::unique_lock lock(stats_mutex);
        switch (action) {
            case tiering_action::move:
                stats.objects_moved++;
                stats.bytes_moved += bytes;
                break;
            case tiering_action::copy:
                stats.objects_copied++;
                break;
            case tiering_action::archive:
                stats.objects_archived++;
                break;
            case tiering_action::delete_obj:
                stats.objects_deleted++;
                break;
        }
        stats.last_execution = std::chrono::system_clock::now();
    }

    void record_error() {
        std::unique_lock lock(stats_mutex);
        stats.errors++;
    }

    auto evaluate_object(const stored_object_metadata& metadata)
        -> policy_evaluation_result {

        policy_evaluation_result result;
        result.key = metadata.key;
        result.current_tier = metadata.tier;
        result.target_tier = metadata.tier;

        // Check each rule in priority order
        std::shared_lock lock(rules_mutex);
        for (const auto& rule : rules) {
            if (!rule.enabled) continue;

            // Check key pattern
            if (rule.key_pattern && !matches_glob_pattern(*rule.key_pattern, metadata.key)) {
                continue;
            }

            // Check source tier
            if (rule.source_tier && *rule.source_tier != metadata.tier) {
                continue;
            }

            bool rule_matches = false;

            // Check trigger conditions
            switch (rule.trigger) {
                case tiering_trigger::age:
                    if (rule.min_age || rule.max_age) {
                        auto now = std::chrono::system_clock::now();
                        auto age = std::chrono::duration_cast<std::chrono::hours>(
                            now - metadata.last_modified);

                        bool age_matches = true;
                        if (rule.min_age && age < *rule.min_age) {
                            age_matches = false;
                        }
                        if (rule.max_age && age > *rule.max_age) {
                            age_matches = false;
                        }
                        rule_matches = age_matches;
                    }
                    break;

                case tiering_trigger::size:
                    if (rule.min_size || rule.max_size) {
                        bool size_matches = true;
                        if (rule.min_size && metadata.size < *rule.min_size) {
                            size_matches = false;
                        }
                        if (rule.max_size && metadata.size > *rule.max_size) {
                            size_matches = false;
                        }
                        rule_matches = size_matches;
                    }
                    break;

                case tiering_trigger::access_pattern:
                    if (rule.max_access_count) {
                        rule_matches = metadata.access_count <= *rule.max_access_count;
                    }
                    break;

                case tiering_trigger::manual:
                    // Manual rules don't auto-match
                    continue;
            }

            if (rule_matches) {
                result.matched_rule = rule.name;
                result.recommended_action = rule.action;
                result.target_tier = rule.target_tier;
                result.reason = "Matched rule: " + rule.name;
                break;
            }
        }

        // Check built-in tiering if no rule matched
        if (result.matched_rule.empty()) {
            // Age-based tiering
            if (age_config) {
                auto now = std::chrono::system_clock::now();
                auto age = std::chrono::duration_cast<std::chrono::hours>(
                    now - metadata.last_modified);

                if (metadata.tier == storage_tier::hot &&
                    age >= age_config->hot_to_warm_age) {
                    result.target_tier = storage_tier::warm;
                    result.recommended_action = tiering_action::move;
                    result.reason = "Age exceeds hot tier threshold";
                } else if (metadata.tier == storage_tier::warm &&
                           age >= age_config->warm_to_cold_age) {
                    result.target_tier = storage_tier::cold;
                    result.recommended_action = tiering_action::move;
                    result.reason = "Age exceeds warm tier threshold";
                } else if (metadata.tier == storage_tier::cold &&
                           age >= age_config->cold_to_archive_age) {
                    result.target_tier = storage_tier::archive;
                    result.recommended_action = tiering_action::archive;
                    result.reason = "Age exceeds cold tier threshold";
                }
            }

            // Size-based tiering (only if no age-based action)
            if (size_config && result.target_tier == result.current_tier) {
                if (metadata.size <= size_config->hot_max_size) {
                    if (metadata.tier != storage_tier::hot) {
                        result.target_tier = storage_tier::hot;
                        result.recommended_action = tiering_action::move;
                        result.reason = "Size qualifies for hot tier";
                    }
                } else if (metadata.size <= size_config->warm_max_size) {
                    if (metadata.tier == storage_tier::hot) {
                        result.target_tier = storage_tier::warm;
                        result.recommended_action = tiering_action::move;
                        result.reason = "Size exceeds hot tier threshold";
                    }
                } else {
                    if (metadata.tier != storage_tier::cold &&
                        metadata.tier != storage_tier::archive) {
                        result.target_tier = storage_tier::cold;
                        result.recommended_action = tiering_action::move;
                        result.reason = "Size exceeds warm tier threshold";
                    }
                }
            }
        }

        // Check retention
        std::lock_guard ret_lock(retention_mutex);
        const bool excluded = std::any_of(
            retention_policy_.exclusions.begin(), retention_policy_.exclusions.end(),
            [&](const auto& pattern) { return matches_glob_pattern(pattern, metadata.key); });
        if (result.recommended_action == tiering_action::delete_obj && !excluded) {
            if (retention_policy_.legal_hold ||
                retention_policy_.compliance_mode) {
                result.blocked_by_retention = true;
                result.reason = "Blocked by retention policy";
            } else if (retention_policy_.min_retention.count() > 0) {
                auto now = std::chrono::system_clock::now();
                auto age = std::chrono::duration_cast<std::chrono::hours>(
                    now - metadata.last_modified);
                if (age < retention_policy_.min_retention) {
                    result.blocked_by_retention = true;
                    result.reason = "Minimum retention period not reached";
                }
            }
        }

        return result;
    }
};

storage_policy::builder::builder()
    : data_(std::make_unique<builder_data>()) {}

storage_policy::builder::~builder() = default;
storage_policy::builder::builder(builder&&) noexcept = default;
auto storage_policy::builder::operator=(builder&&) noexcept -> storage_policy::builder& = default;

auto storage_policy::builder::with_access_pattern_tiering(
    const access_pattern_config& config) -> builder& {
    data_->access_config = config;
    return *this;
}

auto storage_policy::builder::with_age_tiering(
    const age_tiering_config& config) -> builder& {
    data_->age_config = config;
    return *this;
}

auto storage_policy::builder::with_size_tiering(
    const size_tiering_config& config) -> builder& {
    data_->size_config = config;
    return *this;
}

auto storage_policy::builder::with_rule(tiering_rule rule) -> builder& {
    data_->rules.push_back(std::move(rule));
    return *this;
}

auto storage_policy::builder::with_retention(retention_policy policy) -> builder& {
    data_->retention = std::move(policy);
    return *this;
}

auto storage_policy::builder::with_auto_evaluation(
    std::chrono::seconds interval) -> builder& {
    data_->auto_eval_interval = interval;
    return *this;
}

auto storage_policy::builder::with_auto_execution(bool enable) -> builder& {
    data_->auto_execution = enable;
    return *this;
}

auto storage_policy::builder::with_dry_run(bool enable) -> builder& {
    data_->dry_run = enable;
    return *this;
}

auto storage_policy::builder::build() -> std::unique_ptr<storage_policy> {
    auto policy = std::unique_ptr<storage_policy>(new storage_policy());

    policy->impl_->access_config = data_->access_config;
    policy->impl_->age_config = data_->age_config;
    policy->impl_->size_config = data_->size_config;
    policy->impl_->rules = std::move(data_->rules);
    policy->impl_->retention_policy_ = data_->retention.value_or(retention_policy{});
    policy->impl_->auto_eval_interval = data_->auto_eval_interval;
    policy->impl_->auto_execution = data_->auto_execution;
    policy->impl_->dry_run = data_->dry_run;

    // Sort rules by priority
    std::sort(policy->impl_->rules.begin(), policy->impl_->rules.end(),
        [](const tiering_rule& a, const tiering_rule& b) {
            return a.priority > b.priority;
        });

    return policy;
}

// ============================================================================
// storage_policy implementation

storage_policy::storage_policy()
    : impl_(std::make_unique<impl>()) {}

storage_policy::storage_policy(storage_policy&&) noexcept = default;
auto storage_policy::operator=(storage_policy&&) noexcept -> storage_policy& = default;
storage_policy::~storage_policy() = default;

void storage_policy::attach(storage_manager& manager) {
    impl_->manager = &manager;
}

void storage_policy::detach() {
    impl_->manager = nullptr;
}

auto storage_policy::is_attached() const -> bool {
    return impl_->manager != nullptr;
}

auto storage_policy::evaluate(const std::string& key)
    -> result<policy_evaluation_result> {

    if (!impl_->manager) {
        return unexpected{error{
            error_code::not_initialized,
            "Storage policy not attached to a storage manager"
        }};
    }

    auto metadata_result = impl_->manager->get_metadata(key);
    if (!metadata_result.has_value()) {
        return unexpected{metadata_result.error()};
    }

    impl_->record_evaluation();
    auto eval_result = impl_->evaluate_object(metadata_result.value());
    impl_->remember_evaluation(eval_result);
    impl_->report_eval(eval_result);

    return result<policy_evaluation_result>(std::move(eval_result));
}

auto storage_policy::evaluate_all()
    -> result<std::vector<policy_evaluation_result>> {

    return evaluate_prefix("");
}

auto storage_policy::evaluate_prefix(const std::string& prefix)
    -> result<std::vector<policy_evaluation_result>> {

    if (!impl_->manager) {
        return unexpected{error{
            error_code::not_initialized,
            "Storage policy not attached to a storage manager"
        }};
    }

    std::vector<policy_evaluation_result> results;

    list_storage_options options;
    options.prefix = prefix;

    std::unordered_set<std::string> seen_tokens;
    for (;;) {
        auto list_result = impl_->manager->list(options);
        if (!list_result.has_value()) {
            return unexpected{list_result.error()};
        }

        const auto& page = list_result.value();
        for (const auto& obj : page.objects) {
            impl_->record_evaluation();
            auto evaluation = impl_->evaluate_object(obj);
            impl_->remember_evaluation(evaluation);
            impl_->report_eval(evaluation);
            results.push_back(std::move(evaluation));
        }

        if (!page.is_truncated) break;
        if (!page.continuation_token || page.continuation_token->empty() ||
            !seen_tokens.insert(*page.continuation_token).second) {
            return unexpected{error{error_code::invalid_configuration,
                "Storage listing did not provide a progressing continuation token"}};
        }
        options.continuation_token = page.continuation_token;
    }

    return result<std::vector<policy_evaluation_result>>(std::move(results));
}

auto storage_policy::execute(const std::string& key) -> result<void> {
    auto eval_result = evaluate(key);
    if (!eval_result.has_value()) {
        return unexpected{eval_result.error()};
    }

    const auto& eval = eval_result.value();
    if (!needs_action(eval)) {
        return {};  // No action needed
    }

    if (eval.blocked_by_retention) {
        return unexpected{error{
            error_code::file_access_denied,
            "Action blocked by retention policy"
        }};
    }

    return execute_action(key, eval.target_tier, eval.recommended_action);
}

auto storage_policy::execute_pending() -> result<std::size_t> {
    if (!impl_->manager) {
        return unexpected{error{
            error_code::not_initialized,
            "Storage policy not attached to a storage manager"
        }};
    }

    std::vector<policy_evaluation_result> actions;
    {
        std::lock_guard lock(impl_->pending_mutex);
        actions = std::move(impl_->pending_actions);
        impl_->pending_actions.clear();
    }

    std::size_t executed = 0;
    for (const auto& action : actions) {
        if (action.blocked_by_retention) continue;
        if (!needs_action(action)) continue;

        auto result = execute_action(
            action.key, action.target_tier, action.recommended_action);

        if (result.has_value()) {
            executed++;
        }
    }

    return result<std::size_t>(executed);
}

auto storage_policy::execute_action(
    const std::string& key,
    storage_tier target_tier,
    tiering_action action) -> result<void> {

    if (!impl_->manager) {
        return unexpected{error{
            error_code::not_initialized,
            "Storage policy not attached to a storage manager"
        }};
    }

    // Get current metadata
    auto metadata_result = impl_->manager->get_metadata(key);
    if (!metadata_result.has_value()) {
        impl_->record_error();
        impl_->report_error(key, metadata_result.error());
        return unexpected{metadata_result.error()};
    }

    storage_tier current_tier = metadata_result.value().tier;
    uint64_t size = metadata_result.value().size;

    // Recheck retention at execution time, including direct and previously queued actions.
    if (action == tiering_action::delete_obj) {
        auto allowed = can_delete(key);
        if (!allowed || !allowed.value()) {
            auto failure = allowed ? error{error_code::file_access_denied,
                "Action blocked by retention policy"} : allowed.error();
            impl_->record_error();
            impl_->report_error(key, failure);
            return unexpected{failure};
        }
    }

    // Dry run mode - just report
    if (impl_->dry_run) {
        impl_->forget_pending(key);
        impl_->report_action(key, action, current_tier, target_tier);
        return result<void>();
    }

    result<void> action_result;

    switch (action) {
        case tiering_action::move:
        case tiering_action::copy:
        case tiering_action::archive:
            action_result = impl_->manager->change_tier(key, target_tier);
            break;

        case tiering_action::delete_obj:
            action_result = impl_->manager->remove(key);
            break;
    }

    if (action_result.has_value()) {
        impl_->forget_pending(key);
        impl_->record_action(action, size);
        impl_->report_action(key, action, current_tier, target_tier);
    } else {
        impl_->record_error();
        impl_->report_error(key, action_result.error());
    }

    return action_result;
}

void storage_policy::add_rule(tiering_rule rule) {
    std::unique_lock lock(impl_->rules_mutex);
    impl_->rules.push_back(std::move(rule));

    // Re-sort by priority
    std::sort(impl_->rules.begin(), impl_->rules.end(),
        [](const tiering_rule& a, const tiering_rule& b) {
            return a.priority > b.priority;
        });
}

auto storage_policy::remove_rule(const std::string& name) -> bool {
    std::unique_lock lock(impl_->rules_mutex);
    auto it = std::remove_if(impl_->rules.begin(), impl_->rules.end(),
        [&name](const tiering_rule& r) { return r.name == name; });

    if (it != impl_->rules.end()) {
        impl_->rules.erase(it, impl_->rules.end());
        return true;
    }
    return false;
}

auto storage_policy::rules() const -> const std::vector<tiering_rule>& {
    return impl_->rules;
}

void storage_policy::set_rule_enabled(const std::string& name, bool enable) {
    std::unique_lock lock(impl_->rules_mutex);
    for (auto& rule : impl_->rules) {
        if (rule.name == name) {
            rule.enabled = enable;
            break;
        }
    }
}

auto storage_policy::can_delete(const std::string& key) -> result<bool> {
    if (!impl_->manager) {
        return unexpected{error{
            error_code::not_initialized,
            "Storage policy not attached"
        }};
    }

    std::lock_guard lock(impl_->retention_mutex);

    // Check exclusions
    for (const auto& pattern : impl_->retention_policy_.exclusions) {
        if (matches_glob_pattern(pattern, key)) {
            return result<bool>(true);
        }
    }

    // Legal hold blocks deletion
    if (impl_->retention_policy_.legal_hold) {
        return result<bool>(false);
    }

    // Compliance mode blocks deletion
    if (impl_->retention_policy_.compliance_mode) {
        return result<bool>(false);
    }

    // Check minimum retention
    if (impl_->retention_policy_.min_retention.count() > 0) {
        auto metadata_result = impl_->manager->get_metadata(key);
        if (!metadata_result.has_value()) {
            return unexpected{metadata_result.error()};
        }
        {
            auto now = std::chrono::system_clock::now();
            auto age = std::chrono::duration_cast<std::chrono::hours>(
                now - metadata_result.value().last_modified);

            if (age < impl_->retention_policy_.min_retention) {
                return result<bool>(false);
            }
        }
    }

    return result<bool>(true);
}

auto storage_policy::can_modify(const std::string& key) -> result<bool> {
    std::lock_guard lock(impl_->retention_mutex);

    // Check exclusions
    for (const auto& pattern : impl_->retention_policy_.exclusions) {
        if (matches_glob_pattern(pattern, key)) {
            return result<bool>(true);
        }
    }

    // Legal hold blocks modification
    if (impl_->retention_policy_.legal_hold) {
        return result<bool>(false);
    }

    // Compliance mode blocks modification
    if (impl_->retention_policy_.compliance_mode) {
        return result<bool>(false);
    }

    // Governance mode allows admin override (return true)
    return result<bool>(true);
}

auto storage_policy::retention() const -> const retention_policy& {
    std::lock_guard lock(impl_->retention_mutex);
    return impl_->retention_policy_;
}

void storage_policy::set_retention(retention_policy policy) {
    std::lock_guard lock(impl_->retention_mutex);
    impl_->retention_policy_ = std::move(policy);
}

auto storage_policy::get_statistics() const -> tiering_statistics {
    std::shared_lock lock(impl_->stats_mutex);
    return impl_->stats;
}

void storage_policy::reset_statistics() {
    std::unique_lock lock(impl_->stats_mutex);
    impl_->stats = tiering_statistics{};
}

void storage_policy::on_evaluation(
    std::function<void(const policy_evaluation_result&)> callback) {
    std::lock_guard lock(impl_->callbacks_mutex);
    impl_->eval_callback = std::move(callback);
}

void storage_policy::on_action(
    std::function<void(const std::string&, tiering_action,
                      storage_tier, storage_tier)> callback) {
    std::lock_guard lock(impl_->callbacks_mutex);
    impl_->action_callback = std::move(callback);
}

void storage_policy::on_error(
    std::function<void(const std::string&, const error&)> callback) {
    std::lock_guard lock(impl_->callbacks_mutex);
    impl_->error_callback = std::move(callback);
}

auto storage_policy::has_access_pattern_tiering() const -> bool {
    return impl_->access_config.has_value();
}

auto storage_policy::has_age_tiering() const -> bool {
    return impl_->age_config.has_value();
}

auto storage_policy::has_size_tiering() const -> bool {
    return impl_->size_config.has_value();
}

auto storage_policy::is_dry_run() const -> bool {
    return impl_->dry_run;
}

void storage_policy::set_dry_run(bool enable) {
    impl_->dry_run = enable;
}

}  // namespace kcenon::file_transfer
