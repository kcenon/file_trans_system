/**
 * @file gcs_example.cpp
 * @brief Google Cloud Storage usage example
 *
 * This example demonstrates how to use the gcs_storage class for
 * uploading and downloading files to/from Google Cloud Storage.
 *
 * Prerequisites:
 * - Google Cloud service account credentials configured
 * - A bucket with appropriate permissions
 *
 * Build:
 *   cmake --build build --target gcs_example
 *
 * Run:
 *   ./build/bin/gcs_example <project-id> <bucket-name>
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "kcenon/file_transfer/cloud/gcs_storage.h"

using namespace kcenon::file_transfer;

namespace {

/**
 * @brief Print usage information
 */
void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " <project-id> <bucket-name> [endpoint]\n\n";
    std::cerr << "Arguments:\n";
    std::cerr << "  project-id      Google Cloud project ID\n";
    std::cerr << "  bucket-name     GCS bucket name\n";
    std::cerr << "  endpoint        Optional custom endpoint (for fake-gcs-server, etc.)\n\n";
    std::cerr << "Environment:\n";
    std::cerr << "  GOOGLE_APPLICATION_CREDENTIALS  Path to service account JSON file\n";
    std::cerr << "  GOOGLE_CLOUD_PROJECT            Google Cloud project ID\n\n";
    std::cerr << "Examples:\n";
    std::cerr << "  " << program << " my-project my-bucket\n";
    std::cerr << "  " << program << " my-project my-bucket http://localhost:4443\n";
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
void demo_basic_operations(gcs_storage& storage) {
    std::cout << "\n=== Basic Operations Demo ===\n\n";

    // Upload data directly
    std::cout << "1. Uploading data directly to GCS...\n";
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
    std::cout << "\n2. Checking if object exists...\n";
    auto exists_result = storage.exists("examples/test-data.bin");
    if (exists_result.has_value()) {
        std::cout << "   Exists: " << (exists_result.value() ? "yes" : "no") << "\n";
    }

    // Get object metadata
    std::cout << "\n3. Getting object metadata...\n";
    auto metadata_result = storage.get_metadata("examples/test-data.bin");
    if (metadata_result.has_value()) {
        auto& metadata = metadata_result.value();
        std::cout << "   Key: " << metadata.key << "\n";
        std::cout << "   Content-Type: " << metadata.content_type << "\n";
    }

    // Download data
    std::cout << "\n4. Downloading data from GCS...\n";
    auto download_result = storage.download("examples/test-data.bin");
    if (download_result.has_value()) {
        std::cout << "   Downloaded: " << download_result.value().size() << " bytes\n";
    } else {
        std::cerr << "   Download failed: " << download_result.error().message << "\n";
    }

    // Delete object
    std::cout << "\n5. Deleting object...\n";
    auto delete_result = storage.delete_object("examples/test-data.bin");
    if (delete_result.has_value()) {
        std::cout << "   Deleted: " << delete_result.value().key << "\n";
    }

    std::cout << "\n=== Basic Operations Complete ===\n";
}

/**
 * @brief Demonstrate file upload/download operations
 */
