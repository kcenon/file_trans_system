/**
 * @file connection_migration.cpp
 * @brief QUIC connection migration implementation
 */

#include "kcenon/file_transfer/transport/connection_migration.h"
#include "kcenon/file_transfer/core/logging.h"

#include <algorithm>
#include <condition_variable>
#include <thread>

#ifdef __APPLE__
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#elif defined(__linux__)
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#elif defined(_WIN32)
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

namespace kcenon::file_transfer {

struct connection_migration_manager::impl {
    migration_config config;
    std::atomic<migration_state> current_state{migration_state::idle};
    std::optional<network_path> current_path_;
    std::vector<network_path> previous_paths_;

    event_callback event_cb;
    network_change_callback network_cb;
    mutable std::mutex callback_mutex;

    mutable std::mutex stats_mutex;
    migration_statistics stats;

    std::atomic<bool> monitoring{false};
    std::thread monitor_thread;
    std::condition_variable monitor_cv;
    std::mutex monitor_mutex;

    std::vector<network_interface> last_known_interfaces;
    mutable std::mutex path_mutex;

    explicit impl(migration_config cfg) : config(std::move(cfg)) {}

    ~impl() {
        stop_monitoring();
    }

    void stop_monitoring() {
        if (monitoring.exchange(false)) {
            monitor_cv.notify_all();
            if (monitor_thread.joinable()) {
                monitor_thread.join();
            }
        }
    }

    void set_state(migration_state new_state) {
        auto old_state = current_state.exchange(new_state);
        if (old_state != new_state) {
            FT_LOG_DEBUG(log_category::transfer,
                "Migration state changed: " +
                std::string(to_string(old_state)) + " -> " +
                std::string(to_string(new_state)));
        }
    }

    void emit_event(const migration_event_data& event) {
        std::lock_guard lock(callback_mutex);
        if (event_cb) {
            event_cb(event);
        }
    }

    void emit_network_change(const std::vector<network_interface>& interfaces) {
        std::lock_guard lock(callback_mutex);
        if (network_cb) {
            network_cb(interfaces);
        }
    }

    void update_stats(const migration_result& result) {
        std::lock_guard lock(stats_mutex);
        stats.total_migrations++;
        if (result.success) {
            stats.successful_migrations++;
            // Update average migration time
            auto total_time = stats.avg_migration_time.count() *
                             (stats.successful_migrations - 1) +
                             result.duration.count();
            stats.avg_migration_time = std::chrono::milliseconds{
                total_time / stats.successful_migrations};
        } else {
            stats.failed_migrations++;
            stats.total_downtime += result.duration;
        }
    }

