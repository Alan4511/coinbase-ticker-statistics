#include "format_timestamp.hpp"

#include <chrono>
#include <format>

namespace coinbase_ticker_statistics {
namespace {
constexpr unsigned minimum_year = 1970;
constexpr unsigned maximum_year = 2200;
} // namespace

Result<std::string> format_utc_timestamp(Timestamp timestamp) {
    const auto days = std::chrono::floor<std::chrono::days>(timestamp);
    const std::chrono::year_month_day date{days};
    const int year = static_cast<int>(date.year());
    if (year < static_cast<int>(minimum_year) || year > static_cast<int>(maximum_year)) {
        return fail(ErrorCode::OutOfRange, "Timestamp year must be between 1970 and 2200");
    }
    return std::format("{:%FT%TZ}", timestamp);
}

} // namespace coinbase_ticker_statistics
