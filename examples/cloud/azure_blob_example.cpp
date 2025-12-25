/**
 * @file azure_blob_example.cpp
 * @brief Azure Blob Storage usage example
 *
 * This example demonstrates how to use the azure_blob_storage class for
 * uploading and downloading files to/from Azure Blob Storage.
 *
 * Prerequisites:
 * - Azure storage account credentials configured
 * - A container with appropriate permissions
 *
 * Build:
 *   cmake --build build --target azure_blob_example
 *
 * Run:
 *   ./build/bin/azure_blob_example <account-name> <container-name>
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "kcenon/file_transfer/cloud/azure_blob_storage.h"

using namespace kcenon::file_transfer;

namespace {

/**
 * @brief Print usage information
 */
void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " <account-name> <container-name> [endpoint]\n\n";
    std::cerr << "Arguments:\n";
    std::cerr << "  account-name    Azure storage account name\n";
    std::cerr << "  container-name  Azure Blob container name\n";
    std::cerr << "  endpoint        Optional custom endpoint (for Azurite emulator, etc.)\n\n";
    std::cerr << "Environment:\n";
    std::cerr << "  AZURE_STORAGE_ACCOUNT         Azure storage account name\n";
    std::cerr << "  AZURE_STORAGE_KEY             Azure storage account key\n";
    std::cerr << "  AZURE_STORAGE_CONNECTION_STRING  Connection string (alternative)\n";
    std::cerr << "  AZURE_STORAGE_SAS_TOKEN       SAS token (alternative)\n\n";
    std::cerr << "Examples:\n";
    std::cerr << "  " << program << " mystorageaccount mycontainer\n";
    std::cerr << "  " << program << " devstoreaccount1 mycontainer http://localhost:10000/devstoreaccount1\n";
}

/**
 * @brief Create a test file with random content
 */
auto create_test_file(const std::filesystem::path& path, std::size_t size) -> bool {
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
 * @brief Demonstrate basic upload and download operations
 */
void demo_basic_operations(azure_blob_storage& storage) {
    std::cout << "\n=== Basic Operations Demo ===\n\n";

    // Upload data directly
    std::cout << "1. Uploading data directly to Azure Blob...\n";
    std::vector<std::byte> data(1024);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::byte>(i % 256);
    }

    auto upload_result = storage.upload("examples/test-data.bin", data);
    if (upload_result.has_value()) {
        std::cout << "   Uploaded: " << upload_result.value().key << "\n";
        std::cout << "   Size: " << upload_result.value().bytes_uploaded << " bytes\n";
        std::cout << "   ETag: " << upload_result.value().etag << "\n";
    } else {
        std::cerr << "   Upload failed: " << upload_result.error().message << "\n";
        return;
    }

    // Check if object exists
    std::cout << "\n2. Checking if blob exists...\n";
    auto exists_result = storage.exists("examples/test-data.bin");
    if (exists_result.has_value()) {
        std::cout << "   Exists: " << (exists_result.value() ? "yes" : "no") << "\n";
    }

    // Get object metadata
    std::cout << "\n3. Getting blob metadata...\n";
    auto metadata_result = storage.get_metadata("examples/test-data.bin");
    if (metadata_result.has_value()) {
        auto& metadata = metadata_result.value();
        std::cout << "   Key: " << metadata.key << "\n";
        std::cout << "   Content-Type: " << metadata.content_type << "\n";
    }

    // Download data
    std::cout << "\n4. Downloading data from Azure Blob...\n";
    auto download_result = storage.download("examples/test-data.bin");
    if (download_result.has_value()) {
        std::cout << "   Downloaded: " << download_result.value().size() << " bytes\n";
    } else {
        std::cerr << "   Download failed: " << download_result.error().message << "\n";
    }

    // Delete object
    std::cout << "\n5. Deleting blob...\n";
    auto delete_result = storage.delete_object("examples/test-data.bin");
    if (delete_result.has_value()) {
        std::cout << "   Deleted: " << delete_result.value().key << "\n";
    }

    std::cout << "\n=== Basic Operations Complete ===\n";
}

/**
 * @brief Demonstrate file upload/download operations
 */
