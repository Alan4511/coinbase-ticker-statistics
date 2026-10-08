#include "format_timestamp.hpp"

#include <chrono>
#include <format>
#include <iterator>
#include <utility>

namespace coinbase_ticker_statistics {
namespace {
constexpr unsigned minimum_year = 1970;
constexpr unsigned maximum_year = 2200;
} // namespace

Result<void> append_utc_timestamp(std::string &destination, Timestamp timestamp) {
    const auto days = std::chrono::floor<std::chrono::days>(timestamp);
    const std::chrono::year_month_day date{days};
    const int year = static_cast<int>(date.year());
    if (year < static_cast<int>(minimum_year) || year > static_cast<int>(maximum_year)) {
        return fail(ErrorCode::OutOfRange, "Timestamp year must be between 1970 and 2200");
    }
    std::format_to(std::back_inserter(destination), "{:%FT%TZ}", timestamp);
    return {};
}

Result<std::string> format_utc_timestamp(Timestamp timestamp) {
    std::string text;
    if (auto formatted = append_utc_timestamp(text, timestamp); !formatted.has_value())
        return std::unexpected(std::move(formatted.error()));
    return text;
}

} // namespace coinbase_ticker_statistics
