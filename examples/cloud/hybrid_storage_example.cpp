/**
 * @file hybrid_storage_example.cpp
 * @brief Hybrid storage usage example (local + cloud)
 *
 * This example demonstrates how to use hybrid storage combining local
 * filesystem and cloud storage (AWS S3). Common use cases include:
 * - Hot files stored locally, cold files archived to cloud
 * - Local cache with cloud backup
 * - Tiered storage based on access patterns
 *
 * Prerequisites:
 * - AWS credentials configured
 * - An S3 bucket with appropriate permissions
 * - Local storage directory with read/write permissions
 *
 * Build:
 *   cmake --build build --target hybrid_storage_example
 *
 * Run:
 *   ./build/bin/hybrid_storage_example <bucket-name> <region> [local-path]
 */

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "kcenon/file_transfer/cloud/s3_storage.h"

using namespace kcenon::file_transfer;
namespace fs = std::filesystem;

namespace {

/**
 * @brief Print usage information
 */
void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " <bucket-name> <region> [local-path]\n\n";
    std::cerr << "Arguments:\n";
    std::cerr << "  bucket-name  S3 bucket name\n";
    std::cerr << "  region       AWS region (e.g., us-east-1)\n";
    std::cerr << "  local-path   Local storage directory (default: /tmp/hybrid_storage)\n\n";
    std::cerr << "Environment:\n";
    std::cerr << "  AWS_ACCESS_KEY_ID      AWS access key\n";
    std::cerr << "  AWS_SECRET_ACCESS_KEY  AWS secret key\n\n";
    std::cerr << "Examples:\n";
    std::cerr << "  " << program << " my-bucket us-east-1\n";
    std::cerr << "  " << program << " my-bucket us-east-1 /data/local-cache\n";
}

/**
 * @brief Create a test file with specified content
 */
auto create_test_file(const fs::path& path, std::size_t size) -> bool {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    std::vector<char> buffer(size);
    for (std::size_t i = 0; i < size; ++i) {
        buffer[i] = static_cast<char>('A' + (i % 26));
    }

    file.write(buffer.data(), static_cast<std::streamsize>(size));
    return file.good();
}

/**
 * @brief Get file age in hours
 */
auto get_file_age_hours(const fs::path& path) -> double {
    auto file_time = fs::last_write_time(path);
    auto now = fs::file_time_type::clock::now();
    auto duration = now - file_time;
    return std::chrono::duration<double, std::ratio<3600>>(duration).count();
}

/**
 * @brief Simple hybrid storage manager
 */
class hybrid_storage_manager {
public:
    hybrid_storage_manager(std::unique_ptr<s3_storage> cloud, fs::path local_path)
        : cloud_storage_(std::move(cloud))
        , local_path_(std::move(local_path)) {
        fs::create_directories(local_path_);
    }

