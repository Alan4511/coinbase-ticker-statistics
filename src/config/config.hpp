#pragma once

#include <feed/transport/feed_config.hpp>
#include <output/csv_config.hpp>
#include <result.hpp>
#include <statistics/window_options.hpp>
#include <types.hpp>

#include <filesystem>
#include <string_view>

namespace coinbase_ticker_statistics {

/** Application settings. Parsing/loading validates them; direct edits require validation again. */
struct Config {
    Symbols symbols;
    FeedConfig feed;
    WindowOptions window;
    CsvConfig output;
};

/** Coordinate module policies and application constraints without starting I/O. */
[[nodiscard]] Result<void> validate_config(const Config &config);

/** Parse and validate all settings without coercion; success satisfies run_application's precondition. */
[[nodiscard]] Result<Config> parse_config(std::string_view text);

/** Load validated JSON and resolve output paths; success satisfies run_application's precondition. */
[[nodiscard]] Result<Config> load_config(const std::filesystem::path &path);

} // namespace coinbase_ticker_statistics
