/**
 * @file multi_cloud_failover_example.cpp
 * @brief Multi-cloud failover usage example
 *
 * This example demonstrates how to implement failover between multiple
 * cloud storage providers. Common use cases include:
 * - High availability across cloud providers
 * - Disaster recovery with geographic redundancy
 * - Vendor lock-in mitigation
 *
 * Prerequisites:
 * - AWS S3 credentials configured
 * - Azure Blob Storage credentials configured
 * - Buckets/containers created in both providers
 *
 * Build:
 *   cmake --build build --target multi_cloud_failover_example
 *
 * Run:
 *   ./build/bin/multi_cloud_failover_example
 */

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "kcenon/file_transfer/cloud/azure_blob_storage.h"
#include "kcenon/file_transfer/cloud/s3_storage.h"

using namespace kcenon::file_transfer;

namespace {

/**
 * @brief Print usage information
 */
void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " <s3-bucket> <s3-region> <azure-account> <azure-container>\n\n";
    std::cerr << "Arguments:\n";
    std::cerr << "  s3-bucket        AWS S3 bucket name\n";
    std::cerr << "  s3-region        AWS region (e.g., us-east-1)\n";
    std::cerr << "  azure-account    Azure storage account name\n";
    std::cerr << "  azure-container  Azure Blob container name\n\n";
    std::cerr << "Environment:\n";
    std::cerr << "  AWS_ACCESS_KEY_ID          AWS access key\n";
    std::cerr << "  AWS_SECRET_ACCESS_KEY      AWS secret key\n";
    std::cerr << "  AZURE_STORAGE_ACCOUNT      Azure storage account\n";
    std::cerr << "  AZURE_STORAGE_KEY          Azure storage key\n";
}

/**
 * @brief Cloud provider abstraction for failover
 */
class cloud_provider_wrapper {
public:
    virtual ~cloud_provider_wrapper() = default;

    [[nodiscard]] virtual auto name() const -> std::string = 0;
    [[nodiscard]] virtual auto is_available() const -> bool = 0;
    [[nodiscard]] virtual auto upload(const std::string& key,
                                      std::span<const std::byte> data) -> result<upload_result> = 0;
    [[nodiscard]] virtual auto download(const std::string& key) -> result<std::vector<std::byte>> = 0;
    [[nodiscard]] virtual auto delete_object(const std::string& key) -> result<delete_result> = 0;
    [[nodiscard]] virtual auto exists(const std::string& key) -> result<bool> = 0;
};

/**
 * @brief S3 provider wrapper
 */
class s3_provider : public cloud_provider_wrapper {
public:
    explicit s3_provider(std::unique_ptr<s3_storage> storage)
        : storage_(std::move(storage)) {}

    [[nodiscard]] auto name() const -> std::string override { return "AWS S3"; }

    [[nodiscard]] auto is_available() const -> bool override {
        return storage_ && storage_->is_connected();
    }

    [[nodiscard]] auto upload(const std::string& key,
                              std::span<const std::byte> data) -> result<upload_result> override {
        return storage_->upload(key, data);
    }

    [[nodiscard]] auto download(const std::string& key) -> result<std::vector<std::byte>> override {
        return storage_->download(key);
    }

    [[nodiscard]] auto delete_object(const std::string& key) -> result<delete_result> override {
        return storage_->delete_object(key);
    }

    [[nodiscard]] auto exists(const std::string& key) -> result<bool> override {
        return storage_->exists(key);
    }

private:
    std::unique_ptr<s3_storage> storage_;
};

/**
 * @brief Azure Blob provider wrapper
 */
class azure_provider : public cloud_provider_wrapper {
public:
    explicit azure_provider(std::unique_ptr<azure_blob_storage> storage)
        : storage_(std::move(storage)) {}

    [[nodiscard]] auto name() const -> std::string override { return "Azure Blob"; }

    [[nodiscard]] auto is_available() const -> bool override {
        return storage_ && storage_->is_connected();
    }

    [[nodiscard]] auto upload(const std::string& key,
                              std::span<const std::byte> data) -> result<upload_result> override {
        return storage_->upload(key, data);
    }

