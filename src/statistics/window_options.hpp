#pragma once

#include <common/result.hpp>
#include <common/types.hpp>

#include <chrono>

namespace coinbase_ticker_statistics {

/** Runtime window rules, independent of configuration syntax and networking. */
struct WindowOptions {
    Duration duration{300};
};

/** Supported windows span 1 second to 365 days, safely representable in nanoseconds. */
[[nodiscard]] inline Result<void> validate(const WindowOptions &options) {
    constexpr auto maximum_duration = std::chrono::days{365};
    if (options.duration <= Duration::zero() || options.duration > maximum_duration)
        return fail(ErrorCode::InvalidConfiguration, "Window duration must be between 1 second and 365 days");
    return {};
}

} // namespace coinbase_ticker_statistics
