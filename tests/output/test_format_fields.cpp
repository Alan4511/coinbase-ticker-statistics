#include "test_result.hpp"
#include <array>
#include <gtest/gtest.h>
#include <limits>
#include <output/format_fields.hpp>
#include <string_view>

namespace coinbase_ticker_statistics {
namespace {

TEST(Price, RejectsInvalidOutput) {
    ASSERT_RESULT_ERROR(format_price(-1), ErrorCode::InvalidInput);
    ASSERT_RESULT_ERROR(format_price(std::numeric_limits<Price>::infinity()), ErrorCode::InvalidInput);
    ASSERT_RESULT_ERROR(format_price(std::numeric_limits<Price>::quiet_NaN()), ErrorCode::InvalidInput);
}

TEST(Timestamp, RejectsInvalidOutput) {
    ASSERT_RESULT_ERROR(format_utc_timestamp(Timestamp{std::chrono::nanoseconds{-1}}), ErrorCode::OutOfRange);
    ASSERT_RESULT_ERROR(format_utc_timestamp(Timestamp::max()), ErrorCode::OutOfRange);
}

TEST(Timestamp, FormatsUtcWithNanosecondPrecision) {
    const auto timestamp = std::chrono::sys_days{std::chrono::year{2024} / 2 / 29} + std::chrono::hours{23} +
                           std::chrono::minutes{59} + std::chrono::seconds{59} + std::chrono::milliseconds{123};
    EXPECT_EQ(format_utc_timestamp(timestamp), "2024-02-29T23:59:59.123000000Z");
    EXPECT_EQ(format_utc_timestamp(Timestamp{}), "1970-01-01T00:00:00.000000000Z");
}

} // namespace
} // namespace coinbase_ticker_statistics
