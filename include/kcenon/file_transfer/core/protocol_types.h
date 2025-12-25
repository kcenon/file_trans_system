/**
 * @file protocol_types.h
 * @brief Protocol message types and payloads for file_trans_system
 * @version 0.2.0
 *
 * This file defines the wire protocol message types and their payload structures.
 * All multi-byte fields use big-endian byte order for network transmission.
 */

#ifndef KCENON_FILE_TRANSFER_CORE_PROTOCOL_TYPES_H
#define KCENON_FILE_TRANSFER_CORE_PROTOCOL_TYPES_H

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kcenon::file_transfer {

/**
 * @brief Protocol magic number ("FTS1")
 */
inline constexpr uint32_t protocol_magic = 0x46545331;

/**
 * @brief Protocol version structure (Major.Minor.Patch.Build)
 */
struct protocol_version {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
    uint8_t build;

    constexpr protocol_version() noexcept
        : major(0), minor(2), patch(0), build(0) {}

    constexpr protocol_version(uint8_t maj, uint8_t min, uint8_t pat,
                               uint8_t bld = 0) noexcept
        : major(maj), minor(min), patch(pat), build(bld) {}

    [[nodiscard]] constexpr auto to_uint32() const noexcept -> uint32_t {
        return (static_cast<uint32_t>(major) << 24) |
               (static_cast<uint32_t>(minor) << 16) |
               (static_cast<uint32_t>(patch) << 8) |
               static_cast<uint32_t>(build);
    }

    [[nodiscard]] static constexpr auto from_uint32(uint32_t v) noexcept
        -> protocol_version {
        return {static_cast<uint8_t>((v >> 24) & 0xFF),
                static_cast<uint8_t>((v >> 16) & 0xFF),
                static_cast<uint8_t>((v >> 8) & 0xFF),
                static_cast<uint8_t>(v & 0xFF)};
    }

    [[nodiscard]] auto to_string() const -> std::string {
        return std::to_string(major) + "." + std::to_string(minor) + "." +
               std::to_string(patch) + "." + std::to_string(build);
    }

    [[nodiscard]] constexpr auto operator==(const protocol_version& other) const
        -> bool = default;
    [[nodiscard]] constexpr auto operator<(const protocol_version& other) const
        -> bool {
        return to_uint32() < other.to_uint32();
    }
};

/**
 * @brief Current protocol version
 */
inline constexpr protocol_version current_protocol_version{0, 2, 0, 0};

/**
 * @brief Message type enumeration
 *
 * Message type codes are organized by category:
 * - 0x01-0x0F: Session management
 * - 0x10-0x1F: Upload control
 * - 0x20-0x2F: Data transfer
 * - 0x30-0x3F: Resume
 * - 0x40-0x4F: Transfer control
 * - 0x50-0x5F: Download control
 * - 0x60-0x6F: File listing
 * - 0xF0-0xFF: Control/Error
 */
enum class message_type : uint8_t {
    // Session management (0x01-0x0F)
    connect = 0x01,
    connect_ack = 0x02,
    disconnect = 0x03,
    heartbeat = 0x04,
    heartbeat_ack = 0x05,

    // Upload control (0x10-0x1F)
    upload_request = 0x10,
    upload_accept = 0x11,
    upload_reject = 0x12,
    upload_complete = 0x13,
    upload_ack = 0x14,

    // Data transfer (0x20-0x2F)
    chunk_data = 0x20,
    chunk_ack = 0x21,
    chunk_nack = 0x22,

    // Resume (0x30-0x3F)
    resume_request = 0x30,
    resume_response = 0x31,

    // Transfer control (0x40-0x4F)
    transfer_cancel = 0x40,
    transfer_pause = 0x41,
    transfer_resume = 0x42,
    transfer_verify = 0x43,

    // Download control (0x50-0x5F)
    download_request = 0x50,
    download_accept = 0x51,
    download_reject = 0x52,
    download_complete = 0x53,
    download_ack = 0x54,

    // File listing (0x60-0x6F)
    list_request = 0x60,
    list_response = 0x61,

    // Control/Error (0xF0-0xFF)
    error = 0xFF,
};

/**
 * @brief Convert message_type to string
 */
[[nodiscard]] constexpr auto to_string(message_type type) noexcept
    -> std::string_view {
    switch (type) {
        case message_type::connect:
            return "CONNECT";
        case message_type::connect_ack:
            return "CONNECT_ACK";
        case message_type::disconnect:
            return "DISCONNECT";
        case message_type::heartbeat:
            return "HEARTBEAT";
        case message_type::heartbeat_ack:
            return "HEARTBEAT_ACK";
        case message_type::upload_request:
            return "UPLOAD_REQUEST";
        case message_type::upload_accept:
            return "UPLOAD_ACCEPT";
        case message_type::upload_reject:
            return "UPLOAD_REJECT";
        case message_type::upload_complete:
            return "UPLOAD_COMPLETE";
        case message_type::upload_ack:
            return "UPLOAD_ACK";
        case message_type::chunk_data:
            return "CHUNK_DATA";
        case message_type::chunk_ack:
            return "CHUNK_ACK";
        case message_type::chunk_nack:
            return "CHUNK_NACK";
        case message_type::resume_request:
            return "RESUME_REQUEST";
        case message_type::resume_response:
            return "RESUME_RESPONSE";
        case message_type::transfer_cancel:
            return "TRANSFER_CANCEL";
        case message_type::transfer_pause:
            return "TRANSFER_PAUSE";
        case message_type::transfer_resume:
            return "TRANSFER_RESUME";
        case message_type::transfer_verify:
            return "TRANSFER_VERIFY";
        case message_type::download_request:
            return "DOWNLOAD_REQUEST";
        case message_type::download_accept:
            return "DOWNLOAD_ACCEPT";
        case message_type::download_reject:
            return "DOWNLOAD_REJECT";
        case message_type::download_complete:
            return "DOWNLOAD_COMPLETE";
        case message_type::download_ack:
            return "DOWNLOAD_ACK";
        case message_type::list_request:
            return "LIST_REQUEST";
        case message_type::list_response:
            return "LIST_RESPONSE";
        case message_type::error:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Client capabilities bitmap
 */
enum class client_capabilities : uint32_t {
    none = 0,
    compression = 1 << 0,
    resume = 1 << 1,
    batch_transfer = 1 << 2,
    quic_support = 1 << 3,
    auto_reconnect = 1 << 4,
    encryption = 1 << 5,        ///< Application-level encryption support
};

[[nodiscard]] constexpr auto operator|(client_capabilities a,
                                       client_capabilities b)
    -> client_capabilities {
    return static_cast<client_capabilities>(static_cast<uint32_t>(a) |
                                            static_cast<uint32_t>(b));
}

[[nodiscard]] constexpr auto operator&(client_capabilities a,
                                       client_capabilities b)
    -> client_capabilities {
    return static_cast<client_capabilities>(static_cast<uint32_t>(a) &
                                            static_cast<uint32_t>(b));
}

[[nodiscard]] constexpr auto has_capability(client_capabilities caps,
                                            client_capabilities cap) -> bool {
    return (static_cast<uint32_t>(caps) & static_cast<uint32_t>(cap)) != 0;
}

/**
 * @brief Transfer options flags
 */
enum class transfer_options : uint32_t {
    none = 0,
    overwrite_existing = 1 << 0,
    verify_checksum = 1 << 1,
    preserve_timestamp = 1 << 2,
    encrypted = 1 << 3,              ///< Enable encryption for this transfer
};

[[nodiscard]] constexpr auto operator|(transfer_options a, transfer_options b)
    -> transfer_options {
    return static_cast<transfer_options>(static_cast<uint32_t>(a) |
                                         static_cast<uint32_t>(b));
}

[[nodiscard]] constexpr auto operator&(transfer_options a, transfer_options b)
    -> transfer_options {
    return static_cast<transfer_options>(static_cast<uint32_t>(a) &
                                         static_cast<uint32_t>(b));
}

[[nodiscard]] constexpr auto has_option(transfer_options opts,
                                        transfer_options opt) -> bool {
    return (static_cast<uint32_t>(opts) & static_cast<uint32_t>(opt)) != 0;
}

/**
 * @brief Compression mode for transfers
 */
enum class wire_compression_mode : uint8_t {
    none = 0x00,
    lz4 = 0x01,
    adaptive = 0x02,
};

/**
 * @brief Encryption algorithm for transfers
 */
enum class wire_encryption_algorithm : uint8_t {
    none = 0x00,
    aes_256_gcm = 0x01,
    chacha20_poly1305 = 0x02,
};

/**
 * @brief Sort field for file listing
 */
enum class list_sort_field : uint8_t {
    name = 0,
    size = 1,
    time = 2,
};

/**
 * @brief Sort order for file listing
 */
enum class list_sort_order : uint8_t {
    ascending = 0,
    descending = 1,
};

// ============================================================================
// Message Payload Structures
// ============================================================================

/**
 * @brief CONNECT message payload (24 bytes)
 */
struct msg_connect {
    protocol_version version;            // 4 bytes
    client_capabilities capabilities;    // 4 bytes
    std::array<uint8_t, 16> client_id;   // 16 bytes (UUID, optional)

    static constexpr std::size_t serialized_size = 24;
};

/**
 * @brief CONNECT_ACK message payload (38+ bytes)
 */
struct msg_connect_ack {
    protocol_version version;             // 4 bytes
    client_capabilities capabilities;     // 4 bytes (negotiated)
    std::array<uint8_t, 16> session_id;   // 16 bytes (UUID)
    uint32_t max_chunk_size;              // 4 bytes
    uint64_t max_file_size;               // 8 bytes
    std::string server_name;              // variable (2-byte length prefix)

    static constexpr std::size_t min_serialized_size = 38;
};

/**
 * @brief DISCONNECT message payload
 */
struct msg_disconnect {
    int32_t reason_code;     // 4 bytes
    std::string message;     // variable (2-byte length prefix)

    static constexpr std::size_t min_serialized_size = 6;
};

/**
 * @brief HEARTBEAT / HEARTBEAT_ACK message payload (12 bytes)
 */
struct msg_heartbeat {
    uint64_t timestamp;      // 8 bytes (microseconds)
    uint32_t sequence;       // 4 bytes

    static constexpr std::size_t serialized_size = 12;
};

/**
 * @brief UPLOAD_REQUEST message payload (72+ bytes)
 */
struct msg_upload_request {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)
    std::string filename;                   // variable (2-byte length prefix)
    uint64_t file_size;                     // 8 bytes
    std::array<uint8_t, 32> sha256_hash;   // 32 bytes
    wire_compression_mode compression;      // 1 byte
    wire_encryption_algorithm encryption;   // 1 byte
    transfer_options options;               // 4 bytes
    uint64_t resume_from;                   // 8 bytes (0 if new)

    static constexpr std::size_t min_serialized_size = 72;
};

/**
 * @brief UPLOAD_ACCEPT message payload (30 bytes)
 */
struct msg_upload_accept {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)
    wire_compression_mode compression;      // 1 byte (agreed)
    wire_encryption_algorithm encryption;   // 1 byte (agreed)
    uint32_t chunk_size;                    // 4 bytes
    uint64_t resume_offset;                 // 8 bytes

    static constexpr std::size_t serialized_size = 30;
};

/**
 * @brief UPLOAD_REJECT message payload
 */
struct msg_upload_reject {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)
    int32_t reason_code;                    // 4 bytes
    std::string message;                    // variable (2-byte length prefix)

    static constexpr std::size_t min_serialized_size = 22;
};

