#include "app/application.hpp"
#include "app/application_feed_handler.hpp"
#include <feed/ticker_feed.hpp>
#include <output/csv_sink.hpp>
#include <runtime/run_control_handle.hpp>

#include <boost/asio/signal_set.hpp>

#include <csignal>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace coinbase_ticker_statistics {
namespace {

Result<void> report_run_result(Logger &logger,
                               const FeedCounts &counts,
                               std::size_t emitted_rows,
                               Result<void> run_result,
                               std::string_view stop_reason) {
    logger.log(run_result.has_value() ? LogLevel::Info : LogLevel::Error,
               "stopped: status=",
               run_result.has_value() ? "success" : "failure",
               " received_messages=",
               counts.received_messages,
               " ticker_updates=",
               counts.ticker_updates,
               " emitted_rows=",
               emitted_rows,
               " reason=",
               run_result.has_value() ? stop_reason : std::string_view(run_result.error().message));
    return run_result;
}

void log_startup(const Config &config, Logger &logger) {
    std::string symbols;
    for (const auto &symbol : config.symbols) {
        if (!symbols.empty())
            symbols += ',';
        symbols += symbol;
    }
    logger.log(LogLevel::Info,
               "starting: symbols=",
               symbols,
               " window_seconds=",
               config.window.duration.count(),
               " max_observations_per_symbol=",
               config.window.max_observations_per_symbol);
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

/** Own the run and its policy. Handle/control outlive even abandoned I/O handlers. */
class Application {
  public:
    Application(const Config &config, Logger &logger, StatisticsProcessor processor)
        : logger_(logger), control_([this] {
              logger_.log(LogLevel::Info, "shutdown requested: reason=", control_.stop_reason());
              if (feed_)
                  feed_->stop();
          }),
          control_handle_(control_), signals_(io_, SIGINT, SIGTERM), sink_(io_, config.output, control_handle_),
          handler_(std::move(processor), sink_, *this) {
    }

    Result<void> run(const Config &config) {
        auto created = Feed::create(io_, config.feed, config.symbols, handler_, control_handle_);
        if (!created.has_value())
            return std::unexpected(std::move(created.error()));
        feed_ = std::move(created.value());
        // Assign the feed before starting operations that can request shutdown through the run-control handle.
        log_startup(config, logger_);
        try {
            signals_.async_wait([this](boost::system::error_code error, int signal) {
                if (!error && (signal == SIGINT || signal == SIGTERM))
                    control_handle_.request_stop(signal == SIGINT ? "SIGINT" : "SIGTERM");
            });
            if (auto started = feed_->start(); !started.has_value())
                control_handle_.fail(std::move(started.error()));
            io_.run();
        } catch (const std::exception &error) {
            abort_run(error);
        }
        return report_run_result(logger_,
                                 feed_->counts(),
                                 handler_.emitted_rows(),
                                 control_.result(),
                                 control_.stop_reason());
    }

    Result<void> on_connected() {
        logger_.log(LogLevel::Info,
                    "connected: protocol=wss tls_verification=enabled channel=ticker subscription=sent");
        // Preserve previous output until the feed has connected and subscribed.
        if (auto opened = sink_.open(); !opened.has_value())
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

    void on_stopped(Result<void> completion) {
        control_handle_.on_stopped(std::move(completion));
        // Normal cleanup: close output and cancel waits so the event loop can drain.
        if (auto closed = sink_.close(); !closed.has_value()) {
            logger_.log(LogLevel::Error, "output cleanup failed: ", closed.error().message);
            control_handle_.fail(std::move(closed.error()));
        }
        boost::system::error_code error;
        signals_.cancel(error);
        if (error)
            control_handle_.fail(Error{ErrorCode::Transport, "signal cancellation: " + error.message()});
    }

  private:
    void abort_run(const std::exception &error) {
        // Emergency cleanup abandons pending I/O, including an in-progress close handshake.
        control_.complete(fail(ErrorCode::UnexpectedFailure, error.what()));
        io_.stop();
        // The feed completion handler may never run. Re-closing an already closed sink is safe.
        if (auto closed = sink_.close(); !closed.has_value()) {
            logger_.log(LogLevel::Error, "output cleanup failed: ", closed.error().message);
            control_handle_.fail(std::move(closed.error()));
        }
    }

    using Handler = ApplicationFeedHandler<CsvSink, Application>;
    using Feed = TickerFeed<Handler>;

    Logger &logger_;
    RunControl control_;
    RunControlHandle control_handle_;
    // Reverse destruction releases all I/O owners/queued callbacks before their run-control handle.
    boost::asio::io_context io_;
    boost::asio::signal_set signals_;
    CsvSink sink_;
    Handler handler_;
    std::unique_ptr<Feed> feed_;
};

} // namespace

Result<void> run_application(const Config &config, Logger &logger) {
    auto processor = StatisticsProcessor::create(config.symbols, config.window);
    if (!processor.has_value())
        return std::unexpected(std::move(processor.error()));
    Application application(config, logger, std::move(processor.value()));
    return application.run(config);
}

} // namespace coinbase_ticker_statistics
