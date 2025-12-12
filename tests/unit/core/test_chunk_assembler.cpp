/**
 * @file test_chunk_assembler.cpp
 * @brief Unit tests for chunk_assembler
 */

#include <gtest/gtest.h>

#include <kcenon/file_transfer/core/checksum.h>
#include <kcenon/file_transfer/core/chunk_assembler.h>
#include <kcenon/file_transfer/core/chunk_splitter.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

namespace kcenon::file_transfer::test {

class ChunkAssemblerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "file_trans_test_assembler";
        output_dir_ = test_dir_ / "output";
        std::filesystem::create_directories(test_dir_);
        std::filesystem::create_directories(output_dir_);
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

    auto create_chunk(
        const transfer_id& id,
        uint64_t index,
        uint64_t total_chunks,
        uint64_t offset,
        const std::vector<std::byte>& data,
        bool is_last = false) -> chunk {
        chunk c;
        c.id = id;
        c.index = index;
        c.total_chunks = total_chunks;
        c.offset = offset;
        c.data = data;
        c.flags = is_last ? chunk_flags::last_chunk : chunk_flags::none;
        c.checksum = checksum::crc32(data);
        return c;
    }

    auto read_file_content(const std::filesystem::path& path) -> std::vector<std::byte> {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        auto size = file.tellg();
        file.seekg(0);

        std::vector<std::byte> content(static_cast<std::size_t>(size));
        file.read(reinterpret_cast<char*>(content.data()), size);
        return content;
    }

    std::filesystem::path test_dir_;
    std::filesystem::path output_dir_;
};

// Session Management Tests

TEST_F(ChunkAssemblerTest, StartSession_Success) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    auto result = assembler.start_session(id, "test.txt", 1000, 1);

    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(assembler.has_session(id));
}

TEST_F(ChunkAssemblerTest, StartSession_DuplicateSession) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    auto result1 = assembler.start_session(id, "test1.txt", 1000, 1);
    EXPECT_TRUE(result1.has_value());

    auto result2 = assembler.start_session(id, "test2.txt", 2000, 2);
    EXPECT_FALSE(result2.has_value());
    EXPECT_EQ(result2.error().code, error_code::already_initialized);
}

TEST_F(ChunkAssemblerTest, HasSession_NotExists) {
    chunk_assembler assembler(output_dir_);

    EXPECT_FALSE(assembler.has_session(transfer_id{999}));
}

TEST_F(ChunkAssemblerTest, CancelSession_RemovesSession) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    assembler.start_session(id, "test.txt", 1000, 1);
    EXPECT_TRUE(assembler.has_session(id));

    assembler.cancel_session(id);
    EXPECT_FALSE(assembler.has_session(id));
}

TEST_F(ChunkAssemblerTest, CancelSession_NonExistent) {
    chunk_assembler assembler(output_dir_);

    // Should not throw
    EXPECT_NO_THROW(assembler.cancel_session(transfer_id{999}));
}

// Process Chunk Tests

TEST_F(ChunkAssemblerTest, ProcessChunk_SessionNotFound) {
    chunk_assembler assembler(output_dir_);

    std::vector<std::byte> data = {std::byte{0x01}, std::byte{0x02}};
    auto c = create_chunk(transfer_id{1}, 0, 1, 0, data, true);

    auto result = assembler.process_chunk(c);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(ChunkAssemblerTest, ProcessChunk_SingleChunk) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    std::vector<std::byte> data = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    auto start_result = assembler.start_session(id, "single.txt", data.size(), 1);
    ASSERT_TRUE(start_result.has_value());

    auto c = create_chunk(id, 0, 1, 0, data, true);
    auto process_result = assembler.process_chunk(c);
    EXPECT_TRUE(process_result.has_value());

    EXPECT_TRUE(assembler.is_complete(id));
}

TEST_F(ChunkAssemblerTest, ProcessChunk_InvalidIndex) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    assembler.start_session(id, "test.txt", 100, 2);

    std::vector<std::byte> data = {std::byte{0x01}};
    auto c = create_chunk(id, 5, 2, 0, data);  // Index 5 out of range

    auto result = assembler.process_chunk(c);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::invalid_chunk_index);
}

TEST_F(ChunkAssemblerTest, ProcessChunk_DuplicateChunk) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    std::vector<std::byte> data = {std::byte{0x01}};

    assembler.start_session(id, "dup.txt", data.size(), 1);

    auto c = create_chunk(id, 0, 1, 0, data, true);

    auto result1 = assembler.process_chunk(c);
    EXPECT_TRUE(result1.has_value());

    // Duplicate should be silently ignored
    auto result2 = assembler.process_chunk(c);
    EXPECT_TRUE(result2.has_value());
}

