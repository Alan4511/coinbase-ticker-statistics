#include "test_json.hpp"
#include "test_result.hpp"
#include <feed/subscription.hpp>

#include <glaze/json.hpp>
#include <gtest/gtest.h>

namespace coinbase_ticker_statistics {
namespace {

TEST(Subscription, BuildsAndValidatesMultiSymbolSubscription) {
    ASSERT_RESULT_VALUE(subscription, encode_ticker_subscription({"BTC-USD", "ETH-USD", "SOL-USD"}));
    const auto document = glz::read_json<test::JsonFields>(subscription).value();
    EXPECT_EQ(document.size(), 3U);
    EXPECT_EQ(document.at("type").str, R"("subscribe")");
    EXPECT_EQ(glz::read_json<Symbols>(document.at("product_ids").str).value(),
              (Symbols{"BTC-USD", "ETH-USD", "SOL-USD"}));
    EXPECT_EQ(glz::read_json<Symbols>(document.at("channels").str).value(), Symbols{"ticker"});
    EXPECT_FALSE(document.contains("api_key"));
    EXPECT_FALSE(document.contains("signature"));
    ASSERT_RESULT_ERROR(encode_ticker_subscription({}), ErrorCode::InvalidConfiguration);
    ASSERT_RESULT_OK(validate(Symbols{"BTC-USD", "ETH-USD"}));
    ASSERT_RESULT_ERROR(validate(Symbols{}), ErrorCode::InvalidConfiguration);
    for (const auto *symbol : {"", "BTC", "-BTC-USD", "BTC-USD-", "BTC--USD", "btc-usd", "BTC/USD", "BTC USD"}) {
        SCOPED_TRACE(symbol);
        ASSERT_RESULT_ERROR(validate(Symbols{symbol}), ErrorCode::InvalidConfiguration);
        ASSERT_RESULT_ERROR(encode_ticker_subscription({symbol}), ErrorCode::InvalidConfiguration);
    }
}

} // namespace
} // namespace coinbase_ticker_statistics