void demo_file_operations(azure_blob_storage& storage) {
    std::cout << "\n=== File Operations Demo ===\n\n";

    // Create a temporary test file
    auto temp_dir = std::filesystem::temp_directory_path();
    auto upload_file = temp_dir / "azure_test_upload.txt";
    auto download_file = temp_dir / "azure_test_download.txt";

    std::cout << "1. Creating test file: " << upload_file << "\n";
    if (!create_test_file(upload_file, 4096)) {
        std::cerr << "   Failed to create test file\n";
        return;
    }
    std::cout << "   Created: " << std::filesystem::file_size(upload_file) << " bytes\n";

    // Upload file
    std::cout << "\n2. Uploading file to Azure Blob...\n";
    auto upload_result = storage.upload_file(upload_file, "examples/uploaded-file.txt");
    if (upload_result.has_value()) {
        std::cout << "   Uploaded: " << upload_result.value().key << "\n";
        std::cout << "   Duration: " << upload_result.value().duration.count() << " ms\n";
    } else {
        std::cerr << "   Upload failed: " << upload_result.error().message << "\n";
        std::filesystem::remove(upload_file);
        return;
    }

    // Download file
    std::cout << "\n3. Downloading file from Azure Blob...\n";
    auto download_result = storage.download_file("examples/uploaded-file.txt", download_file);
    if (download_result.has_value()) {
        std::cout << "   Downloaded to: " << download_file << "\n";
        std::cout << "   Size: " << download_result.value().bytes_downloaded << " bytes\n";
        std::cout << "   Duration: " << download_result.value().duration.count() << " ms\n";
    } else {
        std::cerr << "   Download failed: " << download_result.error().message << "\n";
    }

    // Cleanup
    std::cout << "\n4. Cleaning up...\n";
    storage.delete_object("examples/uploaded-file.txt");
    std::filesystem::remove(upload_file);
    std::filesystem::remove(download_file);
    std::cout << "   Cleanup complete\n";

    std::cout << "\n=== File Operations Complete ===\n";
}

/**
 * @brief Demonstrate SAS token generation
 */
void demo_sas_tokens(azure_blob_storage& storage) {
    std::cout << "\n=== SAS Token Demo ===\n\n";

    // First, upload a test object
    std::vector<std::byte> data(256);
    storage.upload("examples/sas-test.txt", data);

    // Generate blob SAS URL
    std::cout << "1. Generating blob SAS URL...\n";
    presigned_url_options get_options;
    get_options.method = "GET";
    get_options.expiration = std::chrono::seconds{3600};  // 1 hour

    auto blob_sas_result = storage.generate_blob_sas("examples/sas-test.txt", get_options);
    if (blob_sas_result.has_value()) {
        std::cout << "   URL (truncated): " << blob_sas_result.value().substr(0, 100) << "...\n";
        std::cout << "   Expires in: 1 hour\n";
    } else {
        std::cerr << "   Failed: " << blob_sas_result.error().message << "\n";
    }

    // Generate container SAS URL
    std::cout << "\n2. Generating container SAS URL...\n";
    presigned_url_options container_options;
    container_options.method = "GET";
    container_options.expiration = std::chrono::seconds{300};  // 5 minutes

    auto container_sas_result = storage.generate_container_sas(container_options);
    if (container_sas_result.has_value()) {
        std::cout << "   URL (truncated): " << container_sas_result.value().substr(0, 100) << "...\n";
        std::cout << "   Expires in: 5 minutes\n";
    }

    // Generate presigned PUT URL
    std::cout << "\n3. Generating presigned PUT URL...\n";
    presigned_url_options put_options;
    put_options.method = "PUT";
    put_options.expiration = std::chrono::seconds{600};

    auto put_url_result = storage.generate_presigned_url("examples/upload-target.txt", put_options);
    if (put_url_result.has_value()) {
        std::cout << "   URL (truncated): " << put_url_result.value().substr(0, 100) << "...\n";
    }

    // Cleanup
    storage.delete_object("examples/sas-test.txt");

    std::cout << "\n=== SAS Token Demo Complete ===\n";
}

/**
 * @brief Demonstrate streaming upload (block blob)
 */
