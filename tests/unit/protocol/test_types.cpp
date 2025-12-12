/**
 * @file test_types.cpp
 * @brief Unit tests for protocol types (types.h)
 */

#include <gtest/gtest.h>

#include <kcenon/file_transfer/core/types.h>

#include <unordered_map>
#include <unordered_set>

namespace kcenon::file_transfer::test {

// =============================================================================
// error_code Tests
// =============================================================================

class ErrorCodeTest : public ::testing::Test {};

TEST_F(ErrorCodeTest, ToString_Success) {
    EXPECT_STREQ(to_string(error_code::success), "success");
}

TEST_F(ErrorCodeTest, ToString_FileErrors) {
    EXPECT_STREQ(to_string(error_code::file_not_found), "file not found");
    EXPECT_STREQ(to_string(error_code::file_access_denied), "file access denied");
    EXPECT_STREQ(to_string(error_code::file_already_exists), "file already exists");
    EXPECT_STREQ(to_string(error_code::file_too_large), "file too large");
    EXPECT_STREQ(to_string(error_code::invalid_file_path), "invalid file path");
    EXPECT_STREQ(to_string(error_code::file_read_error), "file read error");
    EXPECT_STREQ(to_string(error_code::file_write_error), "file write error");
}

TEST_F(ErrorCodeTest, ToString_ChunkErrors) {
    EXPECT_STREQ(to_string(error_code::chunk_checksum_error), "chunk checksum error");
    EXPECT_STREQ(to_string(error_code::chunk_sequence_error), "chunk sequence error");
    EXPECT_STREQ(to_string(error_code::chunk_size_error), "chunk size error");
    EXPECT_STREQ(to_string(error_code::file_hash_mismatch), "file hash mismatch");
    EXPECT_STREQ(to_string(error_code::invalid_chunk_index), "invalid chunk index");
    EXPECT_STREQ(to_string(error_code::missing_chunks), "missing chunks");
}

TEST_F(ErrorCodeTest, ToString_ConfigErrors) {
    EXPECT_STREQ(to_string(error_code::invalid_chunk_size), "invalid chunk size");
    EXPECT_STREQ(to_string(error_code::invalid_configuration), "invalid configuration");
}

TEST_F(ErrorCodeTest, ToString_NetworkErrors) {
    EXPECT_STREQ(to_string(error_code::connection_failed), "connection failed");
    EXPECT_STREQ(to_string(error_code::connection_timeout), "connection timeout");
    EXPECT_STREQ(to_string(error_code::connection_refused), "connection refused");
    EXPECT_STREQ(to_string(error_code::connection_lost), "connection lost");
    EXPECT_STREQ(to_string(error_code::server_not_running), "server not running");
}

TEST_F(ErrorCodeTest, ToString_QuotaErrors) {
    EXPECT_STREQ(to_string(error_code::quota_exceeded), "quota exceeded");
    EXPECT_STREQ(to_string(error_code::storage_full), "storage full");
}

TEST_F(ErrorCodeTest, ToString_InternalErrors) {
    EXPECT_STREQ(to_string(error_code::internal_error), "internal error");
    EXPECT_STREQ(to_string(error_code::not_initialized), "not initialized");
    EXPECT_STREQ(to_string(error_code::already_initialized), "already initialized");
}

TEST_F(ErrorCodeTest, ToString_UnknownError) {
    auto unknown = static_cast<error_code>(-999);
    EXPECT_STREQ(to_string(unknown), "unknown error");
}

TEST_F(ErrorCodeTest, ErrorCodeRanges) {
    // File errors: -100 to -119
    EXPECT_EQ(static_cast<int>(error_code::file_not_found), -100);
    EXPECT_EQ(static_cast<int>(error_code::file_write_error), -106);

    // Chunk errors: -120 to -139
    EXPECT_EQ(static_cast<int>(error_code::chunk_checksum_error), -120);
    EXPECT_EQ(static_cast<int>(error_code::missing_chunks), -125);

    // Configuration errors: -140 to -159
    EXPECT_EQ(static_cast<int>(error_code::invalid_chunk_size), -140);

    // Network errors: -160 to -179
    EXPECT_EQ(static_cast<int>(error_code::connection_failed), -160);

    // Quota errors: -180 to -199
    EXPECT_EQ(static_cast<int>(error_code::quota_exceeded), -180);

    // Internal errors: -200 to -219
    EXPECT_EQ(static_cast<int>(error_code::internal_error), -200);
}

// =============================================================================
// error struct Tests
// =============================================================================

class ErrorStructTest : public ::testing::Test {};

TEST_F(ErrorStructTest, DefaultConstruction) {
    error err;
    EXPECT_EQ(err.code, error_code::success);
    EXPECT_FALSE(static_cast<bool>(err));
}

TEST_F(ErrorStructTest, ConstructWithCode) {
    error err(error_code::file_not_found);
    EXPECT_EQ(err.code, error_code::file_not_found);
    EXPECT_EQ(err.message, "file not found");
    EXPECT_TRUE(static_cast<bool>(err));
}

TEST_F(ErrorStructTest, ConstructWithCodeAndMessage) {
    error err(error_code::file_not_found, "custom message");
    EXPECT_EQ(err.code, error_code::file_not_found);
    EXPECT_EQ(err.message, "custom message");
    EXPECT_TRUE(static_cast<bool>(err));
}

TEST_F(ErrorStructTest, BoolConversionSuccess) {
    error err(error_code::success);
    EXPECT_FALSE(static_cast<bool>(err));
}

TEST_F(ErrorStructTest, BoolConversionError) {
    error err(error_code::internal_error);
    EXPECT_TRUE(static_cast<bool>(err));
}

// =============================================================================
// result<T> Tests
// =============================================================================

class ResultTest : public ::testing::Test {};

TEST_F(ResultTest, DefaultConstruction) {
    result<int> r;
    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(static_cast<bool>(r));
}

TEST_F(ResultTest, ConstructWithValue) {
    result<int> r(42);
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(static_cast<bool>(r));
    EXPECT_EQ(r.value(), 42);
}

TEST_F(ResultTest, ConstructWithUnexpected) {
    result<int> r{unexpected(error(error_code::file_not_found))};
    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error().code, error_code::file_not_found);
}

