/**
 * @file test_chunk_splitter.cpp
 * @brief Unit tests for chunk_splitter
 */

#include <gtest/gtest.h>

#include <kcenon/file_transfer/core/checksum.h>
#include <kcenon/file_transfer/core/chunk_splitter.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

namespace kcenon::file_transfer::test {

class ChunkSplitterTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "file_trans_test_splitter";
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    auto create_test_file(const std::string& name, std::size_t size)
        -> std::filesystem::path {
        auto path = test_dir_ / name;
        std::ofstream file(path, std::ios::binary);

        std::random_device rd;
        std::mt19937 gen(42);  // Fixed seed for reproducibility
        std::uniform_int_distribution<> dis(0, 255);

        for (std::size_t i = 0; i < size; ++i) {
            char byte = static_cast<char>(dis(gen));
            file.write(&byte, 1);
        }

        return path;
    }

    auto create_test_file_with_content(const std::string& name, const std::vector<std::byte>& content)
        -> std::filesystem::path {
        auto path = test_dir_ / name;
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(content.data()),
                   static_cast<std::streamsize>(content.size()));
        return path;
    }

    std::filesystem::path test_dir_;
};

// chunk_config Tests

TEST_F(ChunkSplitterTest, ChunkConfig_DefaultValues) {
    chunk_config config;

    EXPECT_EQ(config.chunk_size, chunk_config::default_chunk_size);
    EXPECT_EQ(config.chunk_size, 256 * 1024);  // 256KB

    auto result = config.validate();
    EXPECT_TRUE(result.has_value());
}

TEST_F(ChunkSplitterTest, ChunkConfig_CustomSize) {
    chunk_config config(128 * 1024);  // 128KB

    EXPECT_EQ(config.chunk_size, 128 * 1024);

    auto result = config.validate();
    EXPECT_TRUE(result.has_value());
}

TEST_F(ChunkSplitterTest, ChunkConfig_TooSmall) {
    chunk_config config(32 * 1024);  // 32KB - below minimum

    auto result = config.validate();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::invalid_chunk_size);
}

TEST_F(ChunkSplitterTest, ChunkConfig_TooLarge) {
    chunk_config config(2 * 1024 * 1024);  // 2MB - above maximum

    auto result = config.validate();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::invalid_chunk_size);
}

TEST_F(ChunkSplitterTest, ChunkConfig_BoundaryMinimum) {
    chunk_config config(chunk_config::min_chunk_size);

    auto result = config.validate();
    EXPECT_TRUE(result.has_value());
}

TEST_F(ChunkSplitterTest, ChunkConfig_BoundaryMaximum) {
    chunk_config config(chunk_config::max_chunk_size);

    auto result = config.validate();
    EXPECT_TRUE(result.has_value());
}

TEST_F(ChunkSplitterTest, ChunkConfig_CalculateChunkCount) {
    chunk_config config(256 * 1024);  // 256KB chunks

    EXPECT_EQ(config.calculate_chunk_count(0), 0);
    EXPECT_EQ(config.calculate_chunk_count(1), 1);
    EXPECT_EQ(config.calculate_chunk_count(256 * 1024), 1);
    EXPECT_EQ(config.calculate_chunk_count(256 * 1024 + 1), 2);
    EXPECT_EQ(config.calculate_chunk_count(512 * 1024), 2);
    EXPECT_EQ(config.calculate_chunk_count(1024 * 1024), 4);  // 1MB
}

// chunk_splitter Construction Tests

TEST_F(ChunkSplitterTest, Constructor_Default) {
    chunk_splitter splitter;

    EXPECT_EQ(splitter.config().chunk_size, chunk_config::default_chunk_size);
}

TEST_F(ChunkSplitterTest, Constructor_CustomConfig) {
    chunk_config config(128 * 1024);
    chunk_splitter splitter(config);

    EXPECT_EQ(splitter.config().chunk_size, 128 * 1024);
}

// Split Tests

TEST_F(ChunkSplitterTest, Split_FileNotFound) {
    chunk_splitter splitter;
    auto path = test_dir_ / "nonexistent.txt";

    auto result = splitter.split(path, transfer_id::generate());
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::file_not_found);
}

