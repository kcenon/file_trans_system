/**
 * @file benchmark_helpers.cpp
 * @brief Implementation of benchmark helper utilities
 */

#include "utils/benchmark_helpers.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace kcenon::file_transfer::benchmark {

// test_data_generator implementation

auto test_data_generator::generate_random_data(std::size_t size, uint32_t seed)
    -> std::vector<std::byte> {
    std::vector<std::byte> data(size);

    std::mt19937 gen(seed == 0 ? std::random_device{}() : seed);
    std::uniform_int_distribution<uint16_t> dis(0, 255);

    for (auto& byte : data) {
        byte = static_cast<std::byte>(dis(gen));
    }

    return data;
}

auto test_data_generator::generate_text_data(std::size_t size, uint32_t seed)
    -> std::vector<std::byte> {
    std::vector<std::byte> data;
    data.reserve(size);

    std::mt19937 gen(seed == 0 ? std::random_device{}() : seed);

    // Common English words for generating compressible text
    static const std::vector<std::string> words = {
        "the", "be", "to", "of", "and", "a", "in", "that", "have", "I",
        "it", "for", "not", "on", "with", "he", "as", "you", "do", "at",
        "this", "but", "his", "by", "from", "they", "we", "say", "her", "she",
        "or", "an", "will", "my", "one", "all", "would", "there", "their", "what",
        "so", "up", "out", "if", "about", "who", "get", "which", "go", "me",
        "file", "transfer", "data", "system", "server", "client", "chunk", "byte",
        "network", "protocol", "connection", "upload", "download", "compress"
    };

    std::uniform_int_distribution<std::size_t> word_dis(0, words.size() - 1);
    std::uniform_int_distribution<int> space_dis(0, 10);

    while (data.size() < size) {
        const auto& word = words[word_dis(gen)];
        for (char c : word) {
            if (data.size() >= size) break;
            data.push_back(static_cast<std::byte>(c));
        }

        if (data.size() < size) {
            // Add space or newline
            if (space_dis(gen) == 0) {
                data.push_back(static_cast<std::byte>('\n'));
            } else {
                data.push_back(static_cast<std::byte>(' '));
            }
        }
    }

    data.resize(size);
    return data;
}

auto test_data_generator::generate_data_with_compressibility(
    std::size_t size,
    double compressibility_ratio,
    uint32_t seed) -> std::vector<std::byte> {
    std::vector<std::byte> data(size);

    std::mt19937 gen(seed == 0 ? std::random_device{}() : seed);

    // Calculate how many unique values to use
    // Higher compressibility = fewer unique values = more repetition
    auto unique_values = static_cast<int>(256 * (1.0 - compressibility_ratio));
    if (unique_values < 1) unique_values = 1;
    if (unique_values > 256) unique_values = 256;

    std::uniform_int_distribution<int> dis(0, unique_values - 1);

    for (auto& byte : data) {
        byte = static_cast<std::byte>(dis(gen));
    }

    return data;
}

// temp_file_manager implementation

temp_file_manager::temp_file_manager(const std::filesystem::path& base_dir) {
    if (base_dir.empty()) {
        base_dir_ = std::filesystem::temp_directory_path() / "file_trans_benchmarks";
        owns_dir_ = true;
    } else {
        base_dir_ = base_dir;
        owns_dir_ = false;
    }

    std::error_code ec;
    std::filesystem::create_directories(base_dir_, ec);
}

temp_file_manager::~temp_file_manager() {
    cleanup();
}

temp_file_manager::temp_file_manager(temp_file_manager&& other) noexcept
    : base_dir_(std::move(other.base_dir_)),
      created_files_(std::move(other.created_files_)),
      owns_dir_(other.owns_dir_) {
    other.owns_dir_ = false;
}

auto temp_file_manager::operator=(temp_file_manager&& other) noexcept -> temp_file_manager& {
    if (this != &other) {
        cleanup();
        base_dir_ = std::move(other.base_dir_);
        created_files_ = std::move(other.created_files_);
        owns_dir_ = other.owns_dir_;
        other.owns_dir_ = false;
    }
    return *this;
}

auto temp_file_manager::create_file(
    const std::string& name,
    const std::vector<std::byte>& data) -> std::filesystem::path {
    auto path = base_dir_ / name;
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    created_files_.push_back(path);
    return path;
}

auto temp_file_manager::create_random_file(
    const std::string& name,
    std::size_t size,
    uint32_t seed) -> std::filesystem::path {
    auto data = test_data_generator::generate_random_data(size, seed);
    return create_file(name, data);
}

auto temp_file_manager::base_dir() const -> const std::filesystem::path& {
    return base_dir_;
}

void temp_file_manager::cleanup() {
    std::error_code ec;

    for (const auto& path : created_files_) {
        std::filesystem::remove(path, ec);
    }
    created_files_.clear();

    if (owns_dir_) {
        std::filesystem::remove_all(base_dir_, ec);
    }
}

// Utility functions

auto format_bytes(uint64_t bytes) -> std::string {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    if (bytes >= sizes::GB) {
        oss << static_cast<double>(bytes) / sizes::GB << " GB";
    } else if (bytes >= sizes::MB) {
        oss << static_cast<double>(bytes) / sizes::MB << " MB";
    } else if (bytes >= sizes::KB) {
        oss << static_cast<double>(bytes) / sizes::KB << " KB";
    } else {
        oss << bytes << " B";
    }

    return oss.str();
}

auto format_throughput(double bytes_per_second) -> std::string {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    if (bytes_per_second >= sizes::GB) {
        oss << bytes_per_second / sizes::GB << " GB/s";
    } else if (bytes_per_second >= sizes::MB) {
        oss << bytes_per_second / sizes::MB << " MB/s";
    } else if (bytes_per_second >= sizes::KB) {
        oss << bytes_per_second / sizes::KB << " KB/s";
    } else {
        oss << bytes_per_second << " B/s";
    }

    return oss.str();
}

}  // namespace kcenon::file_transfer::benchmark
