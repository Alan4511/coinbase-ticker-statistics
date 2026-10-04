#include "test_result.hpp"
#include <array>
#include <feed/parser/parse_fields.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <output/format_fields.hpp>
#include <string_view>

namespace coinbase_ticker_statistics {
namespace {

TEST(Price, FullPrecisionOutputRoundTripsWithoutDoubleNarrowing) {
    for (const Price price : {0.1L,
                              12345.67890123456789L,
                              std::numeric_limits<Price>::min(),
                              std::numeric_limits<Price>::denorm_min(),
                              std::numeric_limits<Price>::max(),
                              Price{1} + std::numeric_limits<Price>::epsilon()}) {
        ASSERT_RESULT_VALUE(text, format_price(price));
        EXPECT_EQ(parse_price(text), price);
    }
    EXPECT_EQ(format_price(Price{-0.0L}), "0");
}

TEST(Timestamp, DeterministicRoundTripsAcrossCalendarBoundaries) {
    ASSERT_RESULT_VALUE(start, parse_utc_timestamp("1970-01-01T00:00:00Z"));
    ASSERT_RESULT_VALUE(end, parse_utc_timestamp("2200-12-31T23:59:59Z"));
    constexpr auto stride = std::chrono::hours{731};
    for (Timestamp value = start; value < end; value += stride) {
        ASSERT_RESULT_VALUE(formatted, format_utc_timestamp(value));
        EXPECT_EQ(parse_utc_timestamp(formatted), value);
    }
}

} // namespace
} // namespace coinbase_ticker_statistics