    /**
     * @brief Store file with tiered strategy
     *
     * Files are stored locally first, then archived to cloud based on age.
     */
    auto store_file(const std::string& key, std::span<const std::byte> data) -> bool {
        // Always store locally first
        auto local_file = local_path_ / key;
        fs::create_directories(local_file.parent_path());

        std::ofstream file(local_file, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to create local file: " << local_file << "\n";
            return false;
        }

        file.write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));

        if (!file.good()) {
            std::cerr << "Failed to write local file: " << local_file << "\n";
            return false;
        }

        std::cout << "  Stored locally: " << local_file << "\n";
        return true;
    }

    /**
     * @brief Retrieve file (local first, then cloud)
     */
    auto retrieve_file(const std::string& key) -> std::vector<std::byte> {
        auto local_file = local_path_ / key;

        // Try local first
        if (fs::exists(local_file)) {
            std::cout << "  Found in local storage\n";
            return read_local_file(local_file);
        }

        // Fall back to cloud
        std::cout << "  Not in local, fetching from cloud...\n";
        auto result = cloud_storage_->download(key);
        if (!result.has_value()) {
            std::cerr << "  Failed to download from cloud: " << result.error().message << "\n";
            return {};
        }

        // Cache locally
        store_file(key, result.value());
        std::cout << "  Cached locally\n";

        return result.value();
    }

    /**
     * @brief Archive old files to cloud and remove from local
     */
    auto archive_old_files(double max_age_hours) -> std::size_t {
        std::size_t archived = 0;

        for (auto& entry : fs::recursive_directory_iterator(local_path_)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            double age = get_file_age_hours(entry.path());
            if (age <= max_age_hours) {
                continue;
            }

            // Get relative path as key
            auto relative = fs::relative(entry.path(), local_path_);
            auto key = relative.string();

            std::cout << "  Archiving: " << key << " (age: " << age << "h)\n";

            // Upload to cloud
            auto upload_result = cloud_storage_->upload_file(entry.path(), key);
            if (!upload_result.has_value()) {
                std::cerr << "    Failed: " << upload_result.error().message << "\n";
                continue;
            }

            // Remove local copy
            fs::remove(entry.path());
            std::cout << "    Archived to cloud, local copy removed\n";
            ++archived;
        }

        return archived;
    }

    /**
     * @brief Sync cloud to local (download all cloud files)
     */
    auto sync_from_cloud(const std::string& prefix = "") -> std::size_t {
        list_objects_options options;
        if (!prefix.empty()) {
            options.prefix = prefix;
        }

        auto list_result = cloud_storage_->list_objects(options);
        if (!list_result.has_value()) {
            std::cerr << "  Failed to list cloud objects: " << list_result.error().message << "\n";
            return 0;
        }

        std::size_t synced = 0;
        for (const auto& obj : list_result.value().objects) {
            auto local_file = local_path_ / obj.key;

            // Skip if already exists locally
            if (fs::exists(local_file)) {
                continue;
            }

            std::cout << "  Downloading: " << obj.key << "\n";

            fs::create_directories(local_file.parent_path());
            auto download_result = cloud_storage_->download_file(obj.key, local_file);
            if (download_result.has_value()) {
                ++synced;
            }
        }

        return synced;
    }

    /**
     * @brief Backup local to cloud
     */
    auto backup_to_cloud() -> std::size_t {
        std::size_t backed_up = 0;

        for (auto& entry : fs::recursive_directory_iterator(local_path_)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            auto relative = fs::relative(entry.path(), local_path_);
            auto key = relative.string();

            // Check if already in cloud
            auto exists_result = cloud_storage_->exists(key);
            if (exists_result.has_value() && exists_result.value()) {
                continue;
            }

            std::cout << "  Backing up: " << key << "\n";

            auto upload_result = cloud_storage_->upload_file(entry.path(), key);
            if (upload_result.has_value()) {
                ++backed_up;
            }
        }

        return backed_up;
    }

    /**
     * @brief Delete file from both local and cloud
     */
    auto delete_file(const std::string& key) -> bool {
        bool success = true;

        // Delete from local
        auto local_file = local_path_ / key;
        if (fs::exists(local_file)) {
            fs::remove(local_file);
            std::cout << "  Deleted from local\n";
        }

        // Delete from cloud
        auto result = cloud_storage_->delete_object(key);
        if (!result.has_value()) {
            std::cerr << "  Failed to delete from cloud: " << result.error().message << "\n";
            success = false;
        } else {
            std::cout << "  Deleted from cloud\n";
        }

        return success;
    }

    /**
     * @brief Get storage statistics
     */
    void print_statistics() const {
        std::size_t local_files = 0;
        std::uintmax_t local_size = 0;

        for (auto& entry : fs::recursive_directory_iterator(local_path_)) {
            if (entry.is_regular_file()) {
                ++local_files;
                local_size += entry.file_size();
            }
        }

        auto cloud_stats = cloud_storage_->get_statistics();

        std::cout << "Hybrid Storage Statistics:\n";
        std::cout << "  Local files:      " << local_files << "\n";
        std::cout << "  Local size:       " << local_size << " bytes\n";
        std::cout << "  Cloud uploads:    " << cloud_stats.upload_count << "\n";
        std::cout << "  Cloud downloads:  " << cloud_stats.download_count << "\n";
        std::cout << "  Cloud errors:     " << cloud_stats.errors << "\n";
    }

private:
    auto read_local_file(const fs::path& path) -> std::vector<std::byte> {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            return {};
        }

        auto size = file.tellg();
        file.seekg(0);

        std::vector<std::byte> data(static_cast<std::size_t>(size));
        file.read(reinterpret_cast<char*>(data.data()), size);

        return data;
    }

    std::unique_ptr<s3_storage> cloud_storage_;
    fs::path local_path_;
};

/**
 * @brief Demonstrate basic hybrid operations
 */
void demo_basic_operations(hybrid_storage_manager& manager) {
    std::cout << "\n=== Basic Hybrid Operations Demo ===\n\n";

    // Create test data
    std::vector<std::byte> data(2048);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::byte>(i % 256);
    }

    // Store file
    std::cout << "1. Storing file locally...\n";
    if (!manager.store_file("demo/test-file.bin", data)) {
        std::cerr << "   Failed to store file\n";
        return;
    }

    // Retrieve file (should be local)
    std::cout << "\n2. Retrieving file (should be local)...\n";
    auto retrieved = manager.retrieve_file("demo/test-file.bin");
    std::cout << "   Retrieved " << retrieved.size() << " bytes\n";

    // Backup to cloud
    std::cout << "\n3. Backing up to cloud...\n";
    auto backed_up = manager.backup_to_cloud();
    std::cout << "   Backed up " << backed_up << " files\n";

    // Cleanup
    std::cout << "\n4. Cleaning up...\n";
    manager.delete_file("demo/test-file.bin");

    std::cout << "\n=== Basic Operations Complete ===\n";
}

/**
 * @brief Demonstrate tiered storage
 */
