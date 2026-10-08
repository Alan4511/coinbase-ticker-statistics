#include "test_result.hpp"
#include <statistics/sliding_window.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <random>
#include <vector>

namespace coinbase_ticker_statistics {
namespace {

constexpr std::string_view test_symbol = "BTC-USD";

Timestamp at(std::int64_t seconds) {
    return Timestamp{std::chrono::seconds{seconds}};
}

constexpr Price price(std::int64_t whole, std::int64_t fraction = 0) {
    return Price{whole * Price::ticks_per_unit + fraction};
}

TickerUpdate ticker_update(TradeId id, Price value, Timestamp time = Timestamp{}) {
    return TickerUpdate{time, std::string(test_symbol), id, value};
}

void expect_fraction(const Statistic &actual, PriceSum numerator, SampleCount denominator = 1) {
    // Arbitrary precision in the reference prevents comparison itself overflowing.
    using boost::multiprecision::cpp_int;
    EXPECT_NE(actual.denominator, 0U);
    EXPECT_EQ(cpp_int(actual.numerator) * denominator, cpp_int(numerator) * actual.denominator);
}

TEST(SlidingWindow, ValidatesDurationOnDirectConstruction) {
    const auto maximum_duration = std::chrono::duration_cast<Duration>(std::chrono::days{365});
    for (const auto duration : {Duration{1}, Duration{300}, maximum_duration}) {
        SCOPED_TRACE(duration.count());
        ASSERT_RESULT_OK(SlidingWindow::create(WindowOptions{duration}));
    }
    for (const auto duration : {Duration::zero(), Duration{-1}, maximum_duration + Duration{1}, Duration::max()}) {
        SCOPED_TRACE(duration.count());
        ASSERT_RESULT_ERROR(SlidingWindow::create(WindowOptions{duration}), ErrorCode::InvalidConfiguration);
    }
}

TEST(SlidingWindow, ComputesStatisticsAndExpiresAtWindowBoundary) {
    for (const Duration duration : {Duration{300}, Duration{3600}}) {
        SCOPED_TRACE(duration.count());
        ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{duration}));
        EXPECT_EQ(window.size(), 0U);
        EXPECT_FALSE(window.snapshot().has_value());
        struct Step {
            Price price;
            Price mean;
            Price median;
            Price low;
        };
        const Step steps[]{{price(300), price(300), price(300), price(300)},
                           {price(100), price(200), price(200), price(100)},
                           {price(200), price(200), price(200), price(100)},
                           {price(201), price(200, 25'000'000), price(200, 50'000'000), price(100)},
                           {price(201), price(200, 40'000'000), price(201), price(100)}};
        TradeId id{};
        for (const auto &[value, mean, median, low] : steps) {
            ++id;
            SCOPED_TRACE(id);
            ASSERT_RESULT_VALUE(result, window.add_update(ticker_update(id, value, at(id == 1 ? 0 : 1))));
            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(result.value().count, id);
            expect_fraction(result.value().mean, mean.ticks);
            expect_fraction(result.value().median, median.ticks);
            EXPECT_EQ(result.value().low, low);
            EXPECT_EQ(result.value().high, price(300));
        }
        ASSERT_RESULT_VALUE(boundary, window.add_update(ticker_update(++id, price(400), Timestamp{duration})));
        ASSERT_TRUE(boundary.has_value());
        EXPECT_EQ(boundary.value().count, 5U); // The observation exactly one window old has expired.
        expect_fraction(boundary.value().mean, price(220, 40'000'000).ticks);
        expect_fraction(boundary.value().median, price(201).ticks);
        EXPECT_EQ(boundary.value().low, price(100));
        EXPECT_EQ(boundary.value().high, price(400));
        ASSERT_RESULT_VALUE(expired, window.add_update(ticker_update(++id, price(50), Timestamp{duration * 2})));
        ASSERT_TRUE(expired.has_value());
        EXPECT_EQ(expired.value().count, 1U);
        expect_fraction(expired.value().mean, price(50).ticks);
    }
    // Computing the cutoff must also be safe before the representable timestamp range.
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{Duration{1}}));
    const auto start = Timestamp::min();
    ASSERT_RESULT_OK(window.add_update(ticker_update(1, price(100), start)));
    ASSERT_RESULT_OK(window.add_update(ticker_update(2, price(200), start + std::chrono::nanoseconds{1})));
    ASSERT_RESULT_VALUE(boundary, window.add_update(ticker_update(3, price(300), start + Duration{1})));
    ASSERT_TRUE(boundary.has_value());
    EXPECT_EQ(boundary.value().count, 2U);
    EXPECT_EQ(boundary.value().low, price(200));
}