TEST_F(ResultTest, CopyConstruction) {
    result<int> r1(42);
    result<int> r2(r1);
    EXPECT_TRUE(r2.has_value());
    EXPECT_EQ(r2.value(), 42);
}

TEST_F(ResultTest, MoveConstruction) {
    result<std::string> r1(std::string("hello"));
    result<std::string> r2(std::move(r1));
    EXPECT_TRUE(r2.has_value());
    EXPECT_EQ(r2.value(), "hello");
}

TEST_F(ResultTest, CopyAssignment) {
    result<int> r1(42);
    result<int> r2;
    r2 = r1;
    EXPECT_TRUE(r2.has_value());
    EXPECT_EQ(r2.value(), 42);
}

TEST_F(ResultTest, MoveAssignment) {
    result<std::string> r1(std::string("hello"));
    result<std::string> r2;
    r2 = std::move(r1);
    EXPECT_TRUE(r2.has_value());
    EXPECT_EQ(r2.value(), "hello");
}

TEST_F(ResultTest, ValueAccess_Const) {
    const result<int> r(42);
    EXPECT_EQ(r.value(), 42);
}

TEST_F(ResultTest, ValueAccess_Rvalue) {
    result<std::string> r(std::string("hello"));
    std::string s = std::move(r).value();
    EXPECT_EQ(s, "hello");
}

TEST_F(ResultTest, ErrorAccess) {
    result<int> r{unexpected(error(error_code::quota_exceeded, "custom msg"))};
    EXPECT_EQ(r.error().code, error_code::quota_exceeded);
    EXPECT_EQ(r.error().message, "custom msg");
}

// =============================================================================
// result<void> Tests
// =============================================================================

class ResultVoidTest : public ::testing::Test {};

TEST_F(ResultVoidTest, DefaultConstruction) {
    result<void> r;
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST_F(ResultVoidTest, ConstructWithUnexpected) {
    result<void> r{unexpected(error(error_code::internal_error))};
    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error().code, error_code::internal_error);
}

TEST_F(ResultVoidTest, CopyConstruction) {
    result<void> r1;
    result<void> r2(r1);
    EXPECT_TRUE(r2.has_value());
}

TEST_F(ResultVoidTest, MoveConstruction) {
    result<void> r1;
    result<void> r2(std::move(r1));
    EXPECT_TRUE(r2.has_value());
}

// =============================================================================
// transfer_id Tests (UUID-based)
// =============================================================================

class TransferIdTest : public ::testing::Test {};

TEST_F(TransferIdTest, DefaultConstruction) {
    transfer_id id;
    EXPECT_TRUE(id.is_null());
}

TEST_F(TransferIdTest, Generate) {
    auto id = transfer_id::generate();
    EXPECT_FALSE(id.is_null());
}

TEST_F(TransferIdTest, EqualityOperator) {
    auto id1 = transfer_id::generate();
    auto id2 = id1;
    auto id3 = transfer_id::generate();

    EXPECT_TRUE(id1 == id2);
    EXPECT_FALSE(id1 == id3);
}

