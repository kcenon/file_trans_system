#pragma once

namespace kcenon::file_transfer {
// Shared by the client API and compression engine. The old spellings remain
// aliases so including both public headers no longer defines conflicting enums.
enum class compression_mode {
    none = 0, disabled = none,
    always = 1, enabled = always,
    adaptive = 2
};
enum class compression_level {
    fast = 0,
    balanced = 1, high = balanced,
    best = 2
};
}
