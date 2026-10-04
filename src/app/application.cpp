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

struct RunState {
    Result<void> result;
    std::string_view stop_reason{"peer closed connection"};

    void record_failure(Result<void> completion) {
        if (result && !completion)
            result = std::move(completion);
    }
};

Result<void> open_csv_output(CsvSink &sink, Logger &logger) {
    logger.log(LogLevel::Info, "connected: protocol=wss tls_verification=enabled channel=ticker subscription=sent");
    // Preserve previous output until the feed has connected and subscribed.
    if (auto opened = sink.open(); !opened)
        return opened;
    const auto &output = sink.config();
    logger.log(LogLevel::Info,
               "CSV ready: path=",
               output.path.string(),
               " mode=truncate flush_every_rows=",
               output.flush_every_rows,
               " flush_interval_ms=",
               output.flush_interval.count());
    return {};
}

void on_feed_stopped(CsvSink &sink, boost::asio::signal_set &signals, RunState &state, Result<void> completion) {
    state.record_failure(std::move(completion));
    // Cancel output work so its timer cannot keep the event loop alive after completion.
    state.record_failure(sink.close());
    boost::system::error_code error;
    signals.cancel(error);
    if (error)
        state.record_failure(fail(ErrorCode::Transport, "signal cancellation: " + error.message()));
}

template <FeedHandler Handler>
void on_output_error(RunState &state, TickerFeed<Handler> &feed, Error error) {
    state.record_failure(std::unexpected(std::move(error)));
    // Process policy belongs here; the sink only reports its failure.
    feed.stop();
}

template <FeedHandler Handler>
void register_signal_handler(boost::asio::signal_set &signals,
                             TickerFeed<Handler> &feed,
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
    auto processor = StatisticsProcessor::create(config.symbols, config.window);
    if (!processor)
        return std::unexpected(std::move(processor.error()));

    // These owners and the borrowed event handler live until io.run() drains.
    boost::asio::io_context io;
    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    RunState state;

    CsvSink sink(io, config.output);
    const auto on_connected = [&sink, &logger] {
        return open_csv_output(sink, logger);
    };
    const auto on_stopped = [&sink, &signals, &state](Result<void> completion) {
        on_feed_stopped(sink, signals, state, std::move(completion));
    };
    ApplicationFeedHandler handler(std::move(*processor), sink, on_connected, on_stopped);
    using Feed = TickerFeed<decltype(handler)>;
    auto created = Feed::create(io, config.feed, config.symbols, handler);
    if (!created)
        return std::unexpected(std::move(created.error()));
    auto feed = std::move(*created);
    // Bind error reporting to an existing feed, before opening the output or starting I/O.
    sink.set_flush_error_handler([&state, &feed = *feed](Error error) {
        on_output_error(state, feed, std::move(error));
    });
    log_startup(config, logger);
    try {
        register_signal_handler(signals, *feed, logger, state.stop_reason);
        if (auto started = feed->start(); !started) {
            state.record_failure(std::move(started));
            feed->stop();
        }
        io.run();
    } catch (const std::exception &error) {
        // Emergency containment: ordinary I/O failures arrive through the handler.
        state.record_failure(fail(ErrorCode::UnexpectedFailure, error.what()));
        io.stop();
    }
    return finish_run(sink, logger, feed->counts(), handler.emitted_rows(), std::move(state.result), state.stop_reason);
}

} // namespace coinbase_ticker_statistics
