#pragma once
#include "types.h"

namespace kcenon::file_transfer {
// Flush file content and parent-directory changes to stable storage.
auto sync_file(const std::filesystem::path& path) -> result<void>;
auto sync_directory(const std::filesystem::path& path) -> result<void>;
auto durable_write(const std::filesystem::path& path, std::string_view contents) -> result<void>;
// Publish a verified temporary file located on the destination filesystem.
// A failed commit preserves an existing destination.
auto commit_file(const std::filesystem::path& temporary,
                 const std::filesystem::path& destination,
                 bool overwrite) -> result<void>;
}
