/**
 * @file resume_handler.cpp
 * @brief Implementation of resume_handler for transfer state persistence
 */

#include <kcenon/file_transfer/core/resume_handler.h>
#include <kcenon/file_transfer/core/logging.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <unordered_map>

namespace kcenon::file_transfer {

// ============================================================================
// transfer_state implementation
// ============================================================================

transfer_state::transfer_state(
    const transfer_id& transfer_id,
    std::string file_name,
    uint64_t file_size,
    uint32_t num_chunks,
    std::string file_hash)
    : id(transfer_id)
    , filename(std::move(file_name))
    , total_size(file_size)
    , transferred_bytes(0)
    , total_chunks(num_chunks)
    , chunk_bitmap(num_chunks, false)
    , sha256(std::move(file_hash))
    , started_at(std::chrono::system_clock::now())
    , last_activity(std::chrono::system_clock::now()) {
}

auto transfer_state::received_chunk_count() const -> uint32_t {
    return static_cast<uint32_t>(
        std::count(chunk_bitmap.begin(), chunk_bitmap.end(), true));
}

auto transfer_state::completion_percentage() const -> double {
    if (total_chunks == 0) {
        return 0.0;
    }
    return static_cast<double>(received_chunk_count()) /
           static_cast<double>(total_chunks) * 100.0;
}

auto transfer_state::is_complete() const -> bool {
    return received_chunk_count() == total_chunks;
}

// ============================================================================
// resume_handler_config implementation
// ============================================================================

resume_handler_config::resume_handler_config()
    : state_directory(std::filesystem::temp_directory_path() / "file_trans_states")
    , checkpoint_interval(10)
    , state_ttl(86400)
    , auto_cleanup(true) {
}

resume_handler_config::resume_handler_config(std::filesystem::path dir)
    : state_directory(std::move(dir))
    , checkpoint_interval(10)
    , state_ttl(86400)
    , auto_cleanup(true) {
}

// ============================================================================
// JSON serialization helpers (simple implementation without external library)
// ============================================================================

namespace {

auto escape_json_string(const std::string& s) -> std::string {
    std::ostringstream o;
    for (auto c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    o << "\\u" << std::hex << std::setw(4)
                      << std::setfill('0') << static_cast<int>(c);
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

auto unescape_json_string(const std::string& s) -> std::string {
    std::string result;
    result.reserve(s.size());

    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case '"': result += '"'; ++i; break;
                case '\\': result += '\\'; ++i; break;
                case 'b': result += '\b'; ++i; break;
                case 'f': result += '\f'; ++i; break;
                case 'n': result += '\n'; ++i; break;
                case 'r': result += '\r'; ++i; break;
                case 't': result += '\t'; ++i; break;
                case 'u':
                    if (i + 5 < s.size()) {
                        auto hex = s.substr(i + 2, 4);
                        auto code = std::stoi(hex, nullptr, 16);
                        result += static_cast<char>(code);
                        i += 5;
                    }
                    break;
                default: result += s[i]; break;
            }
        } else {
            result += s[i];
        }
    }
    return result;
}

auto time_point_to_int64(std::chrono::system_clock::time_point tp) -> int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()).count();
}

auto int64_to_time_point(int64_t ms) -> std::chrono::system_clock::time_point {
    return std::chrono::system_clock::time_point(
        std::chrono::milliseconds(ms));
}

auto bitmap_to_hex_string(const std::vector<bool>& bitmap) -> std::string {
    std::ostringstream oss;
    uint8_t byte = 0;
    int bit_count = 0;

    for (std::size_t i = 0; i < bitmap.size(); ++i) {
        if (bitmap[i]) {
            byte |= (1 << (7 - bit_count));
        }
        ++bit_count;

        if (bit_count == 8) {
            oss << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(byte);
            byte = 0;
            bit_count = 0;
        }
    }

    // Handle remaining bits
    if (bit_count > 0) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(byte);
    }

    return oss.str();
}

