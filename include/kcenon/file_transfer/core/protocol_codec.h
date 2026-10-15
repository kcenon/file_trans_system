#ifndef KCENON_FILE_TRANSFER_CORE_PROTOCOL_CODEC_H
#define KCENON_FILE_TRANSFER_CORE_PROTOCOL_CODEC_H

#include "protocol_types.h"
#include "types.h"

#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace kcenon::file_transfer::wire {

// A network_system packet contains exactly one FTS frame. Never serialize C++
// object layouts: the protocol uses fixed-width, big-endian integer fields.
inline constexpr std::size_t max_payload_size = 16 * 1024 * 1024;

#define FT_FIELDS(Type, ...) \
    template<class Archive> void fields(Archive& a, Type& v) { a(__VA_ARGS__); }
FT_FIELDS(protocol_version, v.major, v.minor, v.patch, v.build)
FT_FIELDS(msg_connect, v.version, v.capabilities, v.client_id)
FT_FIELDS(msg_connect_ack, v.version, v.capabilities, v.session_id,
          v.max_chunk_size, v.max_file_size, v.server_name)
FT_FIELDS(msg_heartbeat, v.timestamp, v.sequence)
FT_FIELDS(msg_disconnect, v.reason_code, v.message)
template<class Archive> void fields(Archive& a, msg_upload_request& v) {
    a(v.transfer_id, v.filename, v.file_size, v.sha256_hash, v.compression, v.encryption, v.options, v.resume_from);
    if (v.encryption != wire_encryption_algorithm::none) a(v.encryption_salt, v.auth_tag);
    if constexpr (Archive::reading) { if(a.remaining()) a(v.resumable, v.resuming, v.resume_chunk_size, v.resume_hash); }
    else if(v.resumable) a(v.resumable, v.resuming, v.resume_chunk_size, v.resume_hash);
}
template<class Archive> void fields(Archive& a, msg_upload_accept& v) {
    a(v.transfer_id, v.compression, v.encryption, v.chunk_size, v.resume_offset);
    if (v.encryption != wire_encryption_algorithm::none) a(v.encryption_salt, v.auth_tag);
    if constexpr (Archive::reading) { if(a.remaining()) a(v.resumable); }
    else if(v.resumable) a(v.resumable);
}
FT_FIELDS(msg_upload_reject, v.transfer_id, v.reason_code, v.message)
template<class Archive> void fields(Archive& a, msg_upload_complete& v) {
    a(v.transfer_id, v.total_chunks, v.bytes_sent, v.bytes_on_wire);
    if constexpr (Archive::reading) { if (a.remaining()) a(v.auth_tag); }
    else if (std::any_of(v.auth_tag.begin(), v.auth_tag.end(), [](auto b) { return b != 0; })) a(v.auth_tag);
}
template<class Archive> void fields(Archive& a, msg_upload_ack& v) {
    a(v.transfer_id, v.verified, v.stored_path);
    if constexpr (Archive::reading) { if (a.remaining()) a(v.auth_tag); }
    else if (std::any_of(v.auth_tag.begin(), v.auth_tag.end(), [](auto b) { return b != 0; })) a(v.auth_tag);
}
template<class Archive> void fields(Archive& a, msg_download_request& v) {
    a(v.transfer_id, v.filename, v.compression, v.encryption, v.resume_from);
    if (v.encryption != wire_encryption_algorithm::none) a(v.encryption_salt, v.auth_tag);
    if constexpr (Archive::reading) { if(a.remaining()) a(v.resumable, v.resuming, v.resume_chunk_size, v.resume_hash); }
    else if(v.resumable) a(v.resumable, v.resuming, v.resume_chunk_size, v.resume_hash);
}
template<class Archive> void fields(Archive& a, msg_download_accept& v) {
    a(v.transfer_id, v.file_size, v.sha256_hash, v.compression, v.encryption, v.chunk_size, v.total_chunks, v.resume_offset, v.modified_time);
    if (v.encryption != wire_encryption_algorithm::none) a(v.encryption_salt, v.auth_tag);
    if constexpr (Archive::reading) { if(a.remaining()) a(v.resumable); }
    else if(v.resumable) a(v.resumable);
}
FT_FIELDS(msg_download_reject, v.transfer_id, v.reason_code, v.message)
template<class Archive> void fields(Archive& a, msg_download_complete& v) {
    a(v.transfer_id, v.total_chunks, v.bytes_sent, v.bytes_on_wire);
    if constexpr (Archive::reading) { if (a.remaining()) a(v.auth_tag); }
    else if (std::any_of(v.auth_tag.begin(), v.auth_tag.end(), [](auto b) { return b != 0; })) a(v.auth_tag);
}
template<class Archive> void fields(Archive& a, msg_download_ack& v) {
    a(v.transfer_id, v.verified, v.bytes_received);
    if constexpr (Archive::reading) { if (a.remaining()) a(v.auth_tag); }
    else if (std::any_of(v.auth_tag.begin(), v.auth_tag.end(), [](auto b) { return b != 0; })) a(v.auth_tag);
}
FT_FIELDS(msg_list_request, v.request_id, v.pattern, v.offset, v.limit, v.sort_by, v.sort_order)
FT_FIELDS(file_entry, v.filename, v.file_size, v.sha256_hash, v.created_time, v.modified_time)
template<class Archive> void fields(Archive& a, msg_chunk_ack& v) {
    a(v.transfer_id, v.chunk_index);
    if constexpr (Archive::reading) { if (a.remaining()) a(v.auth_tag); }
    else if (std::any_of(v.auth_tag.begin(), v.auth_tag.end(), [](auto b) { return b != 0; })) a(v.auth_tag);
}
FT_FIELDS(msg_chunk_nack, v.transfer_id, v.chunk_index, v.reason_code, v.reserved)
FT_FIELDS(msg_transfer_cancel, v.transfer_id, v.reason_code, v.message)
FT_FIELDS(msg_transfer_pause, v.transfer_id)
FT_FIELDS(msg_transfer_resume, v.transfer_id)
FT_FIELDS(msg_resume_request, v.transfer_id, v.file_hash, v.last_chunk_index)
FT_FIELDS(msg_error, v.error_code, v.message, v.related_id)
#undef FT_FIELDS

