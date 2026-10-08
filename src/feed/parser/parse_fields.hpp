#pragma once

#include <common/result.hpp>
#include <common/types.hpp>

#include <string_view>

namespace coinbase_ticker_statistics {
/** Parse an exact nonnegative plain decimal price on the eight-decimal tick grid. */
[[nodiscard]] Result<Price> parse_price(std::string_view text);

/**
 * Parse a UTC ISO-8601 timestamp (1970..2200) with up to nine fractional digits.
 * @return A timestamp, or InvalidInput for unsupported syntax, range, or calendar dates.
 */
[[nodiscard]] Result<Timestamp> parse_utc_timestamp(std::string_view text);

} // namespace coinbase_ticker_statistics