auto hex_string_to_bitmap(const std::string& hex, uint32_t total_bits)
    -> std::vector<bool> {
    std::vector<bool> bitmap(total_bits, false);

    std::size_t bit_index = 0;
    for (std::size_t i = 0; i < hex.size() && bit_index < total_bits; i += 2) {
        if (i + 1 >= hex.size()) break;

        auto byte_str = hex.substr(i, 2);
        auto byte = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));

        for (int j = 7; j >= 0 && bit_index < total_bits; --j, ++bit_index) {
            bitmap[bit_index] = (byte & (1 << j)) != 0;
        }
    }

    return bitmap;
}

auto serialize_state_to_json(const transfer_state& state) -> std::string {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"id\": \"" << state.id.to_string() << "\",\n";
    oss << "  \"filename\": \"" << escape_json_string(state.filename) << "\",\n";
    oss << "  \"total_size\": " << state.total_size << ",\n";
    oss << "  \"transferred_bytes\": " << state.transferred_bytes << ",\n";
    oss << "  \"total_chunks\": " << state.total_chunks << ",\n";
    oss << "  \"chunk_bitmap\": \"" << bitmap_to_hex_string(state.chunk_bitmap) << "\",\n";
    oss << "  \"sha256\": \"" << state.sha256 << "\",\n";
    oss << "  \"started_at\": " << time_point_to_int64(state.started_at) << ",\n";
    oss << "  \"last_activity\": " << time_point_to_int64(state.last_activity) << "\n";
    oss << "}";
    return oss.str();
}

auto extract_json_value(const std::string& json, const std::string& key)
    -> std::string {
    auto key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) {
        return "";
    }

    auto colon_pos = json.find(':', key_pos);
    if (colon_pos == std::string::npos) {
        return "";
    }

    // Skip whitespace
    auto value_start = colon_pos + 1;
    while (value_start < json.size() &&
           (json[value_start] == ' ' || json[value_start] == '\n' ||
            json[value_start] == '\t')) {
        ++value_start;
    }

    if (value_start >= json.size()) {
        return "";
    }

    // Check if it's a string value
    if (json[value_start] == '"') {
        auto string_end = value_start + 1;
        while (string_end < json.size()) {
            if (json[string_end] == '"' && json[string_end - 1] != '\\') {
                break;
            }
            ++string_end;
        }
        return json.substr(value_start + 1, string_end - value_start - 1);
    }

    // Numeric value - find end
    auto value_end = value_start;
    while (value_end < json.size() &&
           json[value_end] != ',' && json[value_end] != '\n' &&
           json[value_end] != '}') {
        ++value_end;
    }

    auto value = json.substr(value_start, value_end - value_start);
    // Trim whitespace
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }

    return value;
}

auto deserialize_state_from_json(const std::string& json)
    -> result<transfer_state> {
    transfer_state state;

    // Parse id
    auto id_str = extract_json_value(json, "id");
    if (id_str.empty()) {
        return unexpected(error(error_code::internal_error, "missing id field"));
    }
    auto parsed_id = transfer_id::from_string(id_str);
    if (!parsed_id) {
        return unexpected(error(error_code::internal_error, "invalid transfer id"));
    }
    state.id = *parsed_id;

    // Parse filename
    state.filename = unescape_json_string(extract_json_value(json, "filename"));

    // Parse numeric fields
    try {
        state.total_size = std::stoull(extract_json_value(json, "total_size"));
        state.transferred_bytes = std::stoull(
            extract_json_value(json, "transferred_bytes"));
        state.total_chunks = static_cast<uint32_t>(
            std::stoul(extract_json_value(json, "total_chunks")));

        auto started_ms = std::stoll(extract_json_value(json, "started_at"));
        auto activity_ms = std::stoll(extract_json_value(json, "last_activity"));
        state.started_at = int64_to_time_point(started_ms);
        state.last_activity = int64_to_time_point(activity_ms);
    } catch (const std::exception&) {
        return unexpected(error(error_code::internal_error, "invalid numeric field"));
    }

    // Parse sha256
    state.sha256 = extract_json_value(json, "sha256");

    // Parse chunk bitmap
    auto bitmap_hex = extract_json_value(json, "chunk_bitmap");
    state.chunk_bitmap = hex_string_to_bitmap(bitmap_hex, state.total_chunks);

    return state;
}

