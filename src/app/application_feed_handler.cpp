#include "app/application_feed_handler.hpp"

#include <utility>

namespace coinbase_ticker_statistics {

ApplicationFeedHandler::ApplicationFeedHandler(StatisticsProcessor processor,
                                               CsvSink &sink,
                                               Logger &logger,
                                               boost::asio::signal_set &signals,
                                               Result<void> &run_result)
    : processor_(std::move(processor)), sink_(sink), logger_(logger), signals_(signals), run_result_(run_result) {
}

Result<void> ApplicationFeedHandler::on_connected() {
    logger_.log(LogLevel::Info, "connected: protocol=wss tls_verification=enabled channel=ticker subscription=sent");
    // Preserve previous output if validation or network setup fails/cancels.
    if (auto opened = sink_.open(); !opened)
        return opened;
    const auto &output = sink_.config();
    logger_.log(LogLevel::Info,
                "CSV ready: path=",
                output.path.string(),
                " mode=truncate flush_every_rows=",
                output.flush_every_rows,
                " flush_interval_ms=",
                output.flush_interval.count());
    return {};
}

Result<void> ApplicationFeedHandler::on_message(const TickerUpdate &ticker_update) {
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

void ApplicationFeedHandler::on_stopped(Result<void> completion) {
    if (run_result_ && !completion)
        run_result_ = std::move(completion);
    // Cancel output work so its timer cannot keep the event loop alive after the feed stops.
    if (auto closed = sink_.close(); run_result_ && !closed)
        run_result_ = std::move(closed);
    boost::system::error_code error;
    signals_.cancel(error);
    if (error && run_result_)
        run_result_ = fail(ErrorCode::Transport, "signal cancellation: " + error.message());
}

std::size_t ApplicationFeedHandler::emitted_rows() const noexcept {
    return emitted_rows_;
}

} // namespace coinbase_ticker_statistics