    [[nodiscard]] auto download(const std::string& key) -> result<std::vector<std::byte>> override {
        return storage_->download(key);
    }

    [[nodiscard]] auto delete_object(const std::string& key) -> result<delete_result> override {
        return storage_->delete_object(key);
    }

    [[nodiscard]] auto exists(const std::string& key) -> result<bool> override {
        return storage_->exists(key);
    }

private:
    std::unique_ptr<azure_blob_storage> storage_;
};

/**
 * @brief Multi-cloud failover manager
 *
 * Provides automatic failover between cloud providers.
 * Primary provider is tried first, secondary is used on failure.
 */
class multi_cloud_manager {
public:
    multi_cloud_manager(std::unique_ptr<cloud_provider_wrapper> primary,
                        std::unique_ptr<cloud_provider_wrapper> secondary)
        : primary_(std::move(primary))
        , secondary_(std::move(secondary)) {}

    /**
     * @brief Upload with automatic failover
     */
    auto upload(const std::string& key, std::span<const std::byte> data) -> bool {
        std::cout << "  Trying primary (" << primary_->name() << ")...\n";

        if (primary_->is_available()) {
            auto result = primary_->upload(key, data);
            if (result.has_value()) {
                std::cout << "    Success on primary\n";
                ++primary_success_;
                return true;
            }
            std::cout << "    Failed: " << result.error().message << "\n";
        } else {
            std::cout << "    Primary not available\n";
        }

        std::cout << "  Failing over to secondary (" << secondary_->name() << ")...\n";

        if (secondary_->is_available()) {
            auto result = secondary_->upload(key, data);
            if (result.has_value()) {
                std::cout << "    Success on secondary\n";
                ++secondary_success_;
                ++failover_count_;
                return true;
            }
            std::cout << "    Failed: " << result.error().message << "\n";
        } else {
            std::cout << "    Secondary not available\n";
        }

        ++total_failures_;
        return false;
    }

    /**
     * @brief Download with automatic failover
     */
    auto download(const std::string& key) -> std::vector<std::byte> {
        std::cout << "  Trying primary (" << primary_->name() << ")...\n";

        if (primary_->is_available()) {
            auto result = primary_->download(key);
            if (result.has_value()) {
                std::cout << "    Success on primary\n";
                return result.value();
            }
            std::cout << "    Failed: " << result.error().message << "\n";
        }

        std::cout << "  Failing over to secondary (" << secondary_->name() << ")...\n";

        if (secondary_->is_available()) {
            auto result = secondary_->download(key);
            if (result.has_value()) {
                std::cout << "    Success on secondary\n";
                ++failover_count_;
                return result.value();
            }
            std::cout << "    Failed: " << result.error().message << "\n";
        }

        ++total_failures_;
        return {};
    }

    /**
     * @brief Replicate data to both providers
     */
    auto replicate(const std::string& key, std::span<const std::byte> data) -> bool {
        bool primary_ok = false;
        bool secondary_ok = false;

        std::cout << "  Uploading to primary (" << primary_->name() << ")...\n";
        if (primary_->is_available()) {
            auto result = primary_->upload(key, data);
            primary_ok = result.has_value();
            if (primary_ok) {
                std::cout << "    Success\n";
            } else {
                std::cout << "    Failed: " << result.error().message << "\n";
            }
        }

        std::cout << "  Uploading to secondary (" << secondary_->name() << ")...\n";
        if (secondary_->is_available()) {
            auto result = secondary_->upload(key, data);
            secondary_ok = result.has_value();
            if (secondary_ok) {
                std::cout << "    Success\n";
            } else {
                std::cout << "    Failed: " << result.error().message << "\n";
            }
        }

        return primary_ok || secondary_ok;
    }

    /**
     * @brief Delete from both providers
     */
    auto delete_all(const std::string& key) -> bool {
        bool success = false;

        if (primary_->is_available()) {
            auto result = primary_->delete_object(key);
            if (result.has_value()) {
                success = true;
            }
        }

        if (secondary_->is_available()) {
            auto result = secondary_->delete_object(key);
            if (result.has_value()) {
                success = true;
            }
        }

        return success;
    }