/**
 * @brief UPLOAD_COMPLETE message payload (40 bytes)
 */
struct msg_upload_complete {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)
    uint64_t total_chunks;                  // 8 bytes
    uint64_t bytes_sent;                    // 8 bytes (raw)
    uint64_t bytes_on_wire;                 // 8 bytes (compressed)

    static constexpr std::size_t serialized_size = 40;
};

/**
 * @brief UPLOAD_ACK message payload (19+ bytes)
 */
struct msg_upload_ack {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)
    uint8_t verified;                       // 1 byte (bool)
    std::string stored_path;                // variable (2-byte length prefix)

    static constexpr std::size_t min_serialized_size = 19;
};

/**
 * @brief DOWNLOAD_REQUEST message payload (28+ bytes)
 */
struct msg_download_request {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)
    std::string filename;                   // variable (2-byte length prefix)
    wire_compression_mode compression;      // 1 byte
    wire_encryption_algorithm encryption;   // 1 byte
    uint64_t resume_from;                   // 8 bytes (0 if new)

    static constexpr std::size_t min_serialized_size = 28;
};

/**
 * @brief DOWNLOAD_ACCEPT message payload (86 bytes)
 */
struct msg_download_accept {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)
    uint64_t file_size;                     // 8 bytes
    std::array<uint8_t, 32> sha256_hash;   // 32 bytes
    wire_compression_mode compression;      // 1 byte (agreed)
    wire_encryption_algorithm encryption;   // 1 byte (agreed)
    uint32_t chunk_size;                    // 4 bytes
    uint64_t total_chunks;                  // 8 bytes
    uint64_t resume_offset;                 // 8 bytes
    uint64_t modified_time;                 // 8 bytes (timestamp)

    static constexpr std::size_t serialized_size = 86;
};

