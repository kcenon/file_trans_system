/**
 * @file test_checksum.cpp
 * @brief Unit tests for checksum utilities
 */

#include <gtest/gtest.h>

#include <kcenon/file_transfer/core/checksum.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace kcenon::file_transfer::test {

class ChecksumTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "file_trans_test_checksum";
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    auto create_test_file(const std::string& name, const std::vector<std::byte>& content)
        -> std::filesystem::path {
        auto path = test_dir_ / name;
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(content.data()),
                   static_cast<std::streamsize>(content.size()));
        return path;
    }

    auto create_test_file(const std::string& name, const std::string& content)
        -> std::filesystem::path {
        std::vector<std::byte> bytes(content.size());
        if (!content.empty()) {
            std::memcpy(bytes.data(), content.data(), content.size());
        }
        return create_test_file(name, bytes);
    }

    std::filesystem::path test_dir_;
};

// CRC32 Tests

TEST_F(ChecksumTest, CRC32_EmptyData) {
    std::vector<std::byte> empty;
    auto crc = checksum::crc32(empty);
    EXPECT_EQ(crc, 0x00000000);
}

TEST_F(ChecksumTest, CRC32_KnownValues) {
    // Test with known CRC32 values
    // "123456789" -> 0xCBF43926
    std::string test_data = "123456789";
    std::vector<std::byte> data(test_data.size());
    std::memcpy(data.data(), test_data.data(), test_data.size());

    auto crc = checksum::crc32(data);
    EXPECT_EQ(crc, 0xCBF43926);
}

TEST_F(ChecksumTest, CRC32_SingleByte) {
    std::vector<std::byte> data = {std::byte{0x00}};
    auto crc1 = checksum::crc32(data);

    data[0] = std::byte{0xFF};
    auto crc2 = checksum::crc32(data);

    EXPECT_NE(crc1, crc2);
}

TEST_F(ChecksumTest, CRC32_Consistency) {
    std::string test_data = "The quick brown fox jumps over the lazy dog";
    std::vector<std::byte> data(test_data.size());
    std::memcpy(data.data(), test_data.data(), test_data.size());

    auto crc1 = checksum::crc32(data);
    auto crc2 = checksum::crc32(data);

    EXPECT_EQ(crc1, crc2);
}

TEST_F(ChecksumTest, CRC32_DifferentDataDifferentChecksum) {
    std::string data1_str = "Hello";
    std::string data2_str = "World";

    std::vector<std::byte> data1(data1_str.size());
    std::vector<std::byte> data2(data2_str.size());
    std::memcpy(data1.data(), data1_str.data(), data1_str.size());
    std::memcpy(data2.data(), data2_str.data(), data2_str.size());

    auto crc1 = checksum::crc32(data1);
    auto crc2 = checksum::crc32(data2);

    EXPECT_NE(crc1, crc2);
}

TEST_F(ChecksumTest, CRC32_LargeData) {
    // Test with 1MB of random data
    std::vector<std::byte> data(1024 * 1024);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    for (auto& byte : data) {
        byte = static_cast<std::byte>(dis(gen));
    }

    auto crc = checksum::crc32(data);
    EXPECT_NE(crc, 0);

    // Verify consistency
    auto crc2 = checksum::crc32(data);
    EXPECT_EQ(crc, crc2);
}

TEST_F(ChecksumTest, VerifyCRC32_Valid) {
    std::string test_data = "123456789";
    std::vector<std::byte> data(test_data.size());
    std::memcpy(data.data(), test_data.data(), test_data.size());

    EXPECT_TRUE(checksum::verify_crc32(data, 0xCBF43926));
}

TEST_F(ChecksumTest, VerifyCRC32_Invalid) {
    std::string test_data = "123456789";
    std::vector<std::byte> data(test_data.size());
    std::memcpy(data.data(), test_data.data(), test_data.size());

    EXPECT_FALSE(checksum::verify_crc32(data, 0x12345678));
}

TEST_F(ChecksumTest, CRC32_CorruptedDataDetection) {
    std::string original = "Important data that must not be corrupted";
    std::vector<std::byte> data(original.size());
    std::memcpy(data.data(), original.data(), original.size());

    auto original_crc = checksum::crc32(data);

    // Corrupt one byte
    data[10] = std::byte{static_cast<uint8_t>(~static_cast<uint8_t>(data[10]))};

    auto corrupted_crc = checksum::crc32(data);

    EXPECT_NE(original_crc, corrupted_crc);
    EXPECT_FALSE(checksum::verify_crc32(data, original_crc));
}

