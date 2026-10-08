#pragma once

#include "csv_config.hpp"

#include <glaze/json.hpp>

#include <string_view>

namespace coinbase_ticker_statistics::output_json::key {
inline constexpr std::string_view path = "path";
inline constexpr std::string_view flush_every_rows = "flush_every_rows";
inline constexpr std::string_view flush_interval_ms = "flush_interval_ms";
} // namespace coinbase_ticker_statistics::output_json::key

namespace glz {

template <>
struct meta<coinbase_ticker_statistics::CsvConfig> {
    using T = coinbase_ticker_statistics::CsvConfig;
    static constexpr auto value = [] {
        using namespace coinbase_ticker_statistics;
        using namespace output_json;
        return object(key::path,
                      &T::path,
                      key::flush_every_rows,
                      &T::flush_every_rows,
                      key::flush_interval_ms,
                      &T::flush_interval);
    }();
    static constexpr bool requires_key(std::string_view name, bool) {
        return name == coinbase_ticker_statistics::output_json::key::path;
    }
};

} // namespace glz