/**
 * @brief DOWNLOAD_REJECT message payload
 */
struct msg_download_reject {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)
    int32_t reason_code;                    // 4 bytes
    std::string message;                    // variable (2-byte length prefix)

    static constexpr std::size_t min_serialized_size = 22;
};

/**
 * @brief DOWNLOAD_COMPLETE message payload (40 bytes)
 */
struct msg_download_complete {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)
    uint64_t total_chunks;                  // 8 bytes
    uint64_t bytes_sent;                    // 8 bytes (raw)
    uint64_t bytes_on_wire;                 // 8 bytes (compressed)

    static constexpr std::size_t serialized_size = 40;
};

/**
 * @brief DOWNLOAD_ACK message payload (25 bytes)
 */
struct msg_download_ack {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)
    uint8_t verified;                       // 1 byte (bool)
    uint64_t bytes_received;                // 8 bytes

    static constexpr std::size_t serialized_size = 25;
};

/**
 * @brief LIST_REQUEST message payload (28+ bytes)
 */
struct msg_list_request {
    std::array<uint8_t, 16> request_id;    // 16 bytes (UUID)
    std::string pattern;                    // variable (2-byte length prefix)
    uint32_t offset;                        // 4 bytes
    uint32_t limit;                         // 4 bytes
    list_sort_field sort_by;                // 1 byte
    list_sort_order sort_order;             // 1 byte