void demo_streaming_upload(azure_blob_storage& storage) {
    std::cout << "\n=== Streaming Upload Demo ===\n\n";

    // Create upload stream
    std::cout << "1. Creating upload stream...\n";
    auto stream = storage.create_upload_stream("examples/streamed-file.bin");
    if (!stream) {
        std::cerr << "   Failed to create upload stream\n";
        return;
    }

    // Azure doesn't have an upload_id like S3
    std::cout << "   Upload ID: " << stream->upload_id().value_or("N/A (block blob)") << "\n";

    // Write chunks
    std::cout << "\n2. Writing blocks...\n";
    constexpr std::size_t chunk_size = 1024;
    constexpr std::size_t num_chunks = 5;

    for (std::size_t i = 0; i < num_chunks; ++i) {
        std::vector<std::byte> chunk(chunk_size);
        std::fill(chunk.begin(), chunk.end(), static_cast<std::byte>(i));

        auto write_result = stream->write(chunk);
        if (write_result.has_value()) {
            std::cout << "   Block " << (i + 1) << ": " << write_result.value() << " bytes\n";
        } else {
            std::cerr << "   Block " << (i + 1) << " failed: " << write_result.error().message << "\n";
            auto abort_result = stream->abort();
            (void)abort_result;
            return;
        }
    }

    std::cout << "   Total written: " << stream->bytes_written() << " bytes\n";

    // Finalize upload (commit block list)
    std::cout << "\n3. Committing block list...\n";
    auto finalize_result = stream->finalize();
    if (finalize_result.has_value()) {
        std::cout << "   Completed: " << finalize_result.value().key << "\n";
        std::cout << "   Total size: " << finalize_result.value().bytes_uploaded << " bytes\n";
    } else {
        std::cerr << "   Finalize failed: " << finalize_result.error().message << "\n";
    }

    // Cleanup
    storage.delete_object("examples/streamed-file.bin");

    std::cout << "\n=== Streaming Upload Complete ===\n";
}

/**
 * @brief Demonstrate access tier operations
 */
void demo_access_tiers(azure_blob_storage& storage) {
    std::cout << "\n=== Access Tier Demo ===\n\n";

    // Upload a test blob
    std::vector<std::byte> data(256);
    storage.upload("examples/tier-test.bin", data);

    // Get current access tier
    std::cout << "1. Getting current access tier...\n";
    auto get_tier_result = storage.get_access_tier("examples/tier-test.bin");
    if (get_tier_result.has_value()) {
        std::cout << "   Current tier: " << get_tier_result.value() << "\n";
    }

    // Set access tier to Cool
    std::cout << "\n2. Setting access tier to Cool...\n";
    auto set_tier_result = storage.set_access_tier("examples/tier-test.bin", "Cool");
    if (set_tier_result.has_value()) {
        std::cout << "   Tier changed successfully\n";
    } else {
        std::cerr << "   Failed: " << set_tier_result.error().message << "\n";
    }

    // Get updated access tier
    std::cout << "\n3. Verifying new access tier...\n";
    get_tier_result = storage.get_access_tier("examples/tier-test.bin");
    if (get_tier_result.has_value()) {
        std::cout << "   Current tier: " << get_tier_result.value() << "\n";
    }

    // Cleanup
    storage.delete_object("examples/tier-test.bin");

    std::cout << "\n=== Access Tier Demo Complete ===\n";
}

/**
 * @brief Demonstrate progress callbacks
 */
void demo_progress_callbacks(azure_blob_storage& storage) {
    std::cout << "\n=== Progress Callbacks Demo ===\n\n";

    // Set up progress callback
    storage.on_upload_progress([](const upload_progress& progress) {
        std::cout << "\r   Progress: " << progress.percentage() << "% "
                  << "(" << progress.bytes_transferred << "/" << progress.total_bytes << " bytes)"
                  << std::flush;
    });

    // Upload a larger file
    std::cout << "1. Uploading with progress tracking...\n";
    std::vector<std::byte> data(10 * 1024);  // 10 KB
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::byte>(i % 256);
    }

    auto result = storage.upload("examples/progress-test.bin", data);
    std::cout << "\n";

    if (result.has_value()) {
        std::cout << "   Upload complete!\n";
    }

    // Cleanup
    storage.delete_object("examples/progress-test.bin");

    std::cout << "\n=== Progress Callbacks Complete ===\n";
}

