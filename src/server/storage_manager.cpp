/**
 * @file storage_manager.cpp
 * @brief Unified storage manager implementation
 */

#include "kcenon/file_transfer/server/storage_manager.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <shared_mutex>

namespace kcenon::file_transfer {

// ============================================================================
// local_storage_backend implementation
// ============================================================================

struct local_storage_backend::impl {
    std::filesystem::path base_path;
    std::atomic<bool> connected{false};

    mutable std::shared_mutex mutex;
    std::function<void(const storage_progress&)> progress_callback;

    explicit impl(std::filesystem::path path) : base_path(std::move(path)) {}

    auto full_path_for(const std::string& key) const -> std::filesystem::path {
        return base_path / key;
    }

    void report_progress(const storage_progress& progress) {
        std::shared_lock lock(mutex);
        if (progress_callback) {
            progress_callback(progress);
        }
    }
};

local_storage_backend::local_storage_backend(const std::filesystem::path& base_path)
    : impl_(std::make_unique<impl>(base_path)) {}

auto local_storage_backend::create(const std::filesystem::path& base_path)
    -> std::unique_ptr<local_storage_backend> {
    return std::unique_ptr<local_storage_backend>(new local_storage_backend(base_path));
}

auto local_storage_backend::type() const -> storage_backend_type {
    return storage_backend_type::local;
}

auto local_storage_backend::name() const -> std::string_view {
    return "local";
}

auto local_storage_backend::is_available() const -> bool {
    std::error_code ec;
    return std::filesystem::exists(impl_->base_path, ec) && !ec;
}

auto local_storage_backend::connect() -> result<void> {
    std::error_code ec;

    if (!std::filesystem::exists(impl_->base_path, ec)) {
        if (!std::filesystem::create_directories(impl_->base_path, ec) || ec) {
            return result<void>(error{
                error_code::storage_init_failed,
                "Failed to create storage directory: " + impl_->base_path.string()
            });
        }
    }

    impl_->connected = true;
    return result<void>();
}

auto local_storage_backend::disconnect() -> result<void> {
    impl_->connected = false;
    return result<void>();
}

auto local_storage_backend::store(
    const std::string& key,
    std::span<const std::byte> data,
    const store_options& options) -> result<store_result> {

    auto start_time = std::chrono::steady_clock::now();
    auto target_path = impl_->full_path_for(key);

    // Check for existing file
    std::error_code ec;
    if (std::filesystem::exists(target_path, ec) && !options.overwrite) {
        return result<store_result>(error{
            error_code::file_already_exists,
            "File already exists: " + key
        });
    }

    // Create parent directories if needed
    auto parent_path = target_path.parent_path();
    if (!parent_path.empty() && !std::filesystem::exists(parent_path, ec)) {
        std::filesystem::create_directories(parent_path, ec);
        if (ec) {
            return result<store_result>(error{
                error_code::storage_init_failed,
                "Failed to create directory: " + parent_path.string()
            });
        }
    }

    // Write data
    std::ofstream file(target_path, std::ios::binary);
    if (!file) {
        return result<store_result>(error{
            error_code::file_open_failed,
            "Failed to open file for writing: " + target_path.string()
        });
    }

    // Write in chunks and report progress
    constexpr std::size_t chunk_size = 1024 * 1024;  // 1MB
    std::size_t written = 0;
    const auto* ptr = reinterpret_cast<const char*>(data.data());

    while (written < data.size()) {
        auto to_write = std::min(chunk_size, data.size() - written);
        file.write(ptr + written, static_cast<std::streamsize>(to_write));

        if (!file) {
            return result<store_result>(error{
                error_code::file_write_failed,
                "Failed to write data to file: " + target_path.string()
            });
        }

        written += to_write;

        storage_progress progress;
        progress.operation = storage_operation::store;
        progress.key = key;
        progress.bytes_transferred = written;
        progress.total_bytes = data.size();
        progress.backend = storage_backend_type::local;
        impl_->report_progress(progress);
    }

    file.close();

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    store_result res;
    res.key = key;
    res.bytes_stored = data.size();
    res.backend = storage_backend_type::local;
    res.tier = options.tier;
    res.duration = duration;

    return result<store_result>(std::move(res));
}

auto local_storage_backend::store_file(
    const std::string& key,
    const std::filesystem::path& file_path,
    const store_options& options) -> result<store_result> {

    auto start_time = std::chrono::steady_clock::now();

    // Check source file
    std::error_code ec;
    if (!std::filesystem::exists(file_path, ec) || ec) {
        return result<store_result>(error{
            error_code::file_not_found,
            "Source file not found: " + file_path.string()
        });
    }

    auto file_size = std::filesystem::file_size(file_path, ec);
    if (ec) {
        return result<store_result>(error{
            error_code::file_read_failed,
            "Failed to get file size: " + file_path.string()
        });
    }

    auto target_path = impl_->full_path_for(key);

    // Check for existing file
    if (std::filesystem::exists(target_path, ec) && !options.overwrite) {
        return result<store_result>(error{
            error_code::file_already_exists,
            "File already exists: " + key
        });
    }

    // Create parent directories if needed
    auto parent_path = target_path.parent_path();
    if (!parent_path.empty() && !std::filesystem::exists(parent_path, ec)) {
        std::filesystem::create_directories(parent_path, ec);
    }

    // Copy file
    std::filesystem::copy_file(file_path, target_path,
        std::filesystem::copy_options::overwrite_existing, ec);

    if (ec) {
        return result<store_result>(error{
            error_code::file_write_failed,
            "Failed to copy file: " + ec.message()
        });
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    store_result res;
    res.key = key;
    res.bytes_stored = file_size;
    res.backend = storage_backend_type::local;
    res.tier = options.tier;
    res.duration = duration;

    return result<store_result>(std::move(res));
}

auto local_storage_backend::retrieve(
    const std::string& key,
    const retrieve_options& /*options*/) -> result<std::vector<std::byte>> {

    auto target_path = impl_->full_path_for(key);

    std::error_code ec;
    if (!std::filesystem::exists(target_path, ec) || ec) {
        return result<std::vector<std::byte>>(error{
            error_code::file_not_found,
            "File not found: " + key
        });
    }

    auto file_size = std::filesystem::file_size(target_path, ec);
    if (ec) {
        return result<std::vector<std::byte>>(error{
            error_code::file_read_failed,
            "Failed to get file size: " + key
        });
    }

    std::ifstream file(target_path, std::ios::binary);
    if (!file) {
        return result<std::vector<std::byte>>(error{
            error_code::file_open_failed,
            "Failed to open file for reading: " + key
        });
    }

    std::vector<std::byte> data(file_size);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(file_size));

    if (!file) {
        return result<std::vector<std::byte>>(error{
            error_code::file_read_failed,
            "Failed to read file: " + key
        });
    }

    return result<std::vector<std::byte>>(std::move(data));
}

auto local_storage_backend::retrieve_file(
    const std::string& key,
    const std::filesystem::path& file_path,
    const retrieve_options& /*options*/) -> result<retrieve_result> {

    auto start_time = std::chrono::steady_clock::now();
    auto source_path = impl_->full_path_for(key);

    std::error_code ec;
    if (!std::filesystem::exists(source_path, ec) || ec) {
        return result<retrieve_result>(error{
            error_code::file_not_found,
            "File not found: " + key
        });
    }

    auto file_size = std::filesystem::file_size(source_path, ec);
    if (ec) {
        return result<retrieve_result>(error{
            error_code::file_read_failed,
            "Failed to get file size: " + key
        });
    }

    // Create parent directories if needed
    auto parent_path = file_path.parent_path();
    if (!parent_path.empty() && !std::filesystem::exists(parent_path, ec)) {
        std::filesystem::create_directories(parent_path, ec);
    }

    // Copy file
    std::filesystem::copy_file(source_path, file_path,
        std::filesystem::copy_options::overwrite_existing, ec);

    if (ec) {
        return result<retrieve_result>(error{
            error_code::file_write_failed,
            "Failed to copy file: " + ec.message()
        });
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    retrieve_result res;
    res.key = key;
    res.bytes_retrieved = file_size;
    res.backend = storage_backend_type::local;
    res.duration = duration;

    // Populate metadata
    res.metadata.key = key;
    res.metadata.size = file_size;
    res.metadata.backend = storage_backend_type::local;

    auto last_write = std::filesystem::last_write_time(source_path, ec);
    if (!ec) {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            last_write - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now());
        res.metadata.last_modified = sctp;
    }

    return result<retrieve_result>(std::move(res));
}

auto local_storage_backend::remove(const std::string& key) -> result<void> {
    auto target_path = impl_->full_path_for(key);

    std::error_code ec;
    if (!std::filesystem::exists(target_path, ec)) {
        return result<void>(error{
            error_code::file_not_found,
            "File not found: " + key
        });
    }

    std::filesystem::remove(target_path, ec);
    if (ec) {
        return result<void>(error{
            error_code::file_delete_failed,
            "Failed to delete file: " + key
        });
    }

    return result<void>();
}

auto local_storage_backend::exists(const std::string& key) -> result<bool> {
    auto target_path = impl_->full_path_for(key);
    std::error_code ec;
    return result<bool>(std::filesystem::exists(target_path, ec) && !ec);
}

auto local_storage_backend::get_metadata(const std::string& key)
    -> result<stored_object_metadata> {

    auto target_path = impl_->full_path_for(key);

    std::error_code ec;
    if (!std::filesystem::exists(target_path, ec) || ec) {
        return result<stored_object_metadata>(error{
            error_code::file_not_found,
            "File not found: " + key
        });
    }

    stored_object_metadata metadata;
    metadata.key = key;
    metadata.backend = storage_backend_type::local;

    metadata.size = std::filesystem::file_size(target_path, ec);
    if (ec) {
        return result<stored_object_metadata>(error{
            error_code::file_read_failed,
            "Failed to get file size: " + key
        });
    }

    auto last_write = std::filesystem::last_write_time(target_path, ec);
    if (!ec) {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            last_write - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now());
        metadata.last_modified = sctp;
    }

