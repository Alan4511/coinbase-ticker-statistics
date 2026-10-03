#pragma once

#include "result.hpp"
#include "types.hpp"

#include <string>

namespace coinbase_ticker_statistics {
/** Validate nonempty Coinbase product IDs without constructing a wire message. */
[[nodiscard]] Result<void> validate_subscription(const Symbols &symbols);

/** Validate and serialize an unauthenticated ticker-only subscription. */
[[nodiscard]] Result<std::string> make_subscription(const Symbols &symbols);

} // namespace coinbase_ticker_statistics
