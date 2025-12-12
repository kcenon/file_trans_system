/**
 * @file chunk_assembler.cpp
 * @brief Implementation of chunk assembly into files
 */

#include <kcenon/file_transfer/core/chunk_assembler.h>

#include <kcenon/file_transfer/core/checksum.h>

#include <algorithm>
#include <random>

namespace kcenon::file_transfer {

namespace {

auto generate_temp_filename() -> std::string {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);

    constexpr char hex_chars[] = "0123456789abcdef";
    std::string result = ".tmp_";

    for (int i = 0; i < 16; ++i) {
        result += hex_chars[dis(gen)];
    }

    return result;
}

}  // namespace

chunk_assembler::chunk_assembler(const std::filesystem::path& output_dir)
    : output_dir_(output_dir) {
    // Create output directory if it doesn't exist
    std::error_code ec;
    std::filesystem::create_directories(output_dir_, ec);
}

chunk_assembler::chunk_assembler(chunk_assembler&& other) noexcept
    : output_dir_(std::move(other.output_dir_)), contexts_(std::move(other.contexts_)) {}

auto chunk_assembler::operator=(chunk_assembler&& other) noexcept -> chunk_assembler& {
    if (this != &other) {
        output_dir_ = std::move(other.output_dir_);
        contexts_ = std::move(other.contexts_);
    }
    return *this;
}

chunk_assembler::~chunk_assembler() {
    // Clean up any incomplete transfers
    std::unique_lock lock(contexts_mutex_);
    for (auto& [id, ctx] : contexts_) {
        if (ctx && ctx->file) {
            ctx->file->close();
        }
        // Remove temp files
        std::error_code ec;
        std::filesystem::remove(ctx->temp_file_path, ec);
    }
}

auto chunk_assembler::start_session(
    const transfer_id& id,
    const std::string& filename,
    uint64_t file_size,
    uint64_t total_chunks) -> result<void> {
    std::unique_lock lock(contexts_mutex_);

    // Check if session already exists
    if (contexts_.find(id) != contexts_.end()) {
        return unexpected(error{error_code::already_initialized, "session already exists"});
    }

    // Create new context
    auto ctx = std::make_unique<assembly_context>();
    ctx->filename = filename;
    ctx->file_size = file_size;
    ctx->total_chunks = total_chunks;
    ctx->received_chunks.resize(total_chunks, false);
    ctx->received_count = 0;
    ctx->bytes_written = 0;

    // Create temp file path
    ctx->temp_file_path = output_dir_ / generate_temp_filename();
    ctx->final_path = output_dir_ / filename;

    // Open temp file for writing
    ctx->file = std::make_unique<std::ofstream>(ctx->temp_file_path, std::ios::binary);
    if (!ctx->file->is_open()) {
        return unexpected(
            error{error_code::file_access_denied,
                  "cannot create temp file: " + ctx->temp_file_path.string()});
    }

    // Pre-allocate file size for better performance
    if (file_size > 0) {
        ctx->file->seekp(static_cast<std::streamoff>(file_size - 1));
        ctx->file->put('\0');
        ctx->file->seekp(0);
    }

    contexts_[id] = std::move(ctx);
    return {};
}

auto chunk_assembler::process_chunk(const chunk& c) -> result<void> {
    auto* ctx = get_context(c.header.id);
    if (!ctx) {
        return unexpected(error{error_code::not_initialized, "session not found"});
    }

    std::lock_guard lock(ctx->mutex);

    // Validate chunk index
    if (c.header.chunk_index >= ctx->total_chunks) {
        return unexpected(
            error{error_code::invalid_chunk_index,
                  "chunk index " + std::to_string(c.header.chunk_index) + " out of range"});
    }

    // Check if already received
    if (ctx->received_chunks[c.header.chunk_index]) {
        // Duplicate chunk - ignore silently
        return {};
    }

    // Verify CRC32
    if (!verify_chunk_crc32(c)) {
        return unexpected(error{error_code::chunk_checksum_error, "CRC32 verification failed"});
    }

    // Write chunk to file at correct offset
    ctx->file->seekp(static_cast<std::streamoff>(c.header.chunk_offset));
    if (!ctx->file->good()) {
        return unexpected(error{error_code::file_write_error, "seek failed"});
    }

    ctx->file->write(reinterpret_cast<const char*>(c.data.data()),
                     static_cast<std::streamsize>(c.data.size()));
    if (!ctx->file->good()) {
        return unexpected(error{error_code::file_write_error, "write failed"});
    }

    // Update tracking
    ctx->received_chunks[c.header.chunk_index] = true;
    ctx->received_count++;
    ctx->bytes_written += c.data.size();

    return {};
}

