#pragma once

#include <feed/transport/feed_handler.hpp>
#include <output/sink.hpp>
#include <statistics/statistics_processor.hpp>

#include <cstddef>
#include <expected>
#include <utility>

namespace coinbase_ticker_statistics {

/** Route ticker updates into statistics and any OutputSink.
 * The explicit lifecycle owner coordinates startup/cleanup separately from sink delivery.
 * The sink and lifecycle owner must outlive pending feed operations.
 */
template <OutputSink Sink, typename Lifecycle>
class ApplicationFeedHandler final {
  public:
    ApplicationFeedHandler(StatisticsProcessor processor, Sink &sink, Lifecycle &lifecycle)
        : processor_(std::move(processor)), sink_(sink), lifecycle_(lifecycle) {
    }

    [[nodiscard]] Result<void> on_connected(ExecutionContext &context) {
        return lifecycle_.on_connected(context);
    }

    /** Handle one received ticker update, which can represent batched matches. */
    [[nodiscard]] Result<void> on_message(ExecutionContext &, const TickerUpdate &ticker_update) {
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

    void on_stopped(ExecutionContext &context, Result<void> completion) {
        lifecycle_.on_stopped(context, std::move(completion));
    }

    ApplicationFeedHandler(const ApplicationFeedHandler &) = delete;
    ApplicationFeedHandler &operator=(const ApplicationFeedHandler &) = delete;

    [[nodiscard]] std::size_t emitted_rows() const noexcept {
        return emitted_rows_;
    }

  private:
    StatisticsProcessor processor_;
    Sink &sink_;
    Lifecycle &lifecycle_;
    std::size_t emitted_rows_{};
};

} // namespace coinbase_ticker_statistics
