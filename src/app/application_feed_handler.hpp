#pragma once

#include <feed/transport/feed_handler.hpp>
#include <output/sink.hpp>
#include <statistics/statistics_processor.hpp>

#include <cstddef>
#include <functional>
#include <utility>

namespace coinbase_ticker_statistics {

/** Route ticker updates into statistics and any OutputSink.
 * Lifecycle callbacks report to application orchestration; they do not belong to the sink contract.
 * The sink and callback targets must outlive pending feed operations; callbacks must be nonempty.
 */
template <OutputSink Sink>
class ApplicationFeedHandler final {
  public:
    ApplicationFeedHandler(StatisticsProcessor processor,
                           Sink &sink,
                           std::function<Result<void>()> on_connected,
                           std::function<void(Result<void>)> on_stopped)
        : processor_(std::move(processor)), sink_(sink), on_connected_(std::move(on_connected)),
          on_stopped_(std::move(on_stopped)) {
    }

    [[nodiscard]] Result<void> on_connected() {
        return on_connected_();
    }

    /** Handle one received ticker update, which can represent batched matches. */
    [[nodiscard]] Result<void> on_message(const TickerUpdate &ticker_update) {
        auto update = processor_.on_update(ticker_update);
        if (!update)
            return std::unexpected(std::move(update.error()));
        if (!*update)
            return {};
        if (auto written = sink_.write_statistics(**update); !written)
            return written;
        ++emitted_rows_;
        return {};
    }

    void on_stopped(Result<void> completion) {
        on_stopped_(std::move(completion));
    }

    ApplicationFeedHandler(const ApplicationFeedHandler &) = delete;
    ApplicationFeedHandler &operator=(const ApplicationFeedHandler &) = delete;

    [[nodiscard]] std::size_t emitted_rows() const noexcept {
        return emitted_rows_;
    }

  private:
    StatisticsProcessor processor_;
    Sink &sink_;
    std::function<Result<void>()> on_connected_;
    std::function<void(Result<void>)> on_stopped_;
    std::size_t emitted_rows_{};
};

} // namespace coinbase_ticker_statistics