auto chunk_assembler::is_complete(const transfer_id& id) const -> bool {
    const auto* ctx = get_context(id);
    if (!ctx) {
        return false;
    }

    std::lock_guard lock(ctx->mutex);
    return ctx->received_count == ctx->total_chunks;
}

auto chunk_assembler::get_missing_chunks(const transfer_id& id) const -> std::vector<uint64_t> {
    const auto* ctx = get_context(id);
    if (!ctx) {
        return {};
    }

    std::lock_guard lock(ctx->mutex);

    std::vector<uint64_t> missing;
    for (uint64_t i = 0; i < ctx->total_chunks; ++i) {
        if (!ctx->received_chunks[i]) {
            missing.push_back(i);
        }
    }
    return missing;
}

auto chunk_assembler::finalize(const transfer_id& id, const std::string& expected_hash)
    -> result<std::filesystem::path> {
    std::unique_lock ctx_lock(contexts_mutex_);

    auto it = contexts_.find(id);
    if (it == contexts_.end()) {
        return unexpected(error{error_code::not_initialized, "session not found"});
    }

    auto& ctx = it->second;

    {
        std::lock_guard lock(ctx->mutex);

        // Check if complete
        if (ctx->received_count != ctx->total_chunks) {
            return unexpected(
                error{error_code::missing_chunks,
                      "missing " + std::to_string(ctx->total_chunks - ctx->received_count) +
                          " chunks"});
        }

        // Close file
        ctx->file->close();
    }

    // Verify SHA-256 if provided
    if (!expected_hash.empty()) {
        if (!checksum::verify_sha256(ctx->temp_file_path, expected_hash)) {
            // Remove temp file on hash mismatch
            std::error_code ec;
            std::filesystem::remove(ctx->temp_file_path, ec);
            contexts_.erase(it);
            return unexpected(error{error_code::file_hash_mismatch, "SHA-256 hash mismatch"});
        }
    }

    // Move temp file to final location
    std::error_code ec;

    // Remove existing file if present
    if (std::filesystem::exists(ctx->final_path, ec)) {
        std::filesystem::remove(ctx->final_path, ec);
    }

    std::filesystem::rename(ctx->temp_file_path, ctx->final_path, ec);
    if (ec) {
        return unexpected(
            error{error_code::file_write_error,
                  "cannot rename temp file: " + ec.message()});
    }

    auto final_path = ctx->final_path;
    contexts_.erase(it);

    return final_path;
}

auto chunk_assembler::get_progress(const transfer_id& id) const -> std::optional<assembly_progress> {
    const auto* ctx = get_context(id);
    if (!ctx) {
        return std::nullopt;
    }

    std::lock_guard lock(ctx->mutex);

    assembly_progress progress;
    progress.id = id;
    progress.total_chunks = ctx->total_chunks;
    progress.received_chunks = ctx->received_count;
    progress.bytes_written = ctx->bytes_written;

    return progress;
}

void chunk_assembler::cancel_session(const transfer_id& id) {
    std::unique_lock lock(contexts_mutex_);

    auto it = contexts_.find(id);
    if (it == contexts_.end()) {
        return;
    }

    auto& ctx = it->second;

    {
        std::lock_guard ctx_lock(ctx->mutex);
        if (ctx->file) {
            ctx->file->close();
        }
    }

    // Remove temp file
    std::error_code ec;
    std::filesystem::remove(ctx->temp_file_path, ec);

    contexts_.erase(it);
}

auto chunk_assembler::has_session(const transfer_id& id) const -> bool {
    std::shared_lock lock(contexts_mutex_);
    return contexts_.find(id) != contexts_.end();
}

auto chunk_assembler::verify_chunk_crc32(const chunk& c) const -> bool {
    return checksum::verify_crc32(std::span<const std::byte>(c.data), c.header.checksum);
}

auto chunk_assembler::get_context(const transfer_id& id) -> assembly_context* {
    std::shared_lock lock(contexts_mutex_);
    auto it = contexts_.find(id);
    return it != contexts_.end() ? it->second.get() : nullptr;
}

auto chunk_assembler::get_context(const transfer_id& id) const -> const assembly_context* {
    std::shared_lock lock(contexts_mutex_);
    auto it = contexts_.find(id);
    return it != contexts_.end() ? it->second.get() : nullptr;
}

}  // namespace kcenon::file_transfer
