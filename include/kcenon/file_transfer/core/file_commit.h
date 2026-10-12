#pragma once
#include "types.h"

namespace kcenon::file_transfer {
// Publish a verified temporary file located on the destination filesystem.
// A failed commit preserves an existing destination.
auto commit_file(const std::filesystem::path& temporary,
                 const std::filesystem::path& destination,
                 bool overwrite) -> result<void>;
}
