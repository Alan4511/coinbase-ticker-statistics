#include "feed/parser/parse_fields.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <concepts>
#include <locale>
#include <sstream>

namespace coinbase_ticker_statistics {
namespace {

constexpr unsigned decimal_base = 10;
constexpr unsigned timestamp_fraction_digits = 9;
constexpr unsigned minimum_year = 1970;
constexpr unsigned maximum_year = 2200;
constexpr unsigned hours_per_day = 24;
constexpr unsigned minutes_per_hour = 60;
constexpr unsigned seconds_per_minute = 60;
constexpr std::size_t timestamp_seconds_length = 19;

bool is_decimal_digit(char value) {
    return value >= '0' && value <= '9';
}

unsigned parse_decimal_digits(std::string_view text, std::size_t start, std::size_t count) {
    unsigned result{};
    for (const char digit : text.substr(start, count)) {
        // Callers have already validated every character as a decimal digit.
        result = result * decimal_base + static_cast<unsigned>(digit - '0');
    }
    return result;
}

/** Some supported libc++ versions lack floating-point from_chars. Never narrow to double. */
template <std::floating_point Number>
Result<Number> parse_decimal_number(std::string_view text) {
    Number value{};
    if constexpr (requires { std::from_chars(text.data(), text.data() + text.size(), value); }) {
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error == std::errc::result_out_of_range)
            return fail(ErrorCode::OutOfRange, "Price overflow or underflow");
        if (error != std::errc{} || end != text.data() + text.size())
            return fail(ErrorCode::InvalidInput, "Invalid price syntax");
    }
    else {
        std::istringstream input{std::string(text)};
        input.imbue(std::locale::classic());
        input >> std::noskipws >> value;
        // libc++ sets failbit even when conversion produced a representable subnormal.
        // Syntax was already checked; preserve that value, while rejecting underflow to zero.
        if (!input && (input.bad() || std::fpclassify(value) != FP_SUBNORMAL))
            return fail(ErrorCode::OutOfRange, "Price overflow or underflow");
    }
    return value;
}

} // namespace

Result<Price> parse_price(std::string_view text) {
    // Validate syntax before the locale-independent library conversion; this avoids
    // exceptions, whitespace acceptance, and NaN/Inf entering ordered containers.
    if (text.empty() || !is_decimal_digit(text.front())) {
        return fail(ErrorCode::InvalidInput, "Price must begin with a decimal digit");
    }
    std::size_t position{};
    bool nonzero{};
    const auto consume_decimal_digits = [&text, &position, &nonzero] {
        const auto start = position;
        while (position < text.size() && is_decimal_digit(text[position])) {
            nonzero = nonzero || text[position] != '0';
            ++position;
        }
        return position != start;
    };
    consume_decimal_digits();
    if (position < text.size() && text[position] == '.') {
        ++position;
        if (!consume_decimal_digits())
            return fail(ErrorCode::InvalidInput, "Missing fractional digits");
    }
    const bool nonzero_significand = nonzero;
    if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
        ++position;
        if (position < text.size() && (text[position] == '+' || text[position] == '-'))
            ++position;
        if (!consume_decimal_digits())
            return fail(ErrorCode::InvalidInput, "Missing exponent digits");
    }
    if (position != text.size())
        return fail(ErrorCode::InvalidInput, "Invalid price syntax");

    const auto price = parse_decimal_number<Price>(text);
    if (!price)
        return std::unexpected(price.error());
    if (!std::isfinite(*price) || (*price == 0 && nonzero_significand)) {
        return fail(ErrorCode::OutOfRange, "Price overflow or underflow");
    }
    return price;
}

Result<Timestamp> parse_utc_timestamp(std::string_view text) {
    constexpr std::size_t shortest_timestamp = timestamp_seconds_length + 1;
    constexpr std::size_t longest_timestamp = timestamp_seconds_length + 1 + timestamp_fraction_digits + 1;
    if (text.size() < shortest_timestamp || text.size() > longest_timestamp || text.back() != 'Z') {
        return fail(ErrorCode::InvalidInput, "Expected a UTC timestamp ending in Z");
    }
    constexpr std::size_t year_separator = 4;
    constexpr std::size_t month_separator = 7;
    constexpr std::size_t date_separator = 10;
    constexpr std::size_t hour_separator = 13;
    constexpr std::size_t minute_separator = 16;
    if (text[year_separator] != '-' || text[month_separator] != '-' || text[date_separator] != 'T' ||
        text[hour_separator] != ':' || text[minute_separator] != ':') {
        return fail(ErrorCode::InvalidInput, "Expected YYYY-MM-DDTHH:MM:SS[.fraction]Z");
    }
    for (std::size_t index = 0; index < timestamp_seconds_length; ++index) {
        const bool separator = index == year_separator || index == month_separator || index == date_separator ||
                               index == hour_separator || index == minute_separator;
        if (!separator && !is_decimal_digit(text[index])) {
            return fail(ErrorCode::InvalidInput, "Expected a decimal digit in timestamp");
        }
    }
    constexpr std::size_t year_digits = 4;
    constexpr std::size_t component_digits = 2;
    const unsigned parsed_year = parse_decimal_digits(text, 0, year_digits);
    const unsigned parsed_month = parse_decimal_digits(text, year_separator + 1, component_digits);
    const unsigned parsed_day = parse_decimal_digits(text, month_separator + 1, component_digits);
    const unsigned parsed_hour = parse_decimal_digits(text, date_separator + 1, component_digits);
    const unsigned parsed_minute = parse_decimal_digits(text, hour_separator + 1, component_digits);
    const unsigned parsed_second = parse_decimal_digits(text, minute_separator + 1, component_digits);
    if (parsed_year < minimum_year || parsed_year > maximum_year || parsed_hour >= hours_per_day ||
        parsed_minute >= minutes_per_hour || parsed_second >= seconds_per_minute) {
        return fail(ErrorCode::InvalidInput, "Timestamp component is out of range");
    }
    const std::chrono::year_month_day date{std::chrono::year{static_cast<int>(parsed_year)},
                                           std::chrono::month{parsed_month},
                                           std::chrono::day{parsed_day}};
    if (!date.ok()) {
        return fail(ErrorCode::InvalidInput, "Timestamp contains an invalid calendar date");
    }

    unsigned fraction{};
    if (text.size() > shortest_timestamp) {
        constexpr std::size_t fraction_start = timestamp_seconds_length + 1;
        if (text[timestamp_seconds_length] != '.' || text.size() == fraction_start + 1) {
            return fail(ErrorCode::InvalidInput, "Timestamp fraction must contain one to nine decimal digits");
        }
        const std::size_t fraction_digits = text.size() - fraction_start - 1;
        const auto fractional_text = text.substr(fraction_start, fraction_digits);
        if (!std::ranges::all_of(fractional_text, is_decimal_digit))
            return fail(ErrorCode::InvalidInput, "Expected a decimal digit in timestamp fraction");
        fraction = parse_decimal_digits(text, fraction_start, fraction_digits);
        for (std::size_t index = fraction_digits; index < timestamp_fraction_digits; ++index) {
            fraction *= decimal_base;
        }
    }
    return std::chrono::sys_days{date} + std::chrono::hours{parsed_hour} + std::chrono::minutes{parsed_minute} +
           std::chrono::seconds{parsed_second} + std::chrono::nanoseconds{fraction};
}

} // namespace coinbase_ticker_statistics
