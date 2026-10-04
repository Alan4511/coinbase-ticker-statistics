#pragma once

#include <result.hpp>
#include <types.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <concepts>
#include <limits>
#include <string>

namespace coinbase_ticker_statistics {
/** Append a nonnegative numeric CSV field without a temporary string or locale conversion. */
template <typename Number>
    requires((std::unsigned_integral<Number> && !std::same_as<Number, bool>) || std::floating_point<Number>)
[[nodiscard]] Result<void> append_number(std::string &destination, Number value) {
    // Significant/integer digits plus room for sign, decimal point and a signed exponent.
    constexpr auto buffer_size = std::numeric_limits<Number>::max_digits10 + std::numeric_limits<Number>::digits10 +
                                 std::numeric_limits<int>::digits10 + 8;
    std::array<char, buffer_size> buffer;
    const auto convert = [&]() -> std::to_chars_result {
        if constexpr (std::floating_point<Number>)
            return std::to_chars(buffer.data(),
                                 buffer.data() + buffer.size(),
                                 value,
                                 std::chars_format::general,
                                 std::numeric_limits<Number>::max_digits10);
        else
            return std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    };
    if constexpr (std::floating_point<Number>) {
        if (!std::isfinite(value) || value < 0)
            return fail(ErrorCode::InvalidInput, "Price/statistic must be finite and nonnegative");
        if (value == 0)
            value = Number{0}; // Normalize negative zero.
    }
    const auto [end, error] = convert();
    if (error != std::errc{})
        return fail(ErrorCode::OutOfRange, "Numeric CSV field exceeds its conversion buffer");
    destination.append(buffer.data(), end);
    return {};
}

/** Render max_digits10 significant digits for round-trip preservation, not fixed decimal places. */
[[nodiscard]] Result<std::string> format_price(Price price);

/**
 * Format a UTC timestamp with nine fractional digits for deterministic CSV output.
 * @return UTC text, or OutOfRange for a year outside 1970..2200.
 */
[[nodiscard]] Result<std::string> format_utc_timestamp(Timestamp timestamp);

} // namespace coinbase_ticker_statistics