struct writer {
    static constexpr bool reading = false;
    std::vector<uint8_t> data;

    template<class... T> void operator()(T&... values) { (put(values), ...); }
    template<class T> void put(T& value) {
        if constexpr (std::is_enum_v<T>) {
            auto raw = static_cast<std::underlying_type_t<T>>(value);
            put(raw);
        } else if constexpr (std::is_integral_v<T>) {
            using U = std::make_unsigned_t<T>;
            auto raw = static_cast<U>(value);
            for (std::size_t i = sizeof(T); i > 0; --i)
                data.push_back(static_cast<uint8_t>(raw >> ((i - 1) * 8)));
        } else {
            fields(*this, value);
        }
        if (data.size() > max_payload_size) throw std::invalid_argument("FTS payload too large");
    }
    template<class T, std::size_t N> void put(std::array<T, N>& values) {
        for (auto& value : values) put(value);
    }
    void put(std::string& value) {
        if (value.size() > UINT16_MAX) throw std::invalid_argument("FTS string too long");
        auto length = static_cast<uint16_t>(value.size());
        put(length);
        data.insert(data.end(), value.begin(), value.end());
    }
    void raw(std::vector<std::byte>& bytes) {
        if (bytes.size() > max_payload_size - data.size())
            throw std::invalid_argument("FTS payload too large");
        for (auto byte : bytes) data.push_back(std::to_integer<uint8_t>(byte));
    }
};

