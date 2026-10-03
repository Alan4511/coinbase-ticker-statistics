#include "app/application.hpp"
#include "app/logger.hpp"
#include "feed/parser/parse_fields.hpp"
#include "test_files.hpp"
#include "test_result.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <csignal>
#include <filesystem>
#include <optional>
#include <sstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace coinbase_ticker_statistics {
namespace {

using Tcp = boost::asio::ip::tcp;
constexpr auto deadline = std::chrono::seconds{5};
constexpr auto poll_interval = std::chrono::milliseconds{5};
constexpr int error_exit_base = 10;

TEST(Logging, IncludesUtcSeverityAndConnectionContextOnOneLine) {
    std::ostringstream output;
    log_message(output, LogLevel::Error, "connection=", 2, " failed: remote\r\nerror");
    const auto line = output.str();
    const auto timestamp_end = line.find(' ');
    ASSERT_NE(timestamp_end, std::string::npos);
    ASSERT_RESULT_OK(parse_utc_timestamp(line.substr(0, timestamp_end)));
    EXPECT_EQ(line.substr(timestamp_end), " ERROR connection=2 failed: remote\\r\\nerror\n");
}

TEST(Logging, StreamFailuresDoNotEscapeIntoApplicationCode) {
    std::ostringstream output;
    output.exceptions(std::ios::badbit);
    EXPECT_THROW(output.setstate(std::ios::badbit), std::ios_base::failure);
    EXPECT_NO_THROW(log_message(output, LogLevel::Info, "stopping"));
}

/** Exercise the real application in an isolated process so SIGTERM cannot hit the test runner.
 * The local server accepts TCP but deliberately leaves TLS pending. Every wait is bounded.
 */
class RunningApplication {
  public:
    explicit RunningApplication(Config config)
        : acceptor_(io_, Tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0)) {
        acceptor_.non_blocking(true);
        config.feed.host = "127.0.0.1";
        config.feed.port = std::to_string(acceptor_.local_endpoint().port());
        config.feed.connect_timeout = std::chrono::seconds{10};
        pid_ = ::fork();
        if (pid_ == 0) {
            boost::system::error_code ignored;
            acceptor_.close(ignored);
            std::ostringstream diagnostics;
            const auto result = run_application(config, diagnostics);
            ::_exit(result ? 0 : error_exit_base + static_cast<int>(result.error().code));
        }
        EXPECT_GT(pid_, 0);
    }
    ~RunningApplication() {
        if (pid_ > 0 && !status_) {
            ::kill(pid_, SIGKILL);
            int status{};
            ::waitpid(pid_, &status, 0);
        }
    }
    bool accept_connections(std::size_t count) {
        const auto until = std::chrono::steady_clock::now() + deadline;
        while (sockets.size() < count && std::chrono::steady_clock::now() < until) {
            Tcp::socket socket(io_);
            boost::system::error_code error;
            acceptor_.accept(socket, error);
            if (!error)
                sockets.push_back(std::move(socket));
            else if (error != boost::asio::error::would_block && error != boost::asio::error::try_again)
                return false;
            if (finished())
                return false;
            std::this_thread::sleep_for(poll_interval);
        }
        return sockets.size() == count;
    }
    void terminate() {
        ASSERT_GT(pid_, 0);
        ASSERT_EQ(::kill(pid_, SIGTERM), 0);
    }
    std::optional<int> wait() {
        const auto until = std::chrono::steady_clock::now() + deadline;
        while (!finished() && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(poll_interval);
        if (!status_ || !WIFEXITED(*status_))
            return std::nullopt;
        return WEXITSTATUS(*status_);
    }

    // Sockets are destroyed before their event loop.
  private:
    boost::asio::io_context io_;
    Tcp::acceptor acceptor_;
    pid_t pid_{};
    std::optional<int> status_;
    bool finished() {
        if (status_ || pid_ <= 0)
            return true;
        int status{};
        if (::waitpid(pid_, &status, WNOHANG) == pid_)
            status_ = status;
        return status_.has_value();
    }

  public:
    std::vector<Tcp::socket> sockets;
};

Config two_connections(const std::filesystem::path &path) {
    return Config{{{Symbols{"BTC-USD"}}, {Symbols{"ETH-USD"}}}, FeedConfig{}, WindowOptions{}, CsvConfig{path}};
}

TEST(Application, InvalidWindowOrConnectionDoesNotTruncateExistingOutput) {
    test::TemporaryDirectory directory;
    const auto path = directory.file("existing.csv");
    const std::string original = "previous run\n";
    test::write_file(path, original);
    auto config = two_connections(path);
    std::ostringstream diagnostics;
    config.window.duration = Duration{0};
    ASSERT_RESULT_ERROR(run_application(config, diagnostics), ErrorCode::InvalidConfiguration);
    EXPECT_EQ(test::read_file(path), original);
    config.window.duration = Duration{300};
    config.connections.back().symbols = {"invalid-product"};
    ASSERT_RESULT_ERROR(run_application(config, diagnostics), ErrorCode::InvalidConfiguration);
    EXPECT_EQ(test::read_file(path), original);
}

TEST(Application, SignalDuringStartupStopsAllConnectionsAndFlushesHeader) {
    test::TemporaryDirectory directory;
    const auto path = directory.file("nested/output.csv");
    RunningApplication app(two_connections(path));
    ASSERT_TRUE(app.accept_connections(2));
    app.terminate();
    EXPECT_EQ(app.wait(), 0);
    EXPECT_EQ(test::read_file(path), "time,symbol,trade_id,trade_price,count,mean,median,low,high\n");
}

TEST(Application, BatchTimerPublishesWhileConnectionsAreIdleAndCancelsOnShutdown) {
    test::TemporaryDirectory directory;
    const auto path = directory.file("output.csv");
    auto config = two_connections(path);
    config.output.flush_every_rows = 128;
    config.output.flush_interval = std::chrono::milliseconds{20};
    RunningApplication app(config);
    ASSERT_TRUE(app.accept_connections(2));
    const auto until = std::chrono::steady_clock::now() + deadline;
    while (test::read_file(path).empty() && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(poll_interval);
    EXPECT_EQ(test::read_file(path), "time,symbol,trade_id,trade_price,count,mean,median,low,high\n");
    app.terminate();
    EXPECT_EQ(app.wait(), 0);
}

TEST(Application, InvalidFlushPolicyPreservesExistingOutput) {
    test::TemporaryDirectory directory;
    const auto path = directory.file("existing.csv");
    test::write_file(path, "previous run\n");
    auto config = two_connections(path);
    std::ostringstream diagnostics;
    config.output.flush_every_rows = 0;
    ASSERT_RESULT_ERROR(run_application(config, diagnostics), ErrorCode::InvalidConfiguration);
    config.output.flush_every_rows = 128;
    config.output.flush_interval = std::chrono::milliseconds{0};
    ASSERT_RESULT_ERROR(run_application(config, diagnostics), ErrorCode::InvalidConfiguration);
    EXPECT_EQ(test::read_file(path), "previous run\n");
}

TEST(Application, OneConnectionFailureStopsTheOtherPendingHandshake) {
    test::TemporaryDirectory directory;
    RunningApplication app(two_connections(directory.file("output.csv")));
    ASSERT_TRUE(app.accept_connections(2));
    app.sockets.front().close();
    // The other connection's 10-second deadline exceeds this test's 5-second wait.
    // Completion proves application-wide cancellation rather than eventual timeout.
    EXPECT_EQ(app.wait(), error_exit_base + static_cast<int>(ErrorCode::Transport));
}

#if defined(__linux__)
TEST(Application, ShutdownReportsOutputFlushFailure) {
    RunningApplication app(two_connections("/dev/full"));
    if (app.accept_connections(2))
        app.terminate();
    EXPECT_EQ(app.wait(), error_exit_base + static_cast<int>(ErrorCode::OutputIo));
}
#endif

} // namespace
} // namespace coinbase_ticker_statistics
