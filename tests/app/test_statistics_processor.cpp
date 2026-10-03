#include "app/statistics_processor.hpp"
#include "test_result.hpp"

#include <type_traits>
#include <vector>

namespace coinbase_ticker_statistics {
namespace {

struct SinkState {
    std::vector<StatisticsUpdate> events;
    bool fail_write{};
};

class RecordingSink {
  public:
    explicit RecordingSink(SinkState &state) : state_(state) {
    }
    Result<void> write_statistics(const StatisticsUpdate &event) {
        if (state_.fail_write)
            return fail(ErrorCode::OutputIo, "test write failure");
        state_.events.push_back(event);
        return {};
    }

  private:
    SinkState &state_;
};
using TestProcessor = StatisticsProcessor;
static_assert(OutputSink<RecordingSink>);
static_assert(!OutputSink<int>);
static_assert(!std::is_polymorphic_v<RecordingSink>);
static_assert(!std::is_polymorphic_v<TestProcessor>);

Result<TestProcessor> make_processor(Symbols symbols, WindowOptions options = {}) {
    return TestProcessor::create(symbols, options);
}

Trade trade(Symbol symbol, TradeId id, Price ticks, Duration offset) {
    const Timestamp time = std::chrono::sys_days{std::chrono::year{2026} / 10 / 3} + offset;
    return {time, std::move(symbol), id, Price{ticks}};
}

TEST(StatisticsProcessor, RoutesInterleavedSymbolsIndependently) {
    SinkState state;
    RecordingSink sink(state);
    ASSERT_RESULT_VALUE(processor, make_processor({"BTC-USD", "ETH-USD"}));
    EXPECT_EQ(processor.process_trade(trade("BTC-USD", 1, 100, Duration{0}), sink), true);
    EXPECT_EQ(processor.process_trade(trade("ETH-USD", 1, 900, Duration{1}), sink), true);
    EXPECT_EQ(processor.process_trade(trade("BTC-USD", 2, 200, Duration{2}), sink), true);
    ASSERT_EQ(state.events.size(), 3U);
    EXPECT_EQ(state.events.back().statistics.count, 2U);
    EXPECT_EQ(state.events.back().statistics.mean, 150);
    EXPECT_EQ(state.events[1].statistics.low, 900);
}

TEST(StatisticsProcessor, DistinguishesFilteredEventsFromErrors) {
    SinkState state;
    RecordingSink sink(state);
    ASSERT_RESULT_VALUE(processor, make_processor({"BTC-USD"}));
    EXPECT_EQ(processor.process_trade(trade("ETH-USD", 1, 10, Duration{0}), sink), false);
    EXPECT_EQ(processor.process_trade(trade("BTC-USD", 1, 10, Duration{0}), sink), true);
    EXPECT_EQ(processor.process_trade(trade("BTC-USD", 1, 10, Duration{0}), sink), false);
    ASSERT_RESULT_ERROR(processor.process_trade(trade("BTC-USD", 2, 20, Duration{-1}), sink), ErrorCode::LateTrade);
    EXPECT_EQ(state.events.size(), 1U);
}

TEST(StatisticsProcessor, ExchangeTimeControlsExpiration) {
    SinkState state;
    RecordingSink sink(state);
    WindowOptions options;
    options.duration = Duration{5};
    ASSERT_RESULT_VALUE(processor, make_processor({"BTC-USD"}, options));
    auto first = trade("BTC-USD", 1, 10, Duration{0});
    auto second = trade("BTC-USD", 2, 20, Duration{6});
    EXPECT_EQ(processor.process_trade(first, sink), true);
    EXPECT_EQ(processor.process_trade(second, sink), true);
    ASSERT_EQ(state.events.size(), 2U);
    EXPECT_EQ(state.events.back().statistics.count, 1U);
    EXPECT_EQ(state.events.back().trade.exchange_time, second.exchange_time);
}

TEST(StatisticsProcessor, PropagatesSinkWriteErrors) {
    SinkState state;
    state.fail_write = true;
    RecordingSink sink(state);
    ASSERT_RESULT_VALUE(processor, make_processor({"BTC-USD"}));
    ASSERT_RESULT_ERROR(processor.process_trade(trade("BTC-USD", 1, 10, Duration{0}), sink), ErrorCode::OutputIo);
    EXPECT_TRUE(state.events.empty());
}

TEST(StatisticsProcessor, RejectsInvalidRouting) {
    ASSERT_RESULT_ERROR(make_processor({}), ErrorCode::InvalidConfiguration);
    ASSERT_RESULT_ERROR(make_processor({"BTC-USD", "BTC-USD"}), ErrorCode::InvalidConfiguration);
}

struct CountingSink {
    SampleCount &count;
    Result<void> write_statistics(const StatisticsUpdate &) {
        ++count;
        return {};
    }
};

TEST(Sinks, ProcessorAcceptsAnotherConceptConformingSink) {
    SampleCount count{};
    CountingSink sink{count};
    ASSERT_RESULT_VALUE(processor, StatisticsProcessor::create({"BTC-USD"}, {}));
    ASSERT_RESULT_OK(processor.process_trade(trade("BTC-USD", 1, 10, Duration{0}), sink));
    EXPECT_EQ(count, 1U);
}

} // namespace
} // namespace coinbase_ticker_statistics
