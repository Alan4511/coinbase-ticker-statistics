#pragma once

#include <result.hpp>
#include <types.hpp>

#include <chrono>

namespace coinbase_ticker_statistics {

/** Runtime window rules, independent of configuration syntax and networking. */
struct WindowOptions {
    Duration duration{300};
};

/** Window arithmetic requires a positive duration representable in nanoseconds. */
[[nodiscard]] inline Result<void> validate_window_options(const WindowOptions &options) {
    const auto maximum_duration = std::chrono::duration_cast<Duration>(std::chrono::nanoseconds::max());
    if (options.duration <= Duration::zero() || options.duration > maximum_duration)
        return fail(ErrorCode::InvalidConfiguration,
                    "Window duration must be positive and representable in nanoseconds");
    return {};
}

} // namespace coinbase_ticker_statistics
