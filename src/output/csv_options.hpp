#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <utility>

namespace coinbase_ticker_statistics {

/** File and formatting policy for one independently configured CSV sink. */
struct CsvConfig {
    explicit CsvConfig(std::filesystem::path output_path) : path(std::move(output_path)) {
    }

    std::filesystem::path path;
    std::size_t flush_every_rows{1};
    std::chrono::milliseconds flush_interval{1000};
};

} // namespace coinbase_ticker_statistics
