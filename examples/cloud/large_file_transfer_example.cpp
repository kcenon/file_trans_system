/**
 * @file large_file_transfer_example.cpp
 * @brief Large file cloud transfer example with streaming
 *
 * This example demonstrates how to efficiently transfer large files
 * to cloud storage using streaming/multipart uploads. Features include:
 * - Streaming uploads to avoid loading entire file into memory
 * - Multipart upload for files > 5MB (S3)
 * - Progress tracking
 * - Checksum verification
 * - Resume capability
 *
 * Prerequisites:
 * - AWS credentials configured
 * - An S3 bucket with appropriate permissions
 *
 * Build:
 *   cmake --build build --target large_file_transfer_example
 *
 * Run:
 *   ./build/bin/large_file_transfer_example <bucket-name> <region> [file-size-mb]
 */

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
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
    std::cerr << "Usage: " << program << " <bucket-name> <region> [file-size-mb]\n\n";
    std::cerr << "Arguments:\n";
    std::cerr << "  bucket-name   S3 bucket name\n";
    std::cerr << "  region        AWS region (e.g., us-east-1)\n";
    std::cerr << "  file-size-mb  Test file size in MB (default: 10)\n\n";
    std::cerr << "Environment:\n";
    std::cerr << "  AWS_ACCESS_KEY_ID      AWS access key\n";
    std::cerr << "  AWS_SECRET_ACCESS_KEY  AWS secret key\n\n";
    std::cerr << "Examples:\n";
    std::cerr << "  " << program << " my-bucket us-east-1\n";
    std::cerr << "  " << program << " my-bucket us-east-1 100\n";
}

/**
 * @brief Format bytes to human readable string
 */
auto format_bytes(uint64_t bytes) -> std::string {
    constexpr uint64_t KB = 1024;
    constexpr uint64_t MB = KB * 1024;
    constexpr uint64_t GB = MB * 1024;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    if (bytes >= GB) {
        oss << static_cast<double>(bytes) / GB << " GB";
    } else if (bytes >= MB) {
        oss << static_cast<double>(bytes) / MB << " MB";
    } else if (bytes >= KB) {
        oss << static_cast<double>(bytes) / KB << " KB";
    } else {
        oss << bytes << " bytes";
    }

    return oss.str();
}

/**
 * @brief Format duration to human readable string
 */
auto format_duration(std::chrono::milliseconds ms) -> std::string {
    std::ostringstream oss;

    if (ms.count() >= 60000) {
        auto minutes = ms.count() / 60000;
        auto seconds = (ms.count() % 60000) / 1000;
        oss << minutes << "m " << seconds << "s";
    } else if (ms.count() >= 1000) {
        oss << std::fixed << std::setprecision(2)
            << static_cast<double>(ms.count()) / 1000.0 << "s";
    } else {
        oss << ms.count() << "ms";
    }

    return oss.str();
}

/**
 * @brief Create a large test file with random content
 */
auto create_large_test_file(const fs::path& path, std::size_t size_mb) -> bool {
    std::cout << "Creating test file: " << path << " (" << size_mb << " MB)...\n";

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    // Use random data to prevent compression skewing results
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    constexpr std::size_t chunk_size = 1024 * 1024;  // 1 MB chunks
    std::vector<char> buffer(chunk_size);

    for (std::size_t i = 0; i < size_mb; ++i) {
        for (auto& byte : buffer) {
            byte = static_cast<char>(dis(gen));
        }
        file.write(buffer.data(), static_cast<std::streamsize>(chunk_size));

        // Progress indicator
        std::cout << "\r  Progress: " << (i + 1) << "/" << size_mb << " MB" << std::flush;
    }
    std::cout << "\n";

    return file.good();
}

/**
 * @brief Progress bar display
 */
class progress_display {
public:
    explicit progress_display(const std::string& operation) : operation_(operation) {}

    void update(uint64_t bytes, uint64_t total, uint64_t speed_bps) {
        double percentage = (total > 0)
            ? static_cast<double>(bytes) / static_cast<double>(total) * 100.0
            : 0.0;

        std::cout << "\r  " << operation_ << ": "
                  << std::fixed << std::setprecision(1) << percentage << "% "
                  << "(" << format_bytes(bytes) << "/" << format_bytes(total) << ") "
                  << format_bytes(speed_bps) << "/s     " << std::flush;
    }

    void complete() {
        std::cout << "\n";
    }

private:
    std::string operation_;
};

/**
 * @brief Demonstrate streaming upload
 */