TEST_F(ChunkAssemblerTest, ProcessChunk_InvalidChecksum) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    std::vector<std::byte> data = {std::byte{0x01}, std::byte{0x02}};

    assembler.start_session(id, "bad_crc.txt", data.size(), 1);

    chunk c;
    c.id = id;
    c.index = 0;
    c.total_chunks = 1;
    c.offset = 0;
    c.data = data;
    c.flags = chunk_flags::last_chunk;
    c.checksum = 0x12345678;  // Wrong checksum

    auto result = assembler.process_chunk(c);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::chunk_checksum_error);
}

// Sequential Assembly Tests

TEST_F(ChunkAssemblerTest, ProcessChunk_SequentialAssembly) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    std::size_t chunk_size = 10;
    std::size_t total_size = 25;  // 3 chunks: 10, 10, 5

    assembler.start_session(id, "sequential.txt", total_size, 3);

    // Create and process chunks in order
    std::vector<std::byte> data1(chunk_size);
    std::vector<std::byte> data2(chunk_size);
    std::vector<std::byte> data3(5);

    for (std::size_t i = 0; i < chunk_size; ++i) {
        data1[i] = static_cast<std::byte>(i);
        data2[i] = static_cast<std::byte>(i + chunk_size);
    }
    for (std::size_t i = 0; i < 5; ++i) {
        data3[i] = static_cast<std::byte>(i + 2 * chunk_size);
    }

    EXPECT_TRUE(assembler.process_chunk(create_chunk(id, 0, 3, 0, data1)).has_value());
    EXPECT_FALSE(assembler.is_complete(id));

    EXPECT_TRUE(assembler.process_chunk(create_chunk(id, 1, 3, chunk_size, data2)).has_value());
    EXPECT_FALSE(assembler.is_complete(id));

    EXPECT_TRUE(assembler.process_chunk(create_chunk(id, 2, 3, 2 * chunk_size, data3, true)).has_value());
    EXPECT_TRUE(assembler.is_complete(id));
}

// Out-of-Order Assembly Tests

TEST_F(ChunkAssemblerTest, ProcessChunk_OutOfOrderAssembly) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    std::size_t chunk_size = 10;
    std::size_t total_size = 30;  // 3 chunks of 10 bytes each

    assembler.start_session(id, "out_of_order.txt", total_size, 3);

    std::vector<std::byte> data1(chunk_size);
    std::vector<std::byte> data2(chunk_size);
    std::vector<std::byte> data3(chunk_size);

    for (std::size_t i = 0; i < chunk_size; ++i) {
        data1[i] = static_cast<std::byte>(i);
        data2[i] = static_cast<std::byte>(i + chunk_size);
        data3[i] = static_cast<std::byte>(i + 2 * chunk_size);
    }

    // Process chunks out of order: 2, 0, 1
    EXPECT_TRUE(assembler.process_chunk(create_chunk(id, 2, 3, 2 * chunk_size, data3, true)).has_value());
    EXPECT_FALSE(assembler.is_complete(id));

    EXPECT_TRUE(assembler.process_chunk(create_chunk(id, 0, 3, 0, data1)).has_value());
    EXPECT_FALSE(assembler.is_complete(id));

    EXPECT_TRUE(assembler.process_chunk(create_chunk(id, 1, 3, chunk_size, data2)).has_value());
    EXPECT_TRUE(assembler.is_complete(id));
}

// Missing Chunks Tests

TEST_F(ChunkAssemblerTest, GetMissingChunks_AllMissing) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    assembler.start_session(id, "missing.txt", 100, 5);

    auto missing = assembler.get_missing_chunks(id);
    EXPECT_EQ(missing.size(), 5);

    for (uint64_t i = 0; i < 5; ++i) {
        EXPECT_EQ(missing[i], i);
    }
}

TEST_F(ChunkAssemblerTest, GetMissingChunks_SomeMissing) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    std::size_t chunk_size = 10;
    assembler.start_session(id, "some_missing.txt", 50, 5);

    std::vector<std::byte> data(chunk_size);

    // Add chunks 0, 2, 4 (missing 1, 3)
    assembler.process_chunk(create_chunk(id, 0, 5, 0, data));
    assembler.process_chunk(create_chunk(id, 2, 5, 2 * chunk_size, data));
    assembler.process_chunk(create_chunk(id, 4, 5, 4 * chunk_size, data, true));

    auto missing = assembler.get_missing_chunks(id);
    EXPECT_EQ(missing.size(), 2);
    EXPECT_EQ(missing[0], 1);
    EXPECT_EQ(missing[1], 3);
}

TEST_F(ChunkAssemblerTest, GetMissingChunks_NoneMissing) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    std::size_t chunk_size = 10;
    assembler.start_session(id, "none_missing.txt", 20, 2);

    std::vector<std::byte> data(chunk_size);

    assembler.process_chunk(create_chunk(id, 0, 2, 0, data));
    assembler.process_chunk(create_chunk(id, 1, 2, chunk_size, data, true));

    auto missing = assembler.get_missing_chunks(id);
    EXPECT_TRUE(missing.empty());
}

