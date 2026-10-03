#include "feed/parser/parse_fields.hpp"
#include "feed/parser/ticker_parser.hpp"
#include "test_result.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <limits>
#include <stdexcept>
#include <string>

namespace coinbase_ticker_statistics {
namespace {

using namespace std::chrono_literals;
using Json = nlohmann::json;

constexpr auto valid_ticker = R"({
    "type":"ticker", "time":"2026-10-03T10:20:30.123456789Z", "product_id":"BTC-USD",
    "trade_id":18446744073709551615, "price":"12345.67890123", "volume_24h":"10000"
})";

TEST(TickerParser, DecodesPriceFullWidthTradeIdAndExchangeTimestamp) {
    ASSERT_RESULT_VALUE(received, parse_utc_timestamp("2026-10-03T10:20:31Z"));
    ASSERT_RESULT_VALUE(result, parse_ticker_message(valid_ticker));
    ASSERT_TRUE(result);
    EXPECT_EQ(result->symbol, "BTC-USD");
    EXPECT_EQ(result->trade_id, std::numeric_limits<TradeId>::max());
    EXPECT_EQ(result->price, 12345.67890123L);
    EXPECT_EQ(result->exchange_time, received - 1s + 123'456'789ns);
}

TEST(TickerParser, KeepsEachSymbolsIdentityAndAcceptsZeroTradeId) {
    for (const auto *symbol : {"BTC-USD", "ETH-USD", "SOL-USD"}) {
        auto ticker = Json::parse(valid_ticker);
        ticker["product_id"] = symbol;
        ticker["trade_id"] = 0;
        ticker["price"] = "12.34";
        ASSERT_RESULT_VALUE(result, parse_ticker_message(ticker.dump()));
        ASSERT_TRUE(result);
        EXPECT_EQ(result->symbol, symbol);
        EXPECT_EQ(result->trade_id, 0U);
        EXPECT_EQ(result->price, 12.34L);
    }
}

TEST(TickerParser, IgnoresAcknowledgementsAndOtherTypedMessages) {
    {
        ASSERT_RESULT_VALUE(ignored, parse_ticker_message(R"({"type":"subscriptions","channels":[]})"));
        EXPECT_FALSE(ignored);
    }
    {
        ASSERT_RESULT_VALUE(ignored, parse_ticker_message(R"({"type":"heartbeat"})"));
        EXPECT_FALSE(ignored);
    }
    {
        ASSERT_RESULT_VALUE(ignored, parse_ticker_message(R"({"type":"future-message-kind","payload":{}})"));
        EXPECT_FALSE(ignored);
    }
}

TEST(TickerParser, SurfacesExchangeErrorsDistinctFromMalformedMarketData) {
    const auto error = parse_ticker_message(R"({"type":"error","message":"Invalid product"})");
    ASSERT_RESULT_ERROR(error, ErrorCode::Protocol);
    EXPECT_NE(error.error().message.find("Invalid product"), std::string::npos);
    ASSERT_RESULT_ERROR(parse_ticker_message(R"({"type":"error"})"), ErrorCode::Protocol);
    ASSERT_RESULT_ERROR(parse_ticker_message(R"({"type":"error","message":42})"), ErrorCode::Protocol);
}

TEST(TickerParser, RejectsMalformedMessageEnvelope) {
    for (const auto *message : {"not json", "[]", "null", "{}", R"({"type":1})", R"({"type":""})"}) {
        ASSERT_RESULT_ERROR(parse_ticker_message(message), ErrorCode::InvalidInput) << message;
    }
}

TEST(TickerParser, RequiresAllTickerFields) {
    for (const auto *field : {"product_id", "time", "trade_id", "price"}) {
        auto ticker = Json::parse(valid_ticker);
        ticker.erase(field);
        ASSERT_RESULT_ERROR(parse_ticker_message(ticker.dump()), ErrorCode::InvalidInput) << field;
    }
}

TEST(TickerParser, RejectsWrongTypesAndEmptyStringFields) {
    for (const auto *field : {"product_id", "time", "price"}) {
        for (const auto &invalid_value : Json::array({nullptr, true, 1, ""})) {
            auto ticker = Json::parse(valid_ticker);
            ticker[field] = invalid_value;
            ASSERT_RESULT_ERROR(parse_ticker_message(ticker.dump()), ErrorCode::InvalidInput) << field;
        }
    }
}

TEST(TickerParser, RejectsLossyOrInvalidTradeIds) {
    for (const auto &invalid_id : Json::array({nullptr, true, -1, 1.0, "123", 1.8446744073709552e19})) {
        auto ticker = Json::parse(valid_ticker);
        ticker["trade_id"] = invalid_id;
        ASSERT_RESULT_ERROR(parse_ticker_message(ticker.dump()), ErrorCode::InvalidInput);
    }
}

TEST(TickerParser, RejectsBadPricesAndTimestamps) {
    for (const auto *price : {"-1", "NaN", "1e99999", "1e-99999"}) {
        auto ticker = Json::parse(valid_ticker);
        ticker["price"] = price;
        const auto result = parse_ticker_message(ticker.dump());
        ASSERT_FALSE(result) << price;
        EXPECT_TRUE(result.error().code == ErrorCode::InvalidInput || result.error().code == ErrorCode::OutOfRange);
    }
    auto ticker = Json::parse(valid_ticker);
    ticker["price"] = "1.23";
    ticker["time"] = "2026-02-30T10:00:00Z";
    ASSERT_RESULT_ERROR(parse_ticker_message(ticker.dump()), ErrorCode::InvalidInput);
}

} // namespace
} // namespace coinbase_ticker_statistics
