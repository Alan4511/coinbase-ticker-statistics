#include "test_files.hpp"
#include "test_result.hpp"
#include <output/csv_sink.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <csignal>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

namespace coinbase_ticker_statistics {
namespace {

using namespace std::chrono_literals;
constexpr std::string_view header = "time,symbol,trade_id,trade_price,count,mean,median,low,high\n";
constexpr std::string_view row = "1970-01-01T00:00:00.000000000Z,BTC-USD,42,1.25,1,1.25,1.25,1.25,1.25\n";

StatisticsUpdate sample_update() {
    const Price value{125'000'000};
    return {{Timestamp{}, "BTC-USD", 42, value}, {1, {125'000'000, 1}, {125'000'000, 1}, value, value}};
}

TEST(CsvSink, FlushesByThresholdOrDeadline) {
    test::TemporaryDirectory directory;
    CsvConfig config{directory.file("nested/statistics.csv")};
    config.flush_every_rows = 3;
    config.flush_interval = 200ms;
    RunControl control([] {
        ADD_FAILURE() << "unexpected asynchronous flush failure";
    });
    ExecutionContext context(control);
    boost::asio::io_context io;
    CsvSink sink(io, config, context);
    ASSERT_RESULT_OK(sink.open());
    EXPECT_EQ(test::read_file(config.path), header);
    std::string published(header);
    for (std::size_t i = 0; i < config.flush_every_rows; ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(test::read_file(config.path), header);
        ASSERT_RESULT_OK(sink.write_statistics(sample_update()));
        published += row;
    }
    EXPECT_EQ(test::read_file(config.path), published);
    ASSERT_RESULT_OK(sink.write_statistics(sample_update()));
    io.poll(); // The cancelled wait from the flushed batch must leave this new batch pending.
    EXPECT_EQ(test::read_file(config.path), published);
    boost::asio::steady_timer next_update(io, 100ms);
    next_update.async_wait([&sink](boost::system::error_code error) {
        ASSERT_FALSE(error);
        ASSERT_RESULT_OK(sink.write_statistics(sample_update()));
    });
    boost::asio::steady_timer check_visibility(io, 250ms);
    check_visibility.async_wait([&](boost::system::error_code error) {
        ASSERT_FALSE(error);
        EXPECT_EQ(test::read_file(config.path), published + std::string(row) + std::string(row));
    });
    io.run_for(2s);
    ASSERT_RESULT_OK(sink.close());
}

TEST(CsvSink, CloseFlushesAndCancelsPendingWork) {
    test::TemporaryDirectory directory;
    CsvConfig config{directory.file("statistics.csv")};
    config.flush_interval = 30s;
    boost::asio::io_context io;
    RunControl control([] {
        ADD_FAILURE() << "unexpected asynchronous flush failure";
    });
    ExecutionContext context(control);
    CsvSink sink(io, config, context);
    ASSERT_RESULT_OK(sink.close());
    ASSERT_RESULT_OK(sink.open());
    ASSERT_RESULT_OK(sink.write_statistics(sample_update()));
    ASSERT_RESULT_OK(sink.close());
    EXPECT_EQ(test::read_file(config.path), std::string(header) + std::string(row));
    // This must return without waiting for the cancelled thirty-second deadline.
    io.run();
    ASSERT_RESULT_OK(sink.close());
}

TEST(CsvSink, ReportsTimedFlushFailureThroughExecutionContext) {
    test::TemporaryDirectory directory;
    CsvConfig config{directory.file("statistics.csv")};
    config.flush_interval = 20ms;
    const auto child = ::fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        ::alarm(3);
        if (!test::limit_child_file_size(header.size()))
            ::_exit(2);
        boost::asio::io_context io;
        unsigned stop_requests{};
        RunControl control([&stop_requests] {
            ++stop_requests;
        });
        ExecutionContext context(control);
        CsvSink sink(io, config, context);
        if (!sink.open() || !sink.write_statistics(sample_update()))
            ::_exit(3); // The row must be buffered successfully before the timer fails.
        io.run();
        const auto closed = sink.close();
        ::_exit(!control.result() && control.result().error().code == ErrorCode::OutputIo && stop_requests == 1 &&
                        !closed
                    ? 0
                    : 4);
    }
    int status{};
    ASSERT_EQ(::waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

} // namespace
} // namespace coinbase_ticker_statistics
