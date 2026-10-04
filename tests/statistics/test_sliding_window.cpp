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

TEST(SlidingWindow, ComputesStatisticsAndExpiresAtWindowBoundary) {
    for (const Duration duration : {Duration{300}, Duration{3600}}) {
        SCOPED_TRACE(duration.count());
        ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{duration}));
        EXPECT_EQ(window.size(), 0U);
        EXPECT_FALSE(window.snapshot());
        struct Step {
            Price price;
            Statistic mean;
            Statistic median;
            Price low;
        };
        const Step steps[]{{300, 300, 300, 300},
                           {100, 200, 200, 100},
                           {200, 200, 200, 100},
                           {201, 200.25L, 200.5L, 100},
                           {201, 200.4L, 201, 100}};
        TradeId id{};
        for (const auto &[price, mean, median, low] : steps) {
            ++id;
            SCOPED_TRACE(id);
            ASSERT_RESULT_VALUE(result, window.add_update(ticker_update(id, price, at(id == 1 ? 0 : 1))));
            ASSERT_TRUE(result);
            EXPECT_EQ(result->count, id);
            expect_close(result->mean, mean);
            expect_close(result->median, median);
            EXPECT_EQ(result->low, low);
            EXPECT_EQ(result->high, 300);
        }
        ASSERT_RESULT_VALUE(boundary, window.add_update(ticker_update(++id, 400, Timestamp{duration})));
        ASSERT_TRUE(boundary);
        EXPECT_EQ(boundary->count, 5U); // The observation exactly one window old has expired.
        expect_close(boundary->mean, 220.4L);
        EXPECT_EQ(boundary->median, 201);
        EXPECT_EQ(boundary->low, 100);
        EXPECT_EQ(boundary->high, 400);
        ASSERT_RESULT_VALUE(expired, window.add_update(ticker_update(++id, 50, Timestamp{duration * 2})));
        ASSERT_TRUE(expired);
        EXPECT_EQ(expired->count, 1U);
        EXPECT_EQ(expired->mean, 50);
    }
    // Computing the cutoff must also be safe before the representable timestamp range.
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{Duration{1}}));
    const auto start = Timestamp::min();
    ASSERT_RESULT_OK(window.add_update(ticker_update(1, 100, start)));
    ASSERT_RESULT_OK(window.add_update(ticker_update(2, 200, start + std::chrono::nanoseconds{1})));
    ASSERT_RESULT_VALUE(boundary, window.add_update(ticker_update(3, 300, start + Duration{1})));
    ASSERT_TRUE(boundary);
    EXPECT_EQ(boundary->count, 2U);
    EXPECT_EQ(boundary->low, 200);
}

TEST(SlidingWindow, HandlesDuplicateTradeIds) {
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{}));
    ASSERT_RESULT_OK(window.add_update(ticker_update(1, 100, at(0))));
    ASSERT_RESULT_OK(window.add_update(ticker_update(2, 200, at(100))));
    ASSERT_RESULT_VALUE(duplicate, window.add_update(ticker_update(2, 999, at(350))));
    EXPECT_FALSE(duplicate);
    EXPECT_EQ(window.size(), 2U);
    ASSERT_TRUE(window.snapshot());
    EXPECT_EQ(window.snapshot()->low, 100);
    // Ignoring the duplicate must neither expire data nor move the watermark to 350.
    ASSERT_RESULT_OK(window.add_update(ticker_update(3, 300, at(150))));
    ASSERT_RESULT_VALUE(reused, window.add_update(ticker_update(1, 400, at(300))));
    ASSERT_TRUE(reused);
    EXPECT_EQ(reused->count, 3U);
    EXPECT_EQ(reused->low, 200);
    EXPECT_EQ(reused->high, 400);
}

TEST(SlidingWindow, RejectsInvalidOrOutOfOrderUpdatesWithoutMutation) {
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{}));
    const Price maximum = std::numeric_limits<Price>::max();
    ASSERT_RESULT_OK(window.add_update(ticker_update(1, maximum, at(100))));
    struct InvalidCase {
        const char *name;
        Price price;
        Timestamp time;
        ErrorCode error;
    };
    const InvalidCase cases[]{{"decreasing time", 1, at(99), ErrorCode::OutOfOrderTimestamp},
                              {"negative price", -1, at(101), ErrorCode::InvalidInput},
                              {"infinity", std::numeric_limits<Price>::infinity(), at(101), ErrorCode::InvalidInput},
                              {"NaN", std::numeric_limits<Price>::quiet_NaN(), at(101), ErrorCode::InvalidInput},
                              {"sum overflow", maximum, at(101), ErrorCode::OutOfRange}};
    for (const auto &[name, price, time, error] : cases) {
        SCOPED_TRACE(name);
        ASSERT_RESULT_ERROR(window.add_update(ticker_update(2, price, time)), error);
        ASSERT_TRUE(window.snapshot());
        const auto snapshot = *window.snapshot();
        EXPECT_EQ(snapshot.count, 1U);
        EXPECT_EQ(snapshot.mean, maximum);
        EXPECT_EQ(snapshot.median, maximum);
        EXPECT_EQ(snapshot.low, maximum);
        EXPECT_EQ(snapshot.high, maximum);
    }
    ASSERT_RESULT_VALUE(expired, window.add_update(ticker_update(2, 0, at(400))));
    ASSERT_TRUE(expired);
    EXPECT_EQ(expired->mean, 0);
}

TEST(SlidingWindow, PreservesNumericalStabilityAcrossExpiration) {
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{}));
    const Price large = 1 / std::numeric_limits<Price>::epsilon();
    ASSERT_RESULT_OK(window.add_update(ticker_update(1, large, at(0))));
    ASSERT_RESULT_OK(window.add_update(ticker_update(2, 0.125L, at(1))));
    ASSERT_RESULT_VALUE(result, window.add_update(ticker_update(3, 0.375L, at(300))));
    ASSERT_TRUE(result);
    EXPECT_EQ(result->mean, 0.25L);
    EXPECT_EQ(result->median, 0.25L);
}

TEST(SlidingWindow, RandomizedUpdatesMatchIndependentReference) {
    struct Sample {
        Timestamp time;
        Price price;
    };
    constexpr TradeId sample_count = 4000;
    for (const auto duration : {std::chrono::seconds{300}, std::chrono::seconds{3600}}) {
        for (const std::uint64_t random_seed : {0x5EEDU, 0xC0FFEEU}) {
            SCOPED_TRACE(duration.count());
            SCOPED_TRACE(random_seed);
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
