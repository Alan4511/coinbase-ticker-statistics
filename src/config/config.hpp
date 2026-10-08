#pragma once

#include "settings.hpp"
#include <common/result.hpp>

#include <filesystem>
#include <string_view>

namespace coinbase_ticker_statistics {

/** Coordinate module policies and application constraints without starting I/O. */
[[nodiscard]] Result<void> validate_config(const Config &config);

/** Parse and validate settings; the local object is returned only after all checks succeed. */
[[nodiscard]] Result<Config> parse_and_validate_config(std::string_view input);

/** Load validated JSON and resolve output paths; success satisfies run_application's precondition. */
[[nodiscard]] Result<Config> load_config(const std::filesystem::path &path);

} // namespace coinbase_ticker_statistics
