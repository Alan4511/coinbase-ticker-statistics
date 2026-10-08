#include "statistics/sliding_window.hpp"

#include <iterator>
#include <limits>
#include <ranges>
#include <utility>

namespace coinbase_ticker_statistics {

Result<SlidingWindow> SlidingWindow::create(WindowOptions options) {
    if (auto valid = validate(options); !valid.has_value())
        return std::unexpected(std::move(valid.error()));
    return SlidingWindow(std::move(options));
}

SlidingWindow::SlidingWindow(WindowOptions options)
    : duration_(std::chrono::duration_cast<std::chrono::nanoseconds>(options.duration)),
      max_observations_(options.max_observations_per_symbol) {
}

Result<std::optional<Statistics>> SlidingWindow::add_update(const TickerUpdate &ticker_update) {
    const auto window_time = ticker_update.exchange_time;
    if (ticker_update.price.ticks < 0) {
        return fail(ErrorCode::InvalidInput, "Price must be non-negative");
    }
    if (last_accepted_time_.has_value() && window_time < last_accepted_time_.value()) {
        return fail(ErrorCode::OutOfOrderTimestamp, "Window time precedes the last accepted ticker update");
    }

    // Determine the post-expiration state before modifying it. An ignored duplicate
    // must not advance time or expire data that belongs to the last accepted window.
    const auto retained_duplicate = retained_trade_times_.find(ticker_update.trade_id);
    if (retained_duplicate != retained_trade_times_.end()) {
        const auto &[trade_id, exchange_time] = *retained_duplicate;
        if (!is_expired(exchange_time, window_time))
            return std::nullopt;
    }
    auto update = prepare_update(ticker_update);
    if (!update.has_value())
        return std::unexpected(update.error());
    const auto &[expired_sample_count, sum_after_update] = update.value();
    for (SampleCount index = 0; index < expired_sample_count; ++index) {
        remove_oldest_sample();
    }
    // Expiration preserves ordering between partitions. Restore their sizes once
    // before insertion uses the lower partition's maximum to choose a side.
    rebalance_price_partitions();

    insert_update(ticker_update);
    sum_ = sum_after_update;
    last_accepted_time_ = window_time;
    return snapshot();
}

std::optional<Statistics> SlidingWindow::snapshot() const {
    if (samples_.empty()) {
        return std::nullopt;
    }
    const bool has_even_sample_count = lower_prices_.size() == upper_prices_.size();
    const PriceSum middle_price = static_cast<std::uint64_t>(lower_prices_.rbegin()->ticks);
    const Statistic median = has_even_sample_count
                                 ? Statistic{middle_price + static_cast<std::uint64_t>(upper_prices_.begin()->ticks), 2}
                                 : Statistic{middle_price, 1};
    const Price high{upper_prices_.empty() ? *lower_prices_.rbegin() : *upper_prices_.rbegin()};
    return Statistics{samples_.size(), {sum_, samples_.size()}, median, *lower_prices_.begin(), high};
}

SampleCount SlidingWindow::size() const noexcept {
    return samples_.size();
}

bool SlidingWindow::is_expired(Timestamp observation_time, Timestamp window_time) const {
    const auto current_count = window_time.time_since_epoch().count();
    const auto minimum_count = std::numeric_limits<Timestamp::duration::rep>::min();
    // A mathematically valid cutoff can precede Timestamp's representable range.
    if (current_count < minimum_count + duration_.count()) {
        return false;
    }
    const Timestamp cutoff{window_time.time_since_epoch() - duration_};
    return observation_time <= cutoff;
}

Result<SlidingWindow::PendingUpdate> SlidingWindow::prepare_update(const TickerUpdate &ticker_update) const {
    PriceSum candidate_sum = sum_;
    SampleCount expired_sample_count{};
    const auto expired_samples =
        samples_ |
        std::views::take_while([this, window_time = ticker_update.exchange_time](const WindowSample &sample) {
            return is_expired(sample.exchange_time, window_time);
        });
    for (const auto &sample : expired_samples) {
        candidate_sum -= static_cast<std::uint64_t>(sample.price.ticks);
        ++expired_sample_count;
    }
    const SampleCount retained_sample_count = samples_.size() - expired_sample_count;
    if (retained_sample_count >= max_observations_)
        return fail(ErrorCode::OutOfRange, "Window observation limit exceeded; no samples were dropped");
    // The maximum int64 price times the maximum 64-bit count fits in PriceSum.
    candidate_sum += static_cast<std::uint64_t>(ticker_update.price.ticks);
    return PendingUpdate{expired_sample_count, candidate_sum};
}

void SlidingWindow::remove_oldest_sample() {
    const auto &[exchange_time, trade_id, price] = samples_.front();
    const auto lower_match = lower_prices_.find(price);
    if (lower_match != lower_prices_.end()) {
        lower_prices_.erase(lower_match);
    }
    else {
        upper_prices_.erase(upper_prices_.find(price));
    }
    retained_trade_times_.erase(trade_id);
    samples_.pop_front();
}

void SlidingWindow::insert_update(const TickerUpdate &ticker_update) {
    const Price price = ticker_update.price;
    if (lower_prices_.empty() || price <= *lower_prices_.rbegin()) {
        lower_prices_.insert(price);
    }
    else {
        upper_prices_.insert(price);
    }
    samples_.push_back(WindowSample{ticker_update.exchange_time, ticker_update.trade_id, ticker_update.price});
    retained_trade_times_.emplace(ticker_update.trade_id, ticker_update.exchange_time);
    rebalance_price_partitions();
}

void SlidingWindow::rebalance_price_partitions() {
    while (lower_prices_.size() > upper_prices_.size() + 1) {
        upper_prices_.insert(lower_prices_.extract(std::prev(lower_prices_.end())));
    }
    while (lower_prices_.size() < upper_prices_.size()) {
        lower_prices_.insert(upper_prices_.extract(upper_prices_.begin()));
    }
}

} // namespace coinbase_ticker_statistics
