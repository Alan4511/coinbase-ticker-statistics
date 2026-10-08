#pragma once

#include <common/result.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <utility>

namespace coinbase_ticker_statistics {

/** Destination and continuous-output flushing policy for the CSV sink. */
struct CsvConfig {
    CsvConfig() = default;

    explicit CsvConfig(std::filesystem::path output_path) : path(std::move(output_path)) {
    }

    std::filesystem::path path;
    std::size_t flush_every_rows{100};
    std::chrono::milliseconds flush_interval{250};
};

[[nodiscard]] inline Result<void> validate(const CsvConfig &config) {
    if (config.path.empty())
        return fail(ErrorCode::InvalidConfiguration, "output path must not be empty");
    if (config.flush_every_rows == 0)
        return fail(ErrorCode::InvalidConfiguration, "output.flush_every_rows must be positive");
    if (config.flush_interval <= std::chrono::milliseconds::zero() || config.flush_interval > std::chrono::days{365})
        return fail(ErrorCode::InvalidConfiguration, "output.flush_interval_ms must be between 1 ms and 365 days");
    return {};
}

} // namespace coinbase_ticker_statistics
