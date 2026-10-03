#pragma once

#include "result.hpp"

#include "statistics/window_options.hpp"
#include "types.hpp"

#include <deque>
#include <optional>
#include <set>
#include <unordered_map>

namespace coinbase_ticker_statistics {

/**
 * Floating-point statistics for a single symbol, owned by one event-loop thread.
 *
 * Insertion/expiration costs O(log N) per trade; queries cost O(1).
 * Two ordered partitions use O(N) memory without accumulating stale heap entries.
 * Tree nodes are transferred during rebalancing, avoiding additional allocations.
 * The caller must discard this object if an allocation fails during an update:
 * recovering from memory exhaustion would complicate the normal processing path.
 */
class SlidingWindow {
  public:
    /**
     * Construct a validated window.
     * @return A window, or InvalidConfiguration for invalid duration.
     */
    [[nodiscard]] static Result<SlidingWindow> create(WindowOptions options);

    /**
     * Expire old observations, insert this trade, and return statistics.
     * Window time must be nondecreasing across accepted observations. Ignored
     * duplicates return nullopt without changing any state.
     * @return Statistics, a successful nullopt for filtered events, or a typed error.
     * InvalidInput, LateTrade, and OutOfRange leave state unchanged.
     */
    [[nodiscard]] Result<std::optional<Statistics>> add_trade(const Trade &trade);

    /** Return the last accepted window's statistics, or nullopt before its first event. */
    [[nodiscard]] std::optional<Statistics> snapshot() const;

    /** Return the number of retained observations. */
    [[nodiscard]] SampleCount size() const noexcept;

  private:
    /** The factory establishes all option invariants before invoking this constructor. */
    explicit SlidingWindow(WindowOptions options);

    struct WindowSample {
        Timestamp time;
        TradeId trade_id;
        Price price;
    };

    [[nodiscard]] bool is_expired(Timestamp observation_time, Timestamp window_time) const;
    void remove_oldest_sample();
    void rebalance_price_partitions();

    std::chrono::nanoseconds duration_;
    std::optional<Timestamp> last_accepted_time_;
    std::deque<WindowSample> samples_;
    std::unordered_map<TradeId, Timestamp> retained_trade_times_;
    std::multiset<Price> lower_prices_;
    std::multiset<Price> upper_prices_;
    /** Neumaier compensation limits cancellation when expired prices are subtracted. */
    struct CompensatedSum {
        Statistic value{};
        Statistic correction{};
        void add(Statistic amount);
        [[nodiscard]] Statistic total() const {
            return value + correction;
        }
    };
    struct PendingUpdate {
        SampleCount expired_sample_count;
        CompensatedSum sum_after_update;
    };
    [[nodiscard]] Result<PendingUpdate> prepare_trade_update(const Trade &trade) const;
    void insert_trade(const Trade &trade);

    CompensatedSum sum_;
};

} // namespace coinbase_ticker_statistics
