#pragma once

#include <result.hpp>
#include <types.hpp>

#include <optional>
#include <string_view>

namespace coinbase_ticker_statistics {

/** Decode one Coinbase message; unrelated message types return an empty success. */
[[nodiscard]] Result<std::optional<TickerUpdate>> parse_ticker_message(std::string_view message);

} // namespace coinbase_ticker_statistics
