#include "test_json.hpp"
#include "test_result.hpp"
#include <feed/parser/parse_fields.hpp>
#include <feed/parser/ticker_parser.hpp>

#include <glaze/json.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <limits>
#include <string>

namespace coinbase_ticker_statistics {
namespace {

using namespace std::chrono_literals;

constexpr auto valid_ticker = R"({
    "type":"ticker", "time":"2026-10-03T10:20:30.123456789Z", "product_id":"BTC-USD",
    "trade_id":18446744073709551615, "price":"12345.67890123", "volume_24h":"10000"
})";

TEST(TickerParser, DecodesTickerAndFiltersControlMessages) {
    ASSERT_RESULT_VALUE(received, parse_utc_timestamp("2026-10-03T10:20:31Z"));
    ASSERT_RESULT_VALUE(result, parse_ticker_message(valid_ticker));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().symbol, "BTC-USD");
    EXPECT_EQ(result.value().trade_id, std::numeric_limits<TradeId>::max());
    EXPECT_EQ(result.value().price, Price{1'234'567'890'123});
    EXPECT_EQ(result.value().exchange_time, received - 1s + 123'456'789ns);
    // Only the framed view belongs to the message; the next byte is deliberately not a terminator.
    const std::string framed_message = std::string(valid_ticker) + "not part of the frame";
    ASSERT_RESULT_OK(
        parse_ticker_message(std::string_view(framed_message).substr(0, std::string_view(valid_ticker).size())));
    for (const auto *message : {R"({"type":"subscriptions","channels":[]})",
                                R"({"type":"heartbeat"})",
                                R"({"type":"future-message-kind","payload":{}})"}) {
        SCOPED_TRACE(message);
        ASSERT_RESULT_VALUE(ignored, parse_ticker_message(message));
        EXPECT_FALSE(ignored.has_value());
    }

    auto document = glz::read_json<test::JsonFields>(valid_ticker).value();
    for (const auto &[number, expected] :
         {std::pair{"1e2", TradeId{100}}, std::pair{"1844674407370955161e1", TradeId{18'446'744'073'709'551'610ULL}}}) {
        SCOPED_TRACE(number);
        document["trade_id"].str = number;
        ASSERT_RESULT_VALUE(parsed, parse_ticker_message(glz::write_json(document).value()));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(parsed.value().trade_id, expected);
    }

    ASSERT_RESULT_VALUE(update, parse_ticker_message(R"({
        "type":"ticker", "product_id":"BTC-USD", "trade_id":1,
        "price":"\u0031.25", "time":"2026-10-03T10:20:30\u005a"
    })"));
    ASSERT_TRUE(update.has_value());
    EXPECT_EQ(update.value().price, Price{125'000'000});
    EXPECT_EQ(update.value().exchange_time, received - 1s);
}

TEST(TickerParser, RejectsMalformedTickerMessages) {
    for (const auto *message : {"not json",
                                "[]",
                                "null",
                                "{}",
                                R"({"type":1})",
                                R"({"type":""})",
                                R"({"type":"heartbeat","unused":[1,]})",
                                R"({"type":"heartbeat","unused":1e})",
                                R"({"type":"heartbeat","unused":"\q"})",
                                R"({"type":"heartbeat"} trailing)"}) {
        SCOPED_TRACE(message);
        ASSERT_RESULT_ERROR(parse_ticker_message(message), ErrorCode::InvalidInput);
    }
    for (const auto *field : {"product_id", "time", "trade_id", "price"}) {
        SCOPED_TRACE(field);
        auto ticker = glz::read_json<test::JsonFields>(valid_ticker).value();
        ticker.erase(field);
        ASSERT_RESULT_ERROR(parse_ticker_message(glz::write_json(ticker).value()), ErrorCode::InvalidInput);
    }
    struct InvalidField {
        const char *field;
        std::string_view json;
        ErrorCode error;
    };
    constexpr InvalidField cases[]{{"product_id", "null", ErrorCode::InvalidInput},
                                   {"product_id", R"("")", ErrorCode::InvalidInput},
                                   {"price", R"("")", ErrorCode::InvalidInput},
                                   {"time", R"("")", ErrorCode::InvalidInput},
                                   {"price", "true", ErrorCode::InvalidInput},
                                   {"time", "1", ErrorCode::InvalidInput},
                                   {"trade_id", "-1", ErrorCode::InvalidInput},
                                   {"trade_id", "true", ErrorCode::InvalidInput},
                                   {"trade_id", "null", ErrorCode::InvalidInput},
                                   {"trade_id", "1.0", ErrorCode::InvalidInput},
                                   {"trade_id", "1e-2", ErrorCode::InvalidInput},
                                   {"trade_id", "1.5", ErrorCode::InvalidInput},
                                   {"trade_id", "1e20", ErrorCode::InvalidInput},
                                   {"trade_id", "1e", ErrorCode::InvalidInput},
                                   {"trade_id", R"("123")", ErrorCode::InvalidInput},
                                   {"trade_id", "1.8446744073709552e19", ErrorCode::InvalidInput},
                                   {"price", R"("-1")", ErrorCode::InvalidInput},
                                   {"price", R"("NaN")", ErrorCode::InvalidInput},
                                   {"price", R"("1e99999")", ErrorCode::OutOfRange},
                                   {"price", R"("1e-99999")", ErrorCode::OutOfRange},
                                   {"price", R"("1.000000001")", ErrorCode::OutOfRange},
                                   {"time", R"("2026-02-30T10:00:00Z")", ErrorCode::InvalidInput}};
    for (const auto &[field, json, error] : cases) {
        SCOPED_TRACE(std::string(field) + ": " + std::string(json));
        auto ticker = glz::read_json<test::JsonFields>(valid_ticker).value();
        ticker[field].str = json;
        const auto result = parse_ticker_message(glz::write_json(ticker).value());
        ASSERT_RESULT_ERROR(result, error);
        if (error == ErrorCode::OutOfRange)
            EXPECT_TRUE(result.error().message.contains("price")) << result.error().message;
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