    return result<stored_object_metadata>(std::move(metadata));
}

auto local_storage_backend::list(const list_storage_options& options)
    -> result<list_storage_result> {

    list_storage_result res;

    std::error_code ec;
    if (!std::filesystem::exists(impl_->base_path, ec) || ec) {
        return result<list_storage_result>(std::move(res));
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(impl_->base_path, ec)) {
        if (ec) break;

        if (!entry.is_regular_file(ec) || ec) continue;

        auto relative_path = std::filesystem::relative(entry.path(), impl_->base_path, ec);
        if (ec) continue;

        std::string key = relative_path.string();

        // Apply prefix filter
        if (options.prefix && !key.starts_with(*options.prefix)) {
            continue;
        }

        stored_object_metadata metadata;
        metadata.key = key;
        metadata.backend = storage_backend_type::local;
        metadata.size = entry.file_size(ec);

        if (!ec) {
            auto last_write = entry.last_write_time(ec);
            if (!ec) {
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    last_write - std::filesystem::file_time_type::clock::now() +
                    std::chrono::system_clock::now());
                metadata.last_modified = sctp;
            }
        }

        res.objects.push_back(std::move(metadata));

        if (res.objects.size() >= options.max_results) {
            res.is_truncated = true;
            break;
        }
    }

    return result<list_storage_result>(std::move(res));
}