auto get_state_file_path(const std::filesystem::path& dir, const transfer_id& id)
    -> std::filesystem::path {
    return dir / (id.to_string() + ".json");
}

}  // namespace

// ============================================================================
// resume_handler::impl
// ============================================================================

class resume_handler::impl {
public:
    explicit impl(const resume_handler_config& cfg)
        : config_(cfg) {
        // Create state directory if it doesn't exist
        std::error_code ec;
        std::filesystem::create_directories(config_.state_directory, ec);
    }

    auto save_state(const transfer_state& state) -> result<void> {
        std::unique_lock lock(mutex_);

        FT_LOG_DEBUG(log_category::resume,
            "Saving transfer state: " + state.id.to_string() +
            " (" + std::to_string(state.received_chunk_count()) + "/" +
            std::to_string(state.total_chunks) + " chunks)");

        // Update cache
        cache_[state.id] = state;

        // Write to file
        auto path = get_state_file_path(config_.state_directory, state.id);
        std::ofstream file(path);
        if (!file) {
            FT_LOG_ERROR(log_category::resume,
                "Failed to open state file for writing: " + path.string());
            return unexpected(error(error_code::file_write_error,
                "failed to open state file for writing"));
        }

        file << serialize_state_to_json(state);
        if (!file) {
            FT_LOG_ERROR(log_category::resume,
                "Failed to write state file: " + path.string());
            return unexpected(error(error_code::file_write_error,
                "failed to write state file"));
        }

        FT_LOG_TRACE(log_category::resume,
            "State persisted to: " + path.string());
        return {};
    }

    auto load_state(const transfer_id& id) -> result<transfer_state> {
        FT_LOG_TRACE(log_category::resume,
            "Loading transfer state: " + id.to_string());

        // Check cache first
        {
            std::shared_lock lock(mutex_);
            auto it = cache_.find(id);
            if (it != cache_.end()) {
                FT_LOG_TRACE(log_category::resume,
                    "State found in cache: " + id.to_string());
                return it->second;
            }
        }

        // Load from file
        auto path = get_state_file_path(config_.state_directory, id);
        if (!std::filesystem::exists(path)) {
            FT_LOG_DEBUG(log_category::resume,
                "State file not found: " + path.string());
            return unexpected(error(error_code::file_not_found,
                "state file not found"));
        }

        std::ifstream file(path);
        if (!file) {
            FT_LOG_ERROR(log_category::resume,
                "Failed to open state file: " + path.string());
            return unexpected(error(error_code::file_read_error,
                "failed to open state file"));
        }

        std::ostringstream oss;
        oss << file.rdbuf();

        auto result = deserialize_state_from_json(oss.str());
        if (!result) {
            FT_LOG_ERROR(log_category::resume,
                "Failed to deserialize state: " + id.to_string());
            return result;
        }

        FT_LOG_DEBUG(log_category::resume,
            "State recovered: " + id.to_string() +
            " (" + std::to_string(result.value().received_chunk_count()) + "/" +
            std::to_string(result.value().total_chunks) + " chunks, " +
            std::to_string(result.value().completion_percentage()) + "% complete)");

        // Update cache
        {
            std::unique_lock lock(mutex_);
            cache_[id] = result.value();
        }

        return result;
    }

    auto delete_state(const transfer_id& id) -> result<void> {
        std::unique_lock lock(mutex_);

        FT_LOG_DEBUG(log_category::resume,
            "Deleting transfer state: " + id.to_string());

        // Remove from cache
        cache_.erase(id);

        // Remove file
        auto path = get_state_file_path(config_.state_directory, id);
        std::error_code ec;
        if (std::filesystem::exists(path)) {
            std::filesystem::remove(path, ec);
            if (ec) {
                FT_LOG_ERROR(log_category::resume,
                    "Failed to delete state file: " + path.string() +
                    " (" + ec.message() + ")");
                return unexpected(error(error_code::file_write_error,
                    "failed to delete state file: " + ec.message()));
            }
            FT_LOG_TRACE(log_category::resume,
                "State file deleted: " + path.string());
        }

        return {};
    }

    auto has_state(const transfer_id& id) const -> bool {
        {
            std::shared_lock lock(mutex_);
            if (cache_.find(id) != cache_.end()) {
                return true;
            }
        }

        auto path = get_state_file_path(config_.state_directory, id);
        return std::filesystem::exists(path);
    }