    static constexpr std::size_t min_serialized_size = 28;
};

/**
 * @brief File entry for LIST_RESPONSE (58+ bytes per entry)
 */
struct file_entry {
    std::string filename;                   // variable (2-byte length prefix)
    uint64_t file_size;                     // 8 bytes
    std::array<uint8_t, 32> sha256_hash;   // 32 bytes
    uint64_t created_time;                  // 8 bytes (timestamp)
    uint64_t modified_time;                 // 8 bytes (timestamp)

    static constexpr std::size_t min_serialized_size = 58;
};

/**
 * @brief LIST_RESPONSE message payload
 */
struct msg_list_response {
    std::array<uint8_t, 16> request_id;    // 16 bytes (UUID)
    uint32_t total_count;                   // 4 bytes
    uint32_t returned_count;                // 4 bytes
    uint8_t has_more;                       // 1 byte (bool)
    std::vector<file_entry> entries;        // variable

    static constexpr std::size_t min_serialized_size = 25;
};

/**
 * @brief CHUNK_ACK message payload (24 bytes)
 */
struct msg_chunk_ack {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)
    uint64_t chunk_index;                   // 8 bytes

    static constexpr std::size_t serialized_size = 24;
};

/**
 * @brief CHUNK_NACK message payload (32 bytes)
 */