/**
 * @brief Demonstrate statistics
 */
void demo_statistics(azure_blob_storage& storage) {
    std::cout << "\n=== Statistics Demo ===\n\n";

    // Reset statistics
    storage.reset_statistics();

    // Perform some operations
    std::vector<std::byte> data(512);
    storage.upload("examples/stats-test-1.bin", data);
    storage.upload("examples/stats-test-2.bin", data);
    storage.download("examples/stats-test-1.bin");
    storage.list_objects();
    storage.delete_object("examples/stats-test-1.bin");
    storage.delete_object("examples/stats-test-2.bin");

    // Get statistics
    auto stats = storage.get_statistics();

    std::cout << "Statistics:\n";
    std::cout << "  Bytes uploaded:   " << stats.bytes_uploaded << "\n";
    std::cout << "  Bytes downloaded: " << stats.bytes_downloaded << "\n";
    std::cout << "  Upload count:     " << stats.upload_count << "\n";
    std::cout << "  Download count:   " << stats.download_count << "\n";
    std::cout << "  List count:       " << stats.list_count << "\n";
    std::cout << "  Delete count:     " << stats.delete_count << "\n";
    std::cout << "  Errors:           " << stats.errors << "\n";

    std::cout << "\n=== Statistics Complete ===\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    // Parse arguments
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    std::string account_name = argv[1];
    std::string container_name = argv[2];
    std::optional<std::string> endpoint;
    if (argc > 3) {
        endpoint = argv[3];
    }

    std::cout << "Azure Blob Storage Example\n";
    std::cout << "==========================\n\n";
    std::cout << "Configuration:\n";
    std::cout << "  Account:   " << account_name << "\n";
    std::cout << "  Container: " << container_name << "\n";
    if (endpoint.has_value()) {
        std::cout << "  Endpoint:  " << endpoint.value() << "\n";
    }
    std::cout << "\n";

    // Create credential provider
    std::cout << "Creating credential provider...\n";
    auto credentials = azure_blob_credential_provider::create_from_environment();
    if (!credentials) {
        credentials = azure_blob_credential_provider::create_default(account_name);
    }
    if (!credentials) {
        std::cerr << "Failed to create credential provider.\n";
        std::cerr << "Please set AZURE_STORAGE_ACCOUNT and AZURE_STORAGE_KEY,\n";
        std::cerr << "or AZURE_STORAGE_CONNECTION_STRING environment variables.\n";
        return 1;
    }
    std::cout << "  Credentials loaded successfully.\n";
    std::cout << "  Auth type: " << credentials->auth_type() << "\n\n";

    // Create Azure Blob configuration
    auto config_builder = cloud_config_builder::azure_blob()
        .with_account_name(account_name)
        .with_bucket(container_name);

    if (endpoint.has_value()) {
        config_builder.with_endpoint(endpoint.value());
    }

    auto config = config_builder.build_azure_blob();

    // Create Azure Blob storage
    std::cout << "Creating Azure Blob storage...\n";
    auto storage = azure_blob_storage::create(config, std::move(credentials));
    if (!storage) {
        std::cerr << "Failed to create Azure Blob storage.\n";
        return 1;
    }
    std::cout << "  Storage created successfully.\n\n";

    // Connect
    std::cout << "Connecting to Azure Blob Storage...\n";
    auto connect_result = storage->connect();
    if (!connect_result.has_value()) {
        std::cerr << "Failed to connect: " << connect_result.error().message << "\n";
        return 1;
    }
    std::cout << "  Connected successfully.\n";
    std::cout << "  Endpoint URL: " << storage->endpoint_url() << "\n";
    std::cout << "  Container: " << storage->container() << "\n";

    // Run demos
    demo_basic_operations(*storage);
    demo_file_operations(*storage);
    demo_sas_tokens(*storage);
    demo_streaming_upload(*storage);
    demo_access_tiers(*storage);
    demo_progress_callbacks(*storage);
    demo_statistics(*storage);

    // Disconnect
    std::cout << "\nDisconnecting...\n";
    storage->disconnect();
    std::cout << "Done!\n";

    return 0;
}