struct reader {
    static constexpr bool reading = true;
    std::span<const uint8_t> data;
    std::size_t position = 0;
    auto remaining() const -> std::size_t { return data.size() - position; }
    void require(std::size_t size) const {
        if (size > remaining()) throw std::invalid_argument("Truncated FTS payload");
    }
    template<class... T> void operator()(T&... values) { (get(values), ...); }
    template<class T> void get(T& value) {
        if constexpr (std::is_enum_v<T>) {
            std::underlying_type_t<T> raw{};
            get(raw);
            value = static_cast<T>(raw);
        } else if constexpr (std::is_integral_v<T>) {
            require(sizeof(T));
            std::make_unsigned_t<T> raw = 0;
            for (std::size_t i = 0; i < sizeof(T); ++i)
                raw = static_cast<decltype(raw)>((raw << 8) | data[position++]);
            value = std::bit_cast<T>(raw);
        } else {
            fields(*this, value);
        }
    }
    template<class T, std::size_t N> void get(std::array<T, N>& values) {
        for (auto& value : values) get(value);
    }
    void get(std::string& value) {
        uint16_t length{};
        get(length);
        require(length);
        value.assign(reinterpret_cast<const char*>(data.data() + position), length);
        position += length;
    }
    void raw(std::vector<std::byte>& bytes) {
        bytes.resize(remaining());
        for (auto& byte : bytes) byte = std::byte{data[position++]};
    }
};

template<class Archive> void fields(Archive& a, msg_list_response& v) {
    if constexpr (!Archive::reading) {
        if (v.entries.size() > UINT32_MAX) throw std::invalid_argument("Too many file entries");
        v.returned_count = static_cast<uint32_t>(v.entries.size());
    }
    a(v.request_id, v.total_count, v.returned_count, v.has_more);
    if constexpr (Archive::reading) {
        if (v.has_more > 1 || v.returned_count > v.total_count ||
            v.returned_count > a.remaining() / file_entry::min_serialized_size)
            throw std::invalid_argument("Invalid file-list count");
        v.entries.resize(v.returned_count);
    }
    for (auto& entry : v.entries) a(entry);
}

template<class Archive> void fields(Archive& a, chunk& v) {
    auto& h = v.header;
    a(h.id.bytes, h.chunk_index, h.chunk_offset, h.original_size,
      h.compressed_size, h.checksum, h.flags);
    for (auto& byte : h.reserved) a(byte);
    a.raw(v.data);
    if (h.compressed_size != v.data.size() ||
        (static_cast<uint8_t>(h.flags) & 0xf0) != 0 ||
        h.reserved[0] || h.reserved[1] || h.reserved[2])
        throw std::invalid_argument("Invalid chunk framing");
}

template<class Archive> void fields(Archive& a, msg_resume_response& v) {
    a(v.transfer_id, v.can_resume, v.resume_from_chunk, v.resume_from_offset);
    // The remaining payload is a sequence of big-endian missing chunk indices.
    if constexpr (Archive::reading) {
        if (a.remaining() % sizeof(uint64_t))
            throw std::invalid_argument("Invalid resume bitmap");
        v.missing_chunks.resize(a.remaining() / sizeof(uint64_t));
    }
    for (auto& index : v.missing_chunks) a(index);
}

struct frame {
    message_type type;
    std::vector<uint8_t> payload;
};

inline auto checksum(std::span<const uint8_t> bytes) -> uint16_t {
    uint16_t sum = 0;
    for (auto byte : bytes) sum = static_cast<uint16_t>(sum + byte);
    return sum;
}

inline auto decode_frame(std::span<const uint8_t> data) -> result<frame> {
    try {
        if (data.size() < frame_header::total_overhead ||
            data.size() > max_payload_size + frame_header::total_overhead)
            throw std::invalid_argument("Invalid FTS frame size");
        reader in{data};
        uint32_t magic{}, length{};
        message_type type{};
        in(magic, type, length);
        if (magic != protocol_magic || length != data.size() - frame_header::total_overhead ||
            to_string(type) == "UNKNOWN")
            throw std::invalid_argument("Invalid FTS frame header");
        in.position += length;
        uint16_t sum{}, echo{};
        in(sum, echo);
        if (sum != checksum(data.first(data.size() - 4)) || echo != static_cast<uint16_t>(length))
            throw std::invalid_argument("Invalid FTS frame checksum");
        return frame{type, {data.begin() + frame_header::size, data.end() - 4}};
    } catch (const std::invalid_argument& e) {
        return unexpected{error{error_code::invalid_configuration, e.what()}};
    }
}

