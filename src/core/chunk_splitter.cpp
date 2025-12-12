/**
 * @file chunk_splitter.cpp
 * @brief Implementation of file splitting into chunks
 */

#include <kcenon/file_transfer/core/chunk_splitter.h>

#include <kcenon/file_transfer/core/checksum.h>

namespace kcenon::file_transfer {

// chunk_iterator implementation

chunk_splitter::chunk_iterator::chunk_iterator(
    std::ifstream file,
    chunk_config config,
    transfer_id id,
    uint64_t file_size,
    uint64_t total_chunks)
    : file_(std::move(file)),
      config_(config),
      transfer_id_(id),
      file_size_(file_size),
      total_chunks_(total_chunks),
      current_index_(0) {
    buffer_.resize(config_.chunk_size);
}

chunk_splitter::chunk_iterator::chunk_iterator(chunk_iterator&& other) noexcept
    : file_(std::move(other.file_)),
      config_(other.config_),
      transfer_id_(other.transfer_id_),
      file_size_(other.file_size_),
      total_chunks_(other.total_chunks_),
      current_index_(other.current_index_),
      buffer_(std::move(other.buffer_)) {
    other.total_chunks_ = 0;
    other.current_index_ = 0;
}

auto chunk_splitter::chunk_iterator::operator=(chunk_iterator&& other) noexcept
    -> chunk_iterator& {
    if (this != &other) {
        file_ = std::move(other.file_);
        config_ = other.config_;
        transfer_id_ = other.transfer_id_;
        file_size_ = other.file_size_;
        total_chunks_ = other.total_chunks_;
        current_index_ = other.current_index_;
        buffer_ = std::move(other.buffer_);

        other.total_chunks_ = 0;
        other.current_index_ = 0;
    }
    return *this;
}

chunk_splitter::chunk_iterator::~chunk_iterator() = default;

auto chunk_splitter::chunk_iterator::has_next() const -> bool {
    return current_index_ < total_chunks_;
}

auto chunk_splitter::chunk_iterator::next() -> result<chunk> {
    if (!has_next()) {
        return unexpected(error{error_code::invalid_chunk_index, "no more chunks available"});
    }

    if (!file_.good()) {
        return unexpected(error{error_code::file_read_error, "file stream error"});
    }

    // Calculate offset and size for this chunk
    uint64_t offset = current_index_ * config_.chunk_size;
    std::size_t bytes_to_read = config_.chunk_size;

    // For the last chunk, adjust the size
    if (current_index_ == total_chunks_ - 1) {
        uint64_t remaining = file_size_ - offset;
        bytes_to_read = static_cast<std::size_t>(remaining);
    }

    // Seek to the correct position
    file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file_.good()) {
        return unexpected(error{error_code::file_read_error, "seek failed"});
    }

    // Read data
    file_.read(reinterpret_cast<char*>(buffer_.data()), static_cast<std::streamsize>(bytes_to_read));
    auto bytes_read = static_cast<std::size_t>(file_.gcount());

    if (bytes_read != bytes_to_read) {
        return unexpected(
            error{error_code::file_read_error, "failed to read expected bytes"});
    }

    // Create chunk
    chunk c;
    c.id = transfer_id_;
    c.index = current_index_;
    c.total_chunks = total_chunks_;
    c.offset = offset;
    c.flags = chunk_flags::none;

    // Set last chunk flag
    if (current_index_ == total_chunks_ - 1) {
        c.flags = c.flags | chunk_flags::last_chunk;
    }

    // Copy data
    c.data.assign(buffer_.begin(), buffer_.begin() + bytes_read);

    // Calculate CRC32
    c.checksum = checksum::crc32(std::span<const std::byte>(c.data));

    // Move to next chunk
    ++current_index_;

    return c;
}

auto chunk_splitter::chunk_iterator::current_index() const -> uint64_t {
    return current_index_;
}

auto chunk_splitter::chunk_iterator::total_chunks() const -> uint64_t {
    return total_chunks_;
}

auto chunk_splitter::chunk_iterator::file_size() const -> uint64_t {
    return file_size_;
}

// chunk_splitter implementation

chunk_splitter::chunk_splitter() : config_() {}

chunk_splitter::chunk_splitter(const chunk_config& config) : config_(config) {}

auto chunk_splitter::split(const std::filesystem::path& file_path, const transfer_id& id)
    -> result<chunk_iterator> {
    // Validate configuration
    if (auto result = config_.validate(); !result) {
        return unexpected(result.error());
    }

    // Check if file exists
    std::error_code ec;
    if (!std::filesystem::exists(file_path, ec)) {
        return unexpected(
            error{error_code::file_not_found, "file not found: " + file_path.string()});
    }

    // Get file size
    auto file_size = std::filesystem::file_size(file_path, ec);
    if (ec) {
        return unexpected(
            error{error_code::file_access_denied, "cannot get file size: " + file_path.string()});
    }

    // Open file
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        return unexpected(
            error{error_code::file_access_denied, "cannot open file: " + file_path.string()});
    }

    // Calculate total chunks
    uint64_t total_chunks = config_.calculate_chunk_count(file_size);

    // Handle empty file case
    if (total_chunks == 0) {
        total_chunks = 1;  // At least one (empty) chunk for empty files
    }

    return chunk_iterator(std::move(file), config_, id, file_size, total_chunks);
}

auto chunk_splitter::calculate_metadata(const std::filesystem::path& file_path)
    -> result<file_metadata> {
    // Check if file exists
    std::error_code ec;
    if (!std::filesystem::exists(file_path, ec)) {
        return unexpected(
            error{error_code::file_not_found, "file not found: " + file_path.string()});
    }

    // Get file size
    auto file_size = std::filesystem::file_size(file_path, ec);
    if (ec) {
        return unexpected(
            error{error_code::file_access_denied, "cannot get file size: " + file_path.string()});
    }

    // Calculate SHA-256
    auto hash_result = checksum::sha256_file(file_path);
    if (!hash_result) {
        return unexpected(hash_result.error());
    }

    file_metadata metadata;
    metadata.filename = file_path.filename().string();
    metadata.file_size = file_size;
    metadata.chunk_size = config_.chunk_size;
    metadata.total_chunks = config_.calculate_chunk_count(file_size);
    metadata.sha256_hash = hash_result.value();

    // Handle empty file
    if (metadata.total_chunks == 0) {
        metadata.total_chunks = 1;
    }

    return metadata;
}

auto chunk_splitter::config() const -> const chunk_config& {
    return config_;
}

}  // namespace kcenon::file_transfer
