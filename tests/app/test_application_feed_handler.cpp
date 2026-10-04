#include "test_result.hpp"
#include <app/application_feed_handler.hpp>

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace coinbase_ticker_statistics {
namespace {

struct RecordingSink {
    Result<void> write_statistics(const StatisticsUpdate &update) {
        if (!result)
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

struct LifecycleRecorder {
    unsigned connected{};
    Result<void> ready_result;
    std::optional<Result<void>> completion;
};

static_assert(OutputSink<RecordingSink>);
static_assert(OutputSink<CountingSink>);
static_assert(FeedHandler<ApplicationFeedHandler<RecordingSink>>);
static_assert(FeedHandler<ApplicationFeedHandler<CountingSink>>);

template <OutputSink Sink>
auto handler_for(Sink &sink, LifecycleRecorder &lifecycle) {
    return ApplicationFeedHandler{StatisticsProcessor::create({"BTC-USD", "ETH-USD"}, {}).value(),
                                  sink,
                                  [&lifecycle] {
                                      ++lifecycle.connected;
                                      return lifecycle.ready_result;
                                  },
                                  [&lifecycle](Result<void> completion) {
                                      lifecycle.completion = std::move(completion);
                                  }};
}

TickerUpdate update(TradeId id, Price price, Symbol symbol = "BTC-USD", Timestamp time = Timestamp{}) {
    return {time, std::move(symbol), id, price};
}

TEST(ApplicationFeedHandler, RoutesIndependentSymbolStatisticsToASinkWithoutCsvLifecycle) {
    RecordingSink sink;
    LifecycleRecorder lifecycle;
    auto handler = handler_for(sink, lifecycle);
    ASSERT_RESULT_OK(handler.on_message(update(1, 10)));
    ASSERT_RESULT_OK(handler.on_message(update(2, 20)));
    ASSERT_RESULT_OK(handler.on_message(update(1, 100, "ETH-USD")));
    ASSERT_EQ(sink.updates.size(), 3U);
    EXPECT_EQ(sink.updates[1].statistics.count, 2U);
    EXPECT_EQ(sink.updates[1].statistics.mean, 15);
    EXPECT_EQ(sink.updates[2].statistics.count, 1U);
    EXPECT_EQ(sink.updates[2].statistics.mean, 100);
    EXPECT_EQ(handler.emitted_rows(), 3U);
    EXPECT_EQ(lifecycle.connected, 0U);
    EXPECT_FALSE(lifecycle.completion);
}

TEST(ApplicationFeedHandler, SupportsAnotherSinkAndFiltersDuplicatesAndUnsubscribedSymbols) {
    CountingSink sink;
    LifecycleRecorder lifecycle;
    auto handler = handler_for(sink, lifecycle);
    ASSERT_RESULT_OK(handler.on_message(update(1, 10)));
    ASSERT_RESULT_OK(handler.on_message(update(1, 10)));
    ASSERT_RESULT_OK(handler.on_message(update(1, 10, "SOL-USD")));
    EXPECT_EQ(sink.count, 1U);
    EXPECT_EQ(handler.emitted_rows(), 1U);
}

TEST(ApplicationFeedHandler, PropagatesSinkFailureWithoutCountingDeliveryOrDecidingShutdown) {
    RecordingSink sink;
    sink.result = fail(ErrorCode::OutputIo, "sink rejected update");
    LifecycleRecorder lifecycle;
    auto handler = handler_for(sink, lifecycle);
    const auto result = handler.on_message(update(1, 10));
    ASSERT_RESULT_ERROR(result, ErrorCode::OutputIo);
    EXPECT_EQ(result.error().message, "sink rejected update");
    EXPECT_TRUE(sink.updates.empty());
    EXPECT_EQ(handler.emitted_rows(), 0U);
    EXPECT_FALSE(lifecycle.completion);
}

TEST(ApplicationFeedHandler, PropagatesStatisticsFailureWithoutCallingTheSink) {
    RecordingSink sink;
    LifecycleRecorder lifecycle;
    auto handler = handler_for(sink, lifecycle);
    ASSERT_RESULT_OK(handler.on_message(update(1, 10, "BTC-USD", Timestamp{std::chrono::seconds{1}})));
    ASSERT_RESULT_ERROR(handler.on_message(update(2, 20)), ErrorCode::OutOfOrderTimestamp);
    EXPECT_EQ(sink.updates.size(), 1U);
    EXPECT_EQ(handler.emitted_rows(), 1U);
    EXPECT_FALSE(lifecycle.completion);
}

TEST(ApplicationFeedHandler, ReportsLifecycleEventsToItsApplicationCallbacks) {
    CountingSink sink;
    LifecycleRecorder lifecycle;
    lifecycle.ready_result = fail(ErrorCode::FileIo, "application startup failed");
    auto handler = handler_for(sink, lifecycle);
    ASSERT_RESULT_ERROR(handler.on_connected(), ErrorCode::FileIo);
    EXPECT_EQ(lifecycle.connected, 1U);
    handler.on_stopped(fail(ErrorCode::Transport, "peer failed"));
    ASSERT_TRUE(lifecycle.completion);
    ASSERT_RESULT_ERROR(*lifecycle.completion, ErrorCode::Transport);
    EXPECT_EQ(lifecycle.completion->error().message, "peer failed");
    EXPECT_EQ(sink.count, 0U);
}

} // namespace
} // namespace coinbase_ticker_statistics