TEST(SlidingWindow, HandlesDuplicateTradeIds) {
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{}));
    ASSERT_RESULT_OK(window.add_update(ticker_update(1, price(100), at(0))));
    ASSERT_RESULT_OK(window.add_update(ticker_update(2, price(200), at(100))));
    ASSERT_RESULT_VALUE(duplicate, window.add_update(ticker_update(2, price(999), at(350))));
    EXPECT_FALSE(duplicate.has_value());
    EXPECT_EQ(window.size(), 2U);
    ASSERT_TRUE(window.snapshot().has_value());
    EXPECT_EQ(window.snapshot().value().low, price(100));
    // Ignoring the duplicate must neither expire data nor move the watermark to 350.
    ASSERT_RESULT_OK(window.add_update(ticker_update(3, price(300), at(150))));
    ASSERT_RESULT_VALUE(reused, window.add_update(ticker_update(1, price(400), at(300))));
    ASSERT_TRUE(reused.has_value());
    EXPECT_EQ(reused.value().count, 3U);
    EXPECT_EQ(reused.value().low, price(200));
    EXPECT_EQ(reused.value().high, price(400));
}

TEST(SlidingWindow, EnforcesCapacityAfterExpirationWithoutDroppingSamples) {
    ASSERT_RESULT_ERROR(SlidingWindow::create(WindowOptions{Duration{300}, 0}), ErrorCode::InvalidConfiguration);
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{Duration{300}, 2}));
    ASSERT_RESULT_OK(window.add_update(ticker_update(1, price(10), at(0))));
    ASSERT_RESULT_OK(window.add_update(ticker_update(2, price(20), at(1))));
    ASSERT_RESULT_VALUE(duplicate, window.add_update(ticker_update(2, price(999), at(1))));
    EXPECT_FALSE(duplicate.has_value());
    for (const auto time : {at(1), at(299)}) {
        ASSERT_RESULT_ERROR(window.add_update(ticker_update(3, price(30), time)), ErrorCode::OutOfRange);
        EXPECT_EQ(window.size(), 2U);
        const auto statistics = window.snapshot().value();
        expect_fraction(statistics.mean, price(15).ticks);
        EXPECT_EQ(statistics.low, price(10));
        EXPECT_EQ(statistics.high, price(20));
    }
    // A rejected update must not move the watermark to 299.
    ASSERT_RESULT_VALUE(still_duplicate, window.add_update(ticker_update(2, price(20), at(2))));
    EXPECT_FALSE(still_duplicate.has_value());
    ASSERT_RESULT_VALUE(accepted, window.add_update(ticker_update(3, price(30), at(300))));
    ASSERT_TRUE(accepted.has_value());
    EXPECT_EQ(accepted.value().count, 2U);
    expect_fraction(accepted.value().mean, price(25).ticks);
    EXPECT_EQ(accepted.value().low, price(20));
    ASSERT_RESULT_VALUE(reused, window.add_update(ticker_update(1, price(40), at(600))));
    ASSERT_TRUE(reused.has_value());
    EXPECT_EQ(reused.value().count, 1U);
}

TEST(SlidingWindow, RejectsInvalidOrOutOfOrderUpdatesWithoutMutation) {
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{}));
    const Price maximum{std::numeric_limits<std::int64_t>::max()};
    ASSERT_RESULT_OK(window.add_update(ticker_update(1, maximum, at(100))));
    struct InvalidCase {
        const char *name;
        Price price;
        Timestamp time;
        ErrorCode error;
    };
    const InvalidCase cases[]{{"decreasing time", price(1), at(99), ErrorCode::OutOfOrderTimestamp},
                              {"negative price", Price{-1}, at(101), ErrorCode::InvalidInput}};
    for (const auto &[name, price, time, error] : cases) {
        SCOPED_TRACE(name);
        ASSERT_RESULT_ERROR(window.add_update(ticker_update(2, price, time)), error);
        ASSERT_TRUE(window.snapshot().has_value());
        const auto snapshot = window.snapshot().value();
        EXPECT_EQ(snapshot.count, 1U);
        expect_fraction(snapshot.mean, static_cast<std::uint64_t>(maximum.ticks));
        expect_fraction(snapshot.median, static_cast<std::uint64_t>(maximum.ticks));
        EXPECT_EQ(snapshot.low, maximum);
        EXPECT_EQ(snapshot.high, maximum);
    }
    ASSERT_RESULT_VALUE(expired, window.add_update(ticker_update(2, price(0), at(400))));
    ASSERT_TRUE(expired.has_value());
    expect_fraction(expired.value().mean, 0);
}

