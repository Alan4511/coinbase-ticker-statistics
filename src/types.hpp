#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace coinbase_ticker_statistics {

using Timestamp = std::chrono::sys_time<std::chrono::nanoseconds>;
using TradeId = std::uint64_t;
/** Widest standard floating-point type; precision depends on the target ABI. */
using Price = long double;
using Statistic = Price;
using SampleCount = std::size_t;
using Duration = std::chrono::seconds;
using Symbol = std::string;
using Symbols = std::vector<Symbol>;

/** Price and identity from a received Coinbase ticker message, using exchange time. */
struct Trade {
    Timestamp exchange_time;
    Symbol symbol;
    TradeId trade_id;
    Price price;
};

/** Floating-point statistics over a nonempty observation window. */
struct Statistics {
    SampleCount count;
    Statistic mean;
    Statistic median;
    Price low;
    Price high;
};

/** Immutable during synchronous delivery; sinks must copy if they retain an update. */
struct StatisticsUpdate {
    Trade trade;
    Statistics statistics;
};

} // namespace coinbase_ticker_statistics
