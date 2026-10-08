#include "output/format_fields.hpp"

#include <limits>

namespace coinbase_ticker_statistics {
Result<void> append_number(std::string &destination, Price value) {
    if (value.ticks < 0)
        return fail(ErrorCode::InvalidInput, "Price must be nonnegative");
    const auto whole = static_cast<std::uint64_t>(value.ticks / Price::ticks_per_unit);
    if (auto written = append_number(destination, whole); !written.has_value())
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
    PriceSum ticks = value.numerator;
    bool round_up = false;
    if (value.denominator != 1) {
        ticks /= value.denominator;
        const PriceSum remainder = value.numerator % value.denominator;
        const PriceSum complement = PriceSum{value.denominator} - remainder;
        round_up = remainder > complement || (remainder == complement && (ticks & 1) != 0);
    }
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (ticks > maximum || (ticks == maximum && round_up))
        return fail(ErrorCode::OutOfRange, "Statistic exceeds the fixed-point price range");
    if (round_up)
        ++ticks;
    return append_number(destination, Price{ticks.convert_to<std::int64_t>()});
}

Result<std::string> format_price(Price price) {
    std::string text;
    if (auto formatted = append_number(text, price); !formatted.has_value())
        return std::unexpected(formatted.error());
    return text;
}

} // namespace coinbase_ticker_statistics
