#include "kcenon/file_transfer/core/file_commit.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kcenon::file_transfer {
auto commit_file(const std::filesystem::path& temporary,
                 const std::filesystem::path& destination,
                 bool overwrite) -> result<void> {
    std::error_code ec;
    if (!overwrite) {
        // Creating a link fails atomically if the destination already exists.
        std::filesystem::create_hard_link(temporary, destination, ec);
        if (!ec) {
            std::error_code cleanup;
            std::filesystem::remove(temporary, cleanup);
        }
    } else {
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            ec = std::error_code(GetLastError(), std::system_category());
#else
        std::filesystem::rename(temporary, destination, ec);
#endif
    }
    if (ec) return unexpected{error{ec == std::errc::file_exists ? error_code::file_already_exists : error_code::file_write_error,
        "Cannot commit verified file: " + ec.message()}};
    return {};
}
}
