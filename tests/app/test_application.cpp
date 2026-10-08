#include "test_exchange.hpp"
#include "test_files.hpp"
#include "test_result.hpp"
#include <app/application.hpp>
#include <app/logger.hpp>
#include <feed/parser/parse_fields.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace coinbase_ticker_statistics {
namespace {

TEST(Logging, FormatsDiagnosticsAndContainsStreamFailures) {
    std::ostringstream output;
    Logger logger(output);
    logger.log(LogLevel::Error, "connection=", 2, " failed: remote\r\nerror");
    const auto line = output.str();
    const auto timestamp_end = line.find(' ');
    ASSERT_NE(timestamp_end, std::string::npos);
    ASSERT_RESULT_OK(parse_utc_timestamp(line.substr(0, timestamp_end)));
    EXPECT_EQ(line.substr(timestamp_end), " ERROR connection=2 failed: remote\\r\\nerror\n");
    output.exceptions(std::ios::badbit);
    EXPECT_THROW(output.setstate(std::ios::badbit), std::ios_base::failure);
    EXPECT_NO_THROW(logger.log(LogLevel::Info, "stopping"));
}

Config configuration(const std::filesystem::path &path) {
    return Config{Symbols{"BTC-USD", "ETH-USD"}, FeedConfig{}, WindowOptions{}, CsvConfig{path}};
}

const std::string ticker =
    R"({"type":"ticker","product_id":"BTC-USD","trade_id":42,"price":"1.25","time":"2026-01-02T03:04:05Z"})";
const std::string header = "time,symbol,trade_id,trade_price,count,mean,median,low,high\n";
const std::string row = "2026-01-02T03:04:05.000000000Z,BTC-USD,42,1.25,1,1.25,1.25,1.25,1.25\n";

TEST(Application, WritesCsvAndReportsFinalCounts) {
    test::TemporaryDirectory directory;
    auto config = configuration(directory.file("nested/output.csv"));
    config.output.flush_interval = std::chrono::seconds{30};
    test::TestTrustStore trust;
    test::LoopbackExchange exchange(config.symbols, {R"({"type":"subscriptions"})", ticker, ticker});
    config.feed = exchange.config();
    std::ostringstream diagnostics;
    Logger logger(diagnostics);
    ASSERT_RESULT_OK(validate_config(config));
    ASSERT_RESULT_OK(run_application(config, logger));
    EXPECT_EQ(test::read_file(config.output.path), header + row);
    EXPECT_NE(diagnostics.str().find("stopped: status=success received_messages=3 ticker_updates=2 emitted_rows=1 "
                                     "reason=peer closed connection"),
              std::string::npos);
    EXPECT_TRUE(exchange.completed_successfully());
}

TEST(Application, ConnectionFailurePreservesExistingOutput) {
    test::TemporaryDirectory directory;
    auto config = configuration(directory.file("existing.csv"));
    test::write_file(config.output.path, "previous run\n");
    test::TestTrustStore trust(test::fixture_path("ticker_expected.csv"));
    test::LoopbackExchange exchange(config.symbols, {ticker});
    config.feed = exchange.config();
    std::ostringstream diagnostics;
    Logger logger(diagnostics);
    ASSERT_RESULT_OK(validate_config(config));
    ASSERT_RESULT_ERROR(run_application(config, logger), ErrorCode::Transport);
    EXPECT_EQ(test::read_file(config.output.path), "previous run\n");
}

TEST(Application, ProcessingFailureStopsAndFlushesPreviousRows) {
    test::TemporaryDirectory directory;
    auto config = configuration(directory.file("output.csv"));
    test::TestTrustStore trust;
    const std::string earlier =
        R"({"type":"ticker","product_id":"BTC-USD","trade_id":43,"price":"2","time":"2026-01-02T03:04:04Z"})";
    test::LoopbackExchange exchange(config.symbols, {ticker, earlier}, test::ExchangeReply::WaitForClientClose);
    config.feed = exchange.config();
    std::ostringstream diagnostics;
    Logger logger(diagnostics);
    ASSERT_RESULT_OK(validate_config(config));
    ASSERT_RESULT_ERROR(run_application(config, logger), ErrorCode::OutOfOrderTimestamp);
    EXPECT_EQ(test::read_file(config.output.path), header + row);
    EXPECT_NE(
        diagnostics.str().find("ERROR stopped: status=failure received_messages=2 ticker_updates=2 emitted_rows=1"),
        std::string::npos);
    EXPECT_TRUE(exchange.completed_successfully());
}

TEST(Application, ProcessingFailureSurvivesCloseDeadline) {
    test::TemporaryDirectory directory;
    auto config = configuration(directory.file("output.csv"));
    test::TestTrustStore trust;
    const std::string malformed = R"({"type":"ticker"})";
    test::LoopbackExchange exchange(config.symbols, {ticker, malformed}, test::ExchangeReply::RemainIdle);
    config.feed = exchange.config();
    config.feed.close_timeout = Duration{1};
    std::ostringstream diagnostics;
    Logger logger(diagnostics);
    const auto started = std::chrono::steady_clock::now();
    ASSERT_RESULT_ERROR(run_application(config, logger), ErrorCode::InvalidInput);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    EXPECT_GE(elapsed, std::chrono::seconds{1});
    EXPECT_LT(elapsed, std::chrono::seconds{3});
    EXPECT_EQ(test::read_file(config.output.path), header + row);
    EXPECT_NE(diagnostics.str().find("stopped: status=failure"), std::string::npos);
}

TEST(Application, CapacityFailureStopsAndFlushesAcceptedRows) {
    test::TemporaryDirectory directory;
    auto config = configuration(directory.file("output.csv"));
    config.window.max_observations_per_symbol = 1;
    test::TestTrustStore trust;
    const std::string next =
        R"({"type":"ticker","product_id":"BTC-USD","trade_id":43,"price":"2","time":"2026-01-02T03:04:05Z"})";
    test::LoopbackExchange exchange(config.symbols, {ticker, ticker, next}, test::ExchangeReply::WaitForClientClose);
    config.feed = exchange.config();
    std::ostringstream diagnostics;
    Logger logger(diagnostics);
    ASSERT_RESULT_ERROR(run_application(config, logger), ErrorCode::OutOfRange);
    EXPECT_EQ(test::read_file(config.output.path), header + row);
    EXPECT_NE(diagnostics.str().find("Window observation limit exceeded"), std::string::npos);
    EXPECT_TRUE(exchange.completed_successfully());
}

/** Own a child application so signals never target the test runner. */
class ApplicationProcess {
  public:
    ApplicationProcess(const Config &config,
                       const std::filesystem::path &diagnostics_path,
                       std::optional<std::size_t> file_size_limit = std::nullopt,
                       std::optional<ErrorCode> expected_error = std::nullopt) {
        if (auto valid = validate_config(config); !valid.has_value())
            throw std::runtime_error("invalid test application configuration: " + valid.error().message);
        pid_ = ::fork();
        if (pid_ < 0)
            throw std::runtime_error("cannot fork test application");
        if (pid_ == 0) {
            ::alarm(6);
            rlimit original_limit{};
            if (file_size_limit.has_value()) {
                if (::getrlimit(RLIMIT_FSIZE, &original_limit) != 0 ||
                    !test::limit_child_file_size(file_size_limit.value()))
                    ::_exit(2);
            }
            std::ofstream diagnostics(diagnostics_path);
            // Inject failure only into CSV output, not the diagnostic destination.
            std::ostringstream captured;
            Logger logger(file_size_limit.has_value() ? static_cast<std::ostream &>(captured) : diagnostics);
            const auto result = run_application(config, logger);
            if (file_size_limit.has_value()) {
                if (::setrlimit(RLIMIT_FSIZE, &original_limit) != 0)
                    ::_exit(2);
                diagnostics << captured.str();
                diagnostics.flush();
                if (!diagnostics)
                    ::_exit(2);
            }
            if (expected_error.has_value() && (result.has_value() || result.error().code != expected_error.value()))
                ::_exit(3);
            ::_exit(result.has_value() ? 0 : 1);
        }
    }
    ~ApplicationProcess() {
        if (!status_.has_value()) {
            ::kill(pid_, SIGKILL);
            int ignored{};
            ::waitpid(pid_, &ignored, 0);
        }
    }
    ApplicationProcess(const ApplicationProcess &) = delete;
    ApplicationProcess &operator=(const ApplicationProcess &) = delete;
    bool send_signal(int signal) const {
        return ::kill(pid_, signal) == 0;
    }
    bool wait_for_exit() {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (std::chrono::steady_clock::now() < until) {
            int status{};
            if (::waitpid(pid_, &status, WNOHANG) == pid_) {
                status_ = status;
                return WIFEXITED(status);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        return false;
    }
    int exit_code() const {
        int status = status_.value();
        return WEXITSTATUS(status);
    }

  private:
    pid_t pid_{};
    std::optional<int> status_;
};

void check_idle_shutdown(int signal, test::ExchangeReply reply) {
    test::TemporaryDirectory directory;
    auto config = configuration(directory.file("output.csv"));
    const auto diagnostics = directory.file("application.log");
    test::TestTrustStore trust;
    test::LoopbackExchange exchange(config.symbols, {ticker}, reply);
    config.feed = exchange.config();
    config.feed.close_timeout = Duration{1};
    config.output.flush_interval = std::chrono::milliseconds{20};
    ApplicationProcess app(config, diagnostics);
    // Visibility proves a partial batch is flushed while the feed remains idle.
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while ((!std::filesystem::exists(config.output.path) || test::read_file(config.output.path) != header + row) &&
           std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    ASSERT_TRUE(std::filesystem::exists(config.output.path));
    EXPECT_EQ(test::read_file(config.output.path), header + row);
    ASSERT_TRUE(app.send_signal(signal));
    ASSERT_TRUE(app.wait_for_exit()) << "application failed to exit through normal cleanup";
    const auto log = test::read_file(diagnostics);
    EXPECT_NE(log.find("shutdown requested: reason="), std::string::npos);
    EXPECT_NE(log.find("received_messages=1 ticker_updates=1 emitted_rows=1"), std::string::npos);
    if (reply == test::ExchangeReply::WaitForClientClose) {
        EXPECT_EQ(app.exit_code(), 0);
        EXPECT_NE(log.find(signal == SIGINT ? "reason=SIGINT" : "reason=SIGTERM"), std::string::npos);
        EXPECT_TRUE(exchange.completed_successfully());
    }
    else {
        EXPECT_EQ(app.exit_code(), 1);
        EXPECT_NE(log.find("reason=WebSocket close deadline exceeded"), std::string::npos);
    }
}

TEST(Application, SignalShutdownIsGraceful) {
    for (const int signal : {SIGINT, SIGTERM}) {
        SCOPED_TRACE(signal == SIGINT ? "SIGINT" : "SIGTERM");
        check_idle_shutdown(signal, test::ExchangeReply::WaitForClientClose);
    }
}

TEST(Application, TimedOutputFailureStopsIdleFeed) {
    test::TemporaryDirectory directory;
    auto config = configuration(directory.file("output.csv"));
    config.output.flush_interval = std::chrono::milliseconds{20};
    test::TestTrustStore trust;
    test::LoopbackExchange exchange(config.symbols, {ticker}, test::ExchangeReply::WaitForClientClose);
    config.feed = exchange.config();
    ApplicationProcess app(config, directory.file("application.log"), header.size(), ErrorCode::OutputIo);
    ASSERT_TRUE(app.wait_for_exit());
    EXPECT_EQ(app.exit_code(), 1);
    EXPECT_EQ(test::read_file(config.output.path), header);
    EXPECT_TRUE(exchange.completed_successfully());
}

TEST(Application, SignalDuringConnectionSetupPreservesExistingOutput) {
    for (const int signal : {SIGINT, SIGTERM}) {
        SCOPED_TRACE(signal);
        test::TemporaryDirectory directory;
        auto config = configuration(directory.file("existing.csv"));
        test::write_file(config.output.path, "previous run\n");
        const auto accepted = directory.file("accepted.txt");
        const auto diagnostics = directory.file("application.log");
        test::TestTrustStore trust;
        test::LoopbackExchange exchange(config.symbols, {}, test::ExchangeReply::StallTls, accepted);
        config.feed = exchange.config();
        ApplicationProcess app(config, diagnostics);
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (!std::filesystem::exists(accepted) && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        ASSERT_TRUE(std::filesystem::exists(accepted));
        ASSERT_TRUE(app.send_signal(signal));
        ASSERT_TRUE(app.wait_for_exit());
        EXPECT_EQ(app.exit_code(), 0);
        EXPECT_EQ(test::read_file(config.output.path), "previous run\n");
        EXPECT_NE(test::read_file(diagnostics).find("stopped: status=success"), std::string::npos);
    }
}

TEST(Application, CleanupFailureIsLoggedWithoutReplacingProcessingFailure) {
    test::TemporaryDirectory directory;
    auto config = configuration(directory.file("output.csv"));
    config.output.flush_interval = std::chrono::seconds{30};
    const auto diagnostics = directory.file("application.log");
    test::TestTrustStore trust;
    test::LoopbackExchange exchange(config.symbols,
                                    {ticker, R"({"type":"ticker"})"},
                                    test::ExchangeReply::WaitForClientClose);
    config.feed = exchange.config();
    ApplicationProcess app(config, diagnostics, header.size(), ErrorCode::InvalidInput);
    ASSERT_TRUE(app.wait_for_exit());
    EXPECT_EQ(app.exit_code(), 1);
    const auto log = test::read_file(diagnostics);
    EXPECT_NE(log.find("ERROR output cleanup failed:"), std::string::npos);
    EXPECT_NE(log.find("ERROR stopped: status=failure"), std::string::npos);
    EXPECT_EQ(test::read_file(config.output.path), header);
    EXPECT_TRUE(exchange.completed_successfully());
}

TEST(Application, CloseDeadlineBoundsUnresponsivePeer) {
    check_idle_shutdown(SIGTERM, test::ExchangeReply::RemainIdle);
}

} // namespace
} // namespace coinbase_ticker_statistics
