#pragma once

#include "feed/transport/feed_options.hpp"
#include "output/csv_options.hpp"
#include "result.hpp"
#include "statistics/window_options.hpp"
#include "types.hpp"

#include <filesystem>
#include <string_view>
#include <vector>

namespace coinbase_ticker_statistics {

/** One unauthenticated ticker subscription; each symbol belongs to exactly one connection. */
struct ConnectionConfig {
    Symbols symbols;
};
using Connections = std::vector<ConnectionConfig>;

/** Application ownership and operating policy. */
struct Config {
    Connections connections;
    FeedConfig feed;
    WindowOptions window;
    CsvConfig output;
    /** Flatten the configured connection groups for independent per-symbol routing. */
    [[nodiscard]] Symbols symbols() const;
};

/** Require connection groups and an output path; validate optional settings without coercion. */
[[nodiscard]] Result<Config> parse_config(std::string_view text);

/** Load JSON; resolve output paths relative to the configuration file. */
[[nodiscard]] Result<Config> load_config(const std::filesystem::path &path);

} // namespace coinbase_ticker_statistics