TEST(SlidingWindow, KeepsExactSumsAndFractionalStatisticsAcrossExpiration) {
    ASSERT_RESULT_VALUE(window, SlidingWindow::create(WindowOptions{}));
    const Price maximum{std::numeric_limits<std::int64_t>::max()};
    ASSERT_RESULT_OK(window.add_update(ticker_update(1, maximum, at(0))));
    ASSERT_RESULT_VALUE(two_large, window.add_update(ticker_update(2, maximum, at(0))));
    ASSERT_TRUE(two_large.has_value());
    EXPECT_GT(two_large.value().mean.numerator, std::numeric_limits<std::uint64_t>::max() / 2);
    expect_fraction(two_large.value().mean, static_cast<std::uint64_t>(maximum.ticks));
    expect_fraction(two_large.value().median, static_cast<std::uint64_t>(maximum.ticks));
    ASSERT_RESULT_VALUE(three_large, window.add_update(ticker_update(3, maximum, at(0))));
    ASSERT_TRUE(three_large.has_value());
    EXPECT_GT(three_large.value().mean.numerator, std::numeric_limits<std::uint64_t>::max());
    expect_fraction(three_large.value().mean, static_cast<std::uint64_t>(maximum.ticks));
    ASSERT_RESULT_OK(window.add_update(ticker_update(4, Price{1}, at(1))));
    ASSERT_RESULT_OK(window.add_update(ticker_update(5, Price{2}, at(1))));
    ASSERT_RESULT_VALUE(expired, window.add_update(ticker_update(6, Price{0}, at(300))));
    ASSERT_TRUE(expired.has_value());
    expect_fraction(expired.value().mean, 1);
    expect_fraction(expired.value().median, 1);
    ASSERT_RESULT_VALUE(fractional, window.add_update(ticker_update(7, Price{0}, at(301))));
    ASSERT_TRUE(fractional.has_value());
    expect_fraction(fractional.value().mean, 0);

    ASSERT_RESULT_VALUE(fractions, SlidingWindow::create(WindowOptions{}));
    ASSERT_RESULT_OK(fractions.add_update(ticker_update(1, Price{0})));
    ASSERT_RESULT_VALUE(pair, fractions.add_update(ticker_update(2, Price{1})));
    ASSERT_TRUE(pair.has_value());
    expect_fraction(pair.value().mean, 1, 2);
    expect_fraction(pair.value().median, 1, 2);
    ASSERT_RESULT_VALUE(triple, fractions.add_update(ticker_update(3, Price{0})));
    ASSERT_TRUE(triple.has_value());
    expect_fraction(triple.value().mean, 1, 3);
    expect_fraction(triple.value().median, 0);
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
                const Price value{price_value(generator)};
                const Timestamp cutoff = current_time - duration;
                std::erase_if(reference, [cutoff](const Sample &sample) {
                    return sample.time <= cutoff;
                });
                reference.push_back(Sample{current_time, value});
                std::vector<Price> ordered;
                std::uint64_t expected_sum{};
                for (const Sample &sample : reference) {
                    ordered.push_back(sample.price);
                    expected_sum += static_cast<std::uint64_t>(sample.price.ticks);
                }
                std::sort(ordered.begin(), ordered.end());
                const std::size_t middle = ordered.size() / 2;
                const auto expected_median_sum =
                    static_cast<std::uint64_t>(ordered[middle].ticks) +
                    (ordered.size() % 2 == 0 ? static_cast<std::uint64_t>(ordered[middle - 1].ticks) : 0);
                const auto result = window.add_update(ticker_update(id, value, current_time));
                ASSERT_RESULT_OK(result);
                ASSERT_TRUE(result.value().has_value());
                EXPECT_EQ(result.value().value().count, ordered.size());
                EXPECT_EQ(result.value().value().low, ordered.front());
                EXPECT_EQ(result.value().value().high, ordered.back());
                expect_fraction(result.value().value().median, expected_median_sum, ordered.size() % 2 == 0 ? 2 : 1);
                expect_fraction(result.value().value().mean, expected_sum, ordered.size());
            }
        }
    }
}

} // namespace
} // namespace coinbase_ticker_statistics
