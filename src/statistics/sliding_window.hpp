#pragma once

#include <common/result.hpp>

#include "statistics/window_options.hpp"
#include <common/types.hpp>

#include <chrono>
#include <deque>
#include <optional>
#include <set>
#include <unordered_map>

namespace coinbase_ticker_statistics {

/**
 * Exact fixed-point statistics for a single symbol, owned by the calling thread.
 *
 * Insertion/expiration costs O(log N) per sample; queries cost O(1).
 * Two ordered partitions use O(N) memory without accumulating stale heap entries.
 * Tree nodes are transferred during rebalancing, avoiding additional allocations.
 * The caller must discard this object if an allocation fails during an update:
 * recovering from memory exhaustion would complicate the normal processing path.
 */
class SlidingWindow {
  public:
    [[nodiscard]] static Result<SlidingWindow> create(WindowOptions options);

    /**
     * Expire old observations, insert this ticker update, and return statistics.
     * Window time must be nondecreasing across accepted observations. Ignored
     * duplicates return nullopt without changing any state.
     * @return Statistics, a successful nullopt for filtered events, or a typed error.
     * InvalidInput, OutOfOrderTimestamp, and OutOfRange leave state unchanged.
     * Capacity is checked after prospective expiration; exceeding it is OutOfRange.
     */
    [[nodiscard]] Result<std::optional<Statistics>> add_update(const TickerUpdate &ticker_update);

    /** Return the last accepted window's statistics, or nullopt before its first event. */
    [[nodiscard]] std::optional<Statistics> snapshot() const;

    [[nodiscard]] SampleCount size() const noexcept;

  private:
    explicit SlidingWindow(WindowOptions options);

    struct WindowSample {
        Timestamp exchange_time;
        TradeId trade_id;
        Price price;
    };

    struct PendingUpdate {
        SampleCount expired_sample_count;
        PriceSum sum_after_update;
    };

    // Prepare and validate before changing the retained window.
    [[nodiscard]] bool is_expired(Timestamp observation_time, Timestamp window_time) const;
    [[nodiscard]] Result<PendingUpdate> prepare_update(const TickerUpdate &ticker_update) const;

    // Apply expiration/insertion while preserving the median partition invariants.
    void remove_oldest_sample();
    void insert_update(const TickerUpdate &ticker_update);
    void rebalance_price_partitions();

    // Retained observations, their indexes, and the exact aggregate belong to this window.
    std::chrono::nanoseconds duration_;
    SampleCount max_observations_;
    std::optional<Timestamp> last_accepted_time_;
    std::deque<WindowSample> samples_;
    std::unordered_map<TradeId, Timestamp> retained_trade_times_;
    std::multiset<Price> lower_prices_;
    std::multiset<Price> upper_prices_;
    PriceSum sum_{};
};

} // namespace coinbase_ticker_statistics
