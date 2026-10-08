#include "feed/parser/parse_fields.hpp"

#include <boost/date_time/posix_time/posix_time.hpp>

#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace coinbase_ticker_statistics {
namespace {

Result<std::uint64_t> parse_price_digits(std::string_view digits) {
    if (digits.empty())
        return 0;
    std::uint64_t value{};
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (error == std::errc::invalid_argument || end != digits.data() + digits.size())
        return fail(ErrorCode::InvalidInput, "Price must contain only decimal digits and an optional decimal point");
    if (error == std::errc::result_out_of_range)
        return fail(ErrorCode::OutOfRange, "Price exceeds its fixed-point range");
    return value;
}

} // namespace

Result<Price> parse_price(std::string_view text) {
    const auto decimal_point = text.find('.');
    const auto whole = text.substr(0, decimal_point);
    auto fraction = decimal_point == std::string_view::npos ? std::string_view{} : text.substr(decimal_point + 1);
    if (whole.empty())
        return fail(ErrorCode::InvalidInput, "Price must begin with a decimal digit");
    if (decimal_point != std::string_view::npos && fraction.empty())
        return fail(ErrorCode::InvalidInput, "Missing fractional digits");

    // Extra trailing zeros do not require a finer tick grid; input prices are never rounded.
    while (!fraction.empty() && fraction.back() == '0')
        fraction.remove_suffix(1);

    auto whole_value = parse_price_digits(whole);
    if (!whole_value.has_value())
        return std::unexpected(std::move(whole_value.error()));
    auto fraction_value = parse_price_digits(fraction);
    if (!fraction_value.has_value())
        return std::unexpected(std::move(fraction_value.error()));
    if (fraction.size() > Price::decimal_places)
        return fail(ErrorCode::OutOfRange, "Price cannot be represented exactly with eight decimal places");

    auto fractional_ticks = fraction_value.value();
    for (auto digits = fraction.size(); digits < Price::decimal_places; ++digits)
        fractional_ticks *= 10;

    constexpr auto scale = static_cast<std::uint64_t>(Price::ticks_per_unit);
    constexpr auto maximum_ticks = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    // Check before multiplying or adding, so even the maximum supported price is exact.
    if (whole_value.value() > (maximum_ticks - fractional_ticks) / scale)
        return fail(ErrorCode::OutOfRange, "Price exceeds its fixed-point range");
    return Price{static_cast<std::int64_t>(whole_value.value() * scale + fractional_ticks)};
}

Result<Timestamp> parse_utc_timestamp(std::string_view text) {
    constexpr std::string_view layout = "0000-00-00T00:00:00";
    constexpr std::size_t fractional_precision = 9;
    if (text.size() < layout.size() + 1 || text.size() > layout.size() + fractional_precision + 2 ||
        !text.ends_with('Z'))
        return fail(ErrorCode::InvalidInput, "Expected a UTC timestamp ending in Z");

    const auto is_decimal_digit = [](char character) {
        return character >= '0' && character <= '9';
    };
    for (std::size_t position = 0; position < layout.size(); ++position) {
        if (layout[position] == '0' ? !is_decimal_digit(text[position]) : text[position] != layout[position])
            return fail(ErrorCode::InvalidInput, "Expected YYYY-MM-DDTHH:MM:SS[.fraction]Z");
    }
    const auto year = text.substr(0, 4);
    const auto hour = text.substr(11, 2);
    const auto minute = text.substr(14, 2);
    const auto second = text.substr(17, 2);
    if (year < "1970" || year > "2200" || hour > "23" || minute > "59" || second > "59")
        return fail(ErrorCode::InvalidInput, "Timestamp component is out of range");

    const auto body = text.substr(0, text.size() - 1); // Boost's ISO parser expects no UTC suffix.
    const auto fraction = body.substr(layout.size());
    if (!fraction.empty()) {
        if (!fraction.starts_with('.') || fraction.size() < 2)
            return fail(ErrorCode::InvalidInput, "Timestamp fraction must contain one to nine decimal digits");
        if (!std::ranges::all_of(fraction.substr(1), is_decimal_digit))
            return fail(ErrorCode::InvalidInput, "Expected a decimal digit in timestamp fraction");
    }
    // Boost otherwise normalizes invalid clock fields and truncates excess fractional digits.
    // Nanosecond configuration is private to this file; no Boost date/time type crosses the API.
    static_assert(boost::posix_time::time_duration::num_fractional_digits() == fractional_precision);
    try {
        const auto parsed = boost::posix_time::from_iso_extended_string(std::string(body));
        const boost::posix_time::ptime epoch{boost::gregorian::date{1970, 1, 1}};
        return Timestamp{std::chrono::nanoseconds{(parsed - epoch).total_nanoseconds()}};
    } catch (const boost::bad_lexical_cast &error) {
        return fail(ErrorCode::InvalidInput, "Invalid timestamp: " + std::string(error.what()));
    } catch (const std::out_of_range &error) {
        return fail(ErrorCode::InvalidInput,
                    "Timestamp contains an invalid calendar date: " + std::string(error.what()));
    }
}

} // namespace coinbase_ticker_statistics
