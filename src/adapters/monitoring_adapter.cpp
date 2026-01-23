// BSD 3-Clause License
//
// Copyright (c) 2025, kcenon
// All rights reserved.

#include <kcenon/file_transfer/adapters/monitoring_adapter.h>
#include <kcenon/file_transfer/server/file_transfer_server.h>

#include <chrono>

namespace kcenon::file_transfer::adapters {

#if KCENON_WITH_COMMON_SYSTEM

// ============================================================================
// Factory method
// ============================================================================

std::shared_ptr<file_transfer_monitor_adapter> file_transfer_monitor_adapter::create(
    std::shared_ptr<file_transfer_server> server,
    const std::string& source_id) {
    return std::make_shared<file_transfer_monitor_adapter>(std::move(server), source_id);
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

file_transfer_monitor_adapter::file_transfer_monitor_adapter(
    std::shared_ptr<file_transfer_server> server,
    const std::string& source_id)
    : server_(server)
    , source_id_(source_id) {}

file_transfer_monitor_adapter::~file_transfer_monitor_adapter() = default;

file_transfer_monitor_adapter::file_transfer_monitor_adapter(
    file_transfer_monitor_adapter&& other) noexcept
    : server_(std::move(other.server_))
    , source_id_(std::move(other.source_id_)) {
    std::lock_guard<std::mutex> lock(other.metrics_mutex_);
    custom_metrics_ = std::move(other.custom_metrics_);
}

file_transfer_monitor_adapter& file_transfer_monitor_adapter::operator=(
    file_transfer_monitor_adapter&& other) noexcept {
    if (this != &other) {
        server_ = std::move(other.server_);
        source_id_ = std::move(other.source_id_);

        std::lock_guard<std::mutex> lock(other.metrics_mutex_);
        custom_metrics_ = std::move(other.custom_metrics_);
    }
    return *this;
}

// ============================================================================
// IMonitor interface implementation
// ============================================================================

common::VoidResult file_transfer_monitor_adapter::record_metric(
    const std::string& name, double value) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    custom_metrics_[name] = custom_metric{
        value,
        common::interfaces::metric_type::gauge,
        {}
    };
    return common::ok();
}

common::VoidResult file_transfer_monitor_adapter::record_metric(
    const std::string& name,
    double value,
    const std::unordered_map<std::string, std::string>& tags) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    custom_metrics_[name] = custom_metric{
        value,
        common::interfaces::metric_type::gauge,
        tags
    };
    return common::ok();
}

common::Result<common::interfaces::metrics_snapshot> file_transfer_monitor_adapter::get_metrics() {
    return collect_server_metrics();
}

common::Result<common::interfaces::health_check_result> file_transfer_monitor_adapter::check_health() {
    return check_server_health();
}

common::VoidResult file_transfer_monitor_adapter::reset() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    custom_metrics_.clear();
    return common::ok();
}

// ============================================================================
// Additional methods
// ============================================================================

std::string file_transfer_monitor_adapter::get_source_id() const {
    return source_id_;
}

bool file_transfer_monitor_adapter::is_server_available() const {
    return !server_.expired();
}

// ============================================================================
// Private helper methods
// ============================================================================

