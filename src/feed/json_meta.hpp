#pragma once

#include "parser/parse_fields.hpp"
#include "transport/feed_config.hpp"
#include <common/json.hpp>
#include <common/types.hpp>

#include <glaze/json.hpp>

#include <array>
#include <string>
#include <string_view>

namespace coinbase_ticker_statistics::feed_json {

namespace key {
inline constexpr std::string_view type = "type";
inline constexpr std::string_view ticker = "ticker";
inline constexpr std::string_view error = "error";
inline constexpr std::string_view message = "message";
inline constexpr std::string_view time = "time";
inline constexpr std::string_view product_id = "product_id";
inline constexpr std::string_view trade_id = "trade_id";
inline constexpr std::string_view price = "price";
inline constexpr std::string_view host = "host";
inline constexpr std::string_view port = "port";
inline constexpr std::string_view target = "target";
inline constexpr std::string_view connect_timeout_seconds = "connect_timeout_seconds";
inline constexpr std::string_view close_timeout_seconds = "close_timeout_seconds";
inline constexpr std::string_view max_message_bytes = "max_message_bytes";
inline constexpr std::string_view subscribe = "subscribe";
inline constexpr std::string_view product_ids = "product_ids";
inline constexpr std::string_view channels = "channels";
} // namespace key

/** Read the discriminator first so control messages need no ticker fields. */
struct MessageHeader {
    std::string type;
    glz::raw_json_view message;
};

/** Wire view borrows the symbol list for synchronous subscription encoding. */
struct SubscriptionRequest {
    std::string_view type;
    const Symbols &product_ids;
    std::array<std::string_view, 1> channels;
};

} // namespace coinbase_ticker_statistics::feed_json

// Glaze metadata maps wire-field names to existing C++ members without
// putting serialization annotations on the domain types.
namespace glz {

template <>
struct meta<coinbase_ticker_statistics::FeedConfig> {
    using T = coinbase_ticker_statistics::FeedConfig;
    static constexpr auto value = [] {
        using namespace coinbase_ticker_statistics;
        using namespace feed_json;
        return object(key::host,
                      &T::host,
                      key::port,
                      &T::port,
                      key::target,
                      &T::target,
                      key::connect_timeout_seconds,
                      &T::connect_timeout,
                      key::close_timeout_seconds,
                      &T::close_timeout,
                      key::max_message_bytes,
                      &T::max_message_bytes);
    }();
    // Omitted connection settings retain FeedConfig's operational defaults.
    static constexpr bool requires_key(std::string_view, bool) {
        return false;
    }
};

// Coinbase encodes price as a JSON string. Decode it with Glaze, then reuse
// the domain parser so precision and range rules have one owner.
template <>
struct from<JSON, coinbase_ticker_statistics::Price> {
    template <auto Options>
    static void op(auto &price, is_context auto &&context, auto &&current, auto end) {
        std::string text;
        parse<JSON>::op<Options>(text, context, current, end);
        if (parse_failed(context.error))
            return;
        coinbase_ticker_statistics::json_utils::assign_parsed(price,
                                                              coinbase_ticker_statistics::parse_price(text),
                                                              context);
    }
};

// Glaze decodes the JSON string; the domain parser owns UTC syntax, calendar
// validation and nanosecond conversion, keeping timestamp rules in one place.
template <>
struct from<JSON, coinbase_ticker_statistics::Timestamp> {
    template <auto Options>
    static void op(auto &timestamp, is_context auto &&context, auto &&current, auto end) {
        std::string text;
        parse<JSON>::op<Options>(text, context, current, end);
        if (parse_failed(context.error))
            return;
        coinbase_ticker_statistics::json_utils::assign_parsed(timestamp,
                                                              coinbase_ticker_statistics::parse_utc_timestamp(text),
                                                              context);
    }
};

template <>
struct meta<coinbase_ticker_statistics::feed_json::MessageHeader> {
    using T = coinbase_ticker_statistics::feed_json::MessageHeader;
    static constexpr auto value = [] {
        using namespace coinbase_ticker_statistics::feed_json;
        return object(key::type, &T::type, key::message, &T::message);
    }();
    static constexpr bool requires_key(std::string_view name, bool) {
        return name == coinbase_ticker_statistics::feed_json::key::type;
    }
};

template <>
struct meta<coinbase_ticker_statistics::TickerUpdate> {
    using T = coinbase_ticker_statistics::TickerUpdate;
    static constexpr auto value = [] {
        using namespace coinbase_ticker_statistics;
        using namespace feed_json;
        return object(key::product_id,
                      &T::symbol,
                      key::time,
                      &T::exchange_time,
                      key::price,
                      &T::price,
                      key::trade_id,
                      &T::trade_id);
    }();
};

template <>
struct meta<coinbase_ticker_statistics::feed_json::SubscriptionRequest> {
    using T = coinbase_ticker_statistics::feed_json::SubscriptionRequest;
    static constexpr auto value = [] {
        using namespace coinbase_ticker_statistics::feed_json;
        return object(
            key::type,
            &T::type,
            key::product_ids,
            [](const T &request) -> const auto & {
                return request.product_ids;
            },
            key::channels,
            &T::channels);
    }();
};

} // namespace glz