    void add_previous_path(const network_path& path) {
        std::lock_guard lock(path_mutex);
        // Remove if already exists
        previous_paths_.erase(
            std::remove_if(previous_paths_.begin(), previous_paths_.end(),
                [&path](const network_path& p) { return p == path; }),
            previous_paths_.end());

        // Add to front
        previous_paths_.insert(previous_paths_.begin(), path);

        // Trim to max size
        if (previous_paths_.size() > config.max_previous_paths) {
            previous_paths_.resize(config.max_previous_paths);
        }
    }
};

connection_migration_manager::connection_migration_manager(migration_config config)
    : impl_(std::make_unique<impl>(std::move(config))) {
    get_logger().initialize();
    FT_LOG_DEBUG(log_category::transfer, "Connection migration manager created");
}

connection_migration_manager::~connection_migration_manager() {
    if (impl_) {
        impl_->stop_monitoring();
    }
}

connection_migration_manager::connection_migration_manager(
    connection_migration_manager&&) noexcept = default;
auto connection_migration_manager::operator=(
    connection_migration_manager&&) noexcept -> connection_migration_manager& = default;

auto connection_migration_manager::create(const migration_config& config)
    -> std::unique_ptr<connection_migration_manager> {
    return std::unique_ptr<connection_migration_manager>(
        new connection_migration_manager(config));
}

auto connection_migration_manager::start_monitoring() -> result<void> {
    if (impl_->monitoring) {
        return {};  // Already monitoring
    }

    FT_LOG_INFO(log_category::transfer, "Starting network monitoring");

    impl_->monitoring = true;
    impl_->set_state(migration_state::detecting);

    // Get initial interface list
    impl_->last_known_interfaces = get_available_interfaces();

    impl_->monitor_thread = std::thread([this]() {
        while (impl_->monitoring) {
            auto current_interfaces = get_available_interfaces();

            // Check for changes
            bool changed = false;
            if (current_interfaces.size() != impl_->last_known_interfaces.size()) {
                changed = true;
            } else {
                for (size_t i = 0; i < current_interfaces.size(); ++i) {
                    if (current_interfaces[i].name !=
                            impl_->last_known_interfaces[i].name ||
                        current_interfaces[i].address !=
                            impl_->last_known_interfaces[i].address ||
                        current_interfaces[i].is_up !=
                            impl_->last_known_interfaces[i].is_up) {
                        changed = true;
                        break;
                    }
                }
            }

            if (changed) {
                FT_LOG_INFO(log_category::transfer, "Network change detected");

                {
                    std::lock_guard lock(impl_->stats_mutex);
                    impl_->stats.network_changes_detected++;
                }

                migration_event_data event{migration_event::network_change_detected};
                impl_->emit_event(event);
                impl_->emit_network_change(current_interfaces);

                impl_->last_known_interfaces = current_interfaces;

                // Auto-migrate if enabled
                if (impl_->config.auto_migrate && impl_->current_path_.has_value()) {
                    // Find a new suitable path
                    for (const auto& iface : current_interfaces) {
                        if (iface.is_up &&
                            iface.address != impl_->current_path_->local_address) {

                            network_path new_path;
                            new_path.local_address = iface.address;
                            new_path.interface_name = iface.name;
                            new_path.remote_address =
                                impl_->current_path_->remote_address;
                            new_path.remote_port = impl_->current_path_->remote_port;
                            new_path.created_at = std::chrono::steady_clock::now();

                            // Attempt migration
                            auto result = migrate_to_path(new_path);
                            if (result.has_value() && result.value().success) {
                                break;  // Migration successful
                            }
                        }
                    }
                }
            }

            // Wait for next check interval
            std::unique_lock lock(impl_->monitor_mutex);
            impl_->monitor_cv.wait_for(lock, impl_->config.detection_interval,
                [this] { return !impl_->monitoring.load(); });
        }
    });

    return {};
}

void connection_migration_manager::stop_monitoring() {
    impl_->stop_monitoring();
    impl_->set_state(migration_state::idle);
    FT_LOG_INFO(log_category::transfer, "Stopped network monitoring");
}

auto connection_migration_manager::is_monitoring() const -> bool {
    return impl_->monitoring;
}

auto connection_migration_manager::state() const -> migration_state {
    return impl_->current_state;
}

auto connection_migration_manager::current_path() const -> std::optional<network_path> {
    std::lock_guard lock(impl_->path_mutex);
    return impl_->current_path_;
}

void connection_migration_manager::set_current_path(const network_path& path) {
    std::lock_guard lock(impl_->path_mutex);
    impl_->current_path_ = path;
    FT_LOG_DEBUG(log_category::transfer,
        "Current path set: " + path.to_string());
}

auto connection_migration_manager::previous_paths() const -> std::vector<network_path> {
    std::lock_guard lock(impl_->path_mutex);
    return impl_->previous_paths_;
}

auto connection_migration_manager::migrate_to_path(const network_path& new_path)
    -> result<migration_result> {

    auto start_time = std::chrono::steady_clock::now();

    FT_LOG_INFO(log_category::transfer,
        "Starting migration to: " + new_path.to_string());

    impl_->set_state(migration_state::migrating);

    migration_event_data start_event{migration_event::migration_started};
    start_event.new_path = new_path;
    if (impl_->current_path_.has_value()) {
        start_event.old_path = impl_->current_path_;
    }
    impl_->emit_event(start_event);

    // Probe the new path first if enabled
    if (impl_->config.enable_path_probing) {
        impl_->set_state(migration_state::probing);

        auto probe_result = probe_path(new_path);
        if (!probe_result.has_value() || !probe_result.value()) {
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time);

            impl_->set_state(migration_state::failed);

            migration_event_data fail_event{migration_event::migration_failed};
            fail_event.error_message = "Path probe failed";
            fail_event.old_path = impl_->current_path_;
            fail_event.new_path = new_path;
            impl_->emit_event(fail_event);

            auto result = migration_result::failed(
                impl_->current_path_.value_or(network_path{}),
                "Path probe failed");
            result.duration = duration;
            impl_->update_stats(result);

            FT_LOG_ERROR(log_category::transfer,
                "Migration failed: path probe failed");

            return result;
        }
    }

