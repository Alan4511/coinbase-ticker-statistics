#include "test_result.hpp"
#include <statistics/sliding_window.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

namespace coinbase_ticker_statistics {
namespace {

constexpr std::string_view test_symbol = "BTC-USD";

Timestamp at(std::int64_t seconds) {
    return Timestamp{std::chrono::seconds{seconds}};
}

TickerUpdate ticker_update(TradeId id, Price price, Timestamp time = Timestamp{}) {
    return TickerUpdate{time, std::string(test_symbol), id, Price{price}};
}

void expect_close(Statistic actual, Statistic expected) {
    EXPECT_LE(std::abs(actual - expected),
              64 * std::numeric_limits<Statistic>::epsilon() * std::max(Statistic{1}, std::abs(expected)));
}

TEST(SlidingWindow, StartsEmptyAndComputesOddAndEvenStatistics) {
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{}));
    EXPECT_EQ(window.size(), 0);
    EXPECT_FALSE(window.snapshot());
    auto result = window.add_update(ticker_update(1, 300, at(0)));
    ASSERT_RESULT_OK(result);
    ASSERT_TRUE(*result);
    EXPECT_EQ((*result)->count, 1);
    expect_close((*result)->mean, (static_cast<Statistic>(300) / static_cast<Statistic>(1)));
    expect_close((*result)->median, (static_cast<Statistic>(300) / static_cast<Statistic>(1)));
    EXPECT_EQ((*result)->low, 300);
    EXPECT_EQ((*result)->high, 300);

    result = window.add_update(ticker_update(2, 100, at(1)));
    ASSERT_RESULT_OK(result);
    ASSERT_TRUE(*result);
    expect_close((*result)->mean, (static_cast<Statistic>(400) / static_cast<Statistic>(2)));
    expect_close((*result)->median, (static_cast<Statistic>(400) / static_cast<Statistic>(2)));
    EXPECT_EQ((*result)->low, 100);
    EXPECT_EQ((*result)->high, 300);

    result = window.add_update(ticker_update(3, 200, at(2)));
    ASSERT_RESULT_OK(result);
    ASSERT_TRUE(*result);
    EXPECT_EQ((*result)->count, 3);
    expect_close((*result)->mean, (static_cast<Statistic>(600) / static_cast<Statistic>(3)));
    expect_close((*result)->median, (static_cast<Statistic>(200) / static_cast<Statistic>(1)));

    result = window.add_update(ticker_update(4, 201, at(3)));
    ASSERT_RESULT_OK(result);
    ASSERT_TRUE(*result);
    expect_close((*result)->mean, (static_cast<Statistic>(801) / static_cast<Statistic>(4)));
    expect_close((*result)->median, (static_cast<Statistic>(401) / static_cast<Statistic>(2)));
}

TEST(SlidingWindow, DefaultBoundaryExcludesExactlyFiveMinutesOld) {
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{}));
    ASSERT_RESULT_OK(window.add_update(ticker_update(1, 100, at(0))));
    ASSERT_RESULT_OK(window.add_update(ticker_update(2, 200, at(1))));
    auto result = window.add_update(ticker_update(3, 300, at(300)));
    ASSERT_RESULT_OK(result);
    ASSERT_TRUE(*result);
    EXPECT_EQ((*result)->count, 2);
    EXPECT_EQ((*result)->low, 200);
    expect_close((*result)->mean, (static_cast<Statistic>(500) / static_cast<Statistic>(2)));
    result = window.add_update(ticker_update(4, 400, at(601)));
    ASSERT_RESULT_OK(result);
    ASSERT_TRUE(*result);
    EXPECT_EQ((*result)->count, 1);
    EXPECT_EQ((*result)->low, 400);
}

TEST(SlidingWindow, LongerDurationUsesSameWindowRules) {
    WindowOptions options;
    options.duration = std::chrono::hours{1};
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(options));
    ASSERT_RESULT_OK(window.add_update(ticker_update(1, 10, at(0))));
    ASSERT_RESULT_OK(window.add_update(ticker_update(2, 20, at(3599))));
    EXPECT_EQ(window.size(), 2);
    const auto result = window.add_update(ticker_update(3, 30, at(3600)));
    ASSERT_RESULT_OK(result);
    ASSERT_TRUE(*result);
    EXPECT_EQ((*result)->count, 2);
    EXPECT_EQ((*result)->low, 20);
}

