#include "test_result.hpp"
#include <feed/parser/parse_fields.hpp>
#include <feed/parser/ticker_parser.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <limits>
#include <string>

namespace coinbase_ticker_statistics {
namespace {

using namespace std::chrono_literals;
using Json = nlohmann::json;

constexpr auto valid_ticker = R"({
    "type":"ticker", "time":"2026-10-03T10:20:30.123456789Z", "product_id":"BTC-USD",
    "trade_id":18446744073709551615, "price":"12345.67890123", "volume_24h":"10000"
})";

TEST(TickerParser, DecodesTickerAndFiltersControlMessages) {
    ASSERT_RESULT_VALUE(received, parse_utc_timestamp("2026-10-03T10:20:31Z"));
    ASSERT_RESULT_VALUE(result, parse_ticker_message(valid_ticker));
    ASSERT_TRUE(result);
    EXPECT_EQ(result->symbol, "BTC-USD");
    EXPECT_EQ(result->trade_id, std::numeric_limits<TradeId>::max());
    EXPECT_EQ(result->price, 12345.67890123L);
    EXPECT_EQ(result->exchange_time, received - 1s + 123'456'789ns);
    for (const auto *message : {R"({"type":"subscriptions","channels":[]})",
                                R"({"type":"heartbeat"})",
                                R"({"type":"future-message-kind","payload":{}})"}) {
        SCOPED_TRACE(message);
        ASSERT_RESULT_VALUE(ignored, parse_ticker_message(message));
        EXPECT_FALSE(ignored);
    }
}

TEST(TickerParser, RejectsMalformedTickerMessages) {
    for (const auto *message : {"not json", "[]", "null", "{}", R"({"type":1})", R"({"type":""})"}) {
        SCOPED_TRACE(message);
        ASSERT_RESULT_ERROR(parse_ticker_message(message), ErrorCode::InvalidInput);
    }
    for (const auto *field : {"product_id", "time", "trade_id", "price"}) {
        SCOPED_TRACE(field);
        auto ticker = Json::parse(valid_ticker);
        ticker.erase(field);
        ASSERT_RESULT_ERROR(parse_ticker_message(ticker.dump()), ErrorCode::InvalidInput);
    }
    struct InvalidField {
        const char *field;
        Json value;
        ErrorCode error;
    };
    const InvalidField cases[]{{"product_id", nullptr, ErrorCode::InvalidInput},
                               {"product_id", "", ErrorCode::InvalidInput},
                               {"price", true, ErrorCode::InvalidInput},
                               {"time", 1, ErrorCode::InvalidInput},
                               {"trade_id", -1, ErrorCode::InvalidInput},
                               {"trade_id", 1.0, ErrorCode::InvalidInput},
                               {"trade_id", "123", ErrorCode::InvalidInput},
                               {"trade_id", 1.8446744073709552e19, ErrorCode::InvalidInput},
                               {"price", "-1", ErrorCode::InvalidInput},
                               {"price", "NaN", ErrorCode::InvalidInput},
                               {"price", "1e99999", ErrorCode::OutOfRange},
                               {"price", "1e-99999", ErrorCode::OutOfRange},
                               {"time", "2026-02-30T10:00:00Z", ErrorCode::InvalidInput}};
    for (const auto &[field, value, error] : cases) {
        SCOPED_TRACE(std::string(field) + ": " + value.dump());
        auto ticker = Json::parse(valid_ticker);
        ticker[field] = value;
        ASSERT_RESULT_ERROR(parse_ticker_message(ticker.dump()), error);
    }
}

TEST(TickerParser, SurfacesExchangeErrors) {
    const auto error = parse_ticker_message(R"({"type":"error","message":"Invalid product"})");
    ASSERT_RESULT_ERROR(error, ErrorCode::Protocol);
    EXPECT_NE(error.error().message.find("Invalid product"), std::string::npos);
    ASSERT_RESULT_ERROR(parse_ticker_message(R"({"type":"error"})"), ErrorCode::Protocol);
    ASSERT_RESULT_ERROR(parse_ticker_message(R"({"type":"error","message":42})"), ErrorCode::Protocol);
}

} // namespace
} // namespace coinbase_ticker_statistics