TEST_F(ChunkAssemblerTest, GetMissingChunks_SessionNotFound) {
    chunk_assembler assembler(output_dir_);

    auto missing = assembler.get_missing_chunks(transfer_id{999});
    EXPECT_TRUE(missing.empty());
}

// IsComplete Tests

TEST_F(ChunkAssemblerTest, IsComplete_NotComplete) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    assembler.start_session(id, "incomplete.txt", 100, 3);

    EXPECT_FALSE(assembler.is_complete(id));
}

TEST_F(ChunkAssemblerTest, IsComplete_SessionNotFound) {
    chunk_assembler assembler(output_dir_);

    EXPECT_FALSE(assembler.is_complete(transfer_id{999}));
}

// Progress Tests

TEST_F(ChunkAssemblerTest, GetProgress_Initial) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    assembler.start_session(id, "progress.txt", 100, 5);

    auto progress = assembler.get_progress(id);
    ASSERT_TRUE(progress.has_value());

    EXPECT_EQ(progress->total_chunks, 5);
    EXPECT_EQ(progress->received_chunks, 0);
    EXPECT_EQ(progress->bytes_written, 0);
    EXPECT_DOUBLE_EQ(progress->completion_percentage(), 0.0);
}

TEST_F(ChunkAssemblerTest, GetProgress_Partial) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    std::size_t chunk_size = 10;
    assembler.start_session(id, "partial_progress.txt", 50, 5);

    std::vector<std::byte> data(chunk_size);
    assembler.process_chunk(create_chunk(id, 0, 5, 0, data));
    assembler.process_chunk(create_chunk(id, 2, 5, 2 * chunk_size, data));

    auto progress = assembler.get_progress(id);
    ASSERT_TRUE(progress.has_value());

    EXPECT_EQ(progress->received_chunks, 2);
    EXPECT_EQ(progress->bytes_written, 2 * chunk_size);
    EXPECT_DOUBLE_EQ(progress->completion_percentage(), 40.0);
}

TEST_F(ChunkAssemblerTest, GetProgress_Complete) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    std::vector<std::byte> data = {std::byte{0x01}};
    assembler.start_session(id, "complete_progress.txt", 1, 1);

    assembler.process_chunk(create_chunk(id, 0, 1, 0, data, true));

    auto progress = assembler.get_progress(id);
    ASSERT_TRUE(progress.has_value());

    EXPECT_DOUBLE_EQ(progress->completion_percentage(), 100.0);
}

TEST_F(ChunkAssemblerTest, GetProgress_SessionNotFound) {
    chunk_assembler assembler(output_dir_);

    auto progress = assembler.get_progress(transfer_id{999});
    EXPECT_FALSE(progress.has_value());
}

// Finalize Tests

TEST_F(ChunkAssemblerTest, Finalize_Success) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    std::vector<std::byte> data = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    assembler.start_session(id, "finalize.txt", data.size(), 1);
    assembler.process_chunk(create_chunk(id, 0, 1, 0, data, true));

    auto result = assembler.finalize(id);
    ASSERT_TRUE(result.has_value());

    auto& path = result.value();
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_EQ(path.filename(), "finalize.txt");

    // Verify content
    auto content = read_file_content(path);
    EXPECT_EQ(content, data);

    // Session should be removed after finalize
    EXPECT_FALSE(assembler.has_session(id));
}

TEST_F(ChunkAssemblerTest, Finalize_WithSHA256Verification) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    std::vector<std::byte> data = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    // Calculate expected hash
    std::string expected_hash = checksum::sha256(data);

    assembler.start_session(id, "sha256_verify.txt", data.size(), 1);
    assembler.process_chunk(create_chunk(id, 0, 1, 0, data, true));

    auto result = assembler.finalize(id, expected_hash);
    EXPECT_TRUE(result.has_value());
}

TEST_F(ChunkAssemblerTest, Finalize_SHA256Mismatch) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    std::vector<std::byte> data = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    assembler.start_session(id, "sha256_mismatch.txt", data.size(), 1);
    assembler.process_chunk(create_chunk(id, 0, 1, 0, data, true));

    // Use wrong hash
    auto result = assembler.finalize(id, "0000000000000000000000000000000000000000000000000000000000000000");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::file_hash_mismatch);

    // Session should be removed on failure
    EXPECT_FALSE(assembler.has_session(id));
}

TEST_F(ChunkAssemblerTest, Finalize_SessionNotFound) {
    chunk_assembler assembler(output_dir_);

    auto result = assembler.finalize(transfer_id{999});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::not_initialized);
}

