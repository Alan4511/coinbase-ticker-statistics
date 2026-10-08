#pragma once

#include <common/result.hpp>
#include <common/types.hpp>

#include <string>

namespace coinbase_ticker_statistics {
/** Validate nonempty Coinbase product IDs without constructing a wire message. */
[[nodiscard]] Result<void> validate(const Symbols &symbols);

/** Validate and serialize an unauthenticated ticker-only subscription. */
[[nodiscard]] Result<std::string> encode_ticker_subscription(const Symbols &symbols);

} // namespace coinbase_ticker_statistics
