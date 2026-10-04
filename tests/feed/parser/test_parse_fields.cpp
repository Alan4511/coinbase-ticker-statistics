#include "test_result.hpp"
#include <array>
#include <feed/parser/parse_fields.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <string_view>

namespace coinbase_ticker_statistics {
namespace {

TEST(Price, ParsesWithoutConfiguredDecimalScale) {
    EXPECT_EQ(parse_price("123.45"), Price{123.45L});
    EXPECT_EQ(parse_price("0.000000000000123456789"), Price{0.000000000000123456789L});
    EXPECT_EQ(parse_price("1e3"), Price{1000});
    EXPECT_EQ(parse_price("0"), Price{0});
}

TEST(Price, RejectsMalformedNegativeAndNonfiniteInput) {
    for (const auto text : {"", "-1", "+1", ".5", "1.", " 1", "1 ", "NaN", "Inf", "1.2.3", "1e", "1e+"}) {
        ASSERT_RESULT_ERROR(parse_price(text), ErrorCode::InvalidInput);
    }
    ASSERT_RESULT_ERROR(parse_price("1e99999"), ErrorCode::OutOfRange);
    ASSERT_RESULT_ERROR(parse_price("1e-99999"), ErrorCode::OutOfRange);
}

TEST(Timestamp, ParsesUtcToNanosecondPrecision) {
    EXPECT_EQ(parse_utc_timestamp("1970-01-01T00:00:00Z"), Timestamp{});
    EXPECT_EQ(parse_utc_timestamp("1970-01-01T00:00:01.000000001Z"),
              Timestamp{std::chrono::nanoseconds{1'000'000'001}});
    ASSERT_RESULT_VALUE(leap_day, parse_utc_timestamp("2024-02-29T23:59:59.123Z"));
    EXPECT_EQ(leap_day,
              std::chrono::sys_days{std::chrono::year{2024} / 2 / 29} + std::chrono::hours{23} +
                  std::chrono::minutes{59} + std::chrono::seconds{59} + std::chrono::milliseconds{123});
    ASSERT_RESULT_VALUE(last_day, parse_utc_timestamp("2200-12-31T23:59:59.999999999Z"));
    EXPECT_EQ(last_day,
              std::chrono::sys_days{std::chrono::year{2200} / 12 / 31} + std::chrono::hours{23} +
                  std::chrono::minutes{59} + std::chrono::seconds{59} + std::chrono::nanoseconds{999999999});
    ASSERT_RESULT_VALUE(century_leap_day, parse_utc_timestamp("2000-02-29T12:34:56.1Z"));
    EXPECT_EQ(century_leap_day,
              std::chrono::sys_days{std::chrono::year{2000} / 2 / 29} + std::chrono::hours{12} +
                  std::chrono::minutes{34} + std::chrono::seconds{56} + std::chrono::milliseconds{100});
}

TEST(Timestamp, RejectsInvalidDatesSyntaxAndUnsupportedPrecision) {
    constexpr std::array invalid{"",
                                 "1970",
                                 "1969-12-31T23:59:59Z",
                                 "2201-01-01T00:00:00Z",
                                 "2023-02-29T00:00:00Z",
                                 "2100-02-29T00:00:00Z",
                                 "2024-04-31T00:00:00Z",
                                 "2024-00-01T00:00:00Z",
                                 "2024-13-01T00:00:00Z",
                                 "2024-01-00T00:00:00Z",
                                 "2024-01-01T24:00:00Z",
                                 "2024-01-01T00:60:00Z",
                                 "2024-01-01T00:00:60Z",
                                 "2024-01-01T00:00:00.Z",
                                 "2024-01-01T00:00:00.1234567890Z",
                                 "2024-01-01T00:00:00+00:00",
                                 "2024-01-01 00:00:00Z",
                                 "2024-01-01T00:00:00z",
                                 "2024/01/01T00:00:00Z",
                                 "2024-01-01T00:00:00.aZ",
                                 "202A-01-01T00:00:00Z"};
    for (const std::string_view text : invalid) {
        ASSERT_RESULT_ERROR(parse_utc_timestamp(text), ErrorCode::InvalidInput) << text;
    }
}

} // namespace
} // namespace coinbase_ticker_statistics
