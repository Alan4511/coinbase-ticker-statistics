#include "test_result.hpp"
#include <feed/subscription.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace coinbase_ticker_statistics {
namespace {
using Json = nlohmann::json;

TEST(Subscription, BuildsPublicMultiSymbolTickerSubscription) {
    ASSERT_RESULT_VALUE(subscription, encode_ticker_subscription({"BTC-USD", "ETH-USD", "SOL-USD"}));
    const auto document = Json::parse(subscription);
    EXPECT_EQ(document.size(), 3U);
    EXPECT_EQ(document.at("type"), "subscribe");
    EXPECT_EQ(document.at("product_ids"), Json::array({"BTC-USD", "ETH-USD", "SOL-USD"}));
    EXPECT_EQ(document.at("channels"), Json::array({"ticker"}));
    EXPECT_FALSE(document.contains("api_key"));
    EXPECT_FALSE(document.contains("signature"));
    ASSERT_RESULT_ERROR(encode_ticker_subscription({}), ErrorCode::InvalidConfiguration);
}

TEST(Subscription, ValidatesProductIdsWithoutSerialization) {
    ASSERT_RESULT_OK(validate_product_ids({"BTC-USD", "ETH-USD"}));
    ASSERT_RESULT_ERROR(validate_product_ids({}), ErrorCode::InvalidConfiguration);
    for (const auto *symbol : {"", "BTC", "-BTC-USD", "BTC-USD-", "BTC--USD", "btc-usd", "BTC/USD", "BTC USD"}) {
        ASSERT_RESULT_ERROR(validate_product_ids({symbol}), ErrorCode::InvalidConfiguration);
        ASSERT_RESULT_ERROR(encode_ticker_subscription({symbol}), ErrorCode::InvalidConfiguration);
    }
}

} // namespace
} // namespace coinbase_ticker_statistics
