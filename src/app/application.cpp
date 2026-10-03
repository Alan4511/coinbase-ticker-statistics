#include "app/application.hpp"
#include "app/logger.hpp"
#include "app/statistics_processor.hpp"
#include "feed/parser/ticker_parser.hpp"
#include "feed/transport/ticker_feed.hpp"
#include "output/csv_writer.hpp"

#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>

#include <csignal>
#include <exception>
#include <fstream>
#include <memory>
#include <ostream>
#include <utility>
#include <vector>

namespace coinbase_ticker_statistics {
namespace {

using FeedConnections = std::vector<std::unique_ptr<TickerFeed>>;

/** Own one run. Callbacks borrow this object and execute on its event-loop thread. */
class Application {
  public:
    Application(const Config &config, StatisticsProcessor processor, std::ostream &diagnostics)
        : shutdown_signals_(event_loop_, SIGINT, SIGTERM), flush_timer_(event_loop_),
          csv_writer_(output_stream_, config.output.flush_every_rows), statistics_processor_(std::move(processor)),
          config_(config), diagnostics_(diagnostics) {
    }

    Result<void> run() {
        // Validate/configure every connection before creating or truncating output.
        auto prepared = configure_connections();
        if (!prepared)
            return prepared;
        auto opened = open_csv_file();
        if (!opened)
            return opened;
        auto header = csv_writer_.write_header();
        if (!header)
            return header;

        log_message(diagnostics_,
                    LogLevel::Info,
                    "starting: connections=",
                    connections_.size(),
                    " window_seconds=",
                    config_.window.duration.count(),
                    " output=",
                    config_.output.path.string(),
                    " flush_every_rows=",
                    config_.output.flush_every_rows,
                    " flush_interval_ms=",
                    config_.output.flush_interval.count());
        // Handler initiation/allocation can throw even when completion uses error_code.
        // The boundary preserves a final explicit flush for ordinary run failures.
        try {
            wait_for_shutdown_signal();
            if (config_.output.flush_every_rows > 1)
                schedule_csv_flush();
            start_connections();
            event_loop_.run();
        } catch (const std::exception &error) {
            record_first_failure(fail(ErrorCode::UnexpectedFailure, error.what()));
        }

        auto flushed = csv_writer_.flush();
        if (!flushed) {
            log_message(diagnostics_, LogLevel::Error, "output flush failed: ", flushed.error().message);
            record_first_failure(std::move(flushed));
        }
        log_message(diagnostics_,
                    LogLevel::Info,
                    "stopped: status=",
                    run_result_ ? "success" : "failure",
                    " emitted_rows=",
                    emitted_row_count_);
        return run_result_;
    }

  private:
    void wait_for_shutdown_signal() {
        const auto stop_on_signal = [this](const boost::system::error_code &error, int signal) {
            if (!error) {
                log_message(diagnostics_, LogLevel::Info, "shutdown requested: signal=", signal);
                request_shutdown();
            }
        };
        shutdown_signals_.async_wait(stop_on_signal);
    }

    void start_connections() {
        std::size_t connection_index{};
        for (const auto &feed : connections_) {
            if (shutdown_requested_)
                break;
            log_message(diagnostics_,
                        LogLevel::Info,
                        "connecting: connection=",
                        ++connection_index,
                        " host=",
                        config_.feed.host,
                        " port=",
                        config_.feed.port);
            auto started = feed->start();
            if (!started) {
                stop_after_failure(std::move(started));
                break;
            }
        }
    }

    void schedule_csv_flush() {
        static_cast<void>(flush_timer_.expires_after(config_.output.flush_interval));
        const auto publish_pending_rows = [this](const boost::system::error_code &error) {
            if (error == boost::asio::error::operation_aborted || shutdown_requested_)
                return;
            if (error) {
                stop_after_failure(fail(ErrorCode::Transport, "CSV flush timer: " + error.message()));
                return;
            }
            if (auto flushed = csv_writer_.flush(); !flushed) {
                stop_after_failure(std::move(flushed));
                return;
            }
            schedule_csv_flush();
        };
        flush_timer_.async_wait(publish_pending_rows);
    }

