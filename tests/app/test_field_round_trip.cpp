#include "test_result.hpp"
#include <common/format_timestamp.hpp>
#include <feed/parser/parse_fields.hpp>
#include <output/format_fields.hpp>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <limits>
#include <random>
#include <string>
#include <string_view>

namespace coinbase_ticker_statistics {
namespace {

TEST(Price, ParsesAndRoundTripsExactDecimalTicks) {
    struct Case {
        std::string_view text;
        std::int64_t ticks;
        std::string_view canonical;
    };
    const Case cases[]{{"123.45", 12'345'000'000, "123.45"},
                       {"0.00000001", 1, "0.00000001"},
                       {"0.100000000000", 10'000'000, "0.1"},
                       {"0000123.450000", 12'345'000'000, "123.45"},
                       {"1000", 100'000'000'000, "1000"},
                       {"0.000000010000", 1, "0.00000001"},
                       {"00000000000000000000000000000000000001", 100'000'000, "1"},
                       {"0.000000000000", 0, "0"},
                       {"0", 0, "0"},
                       {"92233720368", 9'223'372'036'800'000'000, "92233720368"},
                       {"92233720368.54775807000", std::numeric_limits<std::int64_t>::max(), "92233720368.54775807"},
                       {"92233720368.54775807", std::numeric_limits<std::int64_t>::max(), "92233720368.54775807"}};
    for (const auto &[text, ticks, canonical] : cases) {
        SCOPED_TRACE(text);
        ASSERT_RESULT_VALUE(value, parse_price(text));
        EXPECT_EQ(value, Price{ticks});
        ASSERT_RESULT_VALUE(formatted, format_price(value));
        EXPECT_EQ(formatted, canonical);
        EXPECT_EQ(parse_price(formatted), value);
    }
    std::mt19937_64 generator{0xDEC1A1};
    std::uniform_int_distribution<std::int64_t> ticks(0, std::numeric_limits<std::int64_t>::max());
    for (unsigned iteration = 0; iteration < 1000; ++iteration) {
        const Price expected{ticks(generator)};
        const auto whole = std::to_string(expected.ticks / Price::ticks_per_unit);
        const auto fraction = std::to_string(Price::ticks_per_unit + expected.ticks % Price::ticks_per_unit).substr(1);
        const auto text = whole + '.' + fraction;
        SCOPED_TRACE(text);
        EXPECT_EQ(parse_price(text), expected);
        EXPECT_EQ(parse_price(text + "000"), expected);
        ASSERT_RESULT_VALUE(formatted, format_price(expected));
        auto canonical = text;
        while (canonical.back() == '0')
            canonical.pop_back();
        if (canonical.back() == '.')
            canonical.pop_back();
        EXPECT_EQ(formatted, canonical);
    }
    for (const auto text : {"",
                            "-1",
                            "+1",
                            ".5",
                            "1.",
                            " 1",
                            "1 ",
                            "NaN",
                            "Inf",
                            "1.2.3",
                            "1e",
                            "1e+",
                            "1e3",
                            "1.2345e+2",
                            "100e-10",
                            "0e99999999999999999999",
                            "1e99999",
                            "1e-99999",
                            "1e99999999999999999999",
                            "0e999999x",
                            "0.000000000x",
                            "1.000000000x"}) {
        SCOPED_TRACE(text);
        ASSERT_RESULT_ERROR(parse_price(text), ErrorCode::InvalidInput);
    }
    for (const auto text : {"0.000000001",
                            "1.000000001",
                            "0.000000010001",
                            "0.12345678901234567890123456789",
                            "92233720368.54775808",
                            "92233720369",
                            "999999999999999999999"}) {
        SCOPED_TRACE(text);
        ASSERT_RESULT_ERROR(parse_price(text), ErrorCode::OutOfRange);
    }
    ASSERT_RESULT_ERROR(parse_price(std::string_view{"1.25\0junk", 9}), ErrorCode::InvalidInput);
    ASSERT_RESULT_ERROR(format_price(Price{-1}), ErrorCode::InvalidInput);
}

TEST(Timestamp, ParsesFormatsAndValidatesUtc) {
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
    const auto timestamp = std::chrono::sys_days{std::chrono::year{2024} / 2 / 29} + std::chrono::hours{23} +
                           std::chrono::minutes{59} + std::chrono::seconds{59} + std::chrono::milliseconds{123};
    EXPECT_EQ(format_utc_timestamp(timestamp), "2024-02-29T23:59:59.123000000Z");
    EXPECT_EQ(format_utc_timestamp(Timestamp{}), "1970-01-01T00:00:00.000000000Z");
    ASSERT_RESULT_VALUE(start, parse_utc_timestamp("1970-01-01T00:00:00Z"));
    ASSERT_RESULT_VALUE(end, parse_utc_timestamp("2200-12-31T23:59:59Z"));
    constexpr auto stride = std::chrono::hours{731};
    for (Timestamp value = start; value < end; value += stride) {
        SCOPED_TRACE(value.time_since_epoch().count());
        ASSERT_RESULT_VALUE(formatted, format_utc_timestamp(value));
        EXPECT_EQ(parse_utc_timestamp(formatted), value);
    }
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
        SCOPED_TRACE(text);
        ASSERT_RESULT_ERROR(parse_utc_timestamp(text), ErrorCode::InvalidInput) << text;
    }
}

} // namespace
} // namespace coinbase_ticker_statistics
