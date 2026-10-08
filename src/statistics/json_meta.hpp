#pragma once

#include "window_options.hpp"

#include <glaze/json.hpp>

#include <string_view>

namespace coinbase_ticker_statistics::statistics_json::key {
inline constexpr std::string_view duration_seconds = "duration_seconds";
} // namespace coinbase_ticker_statistics::statistics_json::key

namespace glz {

template <>
struct meta<coinbase_ticker_statistics::WindowOptions> {
    using T = coinbase_ticker_statistics::WindowOptions;
    static constexpr auto value =
        object(coinbase_ticker_statistics::statistics_json::key::duration_seconds, &T::duration);
    static constexpr bool requires_key(std::string_view, bool) {
        return false;
    }
};

} // namespace glz
