#pragma once

#include <common/result.hpp>
#include <common/types.hpp>

#include <array>
#include <charconv>
#include <concepts>
#include <limits>
#include <string>
#include <system_error>

namespace coinbase_ticker_statistics {
/** Append a nonnegative numeric CSV field without a temporary string or locale conversion. */
template <std::unsigned_integral Number>
    requires(!std::same_as<Number, bool>)
[[nodiscard]] Result<void> append_number(std::string &destination, Number value) {
    std::array<char, std::numeric_limits<Number>::digits10 + 1> buffer;
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{})
        return fail(ErrorCode::OutOfRange, "Numeric CSV field exceeds its conversion buffer");
    destination.append(buffer.data(), end);
    return {};
}

/** Append exact price ticks as decimal text, omitting insignificant trailing zeros. */
[[nodiscard]] Result<void> append_number(std::string &destination, Price value);

/** Round an exact fraction to the nearest price tick, with ties going to even. */
[[nodiscard]] Result<void> append_number(std::string &destination, const Statistic &value);

/** Render an exact decimal price without locale conversion or scientific notation. */
[[nodiscard]] Result<std::string> format_price(Price price);

} // namespace coinbase_ticker_statistics
