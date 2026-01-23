// BSD 3-Clause License
//
// Copyright (c) 2025, kcenon
// All rights reserved.

#include <kcenon/file_transfer/adapters/monitorable_adapter.h>
#include <kcenon/file_transfer/server/file_transfer_server.h>
#include <kcenon/file_transfer/client/file_transfer_client.h>

#include <chrono>

namespace kcenon::file_transfer::adapters {

#if KCENON_WITH_COMMON_SYSTEM

// ============================================================================
// file_transfer_server_monitorable
// ============================================================================

// Factory method
std::shared_ptr<file_transfer_server_monitorable> file_transfer_server_monitorable::create(
    std::shared_ptr<file_transfer_server> server,
    const std::string& name) {
    return std::make_shared<file_transfer_server_monitorable>(std::move(server), name);
}

// Constructor / Destructor
file_transfer_server_monitorable::file_transfer_server_monitorable(
    std::shared_ptr<file_transfer_server> server,
    const std::string& name)
    : server_(server)
    , component_name_(name) {}

file_transfer_server_monitorable::~file_transfer_server_monitorable() = default;

file_transfer_server_monitorable::file_transfer_server_monitorable(
    file_transfer_server_monitorable&& other) noexcept
    : server_(std::move(other.server_))
    , component_name_(std::move(other.component_name_)) {}

file_transfer_server_monitorable& file_transfer_server_monitorable::operator=(
    file_transfer_server_monitorable&& other) noexcept {
    if (this != &other) {
        server_ = std::move(other.server_);
        component_name_ = std::move(other.component_name_);
    }
    return *this;
}

// IMonitorable interface implementation
common::Result<common::interfaces::metrics_snapshot> file_transfer_server_monitorable::get_monitoring_data() {
    common::interfaces::metrics_snapshot snapshot;
    snapshot.source_id = component_name_;
    snapshot.capture_time = std::chrono::system_clock::now();

    auto server = server_.lock();
    if (!server) {
        return common::make_error<common::interfaces::metrics_snapshot>(
            common::error_codes::NOT_INITIALIZED,
            "Server reference is not available");
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
        "file_transfer.file_count",
        static_cast<double>(quota_usage.file_count),
        common::interfaces::metric_type::gauge);

    return snapshot;
}

common::Result<common::interfaces::health_check_result> file_transfer_server_monitorable::health_check() {
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
    result.metadata["component_name"] = component_name_;

    result.check_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);

    return result;
}

std::string file_transfer_server_monitorable::get_component_name() const {
    return component_name_;
}

bool file_transfer_server_monitorable::is_server_available() const {
    return !server_.expired();
}

void file_transfer_server_monitorable::set_component_name(const std::string& name) {
    component_name_ = name;
}

// ============================================================================
// file_transfer_client_monitorable
// ============================================================================

// Factory method
std::shared_ptr<file_transfer_client_monitorable> file_transfer_client_monitorable::create(
    std::shared_ptr<file_transfer_client> client,
    const std::string& name) {
    return std::make_shared<file_transfer_client_monitorable>(std::move(client), name);
}

// Constructor / Destructor
file_transfer_client_monitorable::file_transfer_client_monitorable(
    std::shared_ptr<file_transfer_client> client,
    const std::string& name)
    : client_(client)
    , component_name_(name) {}

file_transfer_client_monitorable::~file_transfer_client_monitorable() = default;

file_transfer_client_monitorable::file_transfer_client_monitorable(
    file_transfer_client_monitorable&& other) noexcept
    : client_(std::move(other.client_))
    , component_name_(std::move(other.component_name_)) {}

file_transfer_client_monitorable& file_transfer_client_monitorable::operator=(
    file_transfer_client_monitorable&& other) noexcept {
    if (this != &other) {
        client_ = std::move(other.client_);
        component_name_ = std::move(other.component_name_);
    }
    return *this;
}