TEST_F(TransferIdTest, LessThanOperator) {
    transfer_id id1;
    transfer_id id2;
    id1.bytes[0] = 1;
    id2.bytes[0] = 2;

    EXPECT_TRUE(id1 < id2);
    EXPECT_FALSE(id2 < id1);
    EXPECT_FALSE(id1 < id1);
}

TEST_F(TransferIdTest, HashSupport) {
    auto id1 = transfer_id::generate();
    auto id2 = id1;
    auto id3 = transfer_id::generate();

    std::hash<transfer_id> hasher;
    EXPECT_EQ(hasher(id1), hasher(id2));
    EXPECT_NE(hasher(id1), hasher(id3));
}

TEST_F(TransferIdTest, UseInUnorderedSet) {
    std::unordered_set<transfer_id> ids;
    auto id1 = transfer_id::generate();
    auto id2 = transfer_id::generate();

    ids.insert(id1);
    ids.insert(id2);
    ids.insert(id1);  // Duplicate

    EXPECT_EQ(ids.size(), 2);
    EXPECT_TRUE(ids.count(id1) == 1);
    EXPECT_TRUE(ids.count(id2) == 1);
}

TEST_F(TransferIdTest, UseInUnorderedMap) {
    std::unordered_map<transfer_id, std::string> map;
    auto id1 = transfer_id::generate();
    auto id2 = transfer_id::generate();

    map[id1] = "first";
    map[id2] = "second";

    EXPECT_EQ(map[id1], "first");
    EXPECT_EQ(map[id2], "second");
}

// =============================================================================
// chunk_flags Tests
// =============================================================================

class ChunkFlagsTest : public ::testing::Test {};

TEST_F(ChunkFlagsTest, NoneFlag) {
    EXPECT_EQ(static_cast<uint8_t>(chunk_flags::none), 0);
}

TEST_F(ChunkFlagsTest, IndividualFlagValues) {
    // New flag values per protocol spec
    EXPECT_EQ(static_cast<uint8_t>(chunk_flags::first_chunk), 0x01);
    EXPECT_EQ(static_cast<uint8_t>(chunk_flags::last_chunk), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(chunk_flags::compressed), 0x04);
    EXPECT_EQ(static_cast<uint8_t>(chunk_flags::encrypted), 0x08);
}

TEST_F(ChunkFlagsTest, BitwiseOr) {
    auto combined = chunk_flags::first_chunk | chunk_flags::last_chunk;
    EXPECT_EQ(static_cast<uint8_t>(combined), 0x03);

    combined = combined | chunk_flags::compressed;
    EXPECT_EQ(static_cast<uint8_t>(combined), 0x07);
}

TEST_F(ChunkFlagsTest, BitwiseAnd) {
    auto combined = chunk_flags::compressed | chunk_flags::last_chunk | chunk_flags::encrypted;

    auto result = combined & chunk_flags::compressed;
    EXPECT_EQ(static_cast<uint8_t>(result), 0x04);

    result = combined & chunk_flags::none;
    EXPECT_EQ(static_cast<uint8_t>(result), 0);
}

TEST_F(ChunkFlagsTest, HasFlag_True) {
    auto flags = chunk_flags::compressed | chunk_flags::encrypted;

    EXPECT_TRUE(has_flag(flags, chunk_flags::compressed));
    EXPECT_TRUE(has_flag(flags, chunk_flags::encrypted));
}

TEST_F(ChunkFlagsTest, HasFlag_False) {
    auto flags = chunk_flags::compressed | chunk_flags::encrypted;

    EXPECT_FALSE(has_flag(flags, chunk_flags::last_chunk));
    EXPECT_FALSE(has_flag(flags, chunk_flags::first_chunk));
}

TEST_F(ChunkFlagsTest, HasFlag_None) {
    EXPECT_FALSE(has_flag(chunk_flags::none, chunk_flags::compressed));
    EXPECT_FALSE(has_flag(chunk_flags::none, chunk_flags::last_chunk));
    EXPECT_FALSE(has_flag(chunk_flags::none, chunk_flags::encrypted));
}

TEST_F(ChunkFlagsTest, HasFlag_AllFlags) {
    auto all = chunk_flags::first_chunk | chunk_flags::compressed |
               chunk_flags::last_chunk | chunk_flags::encrypted;

    EXPECT_TRUE(has_flag(all, chunk_flags::first_chunk));
    EXPECT_TRUE(has_flag(all, chunk_flags::compressed));
    EXPECT_TRUE(has_flag(all, chunk_flags::last_chunk));
    EXPECT_TRUE(has_flag(all, chunk_flags::encrypted));
}

// =============================================================================
// chunk struct Tests (header-based)
// =============================================================================

class ChunkStructTest : public ::testing::Test {};

