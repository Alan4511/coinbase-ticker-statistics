#pragma once

#include "json_meta.hpp"
#include "settings.hpp"
#include <common/result.hpp>

#include <filesystem>
#include <string_view>
#include <utility>

namespace coinbase_ticker_statistics {

/** Coordinate module policies and application constraints without starting I/O. */
[[nodiscard]] Result<void> validate_config(const Config &config);

/** Parse and validate settings; the local object is returned only after all checks succeed. */
[[nodiscard]] inline Result<Config> parse_and_validate_config(std::string_view input) {
    return json_utils::read_json<Config>(input, ErrorCode::InvalidConfiguration).and_then([](Config config) {
        return validate_config(config).transform([&] {
            return std::move(config);
        });
    });
}

/** Load validated JSON and resolve output paths; success satisfies run_application's precondition. */
[[nodiscard]] Result<Config> load_config(const std::filesystem::path &path);

} // namespace coinbase_ticker_statistics
