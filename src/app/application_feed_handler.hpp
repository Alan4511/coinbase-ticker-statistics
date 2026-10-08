#pragma once

#include <feed/feed_handler.hpp>
#include <output/sink.hpp>
#include <statistics/statistics_processor.hpp>

#include <cstddef>
#include <expected>
#include <utility>

namespace coinbase_ticker_statistics {

/** Route ticker updates into statistics and any OutputSink.
 * Bound to one application at construction, so callbacks need no run-control handle argument.
 * The application coordinates startup and cleanup separately from sink delivery.
 * The sink and application must outlive pending feed operations.
 */
template <OutputSink Sink, typename Application>
class ApplicationFeedHandler final {
  public:
    ApplicationFeedHandler(StatisticsProcessor processor, Sink &sink, Application &application)
        : processor_(std::move(processor)), sink_(sink), application_(application) {
    }

    [[nodiscard]] Result<void> on_connected() {
        return application_.on_connected();
    }

    /** Handle one received ticker update, which can represent batched matches. */
    [[nodiscard]] Result<void> on_message(const TickerUpdate &ticker_update) {
        auto update = processor_.on_update(ticker_update);
        if (!update.has_value())
            return std::unexpected(std::move(update.error()));
        if (!update.value().has_value())
            return {};
        if (auto written = sink_.write_statistics(update.value().value()); !written.has_value())
            return written;
        ++emitted_rows_;
        return {};
    }

    void on_stopped(Result<void> completion) {
        application_.on_stopped(std::move(completion));
    }

    ApplicationFeedHandler(const ApplicationFeedHandler &) = delete;
    ApplicationFeedHandler &operator=(const ApplicationFeedHandler &) = delete;

    [[nodiscard]] std::size_t emitted_rows() const noexcept {
        return emitted_rows_;
    }

  private:
    StatisticsProcessor processor_;
    Sink &sink_;
    Application &application_;
    std::size_t emitted_rows_{};
};

} // namespace coinbase_ticker_statistics