TEST(SlidingWindow, SameTimestampAndRepeatedPricesAreDistinctSamples) {
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{}));
    for (TradeId id = 1; id <= 20; ++id) {
        ASSERT_RESULT_OK(window.add_update(ticker_update(id, 42, at(0))));
    }
    const auto snapshot = window.snapshot();
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->count, 20);
    expect_close(snapshot->median, (static_cast<Statistic>(42) / static_cast<Statistic>(1)));
    const auto expired = window.add_update(ticker_update(21, 100, at(300)));
    ASSERT_RESULT_OK(expired);
    ASSERT_TRUE(*expired);
    EXPECT_EQ((*expired)->count, 1);
    EXPECT_EQ((*expired)->low, 100);
}

TEST(SlidingWindow, IgnoredDuplicateDoesNotExpireDataOrAdvanceWatermark) {
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{}));
    ASSERT_RESULT_OK(window.add_update(ticker_update(1, 100, at(0))));
    ASSERT_RESULT_OK(window.add_update(ticker_update(2, 200, at(100))));
    EXPECT_EQ(window.add_update(ticker_update(2, 999, at(350))), std::nullopt);
    ASSERT_TRUE(window.snapshot());
    EXPECT_EQ(window.size(), 2);
    EXPECT_EQ(window.snapshot()->low, 100);
    ASSERT_RESULT_OK(window.add_update(ticker_update(3, 300, at(150))));
    EXPECT_EQ(window.size(), 3);
}

TEST(SlidingWindow, DuplicateIdentifiersCanBeReusedAfterExpiry) {
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{}));
    ASSERT_RESULT_OK(window.add_update(ticker_update(1, 100, at(0))));
    const auto result = window.add_update(ticker_update(1, 200, at(300)));
    ASSERT_RESULT_OK(result);
    ASSERT_TRUE(*result);
    EXPECT_EQ((*result)->count, 1);
    EXPECT_EQ((*result)->low, 200);
}

TEST(SlidingWindow, LateEventsFailWithoutMutation) {
    WindowOptions options;
    ASSERT_RESULT_VALUE(rejecting, SlidingWindow::create(options));
    ASSERT_RESULT_OK(rejecting.add_update(ticker_update(1, 100, at(100))));
    ASSERT_RESULT_ERROR(rejecting.add_update(ticker_update(2, 200, at(99))), ErrorCode::OutOfOrderTimestamp);
    EXPECT_EQ(rejecting.size(), 1);
    ASSERT_TRUE(rejecting.snapshot());
    EXPECT_EQ(rejecting.snapshot()->mean, 100);
    ASSERT_RESULT_OK(rejecting.add_update(ticker_update(2, 200, at(101))));
    EXPECT_EQ(rejecting.size(), 2);
}

TEST(SlidingWindow, RejectsNonfiniteInputAndSumOverflowWithoutMutation) {
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{}));
    const Price maximum = std::numeric_limits<Price>::max();
    ASSERT_RESULT_OK(window.add_update(ticker_update(1, maximum, at(0))));
    ASSERT_RESULT_ERROR(window.add_update(ticker_update(2, maximum, at(1))), ErrorCode::OutOfRange);
    EXPECT_EQ(window.size(), 1U);
    EXPECT_EQ(window.snapshot()->mean, maximum);
    for (const Price invalid :
         {-1.0L, std::numeric_limits<Price>::infinity(), std::numeric_limits<Price>::quiet_NaN()}) {
        ASSERT_RESULT_ERROR(window.add_update(ticker_update(2, invalid, at(1))), ErrorCode::InvalidInput);
    }
    ASSERT_RESULT_VALUE(result, window.add_update(ticker_update(3, 0, at(300))));
    ASSERT_TRUE(result);
    EXPECT_EQ(result->mean, 0);
}

