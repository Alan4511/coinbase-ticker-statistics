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

/** Own the run and its policy. Context/control outlive even abandoned I/O handlers. */
class Application {
  public:
    Application(const Config &config, Logger &logger, StatisticsProcessor processor)
        : logger_(logger), control_([this] {
              logger_.log(LogLevel::Info, "shutdown requested: reason=", control_.stop_reason());
              feed_->stop();
          }),
          context_(control_), signals_(io_, SIGINT, SIGTERM), sink_(io_, config.output, context_),
          handler_(std::move(processor), sink_, *this) {
    }

    Result<void> run(const Config &config) {
        auto created = Feed::create(io_, config.feed, config.symbols, handler_, context_);
        if (!created)
            return std::unexpected(std::move(created.error()));
        feed_ = std::move(*created);
        // No operation can report through the context until all dependencies exist.
        log_startup(config, logger_);
        try {
            signals_.async_wait([this](boost::system::error_code error, int signal) {
                if (!error)
                    context_.request_stop(signal == SIGINT ? "SIGINT" : "SIGTERM");
            });
            if (auto started = feed_->start(); !started)
                context_.fail(std::move(started.error()));
            io_.run();
        } catch (const std::exception &error) {
            // Emergency containment: ordinary I/O failures arrive through the handler.
            control_.complete(fail(ErrorCode::UnexpectedFailure, error.what()));
            io_.stop();
        }
        return finish_run(sink_,
                          logger_,
                          feed_->counts(),
                          handler_.emitted_rows(),
                          control_.result(),
                          control_.stop_reason());
    }

    Result<void> on_connected(ExecutionContext &) {
        logger_.log(LogLevel::Info,
                    "connected: protocol=wss tls_verification=enabled channel=ticker subscription=sent");
        // Preserve previous output until the feed has connected and subscribed.
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

    void on_stopped(ExecutionContext &context, Result<void> completion) {
        context.on_stopped(std::move(completion));
        // Cancel output work so its timer cannot keep the loop alive after completion.
        if (auto closed = sink_.close(); !closed)
            context.fail(std::move(closed.error()));
        boost::system::error_code error;
        signals_.cancel(error);
        if (error)
            context.fail(Error{ErrorCode::Transport, "signal cancellation: " + error.message()});
    }

  private:
    using Handler = ApplicationFeedHandler<CsvSink, Application>;
    using Feed = TickerFeed<Handler>;

    Logger &logger_;
    RunControl control_;
    ExecutionContext context_;
    // Reverse destruction releases all I/O owners/queued callbacks before their context.
    boost::asio::io_context io_;
    boost::asio::signal_set signals_;
    CsvSink sink_;
    Handler handler_;
    std::unique_ptr<Feed> feed_;
};

} // namespace

Result<void> run_application(const Config &config, Logger &logger) {
    auto processor = StatisticsProcessor::create(config.symbols, config.window);
    if (!processor)
        return std::unexpected(std::move(processor.error()));
    Application application(config, logger, std::move(*processor));
    return application.run(config);
}

} // namespace coinbase_ticker_statistics
