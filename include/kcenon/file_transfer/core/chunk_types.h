/**
 * @file chunk_types.h
 * @brief Chunk data structures for file_trans_system
 * @version 0.2.0
 *
 * This file defines the chunk-related data structures used for
 * splitting and assembling files during transfer.
 */

#ifndef KCENON_FILE_TRANSFER_CORE_CHUNK_TYPES_H
#define KCENON_FILE_TRANSFER_CORE_CHUNK_TYPES_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kcenon::file_transfer {

/**
 * @brief Unique identifier for a transfer session (16-byte UUID)
 */
struct transfer_id {
    std::array<uint8_t, 16> bytes{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    constexpr transfer_id() noexcept = default;

    explicit constexpr transfer_id(const std::array<uint8_t, 16>& b) noexcept
        : bytes(b) {}

    /**
     * @brief Generate a new random transfer ID
     */
    [[nodiscard]] static auto generate() -> transfer_id;

    /**
     * @brief Convert to string representation (UUID format)
     */
    [[nodiscard]] auto to_string() const -> std::string;

    /**
     * @brief Parse from UUID string
     */
    [[nodiscard]] static auto from_string(std::string_view str)
        -> std::optional<transfer_id>;

    /**
     * @brief Check if the transfer ID is null (all zeros)
     */
    [[nodiscard]] constexpr auto is_null() const noexcept -> bool {
        for (const auto& b : bytes) {
            if (b != 0) return false;
        }
        return true;
    }

    [[nodiscard]] constexpr auto operator==(const transfer_id& other) const
        noexcept -> bool = default;

    [[nodiscard]] constexpr auto operator<(const transfer_id& other) const
        noexcept -> bool {
        return bytes < other.bytes;
    }
};

/**
 * @brief Chunk flags indicating chunk properties
 *
 * Bit layout:
 * - Bit 0 (0x01): first_chunk - First chunk of file
 * - Bit 1 (0x02): last_chunk - Last chunk of file
 * - Bit 2 (0x04): compressed - Data is LZ4 compressed
 * - Bit 3 (0x08): encrypted - Reserved for encryption
 * - Bit 4-7: Reserved (must be 0)
 */
enum class chunk_flags : uint8_t {
    none = 0x00,
    first_chunk = 0x01,
    last_chunk = 0x02,
    compressed = 0x04,
    encrypted = 0x08,
};

[[nodiscard]] constexpr auto operator|(chunk_flags a, chunk_flags b) noexcept
    -> chunk_flags {
    return static_cast<chunk_flags>(static_cast<uint8_t>(a) |
                                    static_cast<uint8_t>(b));
}

[[nodiscard]] constexpr auto operator&(chunk_flags a, chunk_flags b) noexcept
    -> chunk_flags {
    return static_cast<chunk_flags>(static_cast<uint8_t>(a) &
                                    static_cast<uint8_t>(b));
}

[[nodiscard]] constexpr auto operator~(chunk_flags a) noexcept -> chunk_flags {
    return static_cast<chunk_flags>(~static_cast<uint8_t>(a));
}

constexpr auto operator|=(chunk_flags& a, chunk_flags b) noexcept
    -> chunk_flags& {
    a = a | b;
    return a;
}

constexpr auto operator&=(chunk_flags& a, chunk_flags b) noexcept
    -> chunk_flags& {
    a = a & b;
    return a;
}

[[nodiscard]] constexpr auto has_flag(chunk_flags flags,
                                      chunk_flags flag) noexcept -> bool {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

[[nodiscard]] constexpr auto is_first_chunk(chunk_flags flags) noexcept
    -> bool {
    return has_flag(flags, chunk_flags::first_chunk);
}

[[nodiscard]] constexpr auto is_last_chunk(chunk_flags flags) noexcept -> bool {
    return has_flag(flags, chunk_flags::last_chunk);
}

[[nodiscard]] constexpr auto is_compressed(chunk_flags flags) noexcept -> bool {
    return has_flag(flags, chunk_flags::compressed);
}

[[nodiscard]] constexpr auto is_encrypted(chunk_flags flags) noexcept -> bool {
    return has_flag(flags, chunk_flags::encrypted);
}

[[nodiscard]] constexpr auto is_single_chunk(chunk_flags flags) noexcept
    -> bool {
    return is_first_chunk(flags) && is_last_chunk(flags);
}

/**
 * @brief Chunk header structure for wire protocol (48 bytes + data)
 *
 * Memory layout (aligned):
 * - transfer_id: 16 bytes (UUID)
 * - chunk_index: 8 bytes
 * - chunk_offset: 8 bytes
 * - original_size: 4 bytes
 * - compressed_size: 4 bytes
 * - checksum: 4 bytes (CRC32)
 * - flags: 1 byte
 * - reserved: 3 bytes (padding)
 *
 * Total: 48 bytes
 */
#pragma pack(push, 1)
struct chunk_header {
    transfer_id id;              // 16 bytes: Transfer UUID
    uint64_t chunk_index;        // 8 bytes: Chunk sequence number
    uint64_t chunk_offset;       // 8 bytes: Byte offset in file
    uint32_t original_size;      // 4 bytes: Original (uncompressed) size
    uint32_t compressed_size;    // 4 bytes: Compressed size (or same as original)
    uint32_t checksum;           // 4 bytes: CRC32 of original data
    chunk_flags flags;           // 1 byte: Chunk flags
    uint8_t reserved[3];         // 3 bytes: Padding for alignment

    static constexpr std::size_t size = 48;

    constexpr chunk_header() noexcept
        : id{}
        , chunk_index(0)
        , chunk_offset(0)
        , original_size(0)
        , compressed_size(0)
        , checksum(0)
        , flags(chunk_flags::none)
        , reserved{0, 0, 0} {}
};
#pragma pack(pop)

static_assert(sizeof(chunk_header) == chunk_header::size,
              "chunk_header must be exactly 48 bytes");

/**
 * @brief Complete chunk with header and data
 */
struct chunk {
    chunk_header header;
    std::vector<std::byte> data;

    chunk() = default;

    chunk(const chunk_header& hdr, std::vector<std::byte> d)
        : header(hdr), data(std::move(d)) {}

    /**
     * @brief Get the actual data size (compressed or original)
     */
    [[nodiscard]] auto data_size() const noexcept -> std::size_t {
        return data.size();
    }

    /**
     * @brief Check if this chunk is compressed
     */
    [[nodiscard]] auto is_compressed() const noexcept -> bool {
        return has_flag(header.flags, chunk_flags::compressed);
    }

    /**
     * @brief Check if this is the first chunk
     */
    [[nodiscard]] auto is_first() const noexcept -> bool {
        return has_flag(header.flags, chunk_flags::first_chunk);
    }

    /**
     * @brief Check if this is the last chunk
     */
    [[nodiscard]] auto is_last() const noexcept -> bool {
        return has_flag(header.flags, chunk_flags::last_chunk);
    }

    /**
     * @brief Total serialized size of this chunk
     */
    [[nodiscard]] auto total_size() const noexcept -> std::size_t {
        return chunk_header::size + data.size();
    }
};

/**
 * @brief Chunk metadata for tracking received chunks
 */
struct chunk_metadata {
    uint64_t index;
    uint64_t offset;
    uint32_t original_size;
    uint32_t checksum;
    bool received;

    chunk_metadata() noexcept
        : index(0), offset(0), original_size(0), checksum(0), received(false) {}

    chunk_metadata(uint64_t idx, uint64_t off, uint32_t size, uint32_t crc)
        : index(idx), offset(off), original_size(size), checksum(crc),
          received(false) {}
};

/**
 * @brief Statistics for chunk operations
 */
struct chunk_statistics {
    uint64_t total_chunks = 0;
    uint64_t received_chunks = 0;
    uint64_t compressed_chunks = 0;
    uint64_t retransmitted_chunks = 0;
    uint64_t checksum_failures = 0;
    uint64_t bytes_original = 0;
    uint64_t bytes_compressed = 0;

    [[nodiscard]] auto completion_percentage() const noexcept -> double {
        if (total_chunks == 0) return 0.0;
        return static_cast<double>(received_chunks) /
               static_cast<double>(total_chunks) * 100.0;
    }

    [[nodiscard]] auto compression_ratio() const noexcept -> double {
        if (bytes_original == 0) return 1.0;
        return static_cast<double>(bytes_compressed) /
               static_cast<double>(bytes_original);
    }
};

}  // namespace kcenon::file_transfer

// Hash support for transfer_id
template <>
struct std::hash<kcenon::file_transfer::transfer_id> {
    auto operator()(const kcenon::file_transfer::transfer_id& id) const noexcept
        -> std::size_t {
        std::size_t result = 0;
        for (std::size_t i = 0; i < 16; i += sizeof(std::size_t)) {
            std::size_t block = 0;
            for (std::size_t j = 0;
                 j < sizeof(std::size_t) && (i + j) < 16; ++j) {
                block |= static_cast<std::size_t>(id.bytes[i + j])
                         << (j * 8);
            }
            result ^= block + 0x9e3779b9 + (result << 6) + (result >> 2);
        }
        return result;
    }
};

#endif  // KCENON_FILE_TRANSFER_CORE_CHUNK_TYPES_H