    // Validate the new path
    impl_->set_state(migration_state::validating);
    auto validate_result = validate_path(new_path);
    if (!validate_result.has_value() || !validate_result.value()) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);

        impl_->set_state(migration_state::failed);

        migration_event_data fail_event{migration_event::migration_failed};
        fail_event.error_message = "Path validation failed";
        fail_event.old_path = impl_->current_path_;
        fail_event.new_path = new_path;
        impl_->emit_event(fail_event);

        auto result = migration_result::failed(
            impl_->current_path_.value_or(network_path{}),
            "Path validation failed");
        result.duration = duration;
        impl_->update_stats(result);

        // Try fallback if enabled
        if (impl_->config.enable_fallback && !impl_->previous_paths_.empty()) {
            FT_LOG_INFO(log_category::transfer,
                "Attempting fallback to previous path");
            return fallback_to_previous();
        }

        FT_LOG_ERROR(log_category::transfer,
            "Migration failed: path validation failed");

        return result;
    }

    // Store old path in history
    if (impl_->current_path_.has_value() && impl_->config.keep_previous_paths) {
        impl_->add_previous_path(impl_->current_path_.value());
    }

    auto old_path = impl_->current_path_.value_or(network_path{});

    // Update current path
    {
        std::lock_guard lock(impl_->path_mutex);
        impl_->current_path_ = new_path;
        impl_->current_path_->validated = true;
    }

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);

    impl_->set_state(migration_state::completed);

    migration_event_data complete_event{migration_event::migration_completed};
    complete_event.old_path = old_path;
    complete_event.new_path = new_path;
    impl_->emit_event(complete_event);

    auto result = migration_result::succeeded(old_path, new_path, duration);
    impl_->update_stats(result);

    FT_LOG_INFO(log_category::transfer,
        "Migration completed in " + std::to_string(duration.count()) + "ms");

    impl_->set_state(migration_state::idle);

    return result;
}

auto connection_migration_manager::probe_path(const network_path& path)
    -> result<bool> {

    FT_LOG_DEBUG(log_category::transfer,
        "Probing path: " + path.to_string());

    migration_event_data start_event{migration_event::path_probe_started};
    start_event.new_path = path;
    impl_->emit_event(start_event);

    {
        std::lock_guard lock(impl_->stats_mutex);
        impl_->stats.path_probes++;
    }

    // Simulate path probe (in real implementation, send PATH_CHALLENGE)
    // For now, check if the interface exists and is up
    auto interfaces = get_available_interfaces();
    bool found = false;

    for (const auto& iface : interfaces) {
        if (iface.address == path.local_address && iface.is_up) {
            found = true;
            break;
        }
    }

    if (found) {
        migration_event_data success_event{migration_event::path_probe_succeeded};
        success_event.new_path = path;
        impl_->emit_event(success_event);

        FT_LOG_DEBUG(log_category::transfer,
            "Path probe succeeded: " + path.to_string());

        return true;
    } else {
        migration_event_data fail_event{migration_event::path_probe_failed};
        fail_event.new_path = path;
        fail_event.error_message = "Interface not found or down";
        impl_->emit_event(fail_event);

        FT_LOG_DEBUG(log_category::transfer,
            "Path probe failed: " + path.to_string());

        return false;
    }
}

auto connection_migration_manager::validate_path(const network_path& path)
    -> result<bool> {

    FT_LOG_DEBUG(log_category::transfer,
        "Validating path: " + path.to_string());

    // In real implementation, perform QUIC path validation
    // (PATH_CHALLENGE/PATH_RESPONSE exchange)
    // For now, basic validation that the path is reachable

    auto interfaces = get_available_interfaces();
    bool valid = false;

    for (const auto& iface : interfaces) {
        if (iface.address == path.local_address && iface.is_up) {
            valid = true;
            break;
        }
    }

    if (valid) {
        migration_event_data event{migration_event::path_validated};
        event.new_path = path;
        impl_->emit_event(event);

        FT_LOG_DEBUG(log_category::transfer,
            "Path validation succeeded: " + path.to_string());
    }

    return valid;
}

auto connection_migration_manager::fallback_to_previous() -> result<migration_result> {
    std::vector<network_path> paths;
    {
        std::lock_guard lock(impl_->path_mutex);
        paths = impl_->previous_paths_;
    }

    if (paths.empty()) {
        return unexpected{error{error_code::internal_error, "No previous paths available"}};
    }

    FT_LOG_INFO(log_category::transfer, "Triggering fallback to previous path");

    migration_event_data event{migration_event::fallback_triggered};
    event.new_path = paths.front();
    if (impl_->current_path_.has_value()) {
        event.old_path = impl_->current_path_;
    }
    impl_->emit_event(event);

    // Try each previous path in order
    for (const auto& prev_path : paths) {
        auto probe_result = probe_path(prev_path);
        if (probe_result.has_value() && probe_result.value()) {
            return migrate_to_path(prev_path);
        }
    }

    return unexpected{error{error_code::internal_error, "All fallback paths failed"}};
}