// IMonitorable interface implementation
common::Result<common::interfaces::metrics_snapshot> file_transfer_client_monitorable::get_monitoring_data() {
    common::interfaces::metrics_snapshot snapshot;
    snapshot.source_id = component_name_;
    snapshot.capture_time = std::chrono::system_clock::now();

    auto client = client_.lock();
    if (!client) {
        return common::make_error<common::interfaces::metrics_snapshot>(
            common::error_codes::NOT_INITIALIZED,
            "Client reference is not available");
    }

    // Get client statistics
    auto stats = client->get_statistics();

    // Transfer volume metrics (counters)
    snapshot.metrics.emplace_back(
        "file_transfer.bytes_uploaded",
        static_cast<double>(stats.total_bytes_uploaded),
        common::interfaces::metric_type::counter);

    snapshot.metrics.emplace_back(
        "file_transfer.bytes_downloaded",
        static_cast<double>(stats.total_bytes_downloaded),
        common::interfaces::metric_type::counter);

    snapshot.metrics.emplace_back(
        "file_transfer.bytes_on_wire_upload",
        static_cast<double>(stats.total_bytes_on_wire_upload),
        common::interfaces::metric_type::counter);

    snapshot.metrics.emplace_back(
        "file_transfer.bytes_on_wire_download",
        static_cast<double>(stats.total_bytes_on_wire_download),
        common::interfaces::metric_type::counter);

    // Transfer count metrics (counters)
    snapshot.metrics.emplace_back(
        "file_transfer.files_uploaded",
        static_cast<double>(stats.total_files_uploaded),
        common::interfaces::metric_type::counter);

    snapshot.metrics.emplace_back(
        "file_transfer.files_downloaded",
        static_cast<double>(stats.total_files_downloaded),
        common::interfaces::metric_type::counter);

    snapshot.metrics.emplace_back(
        "file_transfer.chunks_transferred",
        static_cast<double>(stats.total_chunks_transferred),
        common::interfaces::metric_type::counter);

    // Active transfer state (gauges)
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

    // Error tracking (counters)
    snapshot.metrics.emplace_back(
        "file_transfer.total_errors",
        static_cast<double>(stats.total_errors),
        common::interfaces::metric_type::counter);

    snapshot.metrics.emplace_back(
        "file_transfer.total_retries",
        static_cast<double>(stats.total_retries),
        common::interfaces::metric_type::counter);

    // Connection status
    snapshot.metrics.emplace_back(
        "file_transfer.connected",
        client->is_connected() ? 1.0 : 0.0,
        common::interfaces::metric_type::gauge);

    return snapshot;
}

common::Result<common::interfaces::health_check_result> file_transfer_client_monitorable::health_check() {
    common::interfaces::health_check_result result;
    result.timestamp = std::chrono::system_clock::now();

    auto start_time = std::chrono::steady_clock::now();

    auto client = client_.lock();
    if (!client) {
        result.status = common::interfaces::health_status::unhealthy;
        result.message = "Client reference is not available";
        result.check_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return result;
    }

    // Check connection status
    auto state = client->state();
    result.metadata["connection_state"] = to_string(state);

    if (client->is_connected()) {
        result.status = common::interfaces::health_status::healthy;
        result.message = "Client is connected and operational";
    } else if (state == connection_state::connecting ||
               state == connection_state::reconnecting) {
        result.status = common::interfaces::health_status::degraded;
        result.message = "Client is reconnecting";
    } else {
        result.status = common::interfaces::health_status::unhealthy;
        result.message = "Client is disconnected";
    }

    // Add transfer statistics
    auto stats = client->get_statistics();
    result.metadata["active_transfers"] = std::to_string(stats.active_transfers);
    result.metadata["total_errors"] = std::to_string(stats.total_errors);
    result.metadata["total_retries"] = std::to_string(stats.total_retries);
    result.metadata["component_name"] = component_name_;

    result.check_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);

    return result;
}

std::string file_transfer_client_monitorable::get_component_name() const {
    return component_name_;
}

bool file_transfer_client_monitorable::is_client_available() const {
    return !client_.expired();
}

void file_transfer_client_monitorable::set_component_name(const std::string& name) {
    component_name_ = name;
}

#endif  // KCENON_WITH_COMMON_SYSTEM

}  // namespace kcenon::file_transfer::adapters