TEST_F(ChunkSplitterTest, Split_EmptyFile) {
    auto path = create_test_file("empty.txt", 0);
    chunk_splitter splitter;

    auto result = splitter.split(path, transfer_id::generate());
    ASSERT_TRUE(result.has_value());

    auto& iterator = result.value();
    EXPECT_EQ(iterator.total_chunks(), 1);  // Empty file creates 1 empty chunk
    EXPECT_EQ(iterator.file_size(), 0);
    EXPECT_TRUE(iterator.has_next());

    auto chunk_result = iterator.next();
    ASSERT_TRUE(chunk_result.has_value());
    EXPECT_EQ(chunk_result.value().data.size(), 0);
    EXPECT_TRUE(has_flag(chunk_result.value().header.flags, chunk_flags::last_chunk));

    EXPECT_FALSE(iterator.has_next());
}

TEST_F(ChunkSplitterTest, Split_SingleChunk) {
    // File smaller than chunk size
    std::size_t file_size = 100;  // 100 bytes
    auto path = create_test_file("small.txt", file_size);

    chunk_splitter splitter;
    auto result = splitter.split(path, transfer_id::generate());
    ASSERT_TRUE(result.has_value());

    auto& iterator = result.value();
    EXPECT_EQ(iterator.total_chunks(), 1);
    EXPECT_EQ(iterator.file_size(), file_size);
}

TEST_F(ChunkSplitterTest, Split_ExactlyOneChunk) {
    // File exactly chunk size
    std::size_t chunk_size = 64 * 1024;
    chunk_config config(chunk_size);
    chunk_splitter splitter(config);

    auto path = create_test_file("exact.txt", chunk_size);
    auto result = splitter.split(path, transfer_id::generate());
    ASSERT_TRUE(result.has_value());

    auto& iterator = result.value();
    EXPECT_EQ(iterator.total_chunks(), 1);
}

TEST_F(ChunkSplitterTest, Split_MultipleChunks) {
    std::size_t chunk_size = 64 * 1024;  // 64KB
    std::size_t file_size = chunk_size * 4;  // 256KB = 4 chunks

    chunk_config config(chunk_size);
    chunk_splitter splitter(config);

    auto path = create_test_file("multi.txt", file_size);
    auto result = splitter.split(path, transfer_id::generate());
    ASSERT_TRUE(result.has_value());

    auto& iterator = result.value();
    EXPECT_EQ(iterator.total_chunks(), 4);
}

TEST_F(ChunkSplitterTest, Split_LastChunkSmaller) {
    std::size_t chunk_size = 64 * 1024;  // 64KB
    std::size_t file_size = chunk_size * 2 + 1000;  // 2 full chunks + 1000 bytes

    chunk_config config(chunk_size);
    chunk_splitter splitter(config);

    auto path = create_test_file("partial.txt", file_size);
    auto result = splitter.split(path, transfer_id::generate());
    ASSERT_TRUE(result.has_value());

    auto& iterator = result.value();
    EXPECT_EQ(iterator.total_chunks(), 3);
}

// Iterator Tests

TEST_F(ChunkSplitterTest, Iterator_SequentialRead) {
    std::size_t chunk_size = 64 * 1024;
    std::size_t file_size = chunk_size * 3;

    chunk_config config(chunk_size);
    chunk_splitter splitter(config);

    auto path = create_test_file("sequential.txt", file_size);
    auto result = splitter.split(path, transfer_id::generate());
    ASSERT_TRUE(result.has_value());

    auto& iterator = result.value();

    uint64_t expected_index = 0;
    while (iterator.has_next()) {
        auto chunk_result = iterator.next();
        ASSERT_TRUE(chunk_result.has_value());

        auto& chunk = chunk_result.value();
        EXPECT_EQ(chunk.header.chunk_index, expected_index);
        EXPECT_EQ(chunk.header.chunk_offset, expected_index * chunk_size);

        if (expected_index == 0) {
            EXPECT_TRUE(has_flag(chunk.header.flags, chunk_flags::first_chunk));
        }
        if (expected_index == 2) {
            EXPECT_TRUE(has_flag(chunk.header.flags, chunk_flags::last_chunk));
        } else {
            EXPECT_FALSE(has_flag(chunk.header.flags, chunk_flags::last_chunk));
        }

        ++expected_index;
    }

    EXPECT_EQ(expected_index, 3);
}