// TCP receive callbacks expose stream fragments, not application messages.
// Retain at most one partial frame and extract coalesced frames independently.
class stream_decoder {
    std::vector<uint8_t> pending_;
    std::size_t frame_limit_;
public:
    explicit stream_decoder(std::size_t frame_limit = max_payload_size + frame_header::total_overhead)
        : frame_limit_(frame_limit) {}

    // Deliver each complete frame before reading another one. This lets the
    // server apply admission without collecting an uncharged vector of frames.
    template<class Consumer>
    auto feed_each(std::span<const uint8_t> input, Consumer consume) -> result<void> {
        while (!input.empty()) {
            std::size_t required = frame_header::size;
            if (pending_.size() >= frame_header::size) {
                reader header{pending_};
                uint32_t magic{}, length{}; message_type type{};
                header(magic, type, length);
                required = static_cast<std::size_t>(length) + frame_header::total_overhead;
                if (magic != protocol_magic || length > max_payload_size ||
                    required > frame_limit_ || to_string(type) == "UNKNOWN") {
                    pending_.clear();
                    return unexpected{error{error_code::invalid_configuration, "Invalid or oversized FTS stream header"}};
                }
            }
            // Reserve the exact advertised size instead of geometrically
            // growing a partial frame beyond its admission size.
            if (pending_.capacity() < required) pending_.reserve(required);
            auto take = std::min(input.size(), required - pending_.size());
            pending_.insert(pending_.end(), input.begin(), input.begin() + take);
            input = input.subspan(take);
            if (required > frame_header::size && pending_.size() == required) {
                { auto decoded = decode_frame(pending_);
                  if (!decoded) { pending_.clear(); return unexpected{decoded.error()}; } }
                auto frame = std::move(pending_); pending_ = {};
                if (!consume(std::move(frame)))
                    return unexpected{error{error_code::connection_lost, "Frame admission closed"}};
            }
        }
        return {};
    }
    auto feed(std::span<const uint8_t> input) -> result<std::vector<std::vector<uint8_t>>> {
        std::vector<std::vector<uint8_t>> frames;
        auto result = feed_each(input, [&](std::vector<uint8_t>&& frame) {
            frames.push_back(std::move(frame)); return true;
        });
        if (!result) return unexpected{result.error()};
        return frames;
    }
};

template<class T> auto decode(std::span<const uint8_t> data) -> result<T> {
    try {
        if (data.size() > max_payload_size) throw std::invalid_argument("FTS payload too large");
        T value{};
        reader in{data};
        in(value);
        if (in.remaining()) throw std::invalid_argument("Trailing FTS payload bytes");
        return value;
    } catch (const std::invalid_argument& e) {
        return unexpected{error{error_code::invalid_configuration, e.what()}};
    }
}

template<class T> auto encode(message_type type, const T& message) -> result<std::vector<uint8_t>> {
    try {
        auto value = message;
        writer payload;
        payload(value);
        writer out;
        uint32_t magic = protocol_magic;
        auto size = static_cast<uint32_t>(payload.data.size());
        out(magic, type, size);
        out.data.insert(out.data.end(), payload.data.begin(), payload.data.end());
        auto sum = checksum(out.data);
        auto echo = static_cast<uint16_t>(size);
        // Frame overhead is outside the payload limit.
        out.data.push_back(static_cast<uint8_t>(sum >> 8));
        out.data.push_back(static_cast<uint8_t>(sum));
        out.data.push_back(static_cast<uint8_t>(echo >> 8));
        out.data.push_back(static_cast<uint8_t>(echo));
        return std::move(out.data);
    } catch (const std::invalid_argument& e) {
        return unexpected{error{error_code::invalid_configuration, e.what()}};
    }
}

} // namespace kcenon::file_transfer::wire
#endif
