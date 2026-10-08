#pragma once

#include <common/types.hpp>
#include <feed/transport/feed_config.hpp>
#include <output/csv_config.hpp>
#include <statistics/window_options.hpp>

namespace coinbase_ticker_statistics {

/** Application settings. Parsing/loading validates them; direct edits require validation again. */
struct Config {
    Symbols symbols;
    FeedConfig feed;
    WindowOptions window;
    CsvConfig output;
};

} // namespace coinbase_ticker_statistics