auto local_storage_backend::store_async(
    const std::string& key,
    std::span<const std::byte> data,
    const store_options& options) -> std::future<result<store_result>> {

    // Copy data since span may not outlive the async operation
    auto data_copy = std::make_shared<std::vector<std::byte>>(data.begin(), data.end());

    return std::async(std::launch::async, [this, key, data_copy, options]() {
        return this->store(key, *data_copy, options);
    });
}

auto local_storage_backend::store_file_async(
    const std::string& key,
    const std::filesystem::path& file_path,
    const store_options& options) -> std::future<result<store_result>> {

    return std::async(std::launch::async, [this, key, file_path, options]() {
        return this->store_file(key, file_path, options);
    });
}

auto local_storage_backend::retrieve_async(
    const std::string& key,
    const retrieve_options& options) -> std::future<result<std::vector<std::byte>>> {

    return std::async(std::launch::async, [this, key, options]() {
        return this->retrieve(key, options);
    });
}

auto local_storage_backend::retrieve_file_async(
    const std::string& key,
    const std::filesystem::path& file_path,
    const retrieve_options& options) -> std::future<result<retrieve_result>> {

    return std::async(std::launch::async, [this, key, file_path, options]() {
        return this->retrieve_file(key, file_path, options);
    });
}

void local_storage_backend::on_progress(
    std::function<void(const storage_progress&)> callback) {
    std::unique_lock lock(impl_->mutex);
    impl_->progress_callback = std::move(callback);
}

auto local_storage_backend::base_path() const -> const std::filesystem::path& {
    return impl_->base_path;
}

auto local_storage_backend::full_path(const std::string& key) const
    -> std::filesystem::path {
    return impl_->full_path_for(key);
}

// ============================================================================
// cloud_storage_backend implementation
// ============================================================================

struct cloud_storage_backend::impl {
    std::unique_ptr<cloud_storage_interface> storage;
    storage_backend_type backend_type;
    std::string backend_name;

    mutable std::shared_mutex mutex;
    std::function<void(const storage_progress&)> progress_callback;

    impl(std::unique_ptr<cloud_storage_interface> s, storage_backend_type type)
        : storage(std::move(s)), backend_type(type) {
        switch (type) {
            case storage_backend_type::cloud_s3:
                backend_name = "cloud_s3";
                break;
            case storage_backend_type::cloud_azure:
                backend_name = "cloud_azure";
                break;
            case storage_backend_type::cloud_gcs:
                backend_name = "cloud_gcs";
                break;
            default:
                backend_name = "cloud_unknown";
                break;
        }
    }

    void report_progress(const storage_progress& progress) {
        std::shared_lock lock(mutex);
        if (progress_callback) {
            progress_callback(progress);
        }
    }
};

cloud_storage_backend::cloud_storage_backend(
    std::unique_ptr<cloud_storage_interface> storage,
    storage_backend_type backend_type)
    : impl_(std::make_unique<impl>(std::move(storage), backend_type)) {}

auto cloud_storage_backend::create(
    std::unique_ptr<cloud_storage_interface> storage,
    storage_backend_type backend_type) -> std::unique_ptr<cloud_storage_backend> {

    if (!storage) {
        return nullptr;
    }

    return std::unique_ptr<cloud_storage_backend>(
        new cloud_storage_backend(std::move(storage), backend_type));
}

auto cloud_storage_backend::type() const -> storage_backend_type {
    return impl_->backend_type;
}

auto cloud_storage_backend::name() const -> std::string_view {
    return impl_->backend_name;
}

auto cloud_storage_backend::is_available() const -> bool {
    return impl_->storage && impl_->storage->is_connected();
}

