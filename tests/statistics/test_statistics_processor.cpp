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

TEST(StatisticsProcessor, RoutesSymbolsAndDistinguishesFilteredEventsFromErrors) {
    ASSERT_RESULT_VALUE(processor, StatisticsProcessor::create({"BTC-USD", "ETH-USD"}, {}));
    ASSERT_RESULT_VALUE(first_btc, processor.on_update(ticker_update("BTC-USD", 1, 100, Duration{0})));
    ASSERT_TRUE(first_btc);
    ASSERT_RESULT_VALUE(eth, processor.on_update(ticker_update("ETH-USD", 1, 900, Duration{1})));
    ASSERT_TRUE(eth);
    EXPECT_EQ(eth->statistics.count, 1U);
    EXPECT_EQ(eth->statistics.mean, 900);
    ASSERT_RESULT_VALUE(second_btc, processor.on_update(ticker_update("BTC-USD", 2, 200, Duration{2})));
    ASSERT_TRUE(second_btc);
    EXPECT_EQ(second_btc->statistics.count, 2U);
    EXPECT_EQ(second_btc->statistics.mean, 150);
    EXPECT_EQ(second_btc->ticker_update.symbol, "BTC-USD");
    EXPECT_EQ(second_btc->ticker_update.trade_id, 2U);
    ASSERT_RESULT_VALUE(duplicate, processor.on_update(ticker_update("BTC-USD", 2, 999, Duration{2})));
    EXPECT_FALSE(duplicate);
    ASSERT_RESULT_VALUE(unsubscribed, processor.on_update(ticker_update("SOL-USD", 1, 10, Duration{0})));
    EXPECT_FALSE(unsubscribed);
    ASSERT_RESULT_ERROR(processor.on_update(ticker_update("BTC-USD", 3, 10, Duration{1})),
                        ErrorCode::OutOfOrderTimestamp);
    ASSERT_RESULT_VALUE(expired, processor.on_update(ticker_update("BTC-USD", 3, 300, Duration{302})));
    ASSERT_TRUE(expired);
    EXPECT_EQ(expired->statistics.count, 1U);
    EXPECT_EQ(expired->statistics.mean, 300);
    // Advancing BTC's window must not expire the independently timed ETH window.
    ASSERT_RESULT_VALUE(next_eth, processor.on_update(ticker_update("ETH-USD", 2, 100, Duration{2})));
    ASSERT_TRUE(next_eth);
    EXPECT_EQ(next_eth->statistics.count, 2U);
    EXPECT_EQ(next_eth->statistics.mean, 500);
}

} // namespace
} // namespace coinbase_ticker_statistics
