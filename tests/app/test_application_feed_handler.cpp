#include "test_result.hpp"
#include <app/application_feed_handler.hpp>
#include <runtime/run_control_handle.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <utility>
#include <vector>

namespace coinbase_ticker_statistics {
namespace {

struct RecordingSink {
    Result<void> write_statistics(const StatisticsUpdate &update) {
        if (!result.has_value())
            return result;
        updates.push_back(update);
        return {};
    }
    std::vector<StatisticsUpdate> updates;
    Result<void> result;
};

struct CountingSink {
    Result<void> write_statistics(const StatisticsUpdate &) {
        ++count;
        return {};
    }
    std::size_t count{};
};

struct TestApplication {
    Result<void> on_connected() {
        return {};
    }
    void on_stopped(Result<void> result) {
        control_handle.on_stopped(std::move(result));
    }
    RunControlHandle &control_handle;
};

static_assert(OutputSink<RecordingSink>);
static_assert(OutputSink<CountingSink>);
static_assert(TickerEventHandler<ApplicationFeedHandler<RecordingSink, TestApplication>>);
static_assert(TickerEventHandler<ApplicationFeedHandler<CountingSink, TestApplication>>);

TickerUpdate update(TradeId id, std::int64_t units, Symbol symbol = "BTC-USD", Timestamp time = Timestamp{}) {
    return {time, std::move(symbol), id, Price{units * Price::ticks_per_unit}};
}

TEST(ApplicationFeedHandler, RoutesStatisticsToAnyOutputSink) {
    RunControl control([] {
        ADD_FAILURE() << "synchronous routing must return its errors";
    });
    RunControlHandle control_handle(control);
    TestApplication application{control_handle};
    const auto route_updates = [&]<OutputSink Sink>(Sink &sink) {
        ASSERT_RESULT_VALUE(processor, StatisticsProcessor::create({"BTC-USD", "ETH-USD"}, {}));
        ApplicationFeedHandler handler(std::move(processor), sink, application);
        ASSERT_RESULT_OK(handler.on_message(update(1, 10)));
        ASSERT_RESULT_OK(handler.on_message(update(2, 20)));
        ASSERT_RESULT_OK(handler.on_message(update(1, 100, "ETH-USD")));
        ASSERT_RESULT_OK(handler.on_message(update(1, 100, "ETH-USD")));
        ASSERT_RESULT_OK(handler.on_message(update(1, 50, "SOL-USD")));
        EXPECT_EQ(handler.emitted_rows(), 3U);
    };
    RecordingSink recorded;
    route_updates(recorded);
    ASSERT_EQ(recorded.updates.size(), 3U);
    EXPECT_EQ(recorded.updates[1].statistics.count, 2U);
    EXPECT_EQ(recorded.updates[1].statistics.mean.numerator,
              PriceSum{15 * Price::ticks_per_unit} * recorded.updates[1].statistics.mean.denominator);
    EXPECT_EQ(recorded.updates[2].statistics.count, 1U);
    EXPECT_EQ(recorded.updates[2].statistics.mean.numerator,
              PriceSum{100 * Price::ticks_per_unit} * recorded.updates[2].statistics.mean.denominator);
    CountingSink counted;
    route_updates(counted);
    EXPECT_EQ(counted.count, 3U);
}

TEST(ApplicationFeedHandler, PropagatesProcessingAndSinkFailures) {
    RunControl control([] {
        ADD_FAILURE() << "synchronous routing must return its errors";
    });
    RunControlHandle control_handle(control);
    TestApplication application{control_handle};
    RecordingSink sink;
    ASSERT_RESULT_VALUE(processor, StatisticsProcessor::create({"BTC-USD"}, {}));
    ApplicationFeedHandler handler(std::move(processor), sink, application);
    ASSERT_RESULT_OK(handler.on_message(update(1, 10, "BTC-USD", Timestamp{std::chrono::seconds{1}})));
    ASSERT_RESULT_ERROR(handler.on_message(update(2, 20)), ErrorCode::OutOfOrderTimestamp);
    EXPECT_EQ(sink.updates.size(), 1U);
    EXPECT_EQ(handler.emitted_rows(), 1U);
    sink.result = fail(ErrorCode::OutputIo, "sink rejected update");
    const auto result = handler.on_message(update(2, 20, "BTC-USD", Timestamp{std::chrono::seconds{2}}));
    ASSERT_RESULT_ERROR(result, ErrorCode::OutputIo);
    EXPECT_EQ(result.error().message, "sink rejected update");
    EXPECT_EQ(sink.updates.size(), 1U);
    EXPECT_EQ(handler.emitted_rows(), 1U);
}

} // namespace
} // namespace coinbase_ticker_statistics
