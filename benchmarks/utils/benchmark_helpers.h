/**
 * @file benchmark_helpers.h
 * @brief Helper utilities for benchmarks
 */

#ifndef KCENON_FILE_TRANSFER_BENCHMARKS_BENCHMARK_HELPERS_H
#define KCENON_FILE_TRANSFER_BENCHMARKS_BENCHMARK_HELPERS_H

#include <cstddef>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace kcenon::file_transfer::benchmark {

/**
 * @brief Helper class for generating test data for benchmarks
 */
class test_data_generator {
public:
    /**
     * @brief Generate random binary data
     * @param size Size in bytes
     * @param seed Random seed (0 for random)
     * @return Vector of random bytes
     */
    static auto generate_random_data(std::size_t size, uint32_t seed = 0)
        -> std::vector<std::byte>;

    /**
     * @brief Generate compressible text data
     * @param size Approximate size in bytes
     * @param seed Random seed (0 for random)
     * @return Vector of text-like bytes
     */
    static auto generate_text_data(std::size_t size, uint32_t seed = 0)
        -> std::vector<std::byte>;

    /**
     * @brief Generate data with specified compressibility ratio
     * @param size Size in bytes
     * @param compressibility_ratio 0.0 = random, 1.0 = highly compressible
     * @param seed Random seed (0 for random)
     * @return Vector of bytes
     */
    static auto generate_data_with_compressibility(
        std::size_t size,
        double compressibility_ratio,
        uint32_t seed = 0) -> std::vector<std::byte>;
};

/**
 * @brief Helper class for managing temporary benchmark files
 */
class temp_file_manager {
public:
    /**
     * @brief Constructor
     * @param base_dir Base directory for temporary files
     */
    explicit temp_file_manager(const std::filesystem::path& base_dir = {});

    /**
     * @brief Destructor - cleans up temporary files
     */
    ~temp_file_manager();

    // Non-copyable
    temp_file_manager(const temp_file_manager&) = delete;
    auto operator=(const temp_file_manager&) -> temp_file_manager& = delete;

    // Movable
    temp_file_manager(temp_file_manager&&) noexcept;
    auto operator=(temp_file_manager&&) noexcept -> temp_file_manager&;

    /**
     * @brief Create a temporary file with the given content
     * @param name File name
     * @param data File content
     * @return Path to created file
     */
    auto create_file(const std::string& name, const std::vector<std::byte>& data)
        -> std::filesystem::path;

    /**
     * @brief Create a temporary file with random data
     * @param name File name
     * @param size File size
     * @param seed Random seed
     * @return Path to created file
     */
    auto create_random_file(const std::string& name, std::size_t size, uint32_t seed = 0)
        -> std::filesystem::path;

    /**
     * @brief Get the base directory
     * @return Base directory path
     */
    [[nodiscard]] auto base_dir() const -> const std::filesystem::path&;

    /**
     * @brief Clean up all temporary files
     */
    void cleanup();

private:
    std::filesystem::path base_dir_;
    std::vector<std::filesystem::path> created_files_;
    bool owns_dir_ = false;
};

/**
 * @brief Format bytes as human-readable string
 * @param bytes Number of bytes
 * @return Formatted string (e.g., "1.5 GB")
 */
auto format_bytes(uint64_t bytes) -> std::string;

/**
 * @brief Format throughput as human-readable string
 * @param bytes_per_second Throughput in bytes per second
 * @return Formatted string (e.g., "500 MB/s")
 */
auto format_throughput(double bytes_per_second) -> std::string;

/**
 * @brief Size constants for benchmarks
 */
namespace sizes {
constexpr std::size_t KB = 1024;
constexpr std::size_t MB = 1024 * KB;
constexpr std::size_t GB = 1024 * MB;

constexpr std::size_t small_file = 100 * KB;    // 100 KB
constexpr std::size_t medium_file = 10 * MB;    // 10 MB
constexpr std::size_t large_file = 100 * MB;    // 100 MB
constexpr std::size_t xlarge_file = 1 * GB;     // 1 GB

// Chunk sizes for testing
constexpr std::size_t min_chunk = 64 * KB;      // 64 KB
constexpr std::size_t default_chunk = 256 * KB; // 256 KB
constexpr std::size_t max_chunk = 1 * MB;       // 1 MB
}  // namespace sizes

/**
 * @brief Performance targets from SRS
 */
namespace targets {
// Throughput targets
constexpr double lan_throughput_mbps = 500.0;      // >= 500 MB/s LAN
constexpr double wan_throughput_mbps = 100.0;      // >= 100 MB/s WAN

// Compression targets
constexpr double lz4_compress_mbps = 400.0;        // >= 400 MB/s
constexpr double lz4_decompress_mbps = 1500.0;     // >= 1.5 GB/s

// Memory targets (bytes)
constexpr std::size_t server_memory = 100 * sizes::MB;  // < 100 MB
constexpr std::size_t client_memory = 50 * sizes::MB;   // < 50 MB
constexpr std::size_t per_connection = 1 * sizes::MB;   // < 1 MB

// Latency targets
constexpr double file_list_response_ms = 100.0;    // < 100ms for 10K files
}  // namespace targets

}  // namespace kcenon::file_transfer::benchmark

#endif  // KCENON_FILE_TRANSFER_BENCHMARKS_BENCHMARK_HELPERS_H
