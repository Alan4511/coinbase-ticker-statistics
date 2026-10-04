#pragma once

#include "statistics/sliding_window.hpp"
#include "statistics/window_options.hpp"
#include <result.hpp>
#include <types.hpp>

#include <optional>
#include <unordered_map>

namespace coinbase_ticker_statistics {

/** Own per-symbol windows and return typed statistics without depending on I/O. */
class StatisticsProcessor {
  public:
    /** Validate symbol routing and window settings. */
    [[nodiscard]] static Result<StatisticsProcessor> create(const Symbols &symbols, WindowOptions options);
    /** Update a symbol's window; empty means filtered, unexpected means failure. */
    [[nodiscard]] Result<std::optional<StatisticsUpdate>> on_update(const TickerUpdate &ticker_update);

  private:
    StatisticsProcessor() = default;
    std::unordered_map<Symbol, SlidingWindow> windows_;
};

} // namespace coinbase_ticker_statistics
