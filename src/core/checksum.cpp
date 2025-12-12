/**
 * @file checksum.cpp
 * @brief Implementation of checksum utilities
 */

#include <kcenon/file_transfer/core/checksum.h>

#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace kcenon::file_transfer {

namespace {

// CRC32 polynomial (IEEE 802.3)
constexpr uint32_t CRC32_POLYNOMIAL = 0xEDB88320;

// Generate CRC32 lookup table at compile time
constexpr auto generate_crc32_table() -> std::array<uint32_t, 256> {
    std::array<uint32_t, 256> table{};

    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }

    return table;
}

// CRC32 lookup table (generated at compile time)
constexpr auto CRC32_TABLE = generate_crc32_table();

// SHA-256 constants
constexpr std::array<uint32_t, 64> SHA256_K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

// SHA-256 initial hash values
constexpr std::array<uint32_t, 8> SHA256_H0 = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                               0xa54ff53a, 0x510e527f, 0x9b05688c,
                                               0x1f83d9ab, 0x5be0cd19};

// Right rotate
constexpr auto rotr(uint32_t x, int n) -> uint32_t {
    return (x >> n) | (x << (32 - n));
}

// SHA-256 compression function
void sha256_transform(std::array<uint32_t, 8>& state, const uint8_t* block) {
    std::array<uint32_t, 64> w{};

    // Prepare message schedule
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(block[i * 4 + 3]);
    }

    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    // Initialize working variables
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    // Main loop
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + SHA256_K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    // Add compressed chunk to current hash value
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

// SHA-256 hash computation
auto compute_sha256(const uint8_t* data, std::size_t length) -> std::array<uint8_t, 32> {
    std::array<uint32_t, 8> state = SHA256_H0;
    std::array<uint8_t, 64> block{};
    std::size_t block_pos = 0;

    // Process complete blocks
    for (std::size_t i = 0; i < length; ++i) {
        block[block_pos++] = data[i];
        if (block_pos == 64) {
            sha256_transform(state, block.data());
            block_pos = 0;
        }
    }

    // Padding
    uint64_t bit_length = length * 8;
    block[block_pos++] = 0x80;

    if (block_pos > 56) {
        while (block_pos < 64) {
            block[block_pos++] = 0;
        }
        sha256_transform(state, block.data());
        block_pos = 0;
    }

    while (block_pos < 56) {
        block[block_pos++] = 0;
    }

    // Append length (big-endian)
    for (int i = 7; i >= 0; --i) {
        block[block_pos++] = static_cast<uint8_t>(bit_length >> (i * 8));
    }

    sha256_transform(state, block.data());

    // Produce final hash value (big-endian)
    std::array<uint8_t, 32> hash{};
    for (int i = 0; i < 8; ++i) {
        hash[i * 4] = static_cast<uint8_t>(state[i] >> 24);
        hash[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
        hash[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
        hash[i * 4 + 3] = static_cast<uint8_t>(state[i]);
    }

    return hash;
}

// Convert hash to hex string
auto hash_to_hex(const std::array<uint8_t, 32>& hash) -> std::string {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t byte : hash) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

}  // namespace

auto checksum::crc32(std::span<const std::byte> data) -> uint32_t {
    uint32_t crc = 0xFFFFFFFF;

    for (std::byte b : data) {
        uint8_t index = static_cast<uint8_t>(crc ^ static_cast<uint8_t>(b));
        crc = CRC32_TABLE[index] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

auto checksum::verify_crc32(std::span<const std::byte> data, uint32_t expected) -> bool {
    return crc32(data) == expected;
}

auto checksum::sha256_file(const std::filesystem::path& path) -> result<std::string> {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return unexpected(
            error{error_code::file_not_found, "cannot open file: " + path.string()});
    }

    std::array<uint32_t, 8> state = SHA256_H0;
    std::array<uint8_t, 64> block{};
    uint64_t total_bytes = 0;

    while (file) {
        file.read(reinterpret_cast<char*>(block.data()), 64);
        auto bytes_read = file.gcount();
        total_bytes += bytes_read;

        if (bytes_read == 64) {
            sha256_transform(state, block.data());
        } else {
            // Final block with padding
            std::size_t block_pos = static_cast<std::size_t>(bytes_read);
            uint64_t bit_length = total_bytes * 8;

            block[block_pos++] = 0x80;

            if (block_pos > 56) {
                while (block_pos < 64) {
                    block[block_pos++] = 0;
                }
                sha256_transform(state, block.data());
                block_pos = 0;
                block.fill(0);
            }

            while (block_pos < 56) {
                block[block_pos++] = 0;
            }

            // Append length (big-endian)
            for (int i = 7; i >= 0; --i) {
                block[block_pos++] = static_cast<uint8_t>(bit_length >> (i * 8));
            }

            sha256_transform(state, block.data());
            break;
        }
    }

    // Produce final hash
    std::array<uint8_t, 32> hash{};
    for (int i = 0; i < 8; ++i) {
        hash[i * 4] = static_cast<uint8_t>(state[i] >> 24);
        hash[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
        hash[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
        hash[i * 4 + 3] = static_cast<uint8_t>(state[i]);
    }

    return hash_to_hex(hash);
}

auto checksum::verify_sha256(const std::filesystem::path& path, const std::string& expected)
    -> bool {
    auto result = sha256_file(path);
    if (!result) {
        return false;
    }
    return result.value() == expected;
}

auto checksum::sha256(std::span<const std::byte> data) -> std::string {
    auto hash = compute_sha256(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    return hash_to_hex(hash);
}

}  // namespace kcenon::file_transfer
