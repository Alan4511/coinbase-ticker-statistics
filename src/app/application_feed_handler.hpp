#pragma once

#include "app/logger.hpp"
#include <feed/transport/feed_handler.hpp>
#include <output/csv_sink.hpp>
#include <statistics/statistics_processor.hpp>

#include <boost/asio/signal_set.hpp>

#include <cstddef>

namespace coinbase_ticker_statistics {

/** Route decoded ticker updates to statistics and output; borrow resources until feed operations drain. */
class ApplicationFeedHandler final {
  public:
    ApplicationFeedHandler(StatisticsProcessor processor,
                           CsvSink &sink,
                           Logger &logger,
                           boost::asio::signal_set &signals,
                           Result<void> &run_result);

    [[nodiscard]] Result<void> on_connected();
    /** Handle one received ticker update, which can represent batched matches. */
    [[nodiscard]] Result<void> on_message(const TickerUpdate &ticker_update);
    void on_stopped(Result<void> completion);

    ApplicationFeedHandler(const ApplicationFeedHandler &) = delete;
    ApplicationFeedHandler &operator=(const ApplicationFeedHandler &) = delete;

    [[nodiscard]] std::size_t emitted_rows() const noexcept;

  private:
    StatisticsProcessor processor_;
    CsvSink &sink_;
    Logger &logger_;
    boost::asio::signal_set &signals_;
    Result<void> &run_result_;
    std::size_t emitted_rows_{};
};

static_assert(FeedHandler<ApplicationFeedHandler>);

} // namespace coinbase_ticker_statistics
