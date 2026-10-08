#include "statistics/statistics_processor.hpp"

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
        if (!window.has_value()) {
            return std::unexpected(std::move(window.error()));
        }
        if (symbol.empty())
            return fail(ErrorCode::InvalidConfiguration, "symbols must be nonempty and unique");
        const auto [window_position, is_new_symbol] = processor.windows_.try_emplace(symbol, std::move(window.value()));
        if (!is_new_symbol)
            return fail(ErrorCode::InvalidConfiguration, "symbols must be nonempty and unique");
    }
    return processor;
}

Result<std::optional<StatisticsUpdate>> StatisticsProcessor::on_update(const TickerUpdate &ticker_update) {
    const auto window_position = windows_.find(ticker_update.symbol);
    if (window_position == windows_.end())
        return std::nullopt;
    auto updated_statistics = window_position->second.add_update(ticker_update);
    if (!updated_statistics.has_value())
        return std::unexpected(std::move(updated_statistics.error()));
    if (!updated_statistics.value().has_value())
        return std::nullopt;
    return StatisticsUpdate{ticker_update, updated_statistics.value().value()};
}

} // namespace coinbase_ticker_statistics
