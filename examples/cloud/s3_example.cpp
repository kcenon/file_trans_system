/**
 * @file s3_example.cpp
 * @brief AWS S3 storage usage example
 *
 * This example demonstrates how to use the s3_storage class for
 * uploading and downloading files to/from AWS S3.
 *
 * Prerequisites:
 * - AWS credentials configured (environment variables or ~/.aws/credentials)
 * - An S3 bucket with appropriate permissions
 *
 * Build:
 *   cmake --build build --target s3_example
 *
 * Run:
 *   ./build/bin/s3_example <bucket-name> <region>
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "kcenon/file_transfer/cloud/s3_storage.h"

using namespace kcenon::file_transfer;

namespace {

/**
 * @brief Print usage information
 */
void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " <bucket-name> <region> [endpoint]\n\n";
    std::cerr << "Arguments:\n";
    std::cerr << "  bucket-name  S3 bucket name\n";
    std::cerr << "  region       AWS region (e.g., us-east-1)\n";
    std::cerr << "  endpoint     Optional custom endpoint (for MinIO, etc.)\n\n";
    std::cerr << "Environment:\n";
    std::cerr << "  AWS_ACCESS_KEY_ID      AWS access key\n";
    std::cerr << "  AWS_SECRET_ACCESS_KEY  AWS secret key\n";
    std::cerr << "  AWS_SESSION_TOKEN      Optional session token\n\n";
    std::cerr << "Examples:\n";
    std::cerr << "  " << program << " my-bucket us-east-1\n";
    std::cerr << "  " << program << " my-bucket us-east-1 http://localhost:9000\n";
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
void demo_basic_operations(s3_storage& storage) {
    std::cout << "\n=== Basic Operations Demo ===\n\n";

    // Upload data directly
    std::cout << "1. Uploading data directly to S3...\n";
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
    std::cout << "\n4. Downloading data from S3...\n";
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
void demo_file_operations(s3_storage& storage) {
    std::cout << "\n=== File Operations Demo ===\n\n";

    // Create a temporary test file
    auto temp_dir = std::filesystem::temp_directory_path();
    auto upload_file = temp_dir / "s3_test_upload.txt";
    auto download_file = temp_dir / "s3_test_download.txt";

    std::cout << "1. Creating test file: " << upload_file << "\n";
    if (!create_test_file(upload_file, 4096)) {
        std::cerr << "   Failed to create test file\n";
        return;
    }
    std::cout << "   Created: " << std::filesystem::file_size(upload_file) << " bytes\n";

    // Upload file
    std::cout << "\n2. Uploading file to S3...\n";
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
    std::cout << "\n3. Downloading file from S3...\n";
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
 * @brief Demonstrate presigned URL generation
 */
void demo_presigned_urls(s3_storage& storage) {
    std::cout << "\n=== Presigned URL Demo ===\n\n";

    // First, upload a test object
    std::vector<std::byte> data(256);
    storage.upload("examples/presigned-test.txt", data);

    // Generate GET presigned URL
    std::cout << "1. Generating presigned GET URL...\n";
    presigned_url_options get_options;
    get_options.method = "GET";
    get_options.expiration = std::chrono::seconds{3600};  // 1 hour

    auto get_url_result = storage.generate_presigned_url("examples/presigned-test.txt", get_options);
    if (get_url_result.has_value()) {
        std::cout << "   URL (truncated): " << get_url_result.value().substr(0, 100) << "...\n";
        std::cout << "   Expires in: 1 hour\n";
    } else {
        std::cerr << "   Failed: " << get_url_result.error().message << "\n";
    }

    // Generate PUT presigned URL
    std::cout << "\n2. Generating presigned PUT URL...\n";
    presigned_url_options put_options;
    put_options.method = "PUT";
    put_options.expiration = std::chrono::seconds{300};  // 5 minutes
    put_options.content_type = "text/plain";

    auto put_url_result = storage.generate_presigned_url("examples/upload-target.txt", put_options);
    if (put_url_result.has_value()) {
        std::cout << "   URL (truncated): " << put_url_result.value().substr(0, 100) << "...\n";
        std::cout << "   Expires in: 5 minutes\n";
    }

    // Cleanup
    storage.delete_object("examples/presigned-test.txt");

    std::cout << "\n=== Presigned URL Demo Complete ===\n";
}

/**
 * @brief Demonstrate streaming upload (multipart)
 */
void demo_streaming_upload(s3_storage& storage) {
    std::cout << "\n=== Streaming Upload Demo ===\n\n";

    // Create upload stream
    std::cout << "1. Creating upload stream...\n";
    auto stream = storage.create_upload_stream("examples/streamed-file.bin");
    if (!stream) {
        std::cerr << "   Failed to create upload stream\n";
        return;
    }

    std::cout << "   Upload ID: " << stream->upload_id().value_or("N/A") << "\n";

    // Write chunks
    std::cout << "\n2. Writing chunks...\n";
    constexpr std::size_t chunk_size = 1024;
    constexpr std::size_t num_chunks = 5;

    for (std::size_t i = 0; i < num_chunks; ++i) {
        std::vector<std::byte> chunk(chunk_size);
        std::fill(chunk.begin(), chunk.end(), static_cast<std::byte>(i));

        auto write_result = stream->write(chunk);
        if (write_result.has_value()) {
            std::cout << "   Chunk " << (i + 1) << ": " << write_result.value() << " bytes\n";
        } else {
            std::cerr << "   Chunk " << (i + 1) << " failed: " << write_result.error().message << "\n";
            auto abort_result = stream->abort();
            (void)abort_result;  // Ignore abort result
            return;
        }
    }

    std::cout << "   Total written: " << stream->bytes_written() << " bytes\n";

    // Finalize upload
    std::cout << "\n3. Finalizing upload...\n";
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
 * @brief Demonstrate progress callbacks
 */
void demo_progress_callbacks(s3_storage& storage) {
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
void demo_statistics(s3_storage& storage) {
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

    std::string bucket = argv[1];
    std::string region = argv[2];
    std::optional<std::string> endpoint;
    if (argc > 3) {
        endpoint = argv[3];
    }

    std::cout << "AWS S3 Storage Example\n";
    std::cout << "======================\n\n";
    std::cout << "Configuration:\n";
    std::cout << "  Bucket:   " << bucket << "\n";
    std::cout << "  Region:   " << region << "\n";
    if (endpoint.has_value()) {
        std::cout << "  Endpoint: " << endpoint.value() << "\n";
    }
    std::cout << "\n";

    // Create credential provider
    std::cout << "Creating credential provider...\n";
    auto credentials = s3_credential_provider::create_default();
    if (!credentials) {
        std::cerr << "Failed to create credential provider.\n";
        std::cerr << "Please set AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY environment variables.\n";
        return 1;
    }
    std::cout << "  Credentials loaded successfully.\n\n";

    // Create S3 configuration
    auto config_builder = cloud_config_builder::s3()
        .with_bucket(bucket)
        .with_region(region);

    if (endpoint.has_value()) {
        config_builder.with_endpoint(endpoint.value())
                      .with_path_style(true);  // Use path-style for custom endpoints
    }

    auto config = config_builder.build_s3();

    // Create S3 storage
    std::cout << "Creating S3 storage...\n";
    auto storage = s3_storage::create(config, std::move(credentials));
    if (!storage) {
        std::cerr << "Failed to create S3 storage.\n";
        return 1;
    }
    std::cout << "  Storage created successfully.\n\n";

    // Connect
    std::cout << "Connecting to S3...\n";
    auto connect_result = storage->connect();
    if (!connect_result.has_value()) {
        std::cerr << "Failed to connect: " << connect_result.error().message << "\n";
        return 1;
    }
    std::cout << "  Connected successfully.\n";
    std::cout << "  Endpoint URL: " << storage->endpoint_url() << "\n";

    // Run demos
    demo_basic_operations(*storage);
    demo_file_operations(*storage);
    demo_presigned_urls(*storage);
    demo_streaming_upload(*storage);
    demo_progress_callbacks(*storage);
    demo_statistics(*storage);

    // Disconnect
    std::cout << "\nDisconnecting...\n";
    storage->disconnect();
    std::cout << "Done!\n";

    return 0;
}
