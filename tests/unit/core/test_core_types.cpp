/**
 * @file test_core_types.cpp
 * @brief Unit tests for core types (error_codes, chunk_types, protocol_types, transfer_types)
 */

#include <gtest/gtest.h>

#include <kcenon/file_transfer/core/chunk_types.h>
#include <kcenon/file_transfer/core/error_codes.h>
#include <kcenon/file_transfer/core/protocol_types.h>
#include <kcenon/file_transfer/core/transfer_types.h>

#include <unordered_map>
#include <unordered_set>

namespace kcenon::file_transfer::test {

// =============================================================================
// transfer_error_code Tests
// =============================================================================

class TransferErrorCodeTest : public ::testing::Test {};

TEST_F(TransferErrorCodeTest, ErrorCodeRanges) {
    // Connection errors: -700 to -709
    EXPECT_EQ(static_cast<int>(transfer_error_code::connection_failed), -700);
    EXPECT_EQ(static_cast<int>(transfer_error_code::protocol_mismatch), -707);

    // Transfer errors: -710 to -719
    EXPECT_EQ(static_cast<int>(transfer_error_code::transfer_init_failed), -710);
    EXPECT_EQ(static_cast<int>(transfer_error_code::transfer_in_progress), -717);

    // Chunk errors: -720 to -739
    EXPECT_EQ(static_cast<int>(transfer_error_code::chunk_checksum_error), -720);
    EXPECT_EQ(static_cast<int>(transfer_error_code::chunk_duplicate), -725);

    // Storage errors: -740 to -749
    EXPECT_EQ(static_cast<int>(transfer_error_code::storage_error), -740);
    EXPECT_EQ(static_cast<int>(transfer_error_code::client_quota_exceeded), -749);

    // File I/O errors: -750 to -759
    EXPECT_EQ(static_cast<int>(transfer_error_code::file_read_error), -750);
    EXPECT_EQ(static_cast<int>(transfer_error_code::file_locked), -756);

    // Resume errors: -760 to -779
    EXPECT_EQ(static_cast<int>(transfer_error_code::resume_state_invalid), -760);
    EXPECT_EQ(static_cast<int>(transfer_error_code::resume_session_mismatch), -765);

    // Compression errors: -780 to -789
    EXPECT_EQ(static_cast<int>(transfer_error_code::compression_failed), -780);
    EXPECT_EQ(static_cast<int>(transfer_error_code::invalid_compression_data), -783);

    // Configuration errors: -790 to -799
    EXPECT_EQ(static_cast<int>(transfer_error_code::config_invalid), -790);
    EXPECT_EQ(static_cast<int>(transfer_error_code::config_reconnect_error), -795);
}

TEST_F(TransferErrorCodeTest, ToString) {
    EXPECT_EQ(to_string(transfer_error_code::success), "success");
    EXPECT_EQ(to_string(transfer_error_code::connection_failed), "connection failed");
    EXPECT_EQ(to_string(transfer_error_code::chunk_checksum_error),
              "chunk CRC32 verification failed");
    EXPECT_EQ(to_string(transfer_error_code::file_hash_mismatch),
              "SHA-256 verification failed");
}

TEST_F(TransferErrorCodeTest, ErrorMessage) {
    EXPECT_EQ(error_message(-700), "connection failed");
    EXPECT_EQ(error_message(-720), "chunk CRC32 verification failed");
    EXPECT_EQ(error_message(-999), "unknown error");
}

TEST_F(TransferErrorCodeTest, IsConnectionError) {
    EXPECT_TRUE(is_connection_error(-700));
    EXPECT_TRUE(is_connection_error(-707));
    EXPECT_FALSE(is_connection_error(-710));
    EXPECT_FALSE(is_connection_error(-699));
}

TEST_F(TransferErrorCodeTest, IsTransferError) {
    EXPECT_TRUE(is_transfer_error(-710));
    EXPECT_TRUE(is_transfer_error(-717));
    EXPECT_FALSE(is_transfer_error(-700));
    EXPECT_FALSE(is_transfer_error(-720));
}

TEST_F(TransferErrorCodeTest, IsChunkError) {
    EXPECT_TRUE(is_chunk_error(-720));
    EXPECT_TRUE(is_chunk_error(-725));
    EXPECT_FALSE(is_chunk_error(-710));
    EXPECT_FALSE(is_chunk_error(-740));
}

TEST_F(TransferErrorCodeTest, IsStorageError) {
    EXPECT_TRUE(is_storage_error(-740));
    EXPECT_TRUE(is_storage_error(-749));
    EXPECT_FALSE(is_storage_error(-720));
    EXPECT_FALSE(is_storage_error(-750));
}

TEST_F(TransferErrorCodeTest, IsIOError) {
    EXPECT_TRUE(is_io_error(-750));
    EXPECT_TRUE(is_io_error(-756));
    EXPECT_FALSE(is_io_error(-740));
    EXPECT_FALSE(is_io_error(-760));
}

TEST_F(TransferErrorCodeTest, IsResumeError) {
    EXPECT_TRUE(is_resume_error(-760));
    EXPECT_TRUE(is_resume_error(-765));
    EXPECT_FALSE(is_resume_error(-750));
    EXPECT_FALSE(is_resume_error(-780));
}

TEST_F(TransferErrorCodeTest, IsCompressionError) {
    EXPECT_TRUE(is_compression_error(-780));
    EXPECT_TRUE(is_compression_error(-783));
    EXPECT_FALSE(is_compression_error(-760));
    EXPECT_FALSE(is_compression_error(-790));
}

TEST_F(TransferErrorCodeTest, IsConfigError) {
    EXPECT_TRUE(is_config_error(-790));
    EXPECT_TRUE(is_config_error(-795));
    EXPECT_FALSE(is_config_error(-780));
    EXPECT_FALSE(is_config_error(-800));
}

TEST_F(TransferErrorCodeTest, IsRetryable) {
    // Retryable errors
    EXPECT_TRUE(is_retryable(-700));  // connection_failed
    EXPECT_TRUE(is_retryable(-701));  // connection_timeout
    EXPECT_TRUE(is_retryable(-712));  // transfer_timeout
    EXPECT_TRUE(is_retryable(-720));  // chunk_checksum_error

    // Non-retryable errors
    EXPECT_FALSE(is_retryable(-711));  // transfer_cancelled
    EXPECT_FALSE(is_retryable(-744));  // file_already_exists
    EXPECT_FALSE(is_retryable(-790));  // config_invalid
}

// =============================================================================
// transfer_id Tests (UUID version)
// =============================================================================

class TransferIdUUIDTest : public ::testing::Test {};

TEST_F(TransferIdUUIDTest, DefaultConstruction) {
    transfer_id id;
    EXPECT_TRUE(id.is_null());
    for (auto b : id.bytes) {
        EXPECT_EQ(b, 0);
    }
}

TEST_F(TransferIdUUIDTest, Generate) {
    auto id = transfer_id::generate();
    EXPECT_FALSE(id.is_null());

    // Check UUID version (bits 4-7 of byte 6 should be 0100)
    EXPECT_EQ((id.bytes[6] & 0xF0), 0x40);

    // Check UUID variant (bits 6-7 of byte 8 should be 10)
    EXPECT_EQ((id.bytes[8] & 0xC0), 0x80);
}

TEST_F(TransferIdUUIDTest, GenerateUniqueness) {
    auto id1 = transfer_id::generate();
    auto id2 = transfer_id::generate();

    EXPECT_NE(id1, id2);
}

TEST_F(TransferIdUUIDTest, ToString) {
    auto id = transfer_id::generate();
    auto str = id.to_string();

    // UUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (36 characters)
    EXPECT_EQ(str.length(), 36);
    EXPECT_EQ(str[8], '-');
    EXPECT_EQ(str[13], '-');
    EXPECT_EQ(str[18], '-');
    EXPECT_EQ(str[23], '-');
}

TEST_F(TransferIdUUIDTest, FromString) {
    auto id1 = transfer_id::generate();
    auto str = id1.to_string();

    auto id2_opt = transfer_id::from_string(str);
    ASSERT_TRUE(id2_opt.has_value());

    EXPECT_EQ(id1, id2_opt.value());
}

TEST_F(TransferIdUUIDTest, FromStringInvalid) {
    auto result = transfer_id::from_string("not-a-valid-uuid");
    EXPECT_FALSE(result.has_value());

    result = transfer_id::from_string("");
    EXPECT_FALSE(result.has_value());

    result = transfer_id::from_string("12345678-1234-1234-1234-12345678901g");  // Invalid hex
    EXPECT_FALSE(result.has_value());
}

TEST_F(TransferIdUUIDTest, EqualityOperator) {
    auto id1 = transfer_id::generate();
    auto id2 = id1;
    auto id3 = transfer_id::generate();

    EXPECT_EQ(id1, id2);
    EXPECT_NE(id1, id3);
}

TEST_F(TransferIdUUIDTest, LessThanOperator) {
    transfer_id id1;
    transfer_id id2;
    id1.bytes[0] = 1;
    id2.bytes[0] = 2;

    EXPECT_LT(id1, id2);
    EXPECT_FALSE(id2 < id1);
}

TEST_F(TransferIdUUIDTest, HashSupport) {
    auto id1 = transfer_id::generate();
    auto id2 = id1;
    auto id3 = transfer_id::generate();

    std::hash<transfer_id> hasher;
    EXPECT_EQ(hasher(id1), hasher(id2));
    EXPECT_NE(hasher(id1), hasher(id3));
}

TEST_F(TransferIdUUIDTest, UseInUnorderedSet) {
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

// =============================================================================
// chunk_flags Tests (extended)
// =============================================================================

class ChunkFlagsExtendedTest : public ::testing::Test {};

TEST_F(ChunkFlagsExtendedTest, FlagValues) {
    EXPECT_EQ(static_cast<uint8_t>(chunk_flags::none), 0x00);
    EXPECT_EQ(static_cast<uint8_t>(chunk_flags::first_chunk), 0x01);
    EXPECT_EQ(static_cast<uint8_t>(chunk_flags::last_chunk), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(chunk_flags::compressed), 0x04);
    EXPECT_EQ(static_cast<uint8_t>(chunk_flags::encrypted), 0x08);
}

TEST_F(ChunkFlagsExtendedTest, HelperFunctions) {
    auto flags = chunk_flags::first_chunk | chunk_flags::last_chunk |
                 chunk_flags::compressed;

    EXPECT_TRUE(is_first_chunk(flags));
    EXPECT_TRUE(is_last_chunk(flags));
    EXPECT_TRUE(is_compressed(flags));
    EXPECT_FALSE(is_encrypted(flags));
    EXPECT_TRUE(is_single_chunk(flags));
}

TEST_F(ChunkFlagsExtendedTest, SingleChunkFile) {
    auto flags = chunk_flags::first_chunk | chunk_flags::last_chunk;
    EXPECT_TRUE(is_single_chunk(flags));

    flags = chunk_flags::first_chunk;
    EXPECT_FALSE(is_single_chunk(flags));

    flags = chunk_flags::last_chunk;
    EXPECT_FALSE(is_single_chunk(flags));
}

TEST_F(ChunkFlagsExtendedTest, CompoundAssignment) {
    auto flags = chunk_flags::none;
    flags |= chunk_flags::compressed;
    EXPECT_TRUE(is_compressed(flags));

    flags |= chunk_flags::first_chunk;
    EXPECT_TRUE(is_first_chunk(flags));
    EXPECT_TRUE(is_compressed(flags));

    flags &= ~chunk_flags::compressed;
    EXPECT_FALSE(is_compressed(flags));
    EXPECT_TRUE(is_first_chunk(flags));
}

// =============================================================================
// chunk_header Tests
// =============================================================================

class ChunkHeaderTest : public ::testing::Test {};

TEST_F(ChunkHeaderTest, Size) {
    EXPECT_EQ(sizeof(chunk_header), 48);
    EXPECT_EQ(chunk_header::size, 48);
}

TEST_F(ChunkHeaderTest, DefaultConstruction) {
    chunk_header header;

    EXPECT_TRUE(header.id.is_null());
    EXPECT_EQ(header.chunk_index, 0);
    EXPECT_EQ(header.chunk_offset, 0);
    EXPECT_EQ(header.original_size, 0);
    EXPECT_EQ(header.compressed_size, 0);
    EXPECT_EQ(header.checksum, 0);
    EXPECT_EQ(header.flags, chunk_flags::none);
}

TEST_F(ChunkHeaderTest, PopulateFields) {
    chunk_header header;

    header.id = transfer_id::generate();
    header.chunk_index = 5;
    header.chunk_offset = 256 * 1024 * 5;
    header.original_size = 256 * 1024;
    header.compressed_size = 128 * 1024;
    header.checksum = 0xDEADBEEF;
    header.flags = chunk_flags::compressed | chunk_flags::last_chunk;

    EXPECT_FALSE(header.id.is_null());
    EXPECT_EQ(header.chunk_index, 5);
    EXPECT_EQ(header.chunk_offset, 256 * 1024 * 5);
    EXPECT_EQ(header.original_size, 256 * 1024);
    EXPECT_EQ(header.compressed_size, 128 * 1024);
    EXPECT_EQ(header.checksum, 0xDEADBEEF);
    EXPECT_TRUE(is_compressed(header.flags));
    EXPECT_TRUE(is_last_chunk(header.flags));
}

// =============================================================================
// chunk Tests
// =============================================================================

class ChunkTest : public ::testing::Test {};

TEST_F(ChunkTest, DefaultConstruction) {
    chunk c;
    EXPECT_TRUE(c.header.id.is_null());
    EXPECT_TRUE(c.data.empty());
}

TEST_F(ChunkTest, ConstructWithHeaderAndData) {
    chunk_header header;
    header.id = transfer_id::generate();
    header.flags = chunk_flags::compressed;

    std::vector<std::byte> data = {std::byte{0x01}, std::byte{0x02}};
    chunk c(header, std::move(data));

    EXPECT_FALSE(c.header.id.is_null());
    EXPECT_EQ(c.data_size(), 2);
    EXPECT_TRUE(c.is_compressed());
}

TEST_F(ChunkTest, HelperMethods) {
    chunk c;
    c.header.flags = chunk_flags::first_chunk | chunk_flags::compressed;
    c.data = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    EXPECT_TRUE(c.is_first());
    EXPECT_FALSE(c.is_last());
    EXPECT_TRUE(c.is_compressed());
    EXPECT_EQ(c.data_size(), 3);
    EXPECT_EQ(c.total_size(), chunk_header::size + 3);
}

// =============================================================================
// protocol_version Tests
// =============================================================================

class ProtocolVersionTest : public ::testing::Test {};

TEST_F(ProtocolVersionTest, DefaultConstruction) {
    protocol_version v;
    EXPECT_EQ(v.major, 0);
    EXPECT_EQ(v.minor, 2);
    EXPECT_EQ(v.patch, 0);
    EXPECT_EQ(v.build, 0);
}

TEST_F(ProtocolVersionTest, ExplicitConstruction) {
    protocol_version v{1, 2, 3, 4};
    EXPECT_EQ(v.major, 1);
    EXPECT_EQ(v.minor, 2);
    EXPECT_EQ(v.patch, 3);
    EXPECT_EQ(v.build, 4);
}

TEST_F(ProtocolVersionTest, ToUint32) {
    protocol_version v{1, 2, 3, 4};
    EXPECT_EQ(v.to_uint32(), 0x01020304);
}

TEST_F(ProtocolVersionTest, FromUint32) {
    auto v = protocol_version::from_uint32(0x01020304);
    EXPECT_EQ(v.major, 1);
    EXPECT_EQ(v.minor, 2);
    EXPECT_EQ(v.patch, 3);
    EXPECT_EQ(v.build, 4);
}

TEST_F(ProtocolVersionTest, ToString) {
    protocol_version v{1, 2, 3, 4};
    EXPECT_EQ(v.to_string(), "1.2.3.4");
}

TEST_F(ProtocolVersionTest, Comparison) {
    protocol_version v1{1, 0, 0, 0};
    protocol_version v2{1, 0, 0, 0};
    protocol_version v3{2, 0, 0, 0};
    protocol_version v4{1, 1, 0, 0};

    EXPECT_EQ(v1, v2);
    EXPECT_NE(v1, v3);
    EXPECT_LT(v1, v3);
    EXPECT_LT(v1, v4);
}

TEST_F(ProtocolVersionTest, CurrentVersion) {
    EXPECT_EQ(current_protocol_version.major, 0);
    EXPECT_EQ(current_protocol_version.minor, 2);
    EXPECT_EQ(current_protocol_version.patch, 0);
}

// =============================================================================
// message_type Tests
// =============================================================================

class MessageTypeTest : public ::testing::Test {};

TEST_F(MessageTypeTest, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(message_type::connect), 0x01);
    EXPECT_EQ(static_cast<uint8_t>(message_type::connect_ack), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(message_type::upload_request), 0x10);
    EXPECT_EQ(static_cast<uint8_t>(message_type::chunk_data), 0x20);
    EXPECT_EQ(static_cast<uint8_t>(message_type::download_request), 0x50);
    EXPECT_EQ(static_cast<uint8_t>(message_type::list_request), 0x60);
    EXPECT_EQ(static_cast<uint8_t>(message_type::error), 0xFF);
}

TEST_F(MessageTypeTest, ToString) {
    EXPECT_EQ(to_string(message_type::connect), "CONNECT");
    EXPECT_EQ(to_string(message_type::connect_ack), "CONNECT_ACK");
    EXPECT_EQ(to_string(message_type::upload_request), "UPLOAD_REQUEST");
    EXPECT_EQ(to_string(message_type::chunk_data), "CHUNK_DATA");
    EXPECT_EQ(to_string(message_type::error), "ERROR");
}

// =============================================================================
// client_capabilities Tests
// =============================================================================

class ClientCapabilitiesTest : public ::testing::Test {};

TEST_F(ClientCapabilitiesTest, FlagValues) {
    EXPECT_EQ(static_cast<uint32_t>(client_capabilities::none), 0);
    EXPECT_EQ(static_cast<uint32_t>(client_capabilities::compression), 1);
    EXPECT_EQ(static_cast<uint32_t>(client_capabilities::resume), 2);
    EXPECT_EQ(static_cast<uint32_t>(client_capabilities::batch_transfer), 4);
}

TEST_F(ClientCapabilitiesTest, BitwiseOperations) {
    auto caps = client_capabilities::compression | client_capabilities::resume;

    EXPECT_TRUE(has_capability(caps, client_capabilities::compression));
    EXPECT_TRUE(has_capability(caps, client_capabilities::resume));
    EXPECT_FALSE(has_capability(caps, client_capabilities::batch_transfer));
}

// =============================================================================
// transfer_direction Tests
// =============================================================================

class TransferDirectionTest : public ::testing::Test {};

TEST_F(TransferDirectionTest, EnumValues) {
    EXPECT_EQ(to_string(transfer_direction::upload), "upload");
    EXPECT_EQ(to_string(transfer_direction::download), "download");
}

// =============================================================================
// transfer_state Tests
// =============================================================================

class TransferStateTest : public ::testing::Test {};

TEST_F(TransferStateTest, EnumValues) {
    EXPECT_EQ(to_string(transfer_state::idle), "idle");
    EXPECT_EQ(to_string(transfer_state::transferring), "transferring");
    EXPECT_EQ(to_string(transfer_state::completed), "completed");
    EXPECT_EQ(to_string(transfer_state::failed), "failed");
}

TEST_F(TransferStateTest, IsTerminalState) {
    EXPECT_FALSE(is_terminal_state(transfer_state::idle));
    EXPECT_FALSE(is_terminal_state(transfer_state::transferring));
    EXPECT_TRUE(is_terminal_state(transfer_state::completed));
    EXPECT_TRUE(is_terminal_state(transfer_state::failed));
    EXPECT_TRUE(is_terminal_state(transfer_state::cancelled));
}

TEST_F(TransferStateTest, IsActiveState) {
    EXPECT_FALSE(is_active_state(transfer_state::idle));
    EXPECT_TRUE(is_active_state(transfer_state::transferring));
    EXPECT_TRUE(is_active_state(transfer_state::verifying));
    EXPECT_FALSE(is_active_state(transfer_state::completed));
    EXPECT_FALSE(is_active_state(transfer_state::failed));
}

// =============================================================================
// detailed_transfer_progress Tests
// =============================================================================

class DetailedTransferProgressTest : public ::testing::Test {};

TEST_F(DetailedTransferProgressTest, DefaultConstruction) {
    detailed_transfer_progress prog;
    EXPECT_EQ(prog.bytes_transferred, 0);
    EXPECT_EQ(prog.total_bytes, 0);
    EXPECT_EQ(prog.state, transfer_state::idle);
}

TEST_F(DetailedTransferProgressTest, CompletionPercentage) {
    detailed_transfer_progress prog;
    prog.total_bytes = 1000;
    prog.bytes_transferred = 500;

    EXPECT_DOUBLE_EQ(prog.completion_percentage(), 50.0);
}

TEST_F(DetailedTransferProgressTest, CompletionPercentageZeroTotal) {
    detailed_transfer_progress prog;
    prog.total_bytes = 0;
    prog.bytes_transferred = 0;

    EXPECT_DOUBLE_EQ(prog.completion_percentage(), 0.0);
}

// =============================================================================
// transfer_error Tests
// =============================================================================

class TransferErrorStructTest : public ::testing::Test {};

TEST_F(TransferErrorStructTest, DefaultConstruction) {
    transfer_error err;
    EXPECT_EQ(err.code, transfer_error_code::success);
    EXPECT_FALSE(static_cast<bool>(err));
}

TEST_F(TransferErrorStructTest, ConstructWithCode) {
    transfer_error err(transfer_error_code::connection_failed);
    EXPECT_EQ(err.code, transfer_error_code::connection_failed);
    EXPECT_TRUE(static_cast<bool>(err));
    EXPECT_TRUE(err.is_retryable());
}

TEST_F(TransferErrorStructTest, ConstructWithCodeAndMessage) {
    transfer_error err(transfer_error_code::file_not_found, "custom message");
    EXPECT_EQ(err.code, transfer_error_code::file_not_found);
    EXPECT_EQ(err.message, "custom message");
}

// =============================================================================
// endpoint Tests (from transfer_types.h)
// =============================================================================

class TransferEndpointTest : public ::testing::Test {};

TEST_F(TransferEndpointTest, DefaultConstruction) {
    endpoint ep;
    EXPECT_TRUE(ep.host.empty());
    EXPECT_EQ(ep.port, 0);
    EXPECT_FALSE(ep.is_valid());
}

TEST_F(TransferEndpointTest, ConstructWithHostAndPort) {
    endpoint ep("localhost", 8080);
    EXPECT_EQ(ep.host, "localhost");
    EXPECT_EQ(ep.port, 8080);
    EXPECT_TRUE(ep.is_valid());
}

TEST_F(TransferEndpointTest, ConstructWithPortOnly) {
    endpoint ep(8080);
    EXPECT_EQ(ep.host, "0.0.0.0");
    EXPECT_EQ(ep.port, 8080);
    EXPECT_TRUE(ep.is_valid());
}

TEST_F(TransferEndpointTest, ToString) {
    endpoint ep("192.168.1.1", 9000);
    EXPECT_EQ(ep.to_string(), "192.168.1.1:9000");
}

TEST_F(TransferEndpointTest, Equality) {
    endpoint ep1("localhost", 8080);
    endpoint ep2("localhost", 8080);
    endpoint ep3("localhost", 9000);

    EXPECT_EQ(ep1, ep2);
    EXPECT_NE(ep1, ep3);
}

// =============================================================================
// frame_header Tests
// =============================================================================

class FrameHeaderTest : public ::testing::Test {};

TEST_F(FrameHeaderTest, Constants) {
    EXPECT_EQ(frame_header::size, 9);
    EXPECT_EQ(frame_header::postfix_size, 4);
    EXPECT_EQ(frame_header::total_overhead, 13);
}

TEST_F(FrameHeaderTest, ProtocolMagic) {
    EXPECT_EQ(protocol_magic, 0x46545331);  // "FTS1"
}

}  // namespace kcenon::file_transfer::test
