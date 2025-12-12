/**
 * @file transfer_id.cpp
 * @brief Implementation of transfer_id generation and serialization
 */

#include "kcenon/file_transfer/core/chunk_types.h"

#include <iomanip>
#include <random>
#include <sstream>

namespace kcenon::file_transfer {

auto transfer_id::generate() -> transfer_id {
    transfer_id id;

    // Use random_device for cryptographically secure random numbers
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;

    // Generate two 64-bit random numbers
    uint64_t part1 = dis(gen);
    uint64_t part2 = dis(gen);

    // Fill the bytes array
    for (int i = 0; i < 8; ++i) {
        id.bytes[i] = static_cast<uint8_t>((part1 >> (i * 8)) & 0xFF);
        id.bytes[i + 8] = static_cast<uint8_t>((part2 >> (i * 8)) & 0xFF);
    }

    // Set version to 4 (random UUID) - RFC 4122
    id.bytes[6] = (id.bytes[6] & 0x0F) | 0x40;

    // Set variant to RFC 4122
    id.bytes[8] = (id.bytes[8] & 0x3F) | 0x80;

    return id;
}

auto transfer_id::to_string() const -> std::string {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    // Format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    for (int i = 0; i < 4; ++i) {
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    oss << '-';
    for (int i = 4; i < 6; ++i) {
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    oss << '-';
    for (int i = 6; i < 8; ++i) {
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    oss << '-';
    for (int i = 8; i < 10; ++i) {
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    oss << '-';
    for (int i = 10; i < 16; ++i) {
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }

    return oss.str();
}

auto transfer_id::from_string(std::string_view str)
    -> std::optional<transfer_id> {
    // Remove dashes and validate length
    std::string hex_str;
    hex_str.reserve(32);

    for (char c : str) {
        if (c == '-') continue;
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return std::nullopt;
        }
        hex_str += c;
    }

    if (hex_str.length() != 32) {
        return std::nullopt;
    }

    transfer_id id;
    for (std::size_t i = 0; i < 16; ++i) {
        std::string byte_str = hex_str.substr(i * 2, 2);
        id.bytes[i] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
    }

    return id;
}

}  // namespace kcenon::file_transfer