    auto mark_chunk_received(const transfer_id& id, uint32_t chunk_index)
        -> result<void> {
        std::unique_lock lock(mutex_);

        auto it = cache_.find(id);
        if (it == cache_.end()) {
            // Try to load from file
            lock.unlock();
            auto load_result = load_state(id);
            if (!load_result) {
                return unexpected(load_result.error());
            }
            lock.lock();
            it = cache_.find(id);
            if (it == cache_.end()) {
                return unexpected(error(error_code::internal_error,
                    "failed to load state into cache"));
            }
        }

        auto& state = it->second;

        if (chunk_index >= state.total_chunks) {
            return unexpected(error(error_code::invalid_chunk_index,
                "chunk index out of range"));
        }

        state.chunk_bitmap[chunk_index] = true;
        state.last_activity = std::chrono::system_clock::now();
        ++chunks_since_checkpoint_;

        // Auto-checkpoint
        if (chunks_since_checkpoint_ >= config_.checkpoint_interval) {
            chunks_since_checkpoint_ = 0;
            lock.unlock();
            return save_state(state);
        }

        return {};
    }

    auto mark_chunks_received(
        const transfer_id& id,
        const std::vector<uint32_t>& chunk_indices) -> result<void> {
        std::unique_lock lock(mutex_);

        auto it = cache_.find(id);
        if (it == cache_.end()) {
            lock.unlock();
            auto load_result = load_state(id);
            if (!load_result) {
                return unexpected(load_result.error());
            }
            lock.lock();
            it = cache_.find(id);
        }

        if (it == cache_.end()) {
            return unexpected(error(error_code::internal_error,
                "failed to load state into cache"));
        }

        auto& state = it->second;

        for (auto index : chunk_indices) {
            if (index >= state.total_chunks) {
                return unexpected(error(error_code::invalid_chunk_index,
                    "chunk index out of range"));
            }
            state.chunk_bitmap[index] = true;
        }

        state.last_activity = std::chrono::system_clock::now();

        // Save after batch update
        lock.unlock();
        return save_state(state);
    }

    auto get_missing_chunks(const transfer_id& id)
        -> result<std::vector<uint32_t>> {
        auto load_result = load_state(id);
        if (!load_result) {
            return unexpected(load_result.error());
        }

        const auto& state = load_result.value();
        std::vector<uint32_t> missing;

        for (uint32_t i = 0; i < state.total_chunks; ++i) {
            if (!state.chunk_bitmap[i]) {
                missing.push_back(i);
            }
        }

        return missing;
    }

    auto is_chunk_received(const transfer_id& id, uint32_t chunk_index) const
        -> bool {
        std::shared_lock lock(mutex_);

        auto it = cache_.find(id);
        if (it == cache_.end()) {
            return false;
        }

        if (chunk_index >= it->second.total_chunks) {
            return false;
        }

        return it->second.chunk_bitmap[chunk_index];
    }

    auto list_resumable_transfers() -> std::vector<transfer_state> {
        FT_LOG_DEBUG(log_category::resume, "Listing resumable transfers");

        std::vector<transfer_state> states;

        std::error_code ec;
        if (!std::filesystem::exists(config_.state_directory)) {
            FT_LOG_DEBUG(log_category::resume,
                "State directory does not exist");
            return states;
        }

        for (const auto& entry :
             std::filesystem::directory_iterator(config_.state_directory, ec)) {
            if (entry.path().extension() != ".json") {
                continue;
            }

            auto filename = entry.path().stem().string();
            auto id = transfer_id::from_string(filename);
            if (!id) {
                continue;
            }

            auto load_result = load_state(*id);
            if (load_result) {
                states.push_back(std::move(load_result.value()));
            }
        }

        FT_LOG_INFO(log_category::resume,
            "Found " + std::to_string(states.size()) + " resumable transfers");
        return states;
    }