void demo_file_operations(gcs_storage& storage) {
    std::cout << "\n=== File Operations Demo ===\n\n";

    // Create a temporary test file
    auto temp_dir = std::filesystem::temp_directory_path();
    auto upload_file = temp_dir / "gcs_test_upload.txt";
    auto download_file = temp_dir / "gcs_test_download.txt";

    std::cout << "1. Creating test file: " << upload_file << "\n";
    if (!create_test_file(upload_file, 4096)) {
        std::cerr << "   Failed to create test file\n";
        return;
    }
    std::cout << "   Created: " << std::filesystem::file_size(upload_file) << " bytes\n";

    // Upload file
    std::cout << "\n2. Uploading file to GCS...\n";
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
    std::cout << "\n3. Downloading file from GCS...\n";
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
 * @brief Demonstrate signed URL generation
 */
void demo_signed_urls(gcs_storage& storage) {
    std::cout << "\n=== Signed URL Demo ===\n\n";

    // First, upload a test object
    std::vector<std::byte> data(256);
    storage.upload("examples/signed-url-test.txt", data);

    // Generate signed URL for GET
    std::cout << "1. Generating signed URL for download...\n";
    presigned_url_options get_options;
    get_options.method = "GET";
    get_options.expiration = std::chrono::seconds{3600};  // 1 hour

    auto get_url_result = storage.generate_signed_url("examples/signed-url-test.txt", get_options);
    if (get_url_result.has_value()) {
        std::cout << "   URL (truncated): " << get_url_result.value().substr(0, 100) << "...\n";
        std::cout << "   Expires in: 1 hour\n";
    } else {
        std::cerr << "   Failed: " << get_url_result.error().message << "\n";
    }

    // Generate signed URL for PUT
    std::cout << "\n2. Generating signed URL for upload...\n";
    presigned_url_options put_options;
    put_options.method = "PUT";
    put_options.expiration = std::chrono::seconds{1800};  // 30 minutes
    put_options.content_type = "application/octet-stream";

    auto put_url_result = storage.generate_signed_url("examples/upload-via-url.txt", put_options);
    if (put_url_result.has_value()) {
        std::cout << "   URL (truncated): " << put_url_result.value().substr(0, 100) << "...\n";
        std::cout << "   Expires in: 30 minutes\n";
    } else {
        std::cerr << "   Failed: " << put_url_result.error().message << "\n";
    }

    // Cleanup
    storage.delete_object("examples/signed-url-test.txt");

    std::cout << "\n=== Signed URL Demo Complete ===\n";
}

/**
 * @brief Demonstrate streaming upload
 */
void demo_streaming_upload(gcs_storage& storage) {
    std::cout << "\n=== Streaming Upload Demo ===\n\n";

    // Create upload stream
    std::cout << "1. Creating upload stream...\n";
    auto stream = storage.create_upload_stream("examples/streamed-file.bin");
    if (!stream) {
        std::cerr << "   Failed to create upload stream\n";
        return;
    }
    std::cout << "   Upload ID: " << stream->upload_id().value_or("N/A") << "\n";

    // Write data in chunks
    std::cout << "\n2. Writing data in chunks...\n";
    for (int i = 0; i < 5; ++i) {
        std::vector<std::byte> chunk(1000, static_cast<std::byte>(i));
        auto result = stream->write(chunk);
        if (result.has_value()) {
            std::cout << "   Chunk " << (i + 1) << ": wrote " << result.value() << " bytes\n";
        } else {
            std::cerr << "   Write failed: " << result.error().message << "\n";
            stream->abort();
            return;
        }
    }

    std::cout << "   Total bytes written: " << stream->bytes_written() << "\n";

    // Finalize upload
    std::cout << "\n3. Finalizing upload...\n";
    auto finalize_result = stream->finalize();
    if (finalize_result.has_value()) {
        std::cout << "   Key: " << finalize_result.value().key << "\n";
        std::cout << "   ETag: " << finalize_result.value().etag << "\n";
        std::cout << "   Bytes uploaded: " << finalize_result.value().bytes_uploaded << "\n";
    } else {
        std::cerr << "   Finalize failed: " << finalize_result.error().message << "\n";
    }

    // Cleanup
    storage.delete_object("examples/streamed-file.bin");

    std::cout << "\n=== Streaming Upload Complete ===\n";
}

/**
 * @brief Demonstrate storage class management
 */
void demo_storage_classes(gcs_storage& storage) {
    std::cout << "\n=== Storage Class Demo ===\n\n";

    // Upload a test object
    std::vector<std::byte> data(512);
    storage.upload("examples/storage-class-test.txt", data);

    // Get current storage class
    std::cout << "1. Getting current storage class...\n";
    auto get_result = storage.get_storage_class("examples/storage-class-test.txt");
    if (get_result.has_value()) {
        std::cout << "   Current: " << get_result.value() << "\n";
    }

    // Change storage class
    std::cout << "\n2. Changing storage class to NEARLINE...\n";
    auto set_result = storage.set_storage_class("examples/storage-class-test.txt", "NEARLINE");
    if (set_result.has_value()) {
        std::cout << "   Storage class changed successfully\n";
    } else {
        std::cerr << "   Failed: " << set_result.error().message << "\n";
    }

    // Cleanup
    storage.delete_object("examples/storage-class-test.txt");

    std::cout << "\n=== Storage Class Demo Complete ===\n";
}

/**
 * @brief Demonstrate object composition
 */
void demo_compose_objects(gcs_storage& storage) {
    std::cout << "\n=== Object Composition Demo ===\n\n";

    // Upload multiple parts
    std::cout << "1. Uploading parts...\n";
    std::vector<std::string> part_keys;
    for (int i = 0; i < 3; ++i) {
        std::string key = "examples/part-" + std::to_string(i) + ".txt";
        std::vector<std::byte> data(500, static_cast<std::byte>('A' + i));

        auto result = storage.upload(key, data);
        if (result.has_value()) {
            std::cout << "   Uploaded: " << key << "\n";
            part_keys.push_back(key);
        }
    }

    // Compose objects
    std::cout << "\n2. Composing objects...\n";
    auto compose_result = storage.compose_objects(part_keys, "examples/composed.txt");
    if (compose_result.has_value()) {
        std::cout << "   Composed key: " << compose_result.value().key << "\n";
    } else {
        std::cerr << "   Compose failed: " << compose_result.error().message << "\n";
    }

    // Cleanup
    std::cout << "\n3. Cleaning up...\n";
    for (const auto& key : part_keys) {
        storage.delete_object(key);
    }
    storage.delete_object("examples/composed.txt");
    std::cout << "   Cleanup complete\n";

    std::cout << "\n=== Object Composition Complete ===\n";
}

/**
 * @brief Demonstrate list operations
 */
void demo_list_operations(gcs_storage& storage) {
    std::cout << "\n=== List Operations Demo ===\n\n";

    // Upload some test objects
    std::cout << "1. Creating test objects...\n";
    for (int i = 0; i < 5; ++i) {
        std::string key = "examples/list-test/file-" + std::to_string(i) + ".txt";
        std::vector<std::byte> data(100, static_cast<std::byte>(i));
        storage.upload(key, data);
        std::cout << "   Created: " << key << "\n";
    }

    // List objects with prefix
    std::cout << "\n2. Listing objects with prefix 'examples/list-test/'...\n";
    list_objects_options options;
    options.prefix = "examples/list-test/";
    options.max_keys = 10;

    auto list_result = storage.list_objects(options);
    if (list_result.has_value()) {
        std::cout << "   Found " << list_result.value().objects.size() << " objects\n";
        for (const auto& obj : list_result.value().objects) {
            std::cout << "     - " << obj.key << " (" << obj.size << " bytes)\n";
        }
    } else {
        std::cerr << "   List failed: " << list_result.error().message << "\n";
    }

    // Cleanup
    std::cout << "\n3. Cleaning up...\n";
    std::vector<std::string> keys_to_delete;
    for (int i = 0; i < 5; ++i) {
        keys_to_delete.push_back("examples/list-test/file-" + std::to_string(i) + ".txt");
    }
    storage.delete_objects(keys_to_delete);
    std::cout << "   Cleanup complete\n";

    std::cout << "\n=== List Operations Complete ===\n";
}

/**
 * @brief Demonstrate statistics tracking
 */
void demo_statistics(gcs_storage& storage) {
    std::cout << "\n=== Statistics Demo ===\n\n";

    // Reset statistics
    storage.reset_statistics();
    std::cout << "1. Statistics reset\n";

    // Perform some operations
    std::cout << "\n2. Performing operations...\n";
    std::vector<std::byte> data(512);

    storage.upload("examples/stats-test-1.txt", data);
    storage.upload("examples/stats-test-2.txt", data);
    storage.download("examples/stats-test-1.txt");
    storage.list_objects();
    storage.delete_object("examples/stats-test-1.txt");
    storage.delete_object("examples/stats-test-2.txt");

    // Get statistics
    std::cout << "\n3. Current statistics:\n";
    auto stats = storage.get_statistics();
    std::cout << "   Upload count: " << stats.upload_count << "\n";
    std::cout << "   Download count: " << stats.download_count << "\n";
    std::cout << "   List count: " << stats.list_count << "\n";
    std::cout << "   Delete count: " << stats.delete_count << "\n";
    std::cout << "   Bytes uploaded: " << stats.bytes_uploaded << "\n";
    std::cout << "   Bytes downloaded: " << stats.bytes_downloaded << "\n";
    std::cout << "   Errors: " << stats.errors << "\n";

    std::cout << "\n=== Statistics Demo Complete ===\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    std::string project_id = argv[1];
    std::string bucket_name = argv[2];
    std::optional<std::string> endpoint;
    if (argc >= 4) {
        endpoint = argv[3];
    }

    std::cout << "Google Cloud Storage Example\n";
    std::cout << "============================\n";
    std::cout << "Project ID: " << project_id << "\n";
    std::cout << "Bucket: " << bucket_name << "\n";
    if (endpoint) {
        std::cout << "Endpoint: " << *endpoint << "\n";
    }

    // Create credentials provider
    std::cout << "\n1. Setting up credentials...\n";
    auto credentials = gcs_credential_provider::create_from_environment();
    if (!credentials) {
        // Try default credentials
        credentials = gcs_credential_provider::create_default(project_id);
    }

    if (!credentials) {
        std::cerr << "   Failed to obtain credentials!\n";
        std::cerr << "   Please set GOOGLE_APPLICATION_CREDENTIALS environment variable\n";
        std::cerr << "   to point to a service account JSON file.\n";
        return 1;
    }

    std::cout << "   Credentials obtained successfully\n";
    std::cout << "   Project ID: " << credentials->project_id() << "\n";
    std::cout << "   Auth type: " << credentials->auth_type() << "\n";
    if (!credentials->service_account_email().empty()) {
        std::cout << "   Service account: " << credentials->service_account_email() << "\n";
    }

    // Create GCS configuration
    auto config_builder = cloud_config_builder::gcs()
        .with_project_id(project_id)
        .with_bucket(bucket_name);

    if (endpoint) {
        config_builder.with_endpoint(*endpoint);
    }

    auto config = config_builder.build_gcs();

    // Create storage instance
    std::cout << "\n2. Creating GCS storage instance...\n";
    auto storage = gcs_storage::create(config, std::move(credentials));
    if (!storage) {
        std::cerr << "   Failed to create GCS storage instance!\n";
        return 1;
    }
    std::cout << "   Storage instance created\n";

    // Connect to storage
    std::cout << "\n3. Connecting to GCS...\n";
    auto connect_result = storage->connect();
    if (!connect_result.has_value()) {
        std::cerr << "   Connection failed: " << connect_result.error().message << "\n";
        return 1;
    }
    std::cout << "   Connected successfully\n";
    std::cout << "   Endpoint: " << storage->endpoint_url() << "\n";

    // Run demos
    try {
        demo_basic_operations(*storage);
        demo_file_operations(*storage);
        demo_signed_urls(*storage);
        demo_streaming_upload(*storage);
        demo_storage_classes(*storage);
        demo_compose_objects(*storage);
        demo_list_operations(*storage);
        demo_statistics(*storage);
    } catch (const std::exception& e) {
        std::cerr << "\nError during demo: " << e.what() << "\n";
    }

    // Disconnect
    std::cout << "\n4. Disconnecting from GCS...\n";
    storage->disconnect();
    std::cout << "   Disconnected\n";

    std::cout << "\nAll demos completed successfully!\n";
    return 0;
}