auto cloud_storage_backend::connect() -> result<void> {
    if (!impl_->storage) {
        return result<void>(error{
            error_code::storage_init_failed,
            "Cloud storage not initialized"
        });
    }

    auto res = impl_->storage->connect();
    if (!res.has_value()) {
        return result<void>(error{
            error_code::storage_init_failed,
            "Failed to connect to cloud storage: " + res.error().message
        });
    }

    return result<void>();
}

auto cloud_storage_backend::disconnect() -> result<void> {
    if (impl_->storage) {
        auto res = impl_->storage->disconnect();
        if (!res.has_value()) {
            return result<void>(error{
                error_code::storage_cleanup_failed,
                "Failed to disconnect from cloud storage"
            });
        }
    }
    return result<void>();
}

auto cloud_storage_backend::store(
    const std::string& key,
    std::span<const std::byte> data,
    const store_options& options) -> result<store_result> {

    auto start_time = std::chrono::steady_clock::now();

    cloud_transfer_options cloud_options;
    if (options.content_type) {
        cloud_options.content_type = *options.content_type;
    }
    if (options.storage_class) {
        cloud_options.storage_class = *options.storage_class;
    }
    for (const auto& [k, v] : options.custom_metadata) {
        cloud_options.custom_metadata.emplace_back(k, v);
    }

    auto upload_result = impl_->storage->upload(key, data, cloud_options);
    if (!upload_result.has_value()) {
        return result<store_result>(error{
            error_code::storage_write_failed,
            "Cloud upload failed: " + upload_result.error().message
        });
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    store_result res;
    res.key = key;
    res.bytes_stored = upload_result.value().bytes_uploaded;
    res.backend = impl_->backend_type;
    res.tier = options.tier;
    res.etag = upload_result.value().etag;
    res.duration = duration;

    return result<store_result>(std::move(res));
}

auto cloud_storage_backend::store_file(
    const std::string& key,
    const std::filesystem::path& file_path,
    const store_options& options) -> result<store_result> {

    auto start_time = std::chrono::steady_clock::now();

    cloud_transfer_options cloud_options;
    if (options.content_type) {
        cloud_options.content_type = *options.content_type;
    }
    if (options.storage_class) {
        cloud_options.storage_class = *options.storage_class;
    }
    for (const auto& [k, v] : options.custom_metadata) {
        cloud_options.custom_metadata.emplace_back(k, v);
    }

    auto upload_result = impl_->storage->upload_file(file_path, key, cloud_options);
    if (!upload_result.has_value()) {
        return result<store_result>(error{
            error_code::storage_write_failed,
            "Cloud file upload failed: " + upload_result.error().message
        });
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    store_result res;
    res.key = key;
    res.bytes_stored = upload_result.value().bytes_uploaded;
    res.backend = impl_->backend_type;
    res.tier = options.tier;
    res.etag = upload_result.value().etag;
    res.duration = duration;

    return result<store_result>(std::move(res));
}

auto cloud_storage_backend::retrieve(
    const std::string& key,
    const retrieve_options& /*options*/) -> result<std::vector<std::byte>> {

    auto download_result = impl_->storage->download(key);
    if (!download_result.has_value()) {
        return result<std::vector<std::byte>>(error{
            error_code::storage_read_failed,
            "Cloud download failed: " + download_result.error().message
        });
    }

    return result<std::vector<std::byte>>(std::move(download_result.value()));
}

auto cloud_storage_backend::retrieve_file(
    const std::string& key,
    const std::filesystem::path& file_path,
    const retrieve_options& /*options*/) -> result<retrieve_result> {

    auto start_time = std::chrono::steady_clock::now();

    auto download_res = impl_->storage->download_file(key, file_path);
    if (!download_res.has_value()) {
        return result<retrieve_result>(error{
            error_code::storage_read_failed,
            "Cloud file download failed: " + download_res.error().message
        });
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    retrieve_result res;
    res.key = key;
    res.bytes_retrieved = download_res.value().bytes_downloaded;
    res.backend = impl_->backend_type;
    res.duration = duration;

    // Convert cloud metadata to stored object metadata
    const auto& cloud_meta = download_res.value().metadata;
    res.metadata.key = cloud_meta.key;
    res.metadata.size = cloud_meta.size;
    res.metadata.last_modified = cloud_meta.last_modified;
    res.metadata.etag = cloud_meta.etag;
    res.metadata.content_type = cloud_meta.content_type;
    res.metadata.backend = impl_->backend_type;
    for (const auto& [k, v] : cloud_meta.custom_metadata) {
        res.metadata.custom_metadata.emplace_back(k, v);
    }

    return result<retrieve_result>(std::move(res));
}

auto cloud_storage_backend::remove(const std::string& key) -> result<void> {
    auto delete_result = impl_->storage->delete_object(key);
    if (!delete_result.has_value()) {
        return result<void>(error{
            error_code::storage_delete_failed,
            "Cloud delete failed: " + delete_result.error().message
        });
    }
    return result<void>();
}

auto cloud_storage_backend::exists(const std::string& key) -> result<bool> {
    auto exists_result = impl_->storage->exists(key);
    if (!exists_result.has_value()) {
        return result<bool>(error{
            error_code::storage_read_failed,
            "Cloud exists check failed: " + exists_result.error().message
        });
    }
    return result<bool>(exists_result.value());
}

auto cloud_storage_backend::get_metadata(const std::string& key)
    -> result<stored_object_metadata> {

    auto meta_result = impl_->storage->get_metadata(key);
    if (!meta_result.has_value()) {
        return result<stored_object_metadata>(error{
            error_code::storage_read_failed,
            "Cloud metadata failed: " + meta_result.error().message
        });
    }

    const auto& cloud_meta = meta_result.value();
    stored_object_metadata metadata;
    metadata.key = cloud_meta.key;
    metadata.size = cloud_meta.size;
    metadata.last_modified = cloud_meta.last_modified;
    metadata.etag = cloud_meta.etag;
    metadata.content_type = cloud_meta.content_type;
    metadata.backend = impl_->backend_type;

    if (cloud_meta.storage_class) {
        // Map cloud storage class to tier
        if (*cloud_meta.storage_class == "STANDARD") {
            metadata.tier = storage_tier::hot;
        } else if (*cloud_meta.storage_class == "STANDARD_IA" ||
                   *cloud_meta.storage_class == "ONEZONE_IA") {
            metadata.tier = storage_tier::warm;
        } else if (*cloud_meta.storage_class == "GLACIER" ||
                   *cloud_meta.storage_class == "GLACIER_IR") {
            metadata.tier = storage_tier::cold;
        } else if (*cloud_meta.storage_class == "DEEP_ARCHIVE") {
            metadata.tier = storage_tier::archive;
        }
    }

    for (const auto& [k, v] : cloud_meta.custom_metadata) {
        metadata.custom_metadata.emplace_back(k, v);
    }

    return result<stored_object_metadata>(std::move(metadata));
}

auto cloud_storage_backend::list(const list_storage_options& options)
    -> result<list_storage_result> {

    list_objects_options cloud_options;
    cloud_options.prefix = options.prefix;
    cloud_options.max_keys = options.max_results;
    cloud_options.continuation_token = options.continuation_token;

    auto list_result = impl_->storage->list_objects(cloud_options);
    if (!list_result.has_value()) {
        return result<list_storage_result>(error{
            error_code::storage_read_failed,
            "Cloud list failed: " + list_result.error().message
        });
    }

    list_storage_result res;
    res.is_truncated = list_result.value().is_truncated;
    res.continuation_token = list_result.value().continuation_token;
    res.total_count = list_result.value().total_count;

    for (const auto& cloud_meta : list_result.value().objects) {
        stored_object_metadata metadata;
        metadata.key = cloud_meta.key;
        metadata.size = cloud_meta.size;
        metadata.last_modified = cloud_meta.last_modified;
        metadata.etag = cloud_meta.etag;
        metadata.content_type = cloud_meta.content_type;
        metadata.backend = impl_->backend_type;

        for (const auto& [k, v] : cloud_meta.custom_metadata) {
            metadata.custom_metadata.emplace_back(k, v);
        }

        res.objects.push_back(std::move(metadata));
    }

    return result<list_storage_result>(std::move(res));
}

auto cloud_storage_backend::store_async(
    const std::string& key,
    std::span<const std::byte> data,
    const store_options& options) -> std::future<result<store_result>> {

    auto data_copy = std::make_shared<std::vector<std::byte>>(data.begin(), data.end());

    return std::async(std::launch::async, [this, key, data_copy, options]() {
        return this->store(key, *data_copy, options);
    });
}

auto cloud_storage_backend::store_file_async(
    const std::string& key,
    const std::filesystem::path& file_path,
    const store_options& options) -> std::future<result<store_result>> {

    return std::async(std::launch::async, [this, key, file_path, options]() {
        return this->store_file(key, file_path, options);
    });
}

auto cloud_storage_backend::retrieve_async(
    const std::string& key,
    const retrieve_options& options) -> std::future<result<std::vector<std::byte>>> {

    return std::async(std::launch::async, [this, key, options]() {
        return this->retrieve(key, options);
    });
}

auto cloud_storage_backend::retrieve_file_async(
    const std::string& key,
    const std::filesystem::path& file_path,
    const retrieve_options& options) -> std::future<result<retrieve_result>> {

    return std::async(std::launch::async, [this, key, file_path, options]() {
        return this->retrieve_file(key, file_path, options);
    });
}

void cloud_storage_backend::on_progress(
    std::function<void(const storage_progress&)> callback) {
    std::unique_lock lock(impl_->mutex);
    impl_->progress_callback = std::move(callback);
}

auto cloud_storage_backend::cloud_storage() -> cloud_storage_interface& {
    return *impl_->storage;
}

auto cloud_storage_backend::cloud_storage() const -> const cloud_storage_interface& {
    return *impl_->storage;
}

// ============================================================================
// storage_manager implementation
// ============================================================================

struct storage_manager::impl {
    storage_manager_config config;
    std::atomic<bool> initialized{false};

    mutable std::shared_mutex mutex;
    storage_manager_statistics stats;

    std::function<void(const storage_progress&)> progress_callback;
    std::function<void(const std::string&, const error&)> error_callback;

    explicit impl(const storage_manager_config& cfg) : config(cfg) {}

    void report_progress(const storage_progress& progress) {
        std::shared_lock lock(mutex);
        if (progress_callback) {
            progress_callback(progress);
        }
    }

    void report_error(const std::string& key, const error& err) {
        std::shared_lock lock(mutex);
        if (error_callback) {
            error_callback(key, err);
        }
    }

    void record_store(uint64_t bytes, storage_backend_type backend) {
        std::unique_lock lock(mutex);
        stats.bytes_stored += bytes;
        stats.store_count++;
        if (backend == storage_backend_type::local) {
            stats.local_bytes += bytes;
            stats.local_file_count++;
        } else {
            stats.cloud_bytes += bytes;
            stats.cloud_file_count++;
        }
    }

    void record_retrieve(uint64_t bytes) {
        std::unique_lock lock(mutex);
        stats.bytes_retrieved += bytes;
        stats.retrieve_count++;
    }

    void record_delete() {
        std::unique_lock lock(mutex);
        stats.delete_count++;
    }

    void record_error() {
        std::unique_lock lock(mutex);
        stats.error_count++;
    }
};

storage_manager::storage_manager(const storage_manager_config& config)
    : impl_(std::make_unique<impl>(config)) {}

storage_manager::storage_manager(storage_manager&&) noexcept = default;
auto storage_manager::operator=(storage_manager&&) noexcept -> storage_manager& = default;
storage_manager::~storage_manager() = default;

auto storage_manager::create(const storage_manager_config& config)
    -> std::unique_ptr<storage_manager> {

    if (!config.is_valid()) {
        return nullptr;
    }

    return std::unique_ptr<storage_manager>(new storage_manager(config));
}

auto storage_manager::initialize() -> result<void> {
    if (impl_->initialized) {
        return result<void>();
    }

    // Connect primary backend
    auto primary_result = impl_->config.primary_backend->connect();
    if (!primary_result.has_value()) {
        return result<void>(error{
            error_code::storage_init_failed,
            "Failed to connect primary backend: " + primary_result.error().message
        });
    }

    // Connect secondary backend if configured
    if (impl_->config.secondary_backend) {
        auto secondary_result = impl_->config.secondary_backend->connect();
        if (!secondary_result.has_value()) {
            // Log warning but don't fail
            impl_->report_error("", secondary_result.error());
        }
    }

    impl_->initialized = true;
    return result<void>();
}

auto storage_manager::shutdown() -> result<void> {
    if (!impl_->initialized) {
        return result<void>();
    }

    // Disconnect secondary backend
    if (impl_->config.secondary_backend) {
        impl_->config.secondary_backend->disconnect();
    }

    // Disconnect primary backend
    impl_->config.primary_backend->disconnect();

    impl_->initialized = false;
    return result<void>();
}

auto storage_manager::store(
    const std::string& key,
    std::span<const std::byte> data,
    const store_options& options) -> result<store_result> {

    if (!impl_->initialized) {
        return result<store_result>(error{
            error_code::storage_init_failed,
            "Storage manager not initialized"
        });
    }

    // Store to primary backend
    auto primary_result = impl_->config.primary_backend->store(key, data, options);
    if (!primary_result.has_value()) {
        impl_->record_error();
        impl_->report_error(key, primary_result.error());
        return primary_result;
    }

    impl_->record_store(primary_result.value().bytes_stored,
                        primary_result.value().backend);

    // Replicate to secondary if configured
    if (impl_->config.replicate_writes && impl_->config.secondary_backend) {
        auto secondary_result = impl_->config.secondary_backend->store(key, data, options);
        if (!secondary_result.has_value()) {
            // Log error but don't fail the operation
            impl_->report_error(key, secondary_result.error());
        }
    }

    return primary_result;
}

auto storage_manager::store_file(
    const std::string& key,
    const std::filesystem::path& file_path,
    const store_options& options) -> result<store_result> {

    if (!impl_->initialized) {
        return result<store_result>(error{
            error_code::storage_init_failed,
            "Storage manager not initialized"
        });
    }

    auto primary_result = impl_->config.primary_backend->store_file(key, file_path, options);
    if (!primary_result.has_value()) {
        impl_->record_error();
        impl_->report_error(key, primary_result.error());
        return primary_result;
    }

    impl_->record_store(primary_result.value().bytes_stored,
                        primary_result.value().backend);

    // Replicate to secondary if configured
    if (impl_->config.replicate_writes && impl_->config.secondary_backend) {
        auto secondary_result = impl_->config.secondary_backend->store_file(
            key, file_path, options);
        if (!secondary_result.has_value()) {
            impl_->report_error(key, secondary_result.error());
        }
    }

    return primary_result;
}

auto storage_manager::retrieve(
    const std::string& key,
    const retrieve_options& options) -> result<std::vector<std::byte>> {

    if (!impl_->initialized) {
        return result<std::vector<std::byte>>(error{
            error_code::storage_init_failed,
            "Storage manager not initialized"
        });
    }

    // Try primary backend first
    auto primary_result = impl_->config.primary_backend->retrieve(key, options);
    if (primary_result.has_value()) {
        impl_->record_retrieve(primary_result.value().size());
        return primary_result;
    }

    // Fallback to secondary if configured
    if (impl_->config.fallback_reads && impl_->config.secondary_backend) {
        auto secondary_result = impl_->config.secondary_backend->retrieve(key, options);
        if (secondary_result.has_value()) {
            impl_->record_retrieve(secondary_result.value().size());
            return secondary_result;
        }
    }

    impl_->record_error();
    return primary_result;
}

auto storage_manager::retrieve_file(
    const std::string& key,
    const std::filesystem::path& file_path,
    const retrieve_options& options) -> result<retrieve_result> {

    if (!impl_->initialized) {
        return result<retrieve_result>(error{
            error_code::storage_init_failed,
            "Storage manager not initialized"
        });
    }

    // Try primary backend first
    auto primary_result = impl_->config.primary_backend->retrieve_file(key, file_path, options);
    if (primary_result.has_value()) {
        impl_->record_retrieve(primary_result.value().bytes_retrieved);
        return primary_result;
    }

    // Fallback to secondary if configured
    if (impl_->config.fallback_reads && impl_->config.secondary_backend) {
        auto secondary_result = impl_->config.secondary_backend->retrieve_file(
            key, file_path, options);
        if (secondary_result.has_value()) {
            impl_->record_retrieve(secondary_result.value().bytes_retrieved);
            return secondary_result;
        }
    }

    impl_->record_error();
    return primary_result;
}

auto storage_manager::remove(const std::string& key) -> result<void> {
    if (!impl_->initialized) {
        return result<void>(error{
            error_code::storage_init_failed,
            "Storage manager not initialized"
        });
    }

    // Remove from primary
    auto primary_result = impl_->config.primary_backend->remove(key);

    // Also remove from secondary if configured
    if (impl_->config.secondary_backend) {
        impl_->config.secondary_backend->remove(key);
    }

    if (primary_result.has_value()) {
        impl_->record_delete();
    } else {
        impl_->record_error();
    }

    return primary_result;
}

auto storage_manager::exists(const std::string& key) -> result<bool> {
    if (!impl_->initialized) {
        return result<bool>(error{
            error_code::storage_init_failed,
            "Storage manager not initialized"
        });
    }

    // Check primary first
    auto primary_result = impl_->config.primary_backend->exists(key);
    if (primary_result.has_value() && primary_result.value()) {
        return result<bool>(true);
    }

    // Check secondary if configured and fallback enabled
    if (impl_->config.fallback_reads && impl_->config.secondary_backend) {
        auto secondary_result = impl_->config.secondary_backend->exists(key);
        if (secondary_result.has_value() && secondary_result.value()) {
            return result<bool>(true);
        }
    }

    return result<bool>(false);
}

auto storage_manager::get_metadata(const std::string& key)
    -> result<stored_object_metadata> {

    if (!impl_->initialized) {
        return result<stored_object_metadata>(error{
            error_code::storage_init_failed,
            "Storage manager not initialized"
        });
    }

    // Try primary first
    auto primary_result = impl_->config.primary_backend->get_metadata(key);
    if (primary_result.has_value()) {
        return primary_result;
    }

    // Fallback to secondary
    if (impl_->config.fallback_reads && impl_->config.secondary_backend) {
        auto secondary_result = impl_->config.secondary_backend->get_metadata(key);
        if (secondary_result.has_value()) {
            return secondary_result;
        }
    }

    return primary_result;
}

auto storage_manager::list(const list_storage_options& options)
    -> result<list_storage_result> {

    if (!impl_->initialized) {
        return result<list_storage_result>(error{
            error_code::storage_init_failed,
            "Storage manager not initialized"
        });
    }

    // Merge results from both backends if hybrid storage
    if (impl_->config.hybrid_storage && impl_->config.secondary_backend) {
        list_storage_result merged_result;
        std::unordered_map<std::string, stored_object_metadata> objects_map;

        // Get from primary
        auto primary_result = impl_->config.primary_backend->list(options);
        if (primary_result.has_value()) {
            for (auto& obj : primary_result.value().objects) {
                objects_map[obj.key] = std::move(obj);
            }
        }

        // Get from secondary
        auto secondary_result = impl_->config.secondary_backend->list(options);
        if (secondary_result.has_value()) {
            for (auto& obj : secondary_result.value().objects) {
                if (objects_map.find(obj.key) == objects_map.end()) {
                    objects_map[obj.key] = std::move(obj);
                }
            }
        }

        for (auto& [_, obj] : objects_map) {
            merged_result.objects.push_back(std::move(obj));
        }

        // Sort by key
        std::sort(merged_result.objects.begin(), merged_result.objects.end(),
            [](const auto& a, const auto& b) { return a.key < b.key; });

        // Apply max_results
        if (merged_result.objects.size() > options.max_results) {
            merged_result.objects.resize(options.max_results);
            merged_result.is_truncated = true;
        }

        return result<list_storage_result>(std::move(merged_result));
    }

    return impl_->config.primary_backend->list(options);
}

auto storage_manager::store_async(
    const std::string& key,
    std::span<const std::byte> data,
    const store_options& options) -> std::future<result<store_result>> {

    auto data_copy = std::make_shared<std::vector<std::byte>>(data.begin(), data.end());

    return std::async(std::launch::async, [this, key, data_copy, options]() {
        return this->store(key, *data_copy, options);
    });
}

auto storage_manager::store_file_async(
    const std::string& key,
    const std::filesystem::path& file_path,
    const store_options& options) -> std::future<result<store_result>> {

    return std::async(std::launch::async, [this, key, file_path, options]() {
        return this->store_file(key, file_path, options);
    });
}

auto storage_manager::retrieve_async(
    const std::string& key,
    const retrieve_options& options) -> std::future<result<std::vector<std::byte>>> {

    return std::async(std::launch::async, [this, key, options]() {
        return this->retrieve(key, options);
    });
}

auto storage_manager::retrieve_file_async(
    const std::string& key,
    const std::filesystem::path& file_path,
    const retrieve_options& options) -> std::future<result<retrieve_result>> {

    return std::async(std::launch::async, [this, key, file_path, options]() {
        return this->retrieve_file(key, file_path, options);
    });
}

auto storage_manager::change_tier(
    const std::string& key,
    storage_tier target_tier) -> result<void> {

    if (!impl_->initialized) {
        return result<void>(error{
            error_code::storage_init_failed,
            "Storage manager not initialized"
        });
    }

    // For tier change, we need to retrieve and re-store with new options
    auto data = retrieve(key);
    if (!data.has_value()) {
        return result<void>(data.error());
    }

    store_options options;
    options.tier = target_tier;
    options.overwrite = true;

    // Map tier to cloud storage class
    switch (target_tier) {
        case storage_tier::hot:
            options.storage_class = "STANDARD";
            break;
        case storage_tier::warm:
            options.storage_class = "STANDARD_IA";
            break;
        case storage_tier::cold:
            options.storage_class = "GLACIER";
            break;
        case storage_tier::archive:
            options.storage_class = "DEEP_ARCHIVE";
            break;
    }

    auto store_res = store(key, data.value(), options);
    if (!store_res.has_value()) {
        return result<void>(store_res.error());
    }

    std::unique_lock lock(impl_->mutex);
    impl_->stats.tier_change_count++;

    return result<void>();
}

auto storage_manager::copy_between_backends(
    const std::string& key,
    storage_backend_type source,
    storage_backend_type destination) -> result<void> {

    if (!impl_->initialized) {
        return result<void>(error{
            error_code::storage_init_failed,
            "Storage manager not initialized"
        });
    }

    storage_backend* src_backend = nullptr;
    storage_backend* dst_backend = nullptr;

    // Determine source and destination backends
    if (impl_->config.primary_backend->type() == source) {
        src_backend = impl_->config.primary_backend.get();
    } else if (impl_->config.secondary_backend &&
               impl_->config.secondary_backend->type() == source) {
        src_backend = impl_->config.secondary_backend.get();
    }

    if (impl_->config.primary_backend->type() == destination) {
        dst_backend = impl_->config.primary_backend.get();
    } else if (impl_->config.secondary_backend &&
               impl_->config.secondary_backend->type() == destination) {
        dst_backend = impl_->config.secondary_backend.get();
    }

    if (!src_backend || !dst_backend) {
        return result<void>(error{
            error_code::invalid_configuration,
            "Invalid source or destination backend"
        });
    }

    // Retrieve from source
    auto data = src_backend->retrieve(key);
    if (!data.has_value()) {
        return result<void>(data.error());
    }

    // Store to destination
    store_options options;
    options.overwrite = true;
    auto store_res = dst_backend->store(key, data.value(), options);
    if (!store_res.has_value()) {
        return result<void>(store_res.error());
    }

    return result<void>();
}

auto storage_manager::get_statistics() const -> storage_manager_statistics {
    std::shared_lock lock(impl_->mutex);
    return impl_->stats;
}

void storage_manager::reset_statistics() {
    std::unique_lock lock(impl_->mutex);
    impl_->stats = storage_manager_statistics{};
}

auto storage_manager::config() const -> const storage_manager_config& {
    return impl_->config;
}

auto storage_manager::primary_backend() -> storage_backend& {
    return *impl_->config.primary_backend;
}

auto storage_manager::primary_backend() const -> const storage_backend& {
    return *impl_->config.primary_backend;
}

auto storage_manager::secondary_backend() -> storage_backend* {
    return impl_->config.secondary_backend.get();
}

auto storage_manager::secondary_backend() const -> const storage_backend* {
    return impl_->config.secondary_backend.get();
}

void storage_manager::on_progress(
    std::function<void(const storage_progress&)> callback) {
    std::unique_lock lock(impl_->mutex);
    impl_->progress_callback = std::move(callback);
}

void storage_manager::on_error(
    std::function<void(const std::string&, const error&)> callback) {
    std::unique_lock lock(impl_->mutex);
    impl_->error_callback = std::move(callback);
}

}  // namespace kcenon::file_transfer
