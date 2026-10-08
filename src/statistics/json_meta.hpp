#pragma once

#include "window_options.hpp"

#include <glaze/json.hpp>

#include <string_view>

namespace coinbase_ticker_statistics::statistics_json::key {
inline constexpr std::string_view duration_seconds = "duration_seconds";
inline constexpr std::string_view max_observations_per_symbol = "max_observations_per_symbol";
} // namespace coinbase_ticker_statistics::statistics_json::key

namespace glz {

template <>
struct meta<coinbase_ticker_statistics::WindowOptions> {
    using T = coinbase_ticker_statistics::WindowOptions;
    static constexpr auto value = [] {
        using namespace coinbase_ticker_statistics::statistics_json;
        return object(key::duration_seconds,
                      &T::duration,
                      key::max_observations_per_symbol,
                      &T::max_observations_per_symbol);
    }();
    // Omitted settings keep WindowOptions' operational defaults.
    static constexpr bool requires_key(std::string_view, bool) {
        return false;
    }
};

} // namespace glz