auto connection_migration_manager::get_available_interfaces() const
    -> std::vector<network_interface> {

    std::vector<network_interface> interfaces;

#if defined(__APPLE__) || defined(__linux__)
    struct ifaddrs* addrs = nullptr;
    if (getifaddrs(&addrs) != 0) {
        return interfaces;
    }

    for (struct ifaddrs* addr = addrs; addr != nullptr; addr = addr->ifa_next) {
        if (addr->ifa_addr == nullptr) {
            continue;
        }

        // Only IPv4 for now
        if (addr->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        network_interface iface;
        iface.name = addr->ifa_name;
        iface.is_up = (addr->ifa_flags & IFF_UP) != 0;

        auto* sin = reinterpret_cast<struct sockaddr_in*>(addr->ifa_addr);
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sin->sin_addr, ip_str, sizeof(ip_str));
        iface.address = ip_str;

        // Skip loopback
        if (iface.address == "127.0.0.1") {
            continue;
        }

        // Detect wireless interfaces (platform-specific)
#ifdef __APPLE__
        iface.is_wireless = (iface.name.find("en") == 0 && iface.name != "en0") ||
                            iface.name.find("awdl") == 0;
#else
        iface.is_wireless = iface.name.find("wlan") == 0 ||
                            iface.name.find("wlp") == 0;
#endif

        // Set priority (wired > wireless)
        iface.priority = iface.is_wireless ? 1 : 2;

        interfaces.push_back(iface);
    }

    freeifaddrs(addrs);

    // Sort by priority (highest first)
    std::sort(interfaces.begin(), interfaces.end(),
        [](const network_interface& a, const network_interface& b) {
            return a.priority > b.priority;
        });

#elif defined(_WIN32)
    ULONG buffer_size = 0;
    GetAdaptersAddresses(AF_INET, 0, nullptr, nullptr, &buffer_size);

    std::vector<uint8_t> buffer(buffer_size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());

    if (GetAdaptersAddresses(AF_INET, 0, nullptr, adapters, &buffer_size) == NO_ERROR) {
        for (auto* adapter = adapters; adapter != nullptr; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp) {
                continue;
            }

            for (auto* unicast = adapter->FirstUnicastAddress;
                 unicast != nullptr;
                 unicast = unicast->Next) {

                if (unicast->Address.lpSockaddr->sa_family != AF_INET) {
                    continue;
                }

                network_interface iface;

                // Convert adapter name
                std::wstring wide_name(adapter->FriendlyName);
                iface.name = std::string(wide_name.begin(), wide_name.end());
                iface.is_up = true;

                auto* sin = reinterpret_cast<struct sockaddr_in*>(
                    unicast->Address.lpSockaddr);
                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &sin->sin_addr, ip_str, sizeof(ip_str));
                iface.address = ip_str;

                // Skip loopback
                if (iface.address == "127.0.0.1") {
                    continue;
                }

                iface.is_wireless =
                    adapter->IfType == IF_TYPE_IEEE80211;
                iface.priority = iface.is_wireless ? 1 : 2;

                interfaces.push_back(iface);
            }
        }
    }

    // Sort by priority
    std::sort(interfaces.begin(), interfaces.end(),
        [](const network_interface& a, const network_interface& b) {
            return a.priority > b.priority;
        });
#endif

    return interfaces;
}

auto connection_migration_manager::detect_network_changes()
    -> std::vector<network_interface> {
    return get_available_interfaces();
}

void connection_migration_manager::on_migration_event(event_callback callback) {
    std::lock_guard lock(impl_->callback_mutex);
    impl_->event_cb = std::move(callback);
}

void connection_migration_manager::on_network_change(network_change_callback callback) {
    std::lock_guard lock(impl_->callback_mutex);
    impl_->network_cb = std::move(callback);
}

auto connection_migration_manager::get_statistics() const -> migration_statistics {
    std::lock_guard lock(impl_->stats_mutex);
    return impl_->stats;
}

void connection_migration_manager::reset_statistics() {
    std::lock_guard lock(impl_->stats_mutex);
    impl_->stats = migration_statistics{};
}

auto connection_migration_manager::config() const -> const migration_config& {
    return impl_->config;
}

void connection_migration_manager::set_config(const migration_config& config) {
    impl_->config = config;
}

auto connection_migration_manager::is_migration_available() const -> bool {
    auto interfaces = get_available_interfaces();
    return interfaces.size() > 1;  // Need at least 2 interfaces for migration
}

void connection_migration_manager::cancel_migration() {
    if (impl_->current_state == migration_state::migrating ||
        impl_->current_state == migration_state::probing ||
        impl_->current_state == migration_state::validating) {

        FT_LOG_INFO(log_category::transfer, "Cancelling ongoing migration");
        impl_->set_state(migration_state::idle);

        migration_event_data event{migration_event::migration_failed};
        event.error_message = "Migration cancelled";
        impl_->emit_event(event);
    }
}

void connection_migration_manager::emit_event(const migration_event_data& event) {
    impl_->emit_event(event);
}

void connection_migration_manager::update_statistics(const migration_result& result) {
    impl_->update_stats(result);
}

}  // namespace kcenon::file_transfer