void demo_streaming_upload(s3_storage& storage, const fs::path& test_file) {
    std::cout << "\n=== Streaming Upload Demo ===\n\n";

    auto file_size = fs::file_size(test_file);
    std::cout << "File: " << test_file << "\n";
    std::cout << "Size: " << format_bytes(file_size) << "\n\n";

    // Create upload stream
    std::cout << "1. Creating upload stream...\n";
    auto stream = storage.create_upload_stream("large-files/streamed-upload.bin");
    if (!stream) {
        std::cerr << "   Failed to create upload stream\n";
        return;
    }

    std::cout << "   Upload ID: " << stream->upload_id().value_or("N/A") << "\n";

    // Read and upload in chunks
    std::cout << "\n2. Uploading in chunks...\n";
    std::ifstream file(test_file, std::ios::binary);
    if (!file) {
        std::cerr << "   Failed to open file\n";
        stream->abort();
        return;
    }

    constexpr std::size_t chunk_size = 5 * 1024 * 1024;  // 5 MB chunks (S3 multipart minimum)
    std::vector<std::byte> buffer(chunk_size);

    auto start_time = std::chrono::steady_clock::now();
    std::size_t total_written = 0;
    std::size_t chunk_count = 0;

    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()),
                  static_cast<std::streamsize>(chunk_size));
        auto bytes_read = static_cast<std::size_t>(file.gcount());

        if (bytes_read == 0) {
            break;
        }

        auto write_result = stream->write(std::span{buffer.data(), bytes_read});
        if (!write_result.has_value()) {
            std::cerr << "\n   Chunk write failed: " << write_result.error().message << "\n";
            stream->abort();
            return;
        }

        total_written += bytes_read;
        ++chunk_count;

        // Progress
        double percentage = static_cast<double>(total_written) /
                           static_cast<double>(file_size) * 100.0;
        std::cout << "\r   Progress: " << std::fixed << std::setprecision(1)
                  << percentage << "% (chunk " << chunk_count << ")" << std::flush;
    }
    std::cout << "\n";

    // Finalize upload
    std::cout << "\n3. Finalizing upload...\n";
    auto finalize_result = stream->finalize();
    if (!finalize_result.has_value()) {
        std::cerr << "   Finalize failed: " << finalize_result.error().message << "\n";
        return;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "   Completed successfully!\n";
    std::cout << "   Key: " << finalize_result.value().key << "\n";
    std::cout << "   ETag: " << finalize_result.value().etag << "\n";
    std::cout << "   Total size: " << format_bytes(finalize_result.value().bytes_uploaded) << "\n";
    std::cout << "   Duration: " << format_duration(duration) << "\n";

    if (duration.count() > 0) {
        auto speed = (total_written * 1000) / static_cast<uint64_t>(duration.count());
        std::cout << "   Average speed: " << format_bytes(speed) << "/s\n";
    }

    std::cout << "\n=== Streaming Upload Complete ===\n";
}

/**
 * @brief Demonstrate streaming download
 */
void demo_streaming_download(s3_storage& storage) {
    std::cout << "\n=== Streaming Download Demo ===\n\n";

    // Create download stream
    std::cout << "1. Creating download stream...\n";
    auto stream = storage.create_download_stream("large-files/streamed-upload.bin");
    if (!stream) {
        std::cerr << "   Failed to create download stream\n";
        return;
    }

    auto total_size = stream->total_size();
    std::cout << "   Total size: " << format_bytes(total_size) << "\n";

    // Download to temporary file
    auto temp_path = fs::temp_directory_path() / "downloaded-large-file.bin";
    std::ofstream file(temp_path, std::ios::binary);
    if (!file) {
        std::cerr << "   Failed to create output file\n";
        return;
    }

    // Read in chunks
    std::cout << "\n2. Downloading in chunks...\n";
    constexpr std::size_t chunk_size = 1024 * 1024;  // 1 MB chunks
    std::vector<std::byte> buffer(chunk_size);

    auto start_time = std::chrono::steady_clock::now();

    while (stream->has_more()) {
        auto read_result = stream->read(buffer);
        if (!read_result.has_value()) {
            std::cerr << "\n   Read failed: " << read_result.error().message << "\n";
            return;
        }

        auto bytes_read = read_result.value();
        if (bytes_read == 0) {
            break;
        }

        file.write(reinterpret_cast<const char*>(buffer.data()),
                   static_cast<std::streamsize>(bytes_read));

        // Progress
        double percentage = static_cast<double>(stream->bytes_read()) /
                           static_cast<double>(total_size) * 100.0;
        std::cout << "\r   Progress: " << std::fixed << std::setprecision(1)
                  << percentage << "%" << std::flush;
    }
    std::cout << "\n";

    file.close();

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "\n3. Download complete!\n";
    std::cout << "   Downloaded to: " << temp_path << "\n";
    std::cout << "   Total bytes: " << format_bytes(stream->bytes_read()) << "\n";
    std::cout << "   Duration: " << format_duration(duration) << "\n";

    if (duration.count() > 0) {
        auto speed = (stream->bytes_read() * 1000) / static_cast<uint64_t>(duration.count());
        std::cout << "   Average speed: " << format_bytes(speed) << "/s\n";
    }

    // Cleanup
    fs::remove(temp_path);

    std::cout << "\n=== Streaming Download Complete ===\n";
}