    /**
     * @brief Check availability of providers
     */
    void check_availability() const {
        std::cout << "Provider Availability:\n";
        std::cout << "  Primary (" << primary_->name() << "): "
                  << (primary_->is_available() ? "Available" : "Unavailable") << "\n";
        std::cout << "  Secondary (" << secondary_->name() << "): "
                  << (secondary_->is_available() ? "Available" : "Unavailable") << "\n";
    }

    /**
     * @brief Print failover statistics
     */
    void print_statistics() const {
        std::cout << "Failover Statistics:\n";
        std::cout << "  Primary successes:   " << primary_success_ << "\n";
        std::cout << "  Secondary successes: " << secondary_success_ << "\n";
        std::cout << "  Failover count:      " << failover_count_ << "\n";
        std::cout << "  Total failures:      " << total_failures_ << "\n";
    }

private:
    std::unique_ptr<cloud_provider_wrapper> primary_;
    std::unique_ptr<cloud_provider_wrapper> secondary_;

    std::size_t primary_success_ = 0;
    std::size_t secondary_success_ = 0;
    std::size_t failover_count_ = 0;
    std::size_t total_failures_ = 0;
};

/**
 * @brief Demonstrate basic failover
 */
void demo_basic_failover(multi_cloud_manager& manager) {
    std::cout << "\n=== Basic Failover Demo ===\n\n";

    // Check availability
    std::cout << "1. Checking provider availability...\n";
    manager.check_availability();

    // Create test data
    std::vector<std::byte> data(1024);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::byte>(i % 256);
    }

    // Upload with failover
    std::cout << "\n2. Uploading with failover...\n";
    if (manager.upload("failover-test/file1.bin", data)) {
        std::cout << "   Upload successful\n";
    } else {
        std::cout << "   Upload failed on all providers\n";
    }

    // Download with failover
    std::cout << "\n3. Downloading with failover...\n";
    auto downloaded = manager.download("failover-test/file1.bin");
    std::cout << "   Downloaded " << downloaded.size() << " bytes\n";

    // Cleanup
    std::cout << "\n4. Cleaning up...\n";
    manager.delete_all("failover-test/file1.bin");

    std::cout << "\n=== Basic Failover Complete ===\n";
}

/**
 * @brief Demonstrate replication
 */
void demo_replication(multi_cloud_manager& manager) {
    std::cout << "\n=== Replication Demo ===\n\n";

    // Create test data
    std::vector<std::byte> data(2048);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::byte>(i % 256);
    }

    // Replicate to both providers
    std::cout << "1. Replicating to both providers...\n";
    if (manager.replicate("replicated/important-file.bin", data)) {
        std::cout << "   Replication successful\n";
    }

    // Download from primary
    std::cout << "\n2. Downloading (primary first)...\n";
    auto downloaded = manager.download("replicated/important-file.bin");
    std::cout << "   Downloaded " << downloaded.size() << " bytes\n";

    // Cleanup
    std::cout << "\n3. Cleaning up from both providers...\n";
    manager.delete_all("replicated/important-file.bin");

    std::cout << "\n=== Replication Complete ===\n";
}

/**
 * @brief Demonstrate failover statistics
 */
