#pragma once
#include <kcenon/file_transfer/client/client_types.h>
#include <algorithm>
#include <cmath>
namespace kcenon::file_transfer::detail {
inline auto valid_reconnect_policy(const reconnect_policy& policy) -> bool {
    return policy.initial_delay.count() >= 0 && policy.max_delay >= policy.initial_delay &&
        std::isfinite(policy.backoff_multiplier) && policy.backoff_multiplier >= 1.0;
}
// The scheduler supplies a cancellable clock wait and one bounded transport
// attempt. Tests supply deterministic virtual time and a scripted transport.
template<class Wait, class Attempt>
auto reconnect_sequence(const reconnect_policy& policy, std::chrono::milliseconds timeout,
                        Wait wait, Attempt attempt) -> bool {
    auto delay = policy.initial_delay;
    for (size_t index=0; index<policy.max_attempts; ++index) {
        if (!wait(delay)) return false;
        if (attempt(timeout)) return true;
        auto grown = static_cast<long double>(delay.count()) * policy.backoff_multiplier;
        delay = std::chrono::milliseconds{static_cast<int64_t>(std::min<long double>(grown, policy.max_delay.count()))};
    }
    return false;
}
} // namespace kcenon::file_transfer::detail
