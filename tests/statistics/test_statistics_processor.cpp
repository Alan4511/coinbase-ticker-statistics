#include "test_result.hpp"
#include <statistics/statistics_processor.hpp>

#include <type_traits>
#include <utility>

namespace coinbase_ticker_statistics {
namespace {

static_assert(!std::is_polymorphic_v<StatisticsProcessor>);

TickerUpdate ticker_update(Symbol symbol, TradeId id, Price price, Duration offset) {
    const Timestamp exchange_time = std::chrono::sys_days{std::chrono::year{2026} / 10 / 3} + offset;
    return {exchange_time, std::move(symbol), id, price};
}

TEST(StatisticsProcessor, RoutesInterleavedSymbolsIndependently) {
    ASSERT_RESULT_VALUE(processor, StatisticsProcessor::create({"BTC-USD", "ETH-USD"}, {}));
    ASSERT_RESULT_VALUE(first_btc, processor.on_update(ticker_update("BTC-USD", 1, 100, Duration{0})));
    ASSERT_TRUE(first_btc);
    EXPECT_EQ(first_btc->statistics.count, 1U);
    EXPECT_EQ(first_btc->statistics.mean, 100);
    ASSERT_RESULT_VALUE(eth, processor.on_update(ticker_update("ETH-USD", 1, 900, Duration{1})));
    ASSERT_TRUE(eth);
    EXPECT_EQ(eth->statistics.count, 1U);
    EXPECT_EQ(eth->statistics.low, 900);
    ASSERT_RESULT_VALUE(second_btc, processor.on_update(ticker_update("BTC-USD", 2, 200, Duration{2})));
    ASSERT_TRUE(second_btc);
    EXPECT_EQ(second_btc->statistics.count, 2U);
    EXPECT_EQ(second_btc->statistics.mean, 150);
    EXPECT_EQ(second_btc->ticker_update.symbol, "BTC-USD");
    EXPECT_EQ(second_btc->ticker_update.trade_id, 2U);
}

TEST(StatisticsProcessor, DistinguishesFilteredEventsFromErrors) {
    ASSERT_RESULT_VALUE(processor, StatisticsProcessor::create({"BTC-USD"}, {}));
    ASSERT_RESULT_VALUE(unsubscribed, processor.on_update(ticker_update("ETH-USD", 1, 10, Duration{0})));
    EXPECT_FALSE(unsubscribed);
    ASSERT_RESULT_VALUE(accepted, processor.on_update(ticker_update("BTC-USD", 1, 10, Duration{0})));
    ASSERT_TRUE(accepted);
    ASSERT_RESULT_VALUE(duplicate, processor.on_update(ticker_update("BTC-USD", 1, 10, Duration{0})));
    EXPECT_FALSE(duplicate);
    ASSERT_RESULT_ERROR(processor.on_update(ticker_update("BTC-USD", 2, 20, Duration{-1})),
                        ErrorCode::OutOfOrderTimestamp);
    ASSERT_RESULT_VALUE(next, processor.on_update(ticker_update("BTC-USD", 3, 30, Duration{1})));
    ASSERT_TRUE(next);
    EXPECT_EQ(next->statistics.count, 2U);
    EXPECT_EQ(next->statistics.mean, 20);
}

TEST(StatisticsProcessor, ExchangeTimeControlsExpiration) {
    WindowOptions options;
    options.duration = Duration{5};
    ASSERT_RESULT_VALUE(processor, StatisticsProcessor::create({"BTC-USD"}, options));
    const auto first = ticker_update("BTC-USD", 1, 10, Duration{0});
    const auto second = ticker_update("BTC-USD", 2, 20, Duration{6});
    ASSERT_RESULT_VALUE(first_update, processor.on_update(first));
    ASSERT_TRUE(first_update);
    ASSERT_RESULT_VALUE(second_update, processor.on_update(second));
    ASSERT_TRUE(second_update);
    EXPECT_EQ(second_update->statistics.count, 1U);
    EXPECT_EQ(second_update->statistics.mean, 20);
    EXPECT_EQ(second_update->ticker_update.exchange_time, second.exchange_time);
}

TEST(StatisticsProcessor, RejectsInvalidRouting) {
    ASSERT_RESULT_ERROR(StatisticsProcessor::create({}, {}), ErrorCode::InvalidConfiguration);
    ASSERT_RESULT_ERROR(StatisticsProcessor::create({"BTC-USD", "BTC-USD"}, {}), ErrorCode::InvalidConfiguration);
    ASSERT_RESULT_ERROR(StatisticsProcessor::create({""}, {}), ErrorCode::InvalidConfiguration);
    WindowOptions options;
    options.duration = Duration{0};
    ASSERT_RESULT_ERROR(StatisticsProcessor::create({"BTC-USD"}, options), ErrorCode::InvalidConfiguration);
}

} // namespace
} // namespace coinbase_ticker_statistics
