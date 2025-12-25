/**
 * @file session_resumption.cpp
 * @brief QUIC session ticket management implementation
 */

#include "kcenon/file_transfer/transport/session_resumption.h"
#include "kcenon/file_transfer/core/logging.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace kcenon::file_transfer {

// ============================================================================
// memory_session_store implementation
// ============================================================================

memory_session_store::memory_session_store(session_store_config config)
    : config_(std::move(config)) {
}

auto memory_session_store::create(const session_store_config& config)
    -> std::unique_ptr<memory_session_store> {
    return std::unique_ptr<memory_session_store>(new memory_session_store(config));
}

auto memory_session_store::store(const session_ticket& ticket) -> result<void> {
    std::lock_guard lock(mutex_);

    // Check capacity
    if (config_.max_tickets > 0 && tickets_.size() >= config_.max_tickets) {
        // Remove oldest or expired ticket
        auto oldest = tickets_.end();
        auto oldest_time = std::chrono::system_clock::time_point::max();

        for (auto it = tickets_.begin(); it != tickets_.end(); ++it) {
            if (!it->second.is_valid()) {
                oldest = it;
                break;
            }
            if (it->second.issued_at < oldest_time) {
                oldest_time = it->second.issued_at;
                oldest = it;
            }
        }

        if (oldest != tickets_.end()) {
            FT_LOG_DEBUG(log_category::transfer,
                "Session store full, removing ticket for: " + oldest->first);
            tickets_.erase(oldest);
        }
    }

    tickets_[ticket.server_id] = ticket;
    FT_LOG_DEBUG(log_category::transfer,
        "Stored session ticket for: " + ticket.server_id);

    return {};
}

auto memory_session_store::retrieve(const std::string& server_id)
    -> std::optional<session_ticket> {
    std::lock_guard lock(mutex_);

    auto it = tickets_.find(server_id);
    if (it == tickets_.end()) {
        return std::nullopt;
    }

    // Check validity
    if (!it->second.is_valid()) {
        FT_LOG_DEBUG(log_category::transfer,
            "Session ticket expired for: " + server_id);
        tickets_.erase(it);
        return std::nullopt;
    }

    // Check minimum remaining lifetime
    auto remaining = it->second.time_until_expiry();
    if (remaining < config_.min_remaining_lifetime) {
        FT_LOG_DEBUG(log_category::transfer,
            "Session ticket near expiry for: " + server_id);
        tickets_.erase(it);
        return std::nullopt;
    }

    return it->second;
}

auto memory_session_store::remove(const std::string& server_id) -> bool {
    std::lock_guard lock(mutex_);
    auto erased = tickets_.erase(server_id);
    if (erased > 0) {
        FT_LOG_DEBUG(log_category::transfer,
            "Removed session ticket for: " + server_id);
    }
    return erased > 0;
}

