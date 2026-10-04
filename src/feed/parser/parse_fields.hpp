#pragma once

#include <result.hpp>
#include <types.hpp>

#include <string_view>

namespace coinbase_ticker_statistics {
/** Parse a finite, nonnegative decimal/scientific price without narrowing through double. */
[[nodiscard]] Result<Price> parse_price(std::string_view text);

/**
 * Parse a UTC ISO-8601 timestamp (1970..2200) with up to nine fractional digits.
 * @return A timestamp, or InvalidInput for unsupported syntax, range, or calendar dates.
 */
[[nodiscard]] Result<Timestamp> parse_utc_timestamp(std::string_view text);

} // namespace coinbase_ticker_statistics