void demo_tiered_storage(hybrid_storage_manager& manager) {
    std::cout << "\n=== Tiered Storage Demo ===\n\n";

    // Create multiple test files
    std::cout << "1. Creating test files...\n";
    for (int i = 1; i <= 3; ++i) {
        std::vector<std::byte> data(1024 * i);
        std::fill(data.begin(), data.end(), static_cast<std::byte>(i));

        std::string key = "tiered/file-" + std::to_string(i) + ".bin";
        manager.store_file(key, data);
    }

    // Show current status
    std::cout << "\n2. Current status:\n";
    manager.print_statistics();

    // Backup all to cloud
    std::cout << "\n3. Backing up all files to cloud...\n";
    auto backed_up = manager.backup_to_cloud();
    std::cout << "   Backed up " << backed_up << " files\n";

    // In a real scenario, we would wait for files to age
    // For demo, we'll archive immediately with age=0
    std::cout << "\n4. Archiving old files (moving to cloud-only)...\n";
    auto archived = manager.archive_old_files(0.0);  // Archive all files
    std::cout << "   Archived " << archived << " files\n";

    // Retrieve from cloud (should fetch since local is gone)
    std::cout << "\n5. Retrieving file (should fetch from cloud)...\n";
    auto retrieved = manager.retrieve_file("tiered/file-1.bin");
    std::cout << "   Retrieved " << retrieved.size() << " bytes\n";

    // Cleanup
    std::cout << "\n6. Cleaning up...\n";
    for (int i = 1; i <= 3; ++i) {
        std::string key = "tiered/file-" + std::to_string(i) + ".bin";
        manager.delete_file(key);
    }

    std::cout << "\n=== Tiered Storage Complete ===\n";
}

/**
 * @brief Demonstrate cloud sync
 */
void demo_cloud_sync(hybrid_storage_manager& manager, s3_storage& cloud) {
    std::cout << "\n=== Cloud Sync Demo ===\n\n";

    // Upload some files directly to cloud
    std::cout << "1. Uploading files directly to cloud...\n";
    for (int i = 1; i <= 2; ++i) {
        std::vector<std::byte> data(512);
        std::fill(data.begin(), data.end(), static_cast<std::byte>(i + 100));

        std::string key = "sync/cloud-file-" + std::to_string(i) + ".bin";
        auto result = cloud.upload(key, data);
        if (result.has_value()) {
            std::cout << "   Uploaded: " << key << "\n";
        }
    }

    // Sync from cloud to local
    std::cout << "\n2. Syncing from cloud to local...\n";
    auto synced = manager.sync_from_cloud("sync/");
    std::cout << "   Synced " << synced << " files\n";

    // Show status
    std::cout << "\n3. Current status:\n";
    manager.print_statistics();

    // Cleanup
    std::cout << "\n4. Cleaning up...\n";
    for (int i = 1; i <= 2; ++i) {
        std::string key = "sync/cloud-file-" + std::to_string(i) + ".bin";
        manager.delete_file(key);
    }

    std::cout << "\n=== Cloud Sync Complete ===\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    // Parse arguments
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    std::string bucket = argv[1];
    std::string region = argv[2];
    fs::path local_path = (argc > 3) ? argv[3] : "/tmp/hybrid_storage";

    std::cout << "Hybrid Storage Example\n";
    std::cout << "======================\n\n";
    std::cout << "Configuration:\n";
    std::cout << "  Bucket:     " << bucket << "\n";
    std::cout << "  Region:     " << region << "\n";
    std::cout << "  Local path: " << local_path << "\n";
    std::cout << "\n";

    // Create credential provider
    std::cout << "Creating credential provider...\n";
    auto credentials = s3_credential_provider::create_default();
    if (!credentials) {
        std::cerr << "Failed to create credential provider.\n";
        std::cerr << "Please set AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY.\n";
        return 1;
    }
    std::cout << "  Credentials loaded successfully.\n\n";

    // Create S3 configuration
    auto config = cloud_config_builder::s3()
        .with_bucket(bucket)
        .with_region(region)
        .build_s3();

    // Create S3 storage
    std::cout << "Creating cloud storage...\n";
    auto cloud = s3_storage::create(config, credentials);
    if (!cloud) {
        std::cerr << "Failed to create S3 storage.\n";
        return 1;
    }
    std::cout << "  Storage created successfully.\n\n";

    // Connect
    std::cout << "Connecting to cloud storage...\n";
    auto connect_result = cloud->connect();
    if (!connect_result.has_value()) {
        std::cerr << "Failed to connect: " << connect_result.error().message << "\n";
        return 1;
    }
    std::cout << "  Connected successfully.\n";

    // Create hybrid storage manager
    // We need to create a second S3 storage for the manager since we're moving ownership
    auto manager_cloud = s3_storage::create(config, credentials);
    if (!manager_cloud) {
        std::cerr << "Failed to create manager storage.\n";
        return 1;
    }
    manager_cloud->connect();

    hybrid_storage_manager manager(std::move(manager_cloud), local_path);

    // Run demos
    demo_basic_operations(manager);
    demo_tiered_storage(manager);
    demo_cloud_sync(manager, *cloud);

    // Final statistics
    std::cout << "\n=== Final Statistics ===\n\n";
    manager.print_statistics();

    // Cleanup local directory
    std::cout << "\nCleaning up local directory...\n";
    fs::remove_all(local_path);

    // Disconnect
    std::cout << "Disconnecting...\n";
    cloud->disconnect();
    std::cout << "Done!\n";

    return 0;
}