TEST(SlidingWindow, CompensationPreservesSmallPricesWhenLargePriceExpires) {
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{}));
    const Price large = 1 / std::numeric_limits<Price>::epsilon();
    ASSERT_RESULT_OK(window.add_update(ticker_update(1, large, at(0))));
    ASSERT_RESULT_OK(window.add_update(ticker_update(2, 0.125L, at(1))));
    ASSERT_RESULT_VALUE(result, window.add_update(ticker_update(3, 0.375L, at(300))));
    ASSERT_TRUE(result);
    EXPECT_EQ(result->mean, 0.25L);
    EXPECT_EQ(result->median, 0.25L);
}

TEST(SlidingWindow, ValidatesDuration) {
    WindowOptions options;
    options.duration = std::chrono::seconds::zero();
    ASSERT_RESULT_ERROR(SlidingWindow::create(options), ErrorCode::InvalidConfiguration);
    options.duration = std::chrono::seconds{-1};
    ASSERT_RESULT_ERROR(SlidingWindow::create(options), ErrorCode::InvalidConfiguration);
    options.duration = std::chrono::seconds::max();
    ASSERT_RESULT_ERROR(SlidingWindow::create(options), ErrorCode::InvalidConfiguration);
    options = WindowOptions{};
    ASSERT_RESULT_OK(SlidingWindow::create(options));
}

TEST(SlidingWindow, HandlesCutoffBeforeRepresentableTimestampRange) {
    WindowOptions options;
    options.duration = std::chrono::seconds{1};
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(options));
    const Timestamp start = Timestamp::min();
    ASSERT_RESULT_OK(window.add_update(ticker_update(1, 100, start)));
    auto result = window.add_update(ticker_update(2, 200, start + std::chrono::nanoseconds{1}));
    ASSERT_RESULT_OK(result);
    ASSERT_TRUE(*result);
    EXPECT_EQ((*result)->count, 2);
    result = window.add_update(ticker_update(3, 300, start + options.duration));
    ASSERT_RESULT_OK(result);
    ASSERT_TRUE(*result);
    EXPECT_EQ((*result)->count, 2);
    EXPECT_EQ((*result)->low, 200);
}

TEST(SlidingWindow, RandomizedUpdatesMatchIndependentSortedReference) {
    struct Sample {
        Timestamp time;
        Price price;
    };
    constexpr TradeId sample_count = 4000;
    for (const auto duration : {std::chrono::seconds{300}, std::chrono::seconds{3600}}) {
        for (const std::uint64_t random_seed : {0x5EEDU, 0xC0FFEEU}) {
            WindowOptions options;
            options.duration = duration;
            ASSERT_RESULT_VALUE(window, SlidingWindow::create(options));
            std::mt19937_64 generator{random_seed};
            std::uniform_int_distribution<std::int64_t> time_step{0, 30};
            std::uniform_int_distribution<std::int64_t> price_value{0, 1'000'000};
            std::vector<Sample> reference;
            Timestamp current_time{};
            for (TradeId id = 0; id < sample_count; ++id) {
                current_time += std::chrono::seconds{time_step(generator)};
                const Price price = static_cast<Price>(price_value(generator)) / 1000;
                const Timestamp cutoff = current_time - duration;
                std::erase_if(reference, [cutoff](const Sample &sample) {
                    return sample.time <= cutoff;
                });
                reference.push_back(Sample{current_time, price});
                std::vector<Price> ordered;
                Statistic expected_sum{};
                for (const Sample &sample : reference) {
                    ordered.push_back(sample.price);
                    expected_sum += sample.price;
                }
                std::sort(ordered.begin(), ordered.end());
                const std::size_t middle = ordered.size() / 2;
                const Statistic expected_median =
                    ordered.size() % 2 == 0 ? std::midpoint(ordered[middle - 1], ordered[middle]) : ordered[middle];
                const auto result = window.add_update(ticker_update(id, price, current_time));
                ASSERT_RESULT_OK(result);
                ASSERT_TRUE(*result);
                EXPECT_EQ((*result)->count, ordered.size());
                EXPECT_EQ((*result)->low, ordered.front());
                EXPECT_EQ((*result)->high, ordered.back());
                expect_close((*result)->median, expected_median);
                expect_close((*result)->mean,
                             (static_cast<Statistic>(expected_sum) /
                              static_cast<Statistic>(static_cast<std::uint64_t>(ordered.size()))));
            }
        }
    }
}

} // namespace
} // namespace coinbase_ticker_statistics
