/**
 * @file file_transfer.h
 * @brief Main header for file_trans_system library
 * @version 0.2.0
 *
 * This is the primary include file for the file_trans_system library.
 * Include this header to access all file transfer functionality.
 *
 * @code
 * #include <kcenon/file_transfer/file_transfer.h>
 *
 * using namespace kcenon::file_transfer;
 *
 * // Create a server
 * auto server = file_transfer_server::builder()
 *     .with_storage_directory("/path/to/storage")
 *     .build();
 *
 * // Create a client
 * auto client = file_transfer_client::builder()
 *     .build();
 * @endcode
 */

#ifndef KCENON_FILE_TRANSFER_FILE_TRANSFER_H
#define KCENON_FILE_TRANSFER_FILE_TRANSFER_H

#include <cstdint>
#include <string>

// Core types
#include "kcenon/file_transfer/core/types.h"

// Server
#include "kcenon/file_transfer/server/server_types.h"
#include "kcenon/file_transfer/server/file_transfer_server.h"

// Client
#include "kcenon/file_transfer/client/client_types.h"
#include "kcenon/file_transfer/client/file_transfer_client.h"

// Adapters
#include "kcenon/file_transfer/adapters/logger_adapter.h"
#include "kcenon/file_transfer/adapters/monitoring_adapter.h"
#include "kcenon/file_transfer/adapters/monitorable_adapter.h"
#include "kcenon/file_transfer/adapters/thread_pool_adapter.h"

namespace kcenon::file_transfer {

/**
 * @brief Library version information
 */
struct version {
    static constexpr int major = 0;
    static constexpr int minor = 2;
    static constexpr int patch = 0;

    /**
     * @brief Get version string
     * @return Version string in format "major.minor.patch"
     */
    static std::string to_string() {
        return std::to_string(major) + "." +
               std::to_string(minor) + "." +
               std::to_string(patch);
    }
};

}  // namespace kcenon::file_transfer

#endif  // KCENON_FILE_TRANSFER_FILE_TRANSFER_H
