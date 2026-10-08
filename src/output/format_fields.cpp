#include "output/format_fields.hpp"

#include <format>
#include <limits>

namespace coinbase_ticker_statistics {
namespace {
constexpr unsigned minimum_year = 1970;
constexpr unsigned maximum_year = 2200;
} // namespace

Result<void> append_number(std::string &destination, Price value) {
    if (value.ticks < 0)
        return fail(ErrorCode::InvalidInput, "Price must be nonnegative");
    const auto whole = static_cast<std::uint64_t>(value.ticks / Price::ticks_per_unit);
    if (auto written = append_number(destination, whole); !written)
        return written;
    auto fraction = value.ticks % Price::ticks_per_unit;
    if (fraction == 0)
        return {};
    destination += '.';
    // Fixed width preserves leading fractional zeros; stop after the last nonzero digit.
    for (auto divisor = Price::ticks_per_unit / 10; fraction != 0; divisor /= 10) {
        destination += static_cast<char>('0' + fraction / divisor);
        fraction %= divisor;
    }
    return {};
}

Result<void> append_number(std::string &destination, const Statistic &value) {
    if (value.denominator == 0)
        return fail(ErrorCode::InvalidInput, "Statistic denominator must be positive");
    PriceSum ticks = value.numerator / value.denominator;
    const PriceSum remainder = value.numerator % value.denominator;
    const PriceSum complement = PriceSum{value.denominator} - remainder;
    const bool round_up = remainder > complement || (remainder == complement && (ticks & 1) != 0);
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (ticks > maximum || (ticks == maximum && round_up))
        return fail(ErrorCode::OutOfRange, "Statistic exceeds the fixed-point price range");
    if (round_up)
        ++ticks;
    return append_number(destination, Price{ticks.convert_to<std::int64_t>()});
}

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
    return std::format("{:%FT%TZ}", timestamp);
}

} // namespace coinbase_ticker_statistics