TEST_F(ChunkSplitterTest, Iterator_ChunkDataIntegrity) {
    // Create file with known content
    std::vector<std::byte> content(256);
    for (std::size_t i = 0; i < content.size(); ++i) {
        content[i] = static_cast<std::byte>(i % 256);
    }

    auto path = create_test_file_with_content("integrity.txt", content);

    chunk_config config(64 * 1024);  // Chunk size larger than file
    chunk_splitter splitter(config);

    auto result = splitter.split(path, transfer_id::generate());
    ASSERT_TRUE(result.has_value());

    auto& iterator = result.value();
    auto chunk_result = iterator.next();
    ASSERT_TRUE(chunk_result.has_value());

    auto& chunk = chunk_result.value();
    EXPECT_EQ(chunk.data.size(), content.size());

    // Verify content matches
    EXPECT_TRUE(std::equal(content.begin(), content.end(), chunk.data.begin()));
}

TEST_F(ChunkSplitterTest, Iterator_ChunkCRC32) {
    auto path = create_test_file("crc_test.txt", 1000);

    chunk_splitter splitter;
    auto result = splitter.split(path, transfer_id::generate());
    ASSERT_TRUE(result.has_value());

    auto& iterator = result.value();
    auto chunk_result = iterator.next();
    ASSERT_TRUE(chunk_result.has_value());

    auto& chunk = chunk_result.value();

    // Verify CRC32 is correct
    auto calculated_crc = checksum::crc32(chunk.data);
    EXPECT_EQ(chunk.header.checksum, calculated_crc);
    EXPECT_TRUE(checksum::verify_crc32(chunk.data, chunk.header.checksum));
}

TEST_F(ChunkSplitterTest, Iterator_TransferIdPropagation) {
    auto path = create_test_file("transfer_id.txt", 1000);

    auto test_id = transfer_id::generate();
    chunk_splitter splitter;
    auto result = splitter.split(path, test_id);
    ASSERT_TRUE(result.has_value());

    auto& iterator = result.value();
    auto chunk_result = iterator.next();
    ASSERT_TRUE(chunk_result.has_value());

    EXPECT_EQ(chunk_result.value().header.id, test_id);
}

TEST_F(ChunkSplitterTest, Iterator_CurrentIndex) {
    std::size_t chunk_size = 64 * 1024;
    std::size_t file_size = chunk_size * 3;

    chunk_config config(chunk_size);
    chunk_splitter splitter(config);

    auto path = create_test_file("index_test.txt", file_size);
    auto result = splitter.split(path, transfer_id::generate());
    ASSERT_TRUE(result.has_value());

    auto& iterator = result.value();

    EXPECT_EQ(iterator.current_index(), 0);

    iterator.next();
    EXPECT_EQ(iterator.current_index(), 1);

    iterator.next();
    EXPECT_EQ(iterator.current_index(), 2);

    iterator.next();
    EXPECT_EQ(iterator.current_index(), 3);
}

TEST_F(ChunkSplitterTest, Iterator_NoMoreChunksError) {
    auto path = create_test_file("no_more.txt", 100);

    chunk_splitter splitter;
    auto result = splitter.split(path, transfer_id::generate());
    ASSERT_TRUE(result.has_value());

    auto& iterator = result.value();

    // Read the only chunk
    auto chunk_result = iterator.next();
    ASSERT_TRUE(chunk_result.has_value());

    // Try to read again
    auto error_result = iterator.next();
    EXPECT_FALSE(error_result.has_value());
    EXPECT_EQ(error_result.error().code, error_code::invalid_chunk_index);
}

// Calculate Metadata Tests

