#pragma once

#include "types.hpp"

namespace coinbase_ticker_statistics {

/** Runtime window rules, independent of configuration syntax and networking. */
struct WindowOptions {
    Duration duration{300};
};

} // namespace coinbase_ticker_statistics
