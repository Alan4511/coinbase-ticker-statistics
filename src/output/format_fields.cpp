#include "output/format_fields.hpp"

#include <iomanip>
#include <locale>
#include <sstream>

namespace coinbase_ticker_statistics {
namespace {
constexpr unsigned timestamp_fraction_digits = 9;
constexpr unsigned minimum_year = 1970;
constexpr unsigned maximum_year = 2200;
} // namespace

Result<std::string> format_price(Price price) {
    std::string text;
    if (auto formatted = append_number(text, price); !formatted)
        return std::unexpected(formatted.error());
    return text;
}

Result<std::string> format_utc_timestamp(Timestamp timestamp) {
    const auto days = std::chrono::floor<std::chrono::days>(timestamp);
    const std::chrono::year_month_day date{days};
    const int year = static_cast<int>(date.year());
    if (year < static_cast<int>(minimum_year) || year > static_cast<int>(maximum_year)) {
        return fail(ErrorCode::OutOfRange, "Timestamp year must be between 1970 and 2200");
    }
    const std::chrono::hh_mm_ss time{timestamp - days};
    constexpr int year_width = 4;
    constexpr int component_width = 2;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setfill('0') << std::setw(year_width) << year << '-' << std::setw(component_width)
           << static_cast<unsigned>(date.month()) << '-' << std::setw(component_width)
           << static_cast<unsigned>(date.day()) << 'T' << std::setw(component_width) << time.hours().count() << ':'
           << std::setw(component_width) << time.minutes().count() << ':' << std::setw(component_width)
           << time.seconds().count() << '.' << std::setw(static_cast<int>(timestamp_fraction_digits))
           << time.subseconds().count() << 'Z';
    return output.str();
}

} // namespace coinbase_ticker_statistics
