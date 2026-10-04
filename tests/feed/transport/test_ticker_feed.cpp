#include "test_exchange.hpp"
#include "test_result.hpp"
#include <feed/transport/ticker_feed.hpp>

#include <gtest/gtest.h>

#include <type_traits>
#include <vector>

namespace coinbase_ticker_statistics {
namespace {

const Symbols symbols{"BTC-USD", "ETH-USD", "SOL-USD"};
const std::string ticker =
    R"({"type":"ticker","product_id":"BTC-USD","trade_id":42,"price":"123.45","time":"2026-01-02T03:04:05Z"})";

class FeedRun final {
  public:
    Result<void> configure(FeedConfig config, const Symbols &products = symbols) {
        auto created = TickerFeed<FeedRun>::create(io, std::move(config), products, *this);
        if (!created)
            return std::unexpected(std::move(created.error()));
        feed = std::move(*created);
        return {};
    }
    Result<void> on_connected() {
        ++connected;
        return {};
    }
    Result<void> on_message(const TickerUpdate &ticker_update) {
        ticker_updates.push_back(ticker_update);
        return message_result;
    }
    void on_stopped(Result<void> result) {
        ++stopped;
        outcome = std::move(result);
    }
    void run() {
        ASSERT_RESULT_OK(feed->start());
        io.run();
        EXPECT_EQ(stopped, 1U);
    }
    boost::asio::io_context io;
    std::unique_ptr<TickerFeed<FeedRun>> feed;
    std::vector<TickerUpdate> ticker_updates;
    Result<void> outcome;
    Result<void> message_result;
    unsigned connected{};
    unsigned stopped{};
};

static_assert(FeedHandler<FeedRun>);
static_assert(FeedConnectionHandler<TickerFeed<FeedRun>>);
static_assert(!std::is_polymorphic_v<TickerFeed<FeedRun>>);
static_assert(!FeedHandler<int>);
static_assert(!std::is_polymorphic_v<FeedRun>);

struct IncompleteHandler {
    Result<void> on_connected();
    Result<void> on_message(const TickerUpdate &);
};

struct WrongResultHandler {
    Result<void> on_connected();
    bool on_message(const TickerUpdate &);
    void on_stopped(Result<void>);
};

static_assert(!FeedHandler<IncompleteHandler>);
static_assert(!FeedHandler<WrongResultHandler>);

TEST(TickerFeed, SubscribesDecodesTickerUpdatesFiltersControlMessagesAndRecognizesPeerClose) {
    test::TestTrustStore trust;
    test::LoopbackExchange exchange(symbols, {R"({"type":"subscriptions"})", ticker});
    FeedRun client;
    ASSERT_RESULT_OK(client.configure(exchange.config()));
    client.run();
    ASSERT_RESULT_OK(client.outcome);
    EXPECT_EQ(client.connected, 1U);
    ASSERT_EQ(client.ticker_updates.size(), 1U);
    const auto &[exchange_time, symbol, trade_id, price] = client.ticker_updates.front();
    EXPECT_EQ(exchange_time,
              Timestamp{std::chrono::sys_days{std::chrono::year{2026} / 1 / 2} + std::chrono::hours{3} +
                        std::chrono::minutes{4} + std::chrono::seconds{5}});
    EXPECT_EQ(symbol, "BTC-USD");
    EXPECT_EQ(trade_id, 42U);
    EXPECT_EQ(price, 123.45L);
    EXPECT_EQ(client.feed->counts().received_messages, 2U);
    EXPECT_EQ(client.feed->counts().ticker_updates, 1U);
    EXPECT_TRUE(exchange.completed_successfully());
}

TEST(TickerFeed, RejectsMalformedInputBeforeNotifyingTheMessageHandler) {
    test::TestTrustStore trust;
    test::LoopbackExchange exchange(symbols, {ticker, R"({"type":"ticker"})"});
    FeedRun client;
    ASSERT_RESULT_OK(client.configure(exchange.config()));
    client.run();
    ASSERT_RESULT_ERROR(client.outcome, ErrorCode::InvalidInput);
    EXPECT_EQ(client.ticker_updates.size(), 1U);
    EXPECT_EQ(client.feed->counts().received_messages, 2U);
    EXPECT_EQ(client.feed->counts().ticker_updates, 1U);
}

TEST(TickerFeed, PreservesMessageHandlerFailureAndStopsDelivery) {
    test::TestTrustStore trust;
    test::LoopbackExchange exchange(symbols, {ticker, ticker});
    FeedRun client;
    client.message_result = fail(ErrorCode::OutputIo, "consumer output failed");
    ASSERT_RESULT_OK(client.configure(exchange.config()));
    client.run();
    ASSERT_RESULT_ERROR(client.outcome, ErrorCode::OutputIo);
    EXPECT_EQ(client.outcome.error().message, "consumer output failed");
    EXPECT_EQ(client.ticker_updates.size(), 1U);
    EXPECT_EQ(client.feed->counts().received_messages, 1U);
}

TEST(TickerFeed, RejectsInvalidConfigurationBeforeConnecting) {
    FeedConfig config;
    config.host.clear();
    FeedRun client;
    ASSERT_RESULT_ERROR(client.configure(config), ErrorCode::InvalidConfiguration);
    config = FeedConfig{};
    config.max_message_bytes = 0;
    ASSERT_RESULT_ERROR(client.configure(config), ErrorCode::InvalidConfiguration);
    ASSERT_RESULT_ERROR(client.configure(FeedConfig{}, {}), ErrorCode::InvalidConfiguration);
}

} // namespace
} // namespace coinbase_ticker_statistics
