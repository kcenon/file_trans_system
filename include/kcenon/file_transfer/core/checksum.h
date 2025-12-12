/**
 * @file checksum.h
 * @brief Checksum utilities for data integrity verification
 */

#ifndef KCENON_FILE_TRANSFER_CORE_CHECKSUM_H
#define KCENON_FILE_TRANSFER_CORE_CHECKSUM_H

#include <kcenon/file_transfer/core/types.h>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>

namespace kcenon::file_transfer {

/**
 * @brief Checksum utilities for CRC32 and SHA-256 calculations
 *
 * Provides static methods for:
 * - CRC32 calculation for chunk integrity verification
 * - SHA-256 calculation for file integrity verification
 */
class checksum {
public:
    /**
     * @brief Calculate CRC32 checksum of data
     * @param data Input data span
     * @return CRC32 checksum value
     */
    [[nodiscard]] static auto crc32(std::span<const std::byte> data) -> uint32_t;

    /**
     * @brief Verify CRC32 checksum of data
     * @param data Input data span
     * @param expected Expected checksum value
     * @return true if checksum matches, false otherwise
     */
    [[nodiscard]] static auto verify_crc32(
        std::span<const std::byte> data, uint32_t expected) -> bool;

    /**
     * @brief Calculate SHA-256 hash of a file
     * @param path Path to the file
     * @return SHA-256 hash as hex string, or error
     */
    [[nodiscard]] static auto sha256_file(const std::filesystem::path& path)
        -> result<std::string>;

    /**
     * @brief Verify SHA-256 hash of a file
     * @param path Path to the file
     * @param expected Expected hash as hex string
     * @return true if hash matches, false otherwise
     */
    [[nodiscard]] static auto verify_sha256(
        const std::filesystem::path& path, const std::string& expected) -> bool;

    /**
     * @brief Calculate SHA-256 hash of data
     * @param data Input data span
     * @return SHA-256 hash as hex string
     */
    [[nodiscard]] static auto sha256(std::span<const std::byte> data) -> std::string;

private:
    // CRC32 lookup table
    static const uint32_t crc32_table[256];

    // Initialize CRC32 table
    static auto init_crc32_table() -> const uint32_t*;
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_CORE_CHECKSUM_H