auto memory_session_store::cleanup_expired() -> std::size_t {
    std::lock_guard lock(mutex_);

    std::size_t removed = 0;
    auto now = std::chrono::system_clock::now();

    for (auto it = tickets_.begin(); it != tickets_.end();) {
        if (it->second.expires_at <= now) {
            FT_LOG_DEBUG(log_category::transfer,
                "Cleaning up expired ticket for: " + it->first);
            it = tickets_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }

    if (removed > 0) {
        FT_LOG_INFO(log_category::transfer,
            "Cleaned up " + std::to_string(removed) + " expired session tickets");
    }

    return removed;
}

void memory_session_store::clear() {
    std::lock_guard lock(mutex_);
    tickets_.clear();
    FT_LOG_DEBUG(log_category::transfer, "Cleared all session tickets");
}

auto memory_session_store::size() const -> std::size_t {
    std::lock_guard lock(mutex_);
    return tickets_.size();
}

auto memory_session_store::has_ticket(const std::string& server_id) const -> bool {
    std::lock_guard lock(mutex_);
    auto it = tickets_.find(server_id);
    return it != tickets_.end() && it->second.is_valid();
}

// ============================================================================
// file_session_store implementation
// ============================================================================

namespace {

// Simple binary format for session tickets
// Format: [magic][version][count][tickets...]
// Ticket: [server_id_len][server_id][ticket_len][ticket_data][issued][expires]
//         [max_early_data][alpn_len][alpn][sni_len][sni]

constexpr uint32_t MAGIC = 0x51534553;  // "SESQ" in little endian
constexpr uint32_t VERSION = 1;

template <typename T>
void serialize_value(std::ostream& os, const T& value) {
    os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
auto deserialize_value(std::istream& is) -> T {
    T value{};
    is.read(reinterpret_cast<char*>(&value), sizeof(T));
    return value;
}

void serialize_string(std::ostream& os, const std::string& str) {
    auto len = static_cast<uint32_t>(str.size());
    serialize_value(os, len);
    os.write(str.data(), static_cast<std::streamsize>(len));
}

auto deserialize_string(std::istream& is) -> std::string {
    auto len = deserialize_value<uint32_t>(is);
    std::string str(len, '\0');
    is.read(str.data(), static_cast<std::streamsize>(len));
    return str;
}

void serialize_bytes(std::ostream& os, const std::vector<uint8_t>& data) {
    auto len = static_cast<uint32_t>(data.size());
    serialize_value(os, len);
    os.write(reinterpret_cast<const char*>(data.data()),
             static_cast<std::streamsize>(len));
}

auto deserialize_bytes(std::istream& is) -> std::vector<uint8_t> {
    auto len = deserialize_value<uint32_t>(is);
    std::vector<uint8_t> data(len);
    is.read(reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(len));
    return data;
}

void serialize_timepoint(std::ostream& os, std::chrono::system_clock::time_point tp) {
    auto duration = tp.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    serialize_value(os, millis);
}

auto deserialize_timepoint(std::istream& is) -> std::chrono::system_clock::time_point {
    auto millis = deserialize_value<int64_t>(is);
    return std::chrono::system_clock::time_point{std::chrono::milliseconds{millis}};
}

}  // namespace

file_session_store::file_session_store(session_store_config config)
    : config_(std::move(config)) {
    if (!config_.storage_path.empty()) {
        (void)load_internal();
    }
}

file_session_store::~file_session_store() {
    if (dirty_ && !config_.storage_path.empty()) {
        (void)save_internal();
    }
}

auto file_session_store::create(const session_store_config& config)
    -> std::unique_ptr<file_session_store> {
    if (config.storage_path.empty()) {
        return nullptr;
    }
    return std::unique_ptr<file_session_store>(new file_session_store(config));
}

auto file_session_store::store(const session_ticket& ticket) -> result<void> {
    std::lock_guard lock(mutex_);

    // Check capacity
    if (config_.max_tickets > 0 && tickets_.size() >= config_.max_tickets) {
        auto oldest = tickets_.end();
        auto oldest_time = std::chrono::system_clock::time_point::max();

        for (auto it = tickets_.begin(); it != tickets_.end(); ++it) {
            if (!it->second.is_valid()) {
                oldest = it;
                break;
            }
            if (it->second.issued_at < oldest_time) {
                oldest_time = it->second.issued_at;
                oldest = it;
            }
        }

        if (oldest != tickets_.end()) {
            tickets_.erase(oldest);
        }
    }

    tickets_[ticket.server_id] = ticket;
    dirty_ = true;

    FT_LOG_DEBUG(log_category::transfer,
        "Stored session ticket for: " + ticket.server_id);

    // Auto-save
    auto result = save_internal();
    if (!result.has_value()) {
        FT_LOG_WARN(log_category::transfer,
            "Failed to persist session ticket: " + result.error().message);
    }

    return {};
}

auto file_session_store::retrieve(const std::string& server_id)
    -> std::optional<session_ticket> {
    std::lock_guard lock(mutex_);

    auto it = tickets_.find(server_id);
    if (it == tickets_.end()) {
        return std::nullopt;
    }

    if (!it->second.is_valid()) {
        tickets_.erase(it);
        dirty_ = true;
        return std::nullopt;
    }

    auto remaining = it->second.time_until_expiry();
    if (remaining < config_.min_remaining_lifetime) {
        tickets_.erase(it);
        dirty_ = true;
        return std::nullopt;
    }

    return it->second;
}

auto file_session_store::remove(const std::string& server_id) -> bool {
    std::lock_guard lock(mutex_);
    auto erased = tickets_.erase(server_id);
    if (erased > 0) {
        dirty_ = true;
    }
    return erased > 0;
}

auto file_session_store::cleanup_expired() -> std::size_t {
    std::lock_guard lock(mutex_);

    std::size_t removed = 0;
    auto now = std::chrono::system_clock::now();

    for (auto it = tickets_.begin(); it != tickets_.end();) {
        if (it->second.expires_at <= now) {
            it = tickets_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }

    if (removed > 0) {
        dirty_ = true;
        FT_LOG_INFO(log_category::transfer,
            "Cleaned up " + std::to_string(removed) + " expired session tickets");
    }

    return removed;
}

void file_session_store::clear() {
    std::lock_guard lock(mutex_);
    tickets_.clear();
    dirty_ = true;
}

auto file_session_store::size() const -> std::size_t {
    std::lock_guard lock(mutex_);
    return tickets_.size();
}

auto file_session_store::has_ticket(const std::string& server_id) const -> bool {
    std::lock_guard lock(mutex_);
    auto it = tickets_.find(server_id);
    return it != tickets_.end() && it->second.is_valid();
}

auto file_session_store::save() -> result<void> {
    std::lock_guard lock(mutex_);
    return save_internal();
}

auto file_session_store::load() -> result<void> {
    std::lock_guard lock(mutex_);
    return load_internal();
}

auto file_session_store::save_internal() -> result<void> {
    if (config_.storage_path.empty()) {
        return {};
    }

    try {
        // Create parent directory if needed
        auto parent = config_.storage_path.parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent)) {
            std::filesystem::create_directories(parent);
        }

        std::ofstream file(config_.storage_path, std::ios::binary);
        if (!file) {
            return unexpected{error{error_code::file_write_error,
                "Failed to open session file for writing"}};
        }

        serialize_value(file, MAGIC);
        serialize_value(file, VERSION);
        serialize_value(file, static_cast<uint32_t>(tickets_.size()));

        for (const auto& [server_id, ticket] : tickets_) {
            serialize_string(file, ticket.server_id);
            serialize_bytes(file, ticket.ticket_data);
            serialize_timepoint(file, ticket.issued_at);
            serialize_timepoint(file, ticket.expires_at);
            serialize_value(file, ticket.max_early_data_size);
            serialize_string(file, ticket.alpn_protocol);
            serialize_string(file, ticket.server_name);
        }

        dirty_ = false;
        FT_LOG_DEBUG(log_category::transfer,
            "Saved " + std::to_string(tickets_.size()) + " session tickets");

        return {};
    } catch (const std::exception& e) {
        return unexpected{error{error_code::file_write_error,
            std::string("Failed to save session tickets: ") + e.what()}};
    }
}

auto file_session_store::load_internal() -> result<void> {
    if (config_.storage_path.empty() ||
        !std::filesystem::exists(config_.storage_path)) {
        return {};
    }

    try {
        std::ifstream file(config_.storage_path, std::ios::binary);
        if (!file) {
            return unexpected{error{error_code::file_read_error,
                "Failed to open session file for reading"}};
        }

        auto magic = deserialize_value<uint32_t>(file);
        if (magic != MAGIC) {
            return unexpected{error{error_code::invalid_configuration,
                "Invalid session file format"}};
        }

        auto version = deserialize_value<uint32_t>(file);
        if (version != VERSION) {
            FT_LOG_WARN(log_category::transfer,
                "Session file version mismatch, clearing tickets");
            tickets_.clear();
            return {};
        }

        auto count = deserialize_value<uint32_t>(file);
        tickets_.clear();

        for (uint32_t i = 0; i < count && file.good(); ++i) {
            session_ticket ticket;
            ticket.server_id = deserialize_string(file);
            ticket.ticket_data = deserialize_bytes(file);
            ticket.issued_at = deserialize_timepoint(file);
            ticket.expires_at = deserialize_timepoint(file);
            ticket.max_early_data_size = deserialize_value<uint32_t>(file);
            ticket.alpn_protocol = deserialize_string(file);
            ticket.server_name = deserialize_string(file);

            if (ticket.is_valid()) {
                tickets_[ticket.server_id] = std::move(ticket);
            }
        }

        FT_LOG_DEBUG(log_category::transfer,
            "Loaded " + std::to_string(tickets_.size()) + " session tickets");

        return {};
    } catch (const std::exception& e) {
        return unexpected{error{error_code::file_read_error,
            std::string("Failed to load session tickets: ") + e.what()}};
    }
}

// ============================================================================
// session_resumption_manager implementation
// ============================================================================

struct session_resumption_manager::impl {
    session_resumption_config config;
    std::unique_ptr<session_store> store;

    explicit impl(session_resumption_config cfg) : config(std::move(cfg)) {
        if (!config.store_config.storage_path.empty()) {
            store = file_session_store::create(config.store_config);
        }
        if (!store) {
            store = memory_session_store::create(config.store_config);
        }
    }
};

session_resumption_manager::session_resumption_manager(session_resumption_config config)
    : impl_(std::make_unique<impl>(std::move(config))) {
    FT_LOG_DEBUG(log_category::transfer, "Session resumption manager created");
}

session_resumption_manager::~session_resumption_manager() = default;

session_resumption_manager::session_resumption_manager(session_resumption_manager&&) noexcept = default;
auto session_resumption_manager::operator=(session_resumption_manager&&) noexcept
    -> session_resumption_manager& = default;

auto session_resumption_manager::create(const session_resumption_config& config)
    -> std::unique_ptr<session_resumption_manager> {
    return std::unique_ptr<session_resumption_manager>(
        new session_resumption_manager(config));
}

auto session_resumption_manager::make_server_id(const std::string& host, uint16_t port)
    -> std::string {
    return host + ":" + std::to_string(port);
}

auto session_resumption_manager::get_ticket_for_server(const std::string& host, uint16_t port)
    -> std::optional<std::vector<uint8_t>> {
    if (!impl_->config.enable_0rtt) {
        return std::nullopt;
    }

    auto server_id = make_server_id(host, port);
    auto ticket = impl_->store->retrieve(server_id);

    if (!ticket.has_value()) {
        return std::nullopt;
    }

    if (!ticket->allows_early_data()) {
        return std::nullopt;
    }

    FT_LOG_DEBUG(log_category::transfer,
        "Retrieved session ticket for 0-RTT: " + server_id);

    return ticket->ticket_data;
}

auto session_resumption_manager::get_session(const std::string& host, uint16_t port)
    -> std::optional<session_ticket> {
    auto server_id = make_server_id(host, port);
    return impl_->store->retrieve(server_id);
}

auto session_resumption_manager::store_ticket(
    const std::string& host,
    uint16_t port,
    std::vector<uint8_t> ticket_data,
    std::optional<std::chrono::seconds> lifetime,
    uint32_t max_early_data,
    const std::string& alpn) -> result<void> {

    session_ticket ticket;
    ticket.server_id = make_server_id(host, port);
    ticket.ticket_data = std::move(ticket_data);
    ticket.issued_at = std::chrono::system_clock::now();

    auto ticket_lifetime = lifetime.value_or(impl_->config.store_config.default_lifetime);
    ticket.expires_at = ticket.issued_at + ticket_lifetime;
    ticket.max_early_data_size = max_early_data;
    ticket.alpn_protocol = alpn;
    ticket.server_name = host;

    FT_LOG_DEBUG(log_category::transfer,
        "Storing new session ticket for: " + ticket.server_id);

    auto result = impl_->store->store(ticket);

    if (result.has_value() && impl_->config.on_ticket_received) {
        impl_->config.on_ticket_received(ticket);
    }

    return result;
}

void session_resumption_manager::on_0rtt_rejected(const std::string& host, uint16_t port) {
    auto server_id = make_server_id(host, port);

    FT_LOG_INFO(log_category::transfer,
        "0-RTT rejected for: " + server_id);

    // Remove the invalid ticket
    impl_->store->remove(server_id);

    if (impl_->config.on_0rtt_rejected) {
        impl_->config.on_0rtt_rejected(server_id);
    }
}

void session_resumption_manager::on_0rtt_accepted(const std::string& host, uint16_t port) {
    auto server_id = make_server_id(host, port);

    FT_LOG_DEBUG(log_category::transfer,
        "0-RTT accepted for: " + server_id);

    if (impl_->config.on_0rtt_accepted) {
        impl_->config.on_0rtt_accepted(server_id);
    }
}

auto session_resumption_manager::can_use_0rtt(const std::string& host, uint16_t port) const
    -> bool {
    if (!impl_->config.enable_0rtt) {
        return false;
    }

    auto server_id = make_server_id(host, port);
    auto ticket = impl_->store->retrieve(server_id);

    return ticket.has_value() && ticket->allows_early_data();
}

auto session_resumption_manager::remove_ticket(const std::string& host, uint16_t port) -> bool {
    auto server_id = make_server_id(host, port);
    return impl_->store->remove(server_id);
}

void session_resumption_manager::clear_all_tickets() {
    impl_->store->clear();
}

auto session_resumption_manager::config() const -> const session_resumption_config& {
    return impl_->config;
}

auto session_resumption_manager::store() -> session_store& {
    return *impl_->store;
}

auto session_resumption_manager::store() const -> const session_store& {
    return *impl_->store;
}

}  // namespace kcenon::file_transfer