    auto cleanup_expired_states() -> std::size_t {
        FT_LOG_INFO(log_category::resume, "Starting expired state cleanup");

        std::size_t removed = 0;
        auto now = std::chrono::system_clock::now();

        std::error_code ec;
        if (!std::filesystem::exists(config_.state_directory)) {
            FT_LOG_DEBUG(log_category::resume,
                "State directory does not exist, nothing to clean");
            return 0;
        }

        std::vector<transfer_id> to_remove;

        for (const auto& entry :
             std::filesystem::directory_iterator(config_.state_directory, ec)) {
            if (entry.path().extension() != ".json") {
                continue;
            }

            auto filename = entry.path().stem().string();
            auto id = transfer_id::from_string(filename);
            if (!id) {
                continue;
            }

            auto load_result = load_state(*id);
            if (!load_result) {
                continue;
            }

            auto age = now - load_result.value().last_activity;
            if (age > config_.state_ttl) {
                FT_LOG_DEBUG(log_category::resume,
                    "State expired: " + id->to_string());
                to_remove.push_back(*id);
            }
        }

        for (const auto& id : to_remove) {
            if (delete_state(id)) {
                ++removed;
            }
        }

        FT_LOG_INFO(log_category::resume,
            "Cleanup completed: " + std::to_string(removed) + " expired states removed");
        return removed;
    }

    auto config() const -> const resume_handler_config& {
        return config_;
    }

    auto update_transferred_bytes(const transfer_id& id, uint64_t bytes)
        -> result<void> {
        std::unique_lock lock(mutex_);

        auto it = cache_.find(id);
        if (it == cache_.end()) {
            lock.unlock();
            auto load_result = load_state(id);
            if (!load_result) {
                return unexpected(load_result.error());
            }
            lock.lock();
            it = cache_.find(id);
        }

        if (it == cache_.end()) {
            return unexpected(error(error_code::internal_error,
                "failed to load state into cache"));
        }

        it->second.transferred_bytes += bytes;
        it->second.last_activity = std::chrono::system_clock::now();

        return {};
    }

private:
    resume_handler_config config_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<transfer_id, transfer_state> cache_;
    uint32_t chunks_since_checkpoint_ = 0;
};

// ============================================================================
// resume_handler public interface
// ============================================================================

resume_handler::resume_handler(const resume_handler_config& config)
    : impl_(std::make_unique<impl>(config)) {
}

resume_handler::~resume_handler() = default;

resume_handler::resume_handler(resume_handler&&) noexcept = default;

auto resume_handler::operator=(resume_handler&&) noexcept
    -> resume_handler& = default;

auto resume_handler::save_state(const transfer_state& state) -> result<void> {
    return impl_->save_state(state);
}

auto resume_handler::load_state(const transfer_id& id)
    -> result<transfer_state> {
    return impl_->load_state(id);
}

auto resume_handler::delete_state(const transfer_id& id) -> result<void> {
    return impl_->delete_state(id);
}

auto resume_handler::has_state(const transfer_id& id) const -> bool {
    return impl_->has_state(id);
}

auto resume_handler::mark_chunk_received(
    const transfer_id& id,
    uint32_t chunk_index) -> result<void> {
    return impl_->mark_chunk_received(id, chunk_index);
}

auto resume_handler::mark_chunks_received(
    const transfer_id& id,
    const std::vector<uint32_t>& chunk_indices) -> result<void> {
    return impl_->mark_chunks_received(id, chunk_indices);
}

auto resume_handler::get_missing_chunks(const transfer_id& id)
    -> result<std::vector<uint32_t>> {
    return impl_->get_missing_chunks(id);
}

auto resume_handler::is_chunk_received(
    const transfer_id& id,
    uint32_t chunk_index) const -> bool {
    return impl_->is_chunk_received(id, chunk_index);
}

auto resume_handler::list_resumable_transfers() -> std::vector<transfer_state> {
    return impl_->list_resumable_transfers();
}

auto resume_handler::cleanup_expired_states() -> std::size_t {
    return impl_->cleanup_expired_states();
}

auto resume_handler::config() const -> const resume_handler_config& {
    return impl_->config();
}

auto resume_handler::update_transferred_bytes(
    const transfer_id& id,
    uint64_t bytes) -> result<void> {
    return impl_->update_transferred_bytes(id, bytes);
}

}  // namespace kcenon::file_transfer