    Result<void> configure_connections() {
        for (const auto &connection : config_.connections) {
            const auto connection_id = connections_.size() + 1;
            FeedCallbacks callbacks;
            callbacks.connected = [this, connection_id]() -> Result<void> {
                log_message(diagnostics_, LogLevel::Info, "subscription sent: connection=", connection_id);
                return {};
            };
            callbacks.message = [this](std::string_view message) {
                return process_feed_message(message);
            };
            callbacks.stopped = [this, connection_id](Result<void> result) {
                if (!result)
                    log_message(diagnostics_,
                                LogLevel::Error,
                                "connection=",
                                connection_id,
                                " failed: ",
                                result.error().message);
                else
                    log_message(diagnostics_, LogLevel::Info, "connection=", connection_id, " closed");
                on_connection_stopped(std::move(result));
            };
            auto feed = TickerFeed::create(event_loop_, config_.feed, connection.symbols, std::move(callbacks));
            if (!feed)
                return std::unexpected(std::move(feed.error()));
            connections_.push_back(std::move(*feed));
        }
        active_connection_count_ = connections_.size();
        return {};
    }

    Result<void> open_csv_file() {
        const auto &path = config_.output.path;
        if (path.empty())
            return fail(ErrorCode::InvalidConfiguration, "CSV output path must not be empty");
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error)
                return fail(ErrorCode::FileIo, "Cannot create CSV parent directory: " + error.message());
        }
        output_stream_.open(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output_stream_)
            return fail(ErrorCode::FileIo, "Cannot open CSV output file: " + path.string());
        return {};
    }

    Result<void> process_feed_message(std::string_view message) {
        auto parsed_trade = parse_ticker_message(message);
        if (!parsed_trade)
            return std::unexpected(std::move(parsed_trade.error()));
        if (!parsed_trade->has_value())
            return {};
        const auto &trade = parsed_trade->value();
        auto processed = statistics_processor_.process_trade(trade, csv_writer_);
        if (!processed)
            return std::unexpected(std::move(processed.error()));
        if (*processed)
            ++emitted_row_count_;
        return {};
    }

    void record_first_failure(Result<void> result) {
        if (!result && run_result_)
            run_result_ = std::move(result);
    }

    void stop_after_failure(Result<void> failure) {
        record_first_failure(std::move(failure));
        request_shutdown();
    }

    void request_shutdown() {
        if (shutdown_requested_)
            return;
        // Set first: stopping a connection before its handshake completes may
        // synchronously invoke on_connection_stopped and re-enter this method.
        shutdown_requested_ = true;
        static_cast<void>(flush_timer_.cancel());
        for (const auto &feed : connections_)
            feed->stop();
    }

    void on_connection_stopped(Result<void> result) {
        --active_connection_count_;
        // A failed/closed connection ends the whole run, avoiding partial symbols.
        record_first_failure(std::move(result));
        request_shutdown();
        if (active_connection_count_ == 0) {
            boost::system::error_code error;
            shutdown_signals_.cancel(error);
            if (error)
                record_first_failure(fail(ErrorCode::Transport, "signal cancellation: " + error.message()));
        }
    }

    // Declaration order keeps the stream alive longer than its writer and the
    // event loop alive longer than its connections and pending handlers.
    boost::asio::io_context event_loop_;
    boost::asio::signal_set shutdown_signals_;
    boost::asio::steady_timer flush_timer_;
    std::ofstream output_stream_;
    CsvWriter csv_writer_;
    StatisticsProcessor statistics_processor_;
    const Config &config_;
    std::ostream &diagnostics_;
    FeedConnections connections_;
    std::size_t active_connection_count_{};
    std::size_t emitted_row_count_{};
    bool shutdown_requested_{};
    Result<void> run_result_;
};

} // namespace

Result<void> run_application(const Config &config, std::ostream &diagnostics) {
    if (config.output.flush_every_rows == 0 || config.output.flush_interval.count() <= 0 ||
        config.output.flush_interval > std::chrono::hours{24})
        return fail(ErrorCode::InvalidConfiguration,
                    "CSV flush rows must be positive and interval within 1..86400000 ms");
    auto processor = StatisticsProcessor::create(config.symbols(), config.window);
    if (!processor)
        return std::unexpected(std::move(processor.error()));
    Application application(config, std::move(*processor), diagnostics);
    return application.run();
}

} // namespace coinbase_ticker_statistics
