#pragma once

#include <boost/multiprecision/cpp_int.hpp>

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace coinbase_ticker_statistics {

using Timestamp = std::chrono::sys_time<std::chrono::nanoseconds>;
using TradeId = std::uint64_t;
using SampleCount = std::size_t;
/** Exact decimal price: one tick is 10^-8, with no implicit conversion from money amounts. */
struct Price {
    static constexpr unsigned decimal_places = 8;
    static constexpr std::int64_t ticks_per_unit = 100'000'000;
    std::int64_t ticks{};
    auto operator<=>(const Price &) const = default;
};

// A nonnegative int64 price times any supported sample count fits in 128 bits.
// Boost's fixed-width backend uses no dynamic allocation and avoids compiler extensions.
using PriceSum = boost::multiprecision::uint128_t;
static_assert(std::numeric_limits<SampleCount>::digits <= 64);

/** Exact fraction in price ticks; only output formatting rounds it to a decimal price. */
struct Statistic {
    PriceSum numerator{};
    SampleCount denominator{1};
};
using Duration = std::chrono::seconds;
using Symbol = std::string;
using Symbols = std::vector<Symbol>;

/** Price and identity from a received Coinbase ticker message, using exchange time. */
struct TickerUpdate {
    Timestamp exchange_time;
    Symbol symbol;
    TradeId trade_id;
    Price price;
};

/** Exact statistics over a nonempty observation window. */
struct Statistics {
    SampleCount count;
    Statistic mean;
    Statistic median;
    Price low;
    Price high;
};

/** Immutable during synchronous delivery; sinks must copy if they retain an update. */
struct StatisticsUpdate {
    TickerUpdate ticker_update;
    Statistics statistics;
};

} // namespace coinbase_ticker_statistics