TEST_F(ChunkSplitterTest, CalculateMetadata_BasicFile) {
    std::size_t file_size = 1000;
    auto path = create_test_file("metadata_test.txt", file_size);

    chunk_splitter splitter;
    auto result = splitter.calculate_metadata(path);
    ASSERT_TRUE(result.has_value());

    auto& metadata = result.value();
    EXPECT_EQ(metadata.filename, "metadata_test.txt");
    EXPECT_EQ(metadata.file_size, file_size);
    EXPECT_EQ(metadata.chunk_size, chunk_config::default_chunk_size);
    EXPECT_EQ(metadata.total_chunks, 1);
    EXPECT_EQ(metadata.sha256_hash.length(), 64);  // SHA-256 hex string length
}

TEST_F(ChunkSplitterTest, CalculateMetadata_MultipleChunks) {
    std::size_t chunk_size = 64 * 1024;
    std::size_t file_size = chunk_size * 5 + 1000;  // 5 full chunks + 1000 bytes

    chunk_config config(chunk_size);
    chunk_splitter splitter(config);

    auto path = create_test_file("multi_metadata.txt", file_size);
    auto result = splitter.calculate_metadata(path);
    ASSERT_TRUE(result.has_value());

    auto& metadata = result.value();
    EXPECT_EQ(metadata.total_chunks, 6);
}

TEST_F(ChunkSplitterTest, CalculateMetadata_FileNotFound) {
    chunk_splitter splitter;
    auto result = splitter.calculate_metadata(test_dir_ / "nonexistent.txt");

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::file_not_found);
}

TEST_F(ChunkSplitterTest, CalculateMetadata_EmptyFile) {
    auto path = create_test_file("empty_metadata.txt", 0);

    chunk_splitter splitter;
    auto result = splitter.calculate_metadata(path);
    ASSERT_TRUE(result.has_value());

    auto& metadata = result.value();
    EXPECT_EQ(metadata.file_size, 0);
    EXPECT_EQ(metadata.total_chunks, 1);  // At least 1 chunk for empty file
}

// Various File Size Tests

TEST_F(ChunkSplitterTest, Split_OneByteLessThanChunk) {
    std::size_t chunk_size = 64 * 1024;
    std::size_t file_size = chunk_size - 1;

    chunk_config config(chunk_size);
    chunk_splitter splitter(config);

    auto path = create_test_file("less_one.txt", file_size);
    auto result = splitter.split(path, transfer_id::generate());
    ASSERT_TRUE(result.has_value());

    auto& iterator = result.value();
    EXPECT_EQ(iterator.total_chunks(), 1);

    auto chunk_result = iterator.next();
    ASSERT_TRUE(chunk_result.has_value());
    EXPECT_EQ(chunk_result.value().data.size(), file_size);
}

TEST_F(ChunkSplitterTest, Split_OneByteMoreThanChunk) {
    std::size_t chunk_size = 64 * 1024;
    std::size_t file_size = chunk_size + 1;

    chunk_config config(chunk_size);
    chunk_splitter splitter(config);

    auto path = create_test_file("more_one.txt", file_size);
    auto result = splitter.split(path, transfer_id::generate());
    ASSERT_TRUE(result.has_value());

    auto& iterator = result.value();
    EXPECT_EQ(iterator.total_chunks(), 2);

    // First chunk should be full size
    auto chunk1 = iterator.next();
    ASSERT_TRUE(chunk1.has_value());
    EXPECT_EQ(chunk1.value().data.size(), chunk_size);

    // Second chunk should be 1 byte
    auto chunk2 = iterator.next();
    ASSERT_TRUE(chunk2.has_value());
    EXPECT_EQ(chunk2.value().data.size(), 1);
}

// Move Semantics Tests

TEST_F(ChunkSplitterTest, Iterator_MoveConstruct) {
    auto path = create_test_file("move_test.txt", 1000);

    chunk_splitter splitter;
    auto result = splitter.split(path, transfer_id::generate());
    ASSERT_TRUE(result.has_value());

    auto iterator1 = std::move(result.value());
    auto iterator2 = std::move(iterator1);

    EXPECT_TRUE(iterator2.has_next());
    auto chunk_result = iterator2.next();
    EXPECT_TRUE(chunk_result.has_value());
}

}  // namespace kcenon::file_transfer::test
