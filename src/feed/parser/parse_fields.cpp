#include "feed/parser/parse_fields.hpp"
#include <common/format_timestamp.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>

#include <charconv>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace coinbase_ticker_statistics {
namespace {

// Different fractional widths represent the same timestamp; compare without zero padding.
std::string_view trim_fractional_zeros(std::string_view text) {
    if (text.find('.') != std::string_view::npos) {
        while (text.ends_with('0'))
            text.remove_suffix(1);
        if (text.ends_with('.'))
            text.remove_suffix(1);
    }
    return text;
}

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
    // The build shares one nanosecond configuration; the API still returns a standard chrono type.
    constexpr auto fractional_precision = boost::posix_time::time_duration::num_fractional_digits();
    static_assert(fractional_precision == 9,
                  "Boost.DateTime requires nanoseconds; reconfigure CMake and refresh the IDE compilation database");
    if (!text.ends_with('Z'))
        return fail(ErrorCode::InvalidInput, "Expected a UTC timestamp ending in Z");
    text.remove_suffix(1); // Boost's ISO parser expects no UTC suffix.
    if (const auto decimal_point = text.find('.'); decimal_point != std::string_view::npos) {
        const auto fraction = text.substr(decimal_point + 1);
        if (fraction.empty() || fraction.size() > fractional_precision)
            return fail(ErrorCode::InvalidInput, "Timestamp fraction must contain one to nine decimal digits");
    }
    try {
        const auto parsed = boost::posix_time::from_iso_extended_string(std::string(text));
        // Check before converting: Boost supports dates outside our nanosecond timestamp range.
        if (parsed.is_special() || parsed.date().year() < 1970 || parsed.date().year() > 2200)
            return fail(ErrorCode::InvalidInput, "Timestamp component is out of range");
        const boost::posix_time::ptime epoch{boost::gregorian::date{1970, 1, 1}};
        const Timestamp timestamp{std::chrono::nanoseconds{(parsed - epoch).total_nanoseconds()}};
        auto canonical = format_utc_timestamp(timestamp);
        if (!canonical.has_value())
            return std::unexpected(std::move(canonical.error()));
        auto canonical_body = std::string_view{canonical.value()};
        canonical_body.remove_suffix(1);
        // Reject Boost's normalization/permissive syntax without duplicating ISO field positions.
        if (trim_fractional_zeros(text) != trim_fractional_zeros(canonical_body))
            return fail(ErrorCode::InvalidInput, "Expected YYYY-MM-DDTHH:MM:SS[.fraction]Z");
        return timestamp;
    } catch (const boost::bad_lexical_cast &error) {
        return fail(ErrorCode::InvalidInput, "Invalid timestamp: " + std::string(error.what()));
    } catch (const std::out_of_range &error) {
        return fail(ErrorCode::InvalidInput,
                    "Timestamp contains an invalid calendar date: " + std::string(error.what()));
    }
}

} // namespace coinbase_ticker_statistics