void demo_statistics(multi_cloud_manager& manager) {
    std::cout << "\n=== Failover Statistics Demo ===\n\n";

    // Perform multiple operations
    std::cout << "1. Performing multiple operations...\n";
    std::vector<std::byte> data(512);

    for (int i = 0; i < 5; ++i) {
        std::string key = "stats/file-" + std::to_string(i) + ".bin";
        std::cout << "\n   Operation " << (i + 1) << ":\n";
        manager.upload(key, data);
    }

    // Show statistics
    std::cout << "\n2. Final statistics:\n";
    manager.print_statistics();

    // Cleanup
    std::cout << "\n3. Cleaning up...\n";
    for (int i = 0; i < 5; ++i) {
        std::string key = "stats/file-" + std::to_string(i) + ".bin";
        manager.delete_all(key);
    }

    std::cout << "\n=== Statistics Demo Complete ===\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    // Parse arguments
    if (argc < 5) {
        print_usage(argv[0]);
        return 1;
    }

    std::string s3_bucket = argv[1];
    std::string s3_region = argv[2];
    std::string azure_account = argv[3];
    std::string azure_container = argv[4];

    std::cout << "Multi-Cloud Failover Example\n";
    std::cout << "============================\n\n";
    std::cout << "Configuration:\n";
    std::cout << "  S3 Bucket:        " << s3_bucket << "\n";
    std::cout << "  S3 Region:        " << s3_region << "\n";
    std::cout << "  Azure Account:    " << azure_account << "\n";
    std::cout << "  Azure Container:  " << azure_container << "\n";
    std::cout << "\n";

    // Create S3 storage (primary)
    std::cout << "Setting up primary (AWS S3)...\n";
    auto s3_credentials = s3_credential_provider::create_default();
    std::unique_ptr<cloud_provider_wrapper> primary;

    if (s3_credentials) {
        auto s3_config = cloud_config_builder::s3()
            .with_bucket(s3_bucket)
            .with_region(s3_region)
            .build_s3();

        auto s3 = s3_storage::create(s3_config, s3_credentials);
        if (s3) {
            auto result = s3->connect();
            if (result.has_value()) {
                std::cout << "  S3 connected successfully\n";
                primary = std::make_unique<s3_provider>(std::move(s3));
            } else {
                std::cout << "  S3 connection failed: " << result.error().message << "\n";
            }
        }
    } else {
        std::cout << "  S3 credentials not available\n";
    }

    // Create Azure storage (secondary)
    std::cout << "\nSetting up secondary (Azure Blob)...\n";
    auto azure_credentials = azure_blob_credential_provider::create_from_environment();
    std::unique_ptr<cloud_provider_wrapper> secondary;

    if (azure_credentials) {
        auto azure_config = cloud_config_builder::azure_blob()
            .with_account_name(azure_account)
            .with_bucket(azure_container)
            .build_azure_blob();

        auto azure = azure_blob_storage::create(azure_config, std::move(azure_credentials));
        if (azure) {
            auto result = azure->connect();
            if (result.has_value()) {
                std::cout << "  Azure connected successfully\n";
                secondary = std::make_unique<azure_provider>(std::move(azure));
            } else {
                std::cout << "  Azure connection failed: " << result.error().message << "\n";
            }
        }
    } else {
        std::cout << "  Azure credentials not available\n";
    }

    // Check if at least one provider is available
    if (!primary && !secondary) {
        std::cerr << "\nError: No cloud providers available.\n";
        std::cerr << "Please configure at least one provider's credentials.\n";
        return 1;
    }

    // Create dummy providers for missing ones
    if (!primary) {
        std::cout << "\nWarning: Primary (S3) not available, using secondary as primary\n";
        primary = std::move(secondary);
        secondary = nullptr;
    }

    if (!secondary) {
        std::cout << "\nWarning: Secondary not available, failover will not work\n";
        // Create a dummy wrapper that always fails
        class dummy_provider : public cloud_provider_wrapper {
        public:
            [[nodiscard]] auto name() const -> std::string override { return "Dummy"; }
            [[nodiscard]] auto is_available() const -> bool override { return false; }
            [[nodiscard]] auto upload(const std::string&, std::span<const std::byte>)
                -> result<upload_result> override {
                return make_error(error_code::connection_failed, "Dummy provider");
            }
            [[nodiscard]] auto download(const std::string&)
                -> result<std::vector<std::byte>> override {
                return make_error(error_code::connection_failed, "Dummy provider");
            }
            [[nodiscard]] auto delete_object(const std::string&)
                -> result<delete_result> override {
                return make_error(error_code::connection_failed, "Dummy provider");
            }
            [[nodiscard]] auto exists(const std::string&) -> result<bool> override {
                return make_error(error_code::connection_failed, "Dummy provider");
            }
        };
        secondary = std::make_unique<dummy_provider>();
    }

    // Create multi-cloud manager
    multi_cloud_manager manager(std::move(primary), std::move(secondary));

    // Run demos
    demo_basic_failover(manager);
    demo_replication(manager);
    demo_statistics(manager);

    std::cout << "\nDone!\n";

    return 0;
}
