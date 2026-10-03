#pragma once

#include "output/sink.hpp"
#include "statistics/sliding_window.hpp"
#include "statistics/window_options.hpp"
#include "types.hpp"

#include <unordered_map>

namespace coinbase_ticker_statistics {

/** Owns per-symbol windows; each call borrows a sink only for synchronous delivery. */
class StatisticsProcessor {
  public:
    /** Validate symbol routing; the caller retains ownership of the sink. */
    [[nodiscard]] static Result<StatisticsProcessor> create(const Symbols &symbols, WindowOptions options);
    /** Update and publish; successful false means filtered, unexpected means failure. */
    template <OutputSink Sink>
    [[nodiscard]] Result<bool> process_trade(const Trade &trade, Sink &sink);

  private:
    StatisticsProcessor() = default;
    std::unordered_map<Symbol, SlidingWindow> windows_;
};

template <OutputSink Sink>
Result<bool> StatisticsProcessor::process_trade(const Trade &trade, Sink &sink) {
    const auto window_position = windows_.find(trade.symbol);
    if (window_position == windows_.end()) {
        return false;
    }
    const auto updated_statistics = window_position->second.add_trade(trade);
    if (!updated_statistics) {
        return std::unexpected(updated_statistics.error());
    }
    if (!updated_statistics->has_value()) {
        return false;
    }
    const StatisticsUpdate update{trade, updated_statistics->value()};
    return sink.write_statistics(update).transform([] {
        return true;
    });
}

} // namespace coinbase_ticker_statistics