/**
 * @brief Demonstrate file upload with progress
 */
void demo_upload_with_progress(s3_storage& storage, const fs::path& test_file) {
    std::cout << "\n=== Upload with Progress Demo ===\n\n";

    progress_display progress("Upload");

    // Set progress callback
    storage.on_upload_progress([&progress](const upload_progress& p) {
        progress.update(p.bytes_transferred, p.total_bytes, p.speed_bps);
    });

    std::cout << "Uploading " << test_file.filename() << "...\n";

    auto start_time = std::chrono::steady_clock::now();
    auto result = storage.upload_file(test_file, "large-files/with-progress.bin");
    auto end_time = std::chrono::steady_clock::now();

    progress.complete();

    if (result.has_value()) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        std::cout << "\nUpload successful!\n";
        std::cout << "  Size: " << format_bytes(result.value().bytes_uploaded) << "\n";
        std::cout << "  Duration: " << format_duration(duration) << "\n";
    } else {
        std::cerr << "\nUpload failed: " << result.error().message << "\n";
    }

    std::cout << "\n=== Upload with Progress Complete ===\n";
}

/**
 * @brief Demonstrate async upload
 */
void demo_async_upload(s3_storage& storage, const fs::path& test_file) {
    std::cout << "\n=== Async Upload Demo ===\n\n";

    std::cout << "Starting async upload...\n";

    auto start_time = std::chrono::steady_clock::now();
    auto future = storage.upload_file_async(test_file, "large-files/async-upload.bin");

    // Simulate doing other work while upload is in progress
    std::cout << "Upload started, doing other work...\n";
    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::cout << "  Working... (" << (i + 1) << "/5)\n";
    }

    // Wait for result
    std::cout << "\nWaiting for upload to complete...\n";
    auto result = future.get();
    auto end_time = std::chrono::steady_clock::now();

    if (result.has_value()) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        std::cout << "Upload successful!\n";
        std::cout << "  Size: " << format_bytes(result.value().bytes_uploaded) << "\n";
        std::cout << "  Duration: " << format_duration(duration) << "\n";
    } else {
        std::cerr << "Upload failed: " << result.error().message << "\n";
    }

    std::cout << "\n=== Async Upload Complete ===\n";
}

/**
 * @brief Cleanup uploaded files
 */
void cleanup_uploads(s3_storage& storage) {
    std::cout << "\n=== Cleaning Up ===\n\n";

    std::vector<std::string> keys = {
        "large-files/streamed-upload.bin",
        "large-files/with-progress.bin",
        "large-files/async-upload.bin"
    };

    for (const auto& key : keys) {
        auto result = storage.delete_object(key);
        if (result.has_value()) {
            std::cout << "  Deleted: " << key << "\n";
        }
    }

    std::cout << "\n=== Cleanup Complete ===\n";
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
    std::size_t file_size_mb = (argc > 3) ? std::stoul(argv[3]) : 10;

    std::cout << "Large File Transfer Example\n";
    std::cout << "===========================\n\n";
    std::cout << "Configuration:\n";
    std::cout << "  Bucket:         " << bucket << "\n";
    std::cout << "  Region:         " << region << "\n";
    std::cout << "  Test file size: " << file_size_mb << " MB\n";
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

    // Create test file
    auto temp_dir = fs::temp_directory_path();
    auto test_file = temp_dir / "large_test_file.bin";

    if (!create_large_test_file(test_file, file_size_mb)) {
        std::cerr << "Failed to create test file.\n";
        return 1;
    }

    // Run demos
    demo_streaming_upload(*storage, test_file);
    demo_streaming_download(*storage);
    demo_upload_with_progress(*storage, test_file);
    demo_async_upload(*storage, test_file);

    // Cleanup
    cleanup_uploads(*storage);

    // Remove test file
    std::cout << "\nRemoving test file...\n";
    fs::remove(test_file);

    // Final statistics
    std::cout << "\n=== Final Statistics ===\n\n";
    auto stats = storage->get_statistics();
    std::cout << "  Bytes uploaded:   " << format_bytes(stats.bytes_uploaded) << "\n";
    std::cout << "  Bytes downloaded: " << format_bytes(stats.bytes_downloaded) << "\n";
    std::cout << "  Upload count:     " << stats.upload_count << "\n";
    std::cout << "  Download count:   " << stats.download_count << "\n";
    std::cout << "  Errors:           " << stats.errors << "\n";

    // Disconnect
    std::cout << "\nDisconnecting...\n";
    storage->disconnect();
    std::cout << "Done!\n";

    return 0;
}