// SHA-256 Tests

TEST_F(ChecksumTest, SHA256_EmptyData) {
    std::vector<std::byte> empty;
    auto hash = checksum::sha256(empty);

    // SHA-256 of empty string
    EXPECT_EQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_F(ChecksumTest, SHA256_KnownValue) {
    // SHA-256("hello") = 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824
    std::string test_data = "hello";
    std::vector<std::byte> data(test_data.size());
    std::memcpy(data.data(), test_data.data(), test_data.size());

    auto hash = checksum::sha256(data);
    EXPECT_EQ(hash, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

TEST_F(ChecksumTest, SHA256_Consistency) {
    std::string test_data = "The quick brown fox jumps over the lazy dog";
    std::vector<std::byte> data(test_data.size());
    std::memcpy(data.data(), test_data.data(), test_data.size());

    auto hash1 = checksum::sha256(data);
    auto hash2 = checksum::sha256(data);

    EXPECT_EQ(hash1, hash2);
}

TEST_F(ChecksumTest, SHA256_DifferentDataDifferentHash) {
    std::string data1_str = "Hello";
    std::string data2_str = "World";

    std::vector<std::byte> data1(data1_str.size());
    std::vector<std::byte> data2(data2_str.size());
    std::memcpy(data1.data(), data1_str.data(), data1_str.size());
    std::memcpy(data2.data(), data2_str.data(), data2_str.size());

    auto hash1 = checksum::sha256(data1);
    auto hash2 = checksum::sha256(data2);

    EXPECT_NE(hash1, hash2);
}

TEST_F(ChecksumTest, SHA256_HashLength) {
    std::string test_data = "test";
    std::vector<std::byte> data(test_data.size());
    std::memcpy(data.data(), test_data.data(), test_data.size());

    auto hash = checksum::sha256(data);

    // SHA-256 produces 64 character hex string
    EXPECT_EQ(hash.length(), 64);
}

// SHA-256 File Tests

TEST_F(ChecksumTest, SHA256File_EmptyFile) {
    auto path = create_test_file("empty.txt", "");

    auto result = checksum::sha256_file(path);
    ASSERT_TRUE(result.has_value());

    // SHA-256 of empty file
    EXPECT_EQ(result.value(),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_F(ChecksumTest, SHA256File_KnownContent) {
    auto path = create_test_file("hello.txt", "hello");

    auto result = checksum::sha256_file(path);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result.value(),
              "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

TEST_F(ChecksumTest, SHA256File_NonExistent) {
    auto path = test_dir_ / "nonexistent.txt";

    auto result = checksum::sha256_file(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::file_not_found);
}

TEST_F(ChecksumTest, SHA256File_LargeFile) {
    // Create 1MB file
    std::vector<std::byte> data(1024 * 1024);
    std::random_device rd;
    std::mt19937 gen(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<> dis(0, 255);

    for (auto& byte : data) {
        byte = static_cast<std::byte>(dis(gen));
    }

    auto path = create_test_file("large.bin", data);

    auto result = checksum::sha256_file(path);
    ASSERT_TRUE(result.has_value());

    // Verify consistency
    auto result2 = checksum::sha256_file(path);
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result.value(), result2.value());
}

TEST_F(ChecksumTest, VerifySHA256_Valid) {
    auto path = create_test_file("test.txt", "hello");

    EXPECT_TRUE(checksum::verify_sha256(
        path, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"));
}

TEST_F(ChecksumTest, VerifySHA256_Invalid) {
    auto path = create_test_file("test.txt", "hello");

    EXPECT_FALSE(checksum::verify_sha256(
        path, "0000000000000000000000000000000000000000000000000000000000000000"));
}

TEST_F(ChecksumTest, VerifySHA256_NonExistentFile) {
    auto path = test_dir_ / "nonexistent.txt";

    EXPECT_FALSE(checksum::verify_sha256(
        path, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"));
}

TEST_F(ChecksumTest, SHA256_MatchesFileHash) {
    std::string content = "Test content for file hash verification";
    auto path = create_test_file("match_test.txt", content);

    // Calculate hash from memory
    std::vector<std::byte> data(content.size());
    std::memcpy(data.data(), content.data(), content.size());
    auto memory_hash = checksum::sha256(data);

    // Calculate hash from file
    auto file_hash_result = checksum::sha256_file(path);
    ASSERT_TRUE(file_hash_result.has_value());

    EXPECT_EQ(memory_hash, file_hash_result.value());
}

}  // namespace kcenon::file_transfer::test
