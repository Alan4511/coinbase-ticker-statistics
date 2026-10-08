#pragma once

#include "result.hpp"
#include "types.hpp"

#include <string>

namespace coinbase_ticker_statistics {

/** Render UTC with nine fractional digits; years outside 1970..2200 return OutOfRange. */
[[nodiscard]] Result<std::string> format_utc_timestamp(Timestamp timestamp);

} // namespace coinbase_ticker_statistics
