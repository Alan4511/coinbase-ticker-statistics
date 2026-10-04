#include "app/application.hpp"
#include "app/application_feed_handler.hpp"
#include <feed/transport/ticker_feed.hpp>
#include <output/csv_sink.hpp>

#include <boost/asio/signal_set.hpp>

#include <csignal>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace coinbase_ticker_statistics {
namespace {

void register_signal_handler(boost::asio::signal_set &signals,
                             TickerFeed &feed,
                             Logger &logger,
                             std::string_view &stop_reason) {
    const auto on_signal = [&logger, &feed, &stop_reason](boost::system::error_code error, int signal) {
        if (error)
            return;
        stop_reason = signal == SIGINT ? "SIGINT" : "SIGTERM";
        logger.log(LogLevel::Info, "shutdown requested: reason=", stop_reason);
        feed.stop();
    };
    signals.async_wait(on_signal);
}

Result<void> finish_run(CsvSink &sink,
                        Logger &logger,
                        const FeedCounts &counts,
                        std::size_t emitted_rows,
                        Result<void> run_result,
                        std::string_view stop_reason) {
    // Also covers exceptions that stop the event loop before the feed's completion handler.
    if (auto closed = sink.close(); !closed) {
        logger.log(LogLevel::Error, "output cleanup failed: ", closed.error().message);
        if (run_result)
            run_result = std::move(closed);
    }
    logger.log(run_result ? LogLevel::Info : LogLevel::Error,
               "stopped: status=",
               run_result ? "success" : "failure",
               " received_messages=",
               counts.received_messages,
               " ticker_updates=",
               counts.ticker_updates,
               " emitted_rows=",
               emitted_rows,
               " reason=",
               run_result ? stop_reason : std::string_view(run_result.error().message));
    return run_result;
}

void log_startup(const Config &config, Logger &logger) {
    std::string symbols;
    for (const auto &symbol : config.symbols) {
        if (!symbols.empty())
            symbols += ',';
        symbols += symbol;
    }
    logger.log(LogLevel::Info, "starting: symbols=", symbols, " window_seconds=", config.window.duration.count());
    logger.log(LogLevel::Info,
               "connecting: host=",
               config.feed.host,
               " port=",
               config.feed.port,
               " target=",
               config.feed.target,
               " max_message_bytes=",
               config.feed.max_message_bytes,
               " connect_timeout_seconds=",
               config.feed.connect_timeout.count(),
               " close_timeout_seconds=",
               config.feed.close_timeout.count());
}

} // namespace

Result<void> run_application(const Config &config, Logger &logger) {
    if (auto valid = validate_config(config); !valid)
        return valid;
    auto processor = StatisticsProcessor::create(config.symbols, config.window);
    if (!processor)
        return std::unexpected(std::move(processor.error()));

    // These owners and the borrowed event handler live until io.run() drains.
    boost::asio::io_context io;
    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    std::string_view stop_reason = "peer closed connection";
    Result<void> run_result;

    std::unique_ptr<TickerFeed> feed;
    const auto on_flush_error = [&feed, &run_result](Error error) {
        if (run_result)
            run_result = std::unexpected(std::move(error));
        feed->stop();
    };
    // The feed is assigned before any flush handler can run.
    CsvSink sink(io, config.output, on_flush_error);
    ApplicationFeedHandler handler(std::move(*processor), sink, logger, signals, run_result);
    auto created = TickerFeed::create(io, config.feed, config.symbols, handler);
    if (!created)
        return std::unexpected(std::move(created.error()));
    feed = std::move(*created);
    log_startup(config, logger);
    try {
        register_signal_handler(signals, *feed, logger, stop_reason);
        if (auto started = feed->start(); !started) {
            if (run_result)
                run_result = std::move(started);
            feed->stop();
        }
        io.run();
    } catch (const std::exception &error) {
        // Emergency containment: ordinary I/O failures arrive through the handler.
        if (run_result)
            run_result = fail(ErrorCode::UnexpectedFailure, error.what());
        io.stop();
    }
    return finish_run(sink, logger, feed->counts(), handler.emitted_rows(), std::move(run_result), stop_reason);
}

} // namespace coinbase_ticker_statistics
