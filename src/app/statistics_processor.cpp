#include "app/statistics_processor.hpp"

#include <utility>

namespace coinbase_ticker_statistics {

Result<StatisticsProcessor> StatisticsProcessor::create(const Symbols &symbols, WindowOptions options) {
    if (symbols.empty()) {
        return fail(ErrorCode::InvalidConfiguration, "at least one symbol is required");
    }
    StatisticsProcessor processor;
    processor.windows_.reserve(symbols.size());
    for (const auto &symbol : symbols) {
        auto window = SlidingWindow::create(options);
        if (!window) {
            return std::unexpected(std::move(window.error()));
        }
        if (symbol.empty())
            return fail(ErrorCode::InvalidConfiguration, "symbols must be nonempty and unique");
        const auto [window_position, is_new_symbol] = processor.windows_.try_emplace(symbol, std::move(*window));
        if (!is_new_symbol)
            return fail(ErrorCode::InvalidConfiguration, "symbols must be nonempty and unique");
    }
    return processor;
}

} // namespace coinbase_ticker_statistics