TEST_F(ChunkAssemblerTest, Finalize_MissingChunks) {
    chunk_assembler assembler(output_dir_);

    transfer_id id{1};
    assembler.start_session(id, "missing_finalize.txt", 100, 3);

    std::vector<std::byte> data(10);
    assembler.process_chunk(create_chunk(id, 0, 3, 0, data));
    // Missing chunks 1 and 2

    auto result = assembler.finalize(id);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, error_code::missing_chunks);

    // Session should still exist
    EXPECT_TRUE(assembler.has_session(id));
}

// Integration Test: Split and Reassemble

TEST_F(ChunkAssemblerTest, Integration_SplitAndReassemble) {
    // Create original file
    auto original_path = create_test_file("original.bin", 100000);  // 100KB

    // Read original content
    auto original_content = read_file_content(original_path);

    // Calculate original hash
    std::string original_hash = checksum::sha256(original_content);

    // Split the file
    chunk_config config(64 * 1024);  // 64KB chunks
    chunk_splitter splitter(config);

    auto metadata_result = splitter.calculate_metadata(original_path);
    ASSERT_TRUE(metadata_result.has_value());

    auto& metadata = metadata_result.value();

    // Create assembler
    chunk_assembler assembler(output_dir_);

    transfer_id id{42};
    auto start_result = assembler.start_session(
        id, "reassembled.bin", metadata.file_size, metadata.total_chunks);
    ASSERT_TRUE(start_result.has_value());

    // Split and reassemble
    auto split_result = splitter.split(original_path, id);
    ASSERT_TRUE(split_result.has_value());

    auto& iterator = split_result.value();
    while (iterator.has_next()) {
        auto chunk_result = iterator.next();
        ASSERT_TRUE(chunk_result.has_value());

        auto process_result = assembler.process_chunk(chunk_result.value());
        EXPECT_TRUE(process_result.has_value());
    }

    EXPECT_TRUE(assembler.is_complete(id));

    // Finalize with hash verification
    auto finalize_result = assembler.finalize(id, original_hash);
    ASSERT_TRUE(finalize_result.has_value());

    // Verify reassembled content
    auto reassembled_content = read_file_content(finalize_result.value());
    EXPECT_EQ(reassembled_content, original_content);
}

TEST_F(ChunkAssemblerTest, Integration_SplitAndReassembleOutOfOrder) {
    auto original_path = create_test_file("original_ooo.bin", 50000);
    auto original_content = read_file_content(original_path);
    std::string original_hash = checksum::sha256(original_content);

    chunk_config config(64 * 1024);
    chunk_splitter splitter(config);

    auto metadata_result = splitter.calculate_metadata(original_path);
    ASSERT_TRUE(metadata_result.has_value());
    auto& metadata = metadata_result.value();

    chunk_assembler assembler(output_dir_);
    transfer_id id{43};
    assembler.start_session(id, "reassembled_ooo.bin", metadata.file_size, metadata.total_chunks);

    // Collect all chunks first
    auto split_result = splitter.split(original_path, id);
    ASSERT_TRUE(split_result.has_value());

    std::vector<chunk> chunks;
    auto& iterator = split_result.value();
    while (iterator.has_next()) {
        auto chunk_result = iterator.next();
        ASSERT_TRUE(chunk_result.has_value());
        chunks.push_back(std::move(chunk_result.value()));
    }

    // Shuffle chunks
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(chunks.begin(), chunks.end(), g);

    // Process shuffled chunks
    for (const auto& c : chunks) {
        auto result = assembler.process_chunk(c);
        EXPECT_TRUE(result.has_value());
    }

    EXPECT_TRUE(assembler.is_complete(id));

    auto finalize_result = assembler.finalize(id, original_hash);
    ASSERT_TRUE(finalize_result.has_value());

    auto reassembled_content = read_file_content(finalize_result.value());
    EXPECT_EQ(reassembled_content, original_content);
}

// Move Semantics Tests

TEST_F(ChunkAssemblerTest, MoveConstruct) {
    chunk_assembler assembler1(output_dir_);
    transfer_id id{1};
    assembler1.start_session(id, "move.txt", 100, 1);

    chunk_assembler assembler2(std::move(assembler1));

    EXPECT_TRUE(assembler2.has_session(id));
}

TEST_F(ChunkAssemblerTest, MoveAssign) {
    chunk_assembler assembler1(output_dir_);
    transfer_id id{1};
    assembler1.start_session(id, "move_assign.txt", 100, 1);

    auto other_dir = test_dir_ / "other_output";
    std::filesystem::create_directories(other_dir);
    chunk_assembler assembler2(other_dir);

    assembler2 = std::move(assembler1);

    EXPECT_TRUE(assembler2.has_session(id));
}

}  // namespace kcenon::file_transfer::test