TEST_F(ChunkStructTest, DefaultConstruction) {
    chunk c;
    EXPECT_TRUE(c.header.id.is_null());
    EXPECT_EQ(c.header.chunk_index, 0);
    EXPECT_EQ(c.header.chunk_offset, 0);
    EXPECT_EQ(c.header.original_size, 0);
    EXPECT_EQ(c.header.compressed_size, 0);
    EXPECT_EQ(c.header.checksum, 0);
    EXPECT_EQ(c.header.flags, chunk_flags::none);
    EXPECT_TRUE(c.data.empty());
}

TEST_F(ChunkStructTest, PopulateFields) {
    chunk c;
    c.header.id = transfer_id::generate();
    c.header.chunk_index = 5;
    c.header.chunk_offset = 1024 * 5;
    c.header.original_size = 1024;
    c.header.compressed_size = 512;
    c.header.checksum = 0xDEADBEEF;
    c.header.flags = chunk_flags::compressed | chunk_flags::last_chunk;
    c.data = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    EXPECT_FALSE(c.header.id.is_null());
    EXPECT_EQ(c.header.chunk_index, 5);
    EXPECT_EQ(c.header.chunk_offset, 1024 * 5);
    EXPECT_EQ(c.header.original_size, 1024);
    EXPECT_EQ(c.header.compressed_size, 512);
    EXPECT_EQ(c.header.checksum, 0xDEADBEEF);
    EXPECT_TRUE(has_flag(c.header.flags, chunk_flags::compressed));
    EXPECT_TRUE(has_flag(c.header.flags, chunk_flags::last_chunk));
    EXPECT_EQ(c.data.size(), 3);
}

TEST_F(ChunkStructTest, HelperMethods) {
    chunk c;
    c.header.flags = chunk_flags::first_chunk | chunk_flags::compressed;
    c.data = {std::byte{0x01}, std::byte{0x02}};

    EXPECT_TRUE(c.is_first());
    EXPECT_FALSE(c.is_last());
    EXPECT_TRUE(c.is_compressed());
    EXPECT_EQ(c.data_size(), 2);
    EXPECT_EQ(c.total_size(), chunk_header::size + 2);
}

// =============================================================================
// file_metadata Tests
// =============================================================================

class FileMetadataTest : public ::testing::Test {};

TEST_F(FileMetadataTest, DefaultConstruction) {
    file_metadata meta;
    EXPECT_TRUE(meta.filename.empty());
    EXPECT_EQ(meta.file_size, 0);
    EXPECT_EQ(meta.total_chunks, 0);
    EXPECT_EQ(meta.chunk_size, 0);
    EXPECT_TRUE(meta.sha256_hash.empty());
}

TEST_F(FileMetadataTest, PopulateFields) {
    file_metadata meta;
    meta.filename = "test.txt";
    meta.file_size = 1024 * 1024;
    meta.total_chunks = 4;
    meta.chunk_size = 256 * 1024;
    meta.sha256_hash = "abc123";

    EXPECT_EQ(meta.filename, "test.txt");
    EXPECT_EQ(meta.file_size, 1024 * 1024);
    EXPECT_EQ(meta.total_chunks, 4);
    EXPECT_EQ(meta.chunk_size, 256 * 1024);
    EXPECT_EQ(meta.sha256_hash, "abc123");
}

// =============================================================================
// assembly_progress Tests
// =============================================================================

class AssemblyProgressTest : public ::testing::Test {};

TEST_F(AssemblyProgressTest, CompletionPercentage_ZeroChunks) {
    assembly_progress prog;
    prog.total_chunks = 0;
    prog.received_chunks = 0;

    EXPECT_DOUBLE_EQ(prog.completion_percentage(), 0.0);
}

TEST_F(AssemblyProgressTest, CompletionPercentage_NoProgress) {
    assembly_progress prog;
    prog.total_chunks = 100;
    prog.received_chunks = 0;

    EXPECT_DOUBLE_EQ(prog.completion_percentage(), 0.0);
}

TEST_F(AssemblyProgressTest, CompletionPercentage_HalfComplete) {
    assembly_progress prog;
    prog.total_chunks = 100;
    prog.received_chunks = 50;

    EXPECT_DOUBLE_EQ(prog.completion_percentage(), 50.0);
}

TEST_F(AssemblyProgressTest, CompletionPercentage_Complete) {
    assembly_progress prog;
    prog.total_chunks = 100;
    prog.received_chunks = 100;

    EXPECT_DOUBLE_EQ(prog.completion_percentage(), 100.0);
}

TEST_F(AssemblyProgressTest, CompletionPercentage_PartialProgress) {
    assembly_progress prog;
    prog.total_chunks = 3;
    prog.received_chunks = 1;

    EXPECT_NEAR(prog.completion_percentage(), 33.333, 0.01);
}

}  // namespace kcenon::file_transfer::test
