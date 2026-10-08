#pragma once

#include "settings.hpp"
#include <common/json.hpp>
#include <feed/json_meta.hpp>
#include <output/json_meta.hpp>
#include <statistics/json_meta.hpp>

#include <glaze/json.hpp>

#include <string_view>
#include <utility>

namespace coinbase_ticker_statistics::config_json::key {
inline constexpr std::string_view symbols = "symbols";
inline constexpr std::string_view feed = "feed";
inline constexpr std::string_view window = "window";
inline constexpr std::string_view output = "output";
} // namespace coinbase_ticker_statistics::config_json::key

namespace glz {

template <>
struct meta<coinbase_ticker_statistics::Config> {
    using T = coinbase_ticker_statistics::Config;
    // Each section is decoded into a fresh value so duplicate objects replace, rather than merge.
    static constexpr auto read_feed = [](T &config, coinbase_ticker_statistics::FeedConfig feed) {
        config.feed = std::move(feed);
    };
    static constexpr auto read_window = [](T &config, coinbase_ticker_statistics::WindowOptions window) {
        config.window = window;
    };
    static constexpr auto read_output = [](T &config, coinbase_ticker_statistics::CsvConfig output) {
        config.output = std::move(output);
    };
    static constexpr auto value = [] {
        using namespace coinbase_ticker_statistics::config_json;
        return object(key::symbols,
                      &T::symbols,
                      key::feed,
                      custom<read_feed, &T::feed>,
                      key::window,
                      custom<read_window, &T::window>,
                      key::output,
                      custom<read_output, &T::output>);
    }();
    static constexpr bool requires_key(std::string_view name, bool) {
        using namespace coinbase_ticker_statistics::config_json;
        return name == key::symbols || name == key::output;
    }
};

} // namespace glz