struct msg_chunk_nack {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)
    uint64_t chunk_index;                   // 8 bytes
    int32_t reason_code;                    // 4 bytes
    uint32_t reserved;                      // 4 bytes (padding)

    static constexpr std::size_t serialized_size = 32;
};

/**
 * @brief RESUME_REQUEST message payload
 */
struct msg_resume_request {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)
    std::array<uint8_t, 32> file_hash;     // 32 bytes (SHA-256)
    uint64_t last_chunk_index;              // 8 bytes

    static constexpr std::size_t serialized_size = 56;
};

/**
 * @brief RESUME_RESPONSE message payload
 */
struct msg_resume_response {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)
    uint8_t can_resume;                     // 1 byte (bool)
    uint64_t resume_from_chunk;             // 8 bytes
    uint64_t resume_from_offset;            // 8 bytes
    std::vector<uint64_t> missing_chunks;   // variable

    static constexpr std::size_t min_serialized_size = 33;
};

/**
 * @brief TRANSFER_CANCEL message payload
 */
struct msg_transfer_cancel {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)
    int32_t reason_code;                    // 4 bytes
    std::string message;                    // variable (2-byte length prefix)

    static constexpr std::size_t min_serialized_size = 22;
};

/**
 * @brief TRANSFER_PAUSE message payload (16 bytes)
 */
struct msg_transfer_pause {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)

    static constexpr std::size_t serialized_size = 16;
};

/**
 * @brief TRANSFER_RESUME message payload (16 bytes)
 */
struct msg_transfer_resume {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)

    static constexpr std::size_t serialized_size = 16;
};

/**
 * @brief TRANSFER_VERIFY message payload (49 bytes)
 */
struct msg_transfer_verify {
    std::array<uint8_t, 16> transfer_id;   // 16 bytes (UUID)
    uint8_t verified;                       // 1 byte (bool)
    std::array<uint8_t, 32> computed_hash; // 32 bytes (SHA-256)

    static constexpr std::size_t serialized_size = 49;
};

/**
 * @brief ERROR message payload
 */
struct msg_error {
    int32_t error_code;                     // 4 bytes
    std::string message;                    // variable (2-byte length prefix)
    std::array<uint8_t, 16> related_id;    // 16 bytes (optional transfer/request ID)

    static constexpr std::size_t min_serialized_size = 22;
};

/**
 * @brief Protocol frame header structure (13 bytes)
 *
 * Frame layout:
 * - prefix (4 bytes): Magic number 0x46545331 ("FTS1")
 * - message_type (1 byte): Message type code
 * - payload_length (4 bytes): Big-endian payload length
 * - payload (N bytes): Variable length payload
 * - checksum (2 bytes): Sum of bytes [0..9+N-1] mod 65536
 * - length_echo (2 bytes): Lower 16 bits of payload_length
 */
struct frame_header {
    uint32_t prefix;              // Magic number
    message_type type;            // Message type
    uint32_t payload_length;      // Payload length

    static constexpr std::size_t size = 9;
    static constexpr std::size_t postfix_size = 4;  // checksum + length_echo
    static constexpr std::size_t total_overhead = size + postfix_size;  // 13 bytes
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CORE_PROTOCOL_TYPES_H