common::interfaces::metrics_snapshot file_transfer_monitor_adapter::collect_server_metrics() const {
    common::interfaces::metrics_snapshot snapshot;
    snapshot.source_id = source_id_;
    snapshot.capture_time = std::chrono::system_clock::now();

    auto server = server_.lock();
    if (!server) {
        // Return empty snapshot if server is not available
        return snapshot;
    }

    // Get server statistics
    auto stats = server->get_statistics();

    // Transfer metrics (counters)
    snapshot.metrics.emplace_back(
        "file_transfer.bytes_sent",
        static_cast<double>(stats.total_bytes_sent),
        common::interfaces::metric_type::counter);

    snapshot.metrics.emplace_back(
        "file_transfer.bytes_received",
        static_cast<double>(stats.total_bytes_received),
        common::interfaces::metric_type::counter);

    snapshot.metrics.emplace_back(
        "file_transfer.completed_uploads",
        static_cast<double>(stats.total_files_uploaded),
        common::interfaces::metric_type::counter);

    snapshot.metrics.emplace_back(
        "file_transfer.completed_downloads",
        static_cast<double>(stats.total_files_downloaded),
        common::interfaces::metric_type::counter);

    // Active transfer metrics (gauges)
    snapshot.metrics.emplace_back(
        "file_transfer.active_transfers",
        static_cast<double>(stats.active_transfers),
        common::interfaces::metric_type::gauge);

    snapshot.metrics.emplace_back(
        "file_transfer.active_uploads",
        static_cast<double>(stats.active_uploads),
        common::interfaces::metric_type::gauge);

    snapshot.metrics.emplace_back(
        "file_transfer.active_downloads",
        static_cast<double>(stats.active_downloads),
        common::interfaces::metric_type::gauge);

    snapshot.metrics.emplace_back(
        "file_transfer.active_connections",
        static_cast<double>(stats.active_connections),
        common::interfaces::metric_type::gauge);

    // Uptime metric (counter)
    snapshot.metrics.emplace_back(
        "file_transfer.uptime_ms",
        static_cast<double>(stats.uptime.count()),
        common::interfaces::metric_type::counter);

    // Quota metrics
    auto quota_usage = server->get_quota_usage();

    snapshot.metrics.emplace_back(
        "file_transfer.quota_usage_percent",
        quota_usage.usage_percent,
        common::interfaces::metric_type::gauge);

    snapshot.metrics.emplace_back(
        "file_transfer.quota_used_bytes",
        static_cast<double>(quota_usage.used_bytes),
        common::interfaces::metric_type::gauge);

    snapshot.metrics.emplace_back(
        "file_transfer.quota_available_bytes",
        static_cast<double>(quota_usage.available_bytes),
        common::interfaces::metric_type::gauge);

    snapshot.metrics.emplace_back(
        "file_transfer.quota_total_bytes",
        static_cast<double>(quota_usage.total_quota),
        common::interfaces::metric_type::gauge);

    snapshot.metrics.emplace_back(
        "file_transfer.file_count",
        static_cast<double>(quota_usage.file_count),
        common::interfaces::metric_type::gauge);

    // Add custom metrics
    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        for (const auto& [name, metric] : custom_metrics_) {
            common::interfaces::metric_value mv(name, metric.value, metric.type);
            mv.tags = metric.tags;
            snapshot.metrics.push_back(std::move(mv));
        }
    }

    return snapshot;
}

common::interfaces::health_check_result file_transfer_monitor_adapter::check_server_health() const {
    common::interfaces::health_check_result result;
    result.timestamp = std::chrono::system_clock::now();

    auto start_time = std::chrono::steady_clock::now();

    auto server = server_.lock();
    if (!server) {
        result.status = common::interfaces::health_status::unhealthy;
        result.message = "Server reference is not available";
        result.check_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return result;
    }

    // Check 1: Server running status
    if (!server->is_running()) {
        result.status = common::interfaces::health_status::unhealthy;
        result.message = "Server is not running";
        result.metadata["server_state"] = to_string(server->state());
        result.check_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return result;
    }

    // Initialize as healthy
    result.status = common::interfaces::health_status::healthy;
    result.message = "Server is healthy";

    // Check 2: Quota status
    auto quota_usage = server->get_quota_usage();
    result.metadata["quota_usage_percent"] = std::to_string(quota_usage.usage_percent);

    if (quota_usage.usage_percent > 95.0) {
        result.status = common::interfaces::health_status::unhealthy;
        result.message = "Storage quota critical (>95%)";
    } else if (quota_usage.usage_percent > 80.0) {
        if (result.status == common::interfaces::health_status::healthy) {
            result.status = common::interfaces::health_status::degraded;
            result.message = "Storage quota warning (>80%)";
        }
    }

    // Check 3: Connection capacity
    auto stats = server->get_statistics();
    const auto& config = server->config();
    double connection_usage = config.max_connections > 0
        ? static_cast<double>(stats.active_connections) / static_cast<double>(config.max_connections) * 100.0
        : 0.0;

    result.metadata["connection_usage_percent"] = std::to_string(connection_usage);
    result.metadata["active_connections"] = std::to_string(stats.active_connections);
    result.metadata["max_connections"] = std::to_string(config.max_connections);

    if (connection_usage >= 90.0) {
        if (result.status == common::interfaces::health_status::healthy) {
            result.status = common::interfaces::health_status::degraded;
            result.message = "Near connection limit (>=90%)";
        }
    }

    // Add additional metadata
    result.metadata["server_state"] = to_string(server->state());
    result.metadata["uptime_ms"] = std::to_string(stats.uptime.count());
    result.metadata["active_transfers"] = std::to_string(stats.active_transfers);

    result.check_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);

    return result;
}

#endif  // KCENON_WITH_COMMON_SYSTEM

}  // namespace kcenon::file_transfer::adapters
