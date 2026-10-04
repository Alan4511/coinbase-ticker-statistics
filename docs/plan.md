Refactor the current Coinbase ticker statistics project with a focus on ownership, lifecycle separation, real extensibility, and removing redundant validation. Treat this as production-quality code within the explicit assignment scope, but do not add out-of-scope operational features such as reconnect/backoff, heartbeat monitoring, sequence recovery, stale-feed watchdogs, worker pools, or crash-durability machinery.

The existing statistics core and the `FeedConnection` / `TickerFeed` separation are already coherent. Make meaningful architectural changes where they improve ownership, separation of concerns, failure propagation, or future extensibility, but avoid unrelated rewrites and speculative abstractions.

## 1. Make configuration ownership explicit

The `config` module should own parsing and validation of application configuration.

The normal executable flow is already:

```text
main
  -> load_config()
  -> parse_config()
  -> validate_config()
  -> run_application(valid config)
```

Therefore:

- remove the redundant `validate_config(config)` call from `run_application()`,
- define the contract of `run_application()` as requiring a validated `Config`,
- document that `Config` values obtained from `load_config()` / `parse_config()` satisfy that precondition,
- keep all configuration validation rules in the `config` module,
- keep `validate_config()` as the composition-level validation function that may call validation helpers from owning modules such as:
  - symbol/subscription validation,
  - `validate_feed_config`,
  - `validate_window_options`,
  - `validate_csv_config`.

Do not move module-specific validation logic into `application.cpp`.

The desired boundary is:

```text
config module
  parses + validates + resolves config
        |
        v
     valid Config
        |
        v
application module
  consumes config only
```

If tests directly construct `Config` and call `run_application()`, update those tests/helpers so they construct valid configs rather than relying on `run_application()` to validate them.

---

## 2. Keep and strengthen the OutputSink abstraction

Retain `sink.hpp`.

The sink abstraction is intentional because the output destination may later change without touching statistics logic.

Desired extension model:

```text
StatisticsUpdate
      |
      v
  OutputSink
    |   |   |
    |   |   +-- future DatabaseSink
    |   +------ future BrokerSink
    +---------- CsvSink
```

Keep the common sink contract narrow, ideally just the operation needed by the event-processing path:

```cpp
template <typename Sink>
concept OutputSink = requires(
    Sink& sink,
    const StatisticsUpdate& update
) {
    { sink.write_statistics(update) }
        -> std::same_as<Result<void>>;
};
```

Do not force CSV-specific lifecycle operations such as `open`, `flush`, file paths, timers, or `close` into the common concept unless they are genuinely common to all sinks.

Prefer compile-time polymorphism rather than introducing a virtual base class.

---

## 3. Make ApplicationFeedHandler genuinely sink-agnostic

`ApplicationFeedHandler` currently depends directly on `CsvSink`.

Refactor it so it depends on the `OutputSink` contract instead.

Preferred direction:

```cpp
template <OutputSink Sink>
class ApplicationFeedHandler {
public:
    ApplicationFeedHandler(
        StatisticsProcessor processor,
        Sink& sink,
        ...
    );

private:
    StatisticsProcessor processor_;
    Sink& sink_;
};
```

Current production wiring should naturally use:

```cpp
ApplicationFeedHandler<CsvSink>
```

A future sink such as `BrokerSink` should require only changing composition/wiring, not statistics processing logic.

Do not introduce runtime polymorphism unless the application actually needs runtime-selectable sink types.

---

## 4. Move application lifecycle responsibilities out of ApplicationFeedHandler

Clarify this rule:

> Handlers process events. Application owns lifecycle.

`ApplicationFeedHandler` should primarily handle:

```text
TickerUpdate
    |
    v
StatisticsProcessor
    |
    v
StatisticsUpdate
    |
    v
OutputSink
```

Avoid making it responsible for unrelated application lifecycle concerns such as:

- cancelling `signal_set`,
- owning or mutating the final application result,
- deciding process-level shutdown policy,
- final global cleanup.

Move those concerns toward the application orchestration layer.

If the feed handler must receive `on_stopped`, use that callback to report completion upward rather than directly manipulating unrelated application resources where possible.

---

## 5. Remove the CsvSink -> TickerFeed shutdown dependency

The current flush-error path captures the `TickerFeed` and calls `feed->stop()`.

Remove that sideways dependency.

The desired direction is:

```text
CsvSink detects output error
        |
        v
Application receives/records fatal error
        |
        v
Application decides to stop TickerFeed
```

The output layer should report errors, not control the network feed.

This should also make future sink types cleaner: a `BrokerSink` or `KafkaSink` should not need access to the feed in order to report failure.

Refactor the lifecycle/error wiring so that the application is the coordinator.

Avoid fragile temporal assumptions such as callbacks being safe only because a `unique_ptr<TickerFeed>` is guaranteed to have been assigned before the callback can run.

---

## 6. Preserve FeedConnection / TickerFeed separation

Keep the current semantic split.

`FeedConnection` should own generic transport/session concerns:

- DNS,
- TCP,
- TLS,
- WebSocket handshake,
- subscription write,
- async reads,
- bounded setup/shutdown,
- transport errors.

It should not know about:

- ticker JSON semantics,
- sliding windows,
- CSV,
- statistics,
- application shutdown policy.

`TickerFeed` should own Coinbase ticker protocol semantics:

```text
raw WebSocket message
      |
      v
parse ticker message
      |
      v
TickerUpdate
```

This is a good future extension point:

```text
FeedConnection
     |
     +-- TickerFeed
     +-- future Level2Feed
     +-- future TradesFeed
```

Do not merge protocol parsing into transport.

---

## 7. Keep the statistics module essentially unchanged

Preserve the independence of:

```text
StatisticsProcessor
SlidingWindow
```

They should remain free of:

- Asio,
- Beast,
- JSON,
- file I/O,
- logging,
- output lifecycle,
- shutdown policy.

Keep the existing per-symbol ownership model.

Do not genericize `SlidingWindow` with strategy classes or template parameters unless there is a concrete need.

Preserve the current data-structure model:

```text
deque                 -> expiration order
two ordered multisets -> median / low / high
trade-id index        -> retained duplicate detection
compensated sum       -> mean
```

If useful, improve comments/tests around the median invariants:

```text
lower.size() == upper.size()
or
lower.size() == upper.size() + 1

max(lower) <= min(upper)
```

Avoid unrelated changes to this module.

---

## 8. Keep one WebSocket connection for the assignment

Do not reintroduce configurable connection groups now.

The current single connection carrying multiple symbols satisfies the assignment.

Document the future scaling path instead of implementing it.

Potential future component:

```text
FeedManager
```

Semantic meaning:

```text
FeedConnection = one transport session
TickerFeed     = Coinbase ticker protocol on one connection
FeedManager    = coordination/ownership of multiple TickerFeed instances
```

Future architecture could become:

```text
FeedManager
  +-- TickerFeed #1 -> BTC-USD
  +-- TickerFeed #2 -> ETH-USD
  +-- TickerFeed #3 -> SOL-USD
```

without changing statistics/output.

Do not implement `FeedManager` until multiple feed instances actually exist.

---

## 9. Preserve the single-threaded Asio model

Keep one `io_context` thread.

The reasoning should remain:

- per-message processing is cheap,
- state has one owner,
- ordering is deterministic,
- no locks are needed,
- async I/O allows multiple outstanding I/O activities without blocking a thread per connection.

Do not add worker threads unless profiling demonstrates an actual CPU bottleneck.

---

## 10. Review timed CSV flushing deliberately

The current `CsvSink` uses:

- row-count threshold,
- timer-based flush,
- buffered output.

Keep this only if the intended semantic is:

> buffered rows should become externally visible within a bounded time even if the feed becomes idle.

If that is intentional, retain it and document the rationale clearly.

If that behavior is not important for the assignment, simplify the sink.

Do not switch to flush-per-row.

Regardless of whether timed flushing remains, errors should be reported upward to the application rather than causing the sink to directly stop the feed.

---

## 11. Keep Result-based error handling

Continue using:

```cpp
std::expected<T, Error>
```

for normal validation, parsing, protocol, statistics, transport, and output failures.

Keep exception handling only at unavoidable library/emergency boundaries.

Do not add an exception hierarchy.

---

## 12. Preserve module-owned configuration types

Keep:

```cpp
Config {
    Symbols symbols;
    FeedConfig feed;
    WindowOptions window;
    CsvConfig output;
}
```

with sub-config types owned by the modules that understand them:

```text
FeedConfig      -> feed
WindowOptions   -> statistics
CsvConfig       -> output
Config          -> composition/config module
```

Do not pass the whole application `Config` into lower-level modules.

---

## 13. Update design documentation

Update the README / architecture documentation so the intended boundaries are explicit.

Use a concise conceptual diagram such as:

```text
                         Application
                lifecycle / orchestration
                           |
        +------------------+------------------+
        |                  |                  |
    TickerFeed     StatisticsProcessor      OutputSink
        |                  |                  |
 FeedConnection       SlidingWindow         CsvSink
        |                                     |
   Beast/Asio                            CsvWriter
```

Document these extension paths:

### Output extension

```text
StatisticsUpdate -> CsvSink
                 -> future BrokerSink
                 -> future KafkaSink
                 -> future DatabaseSink
```

### Feed protocol extension

```text
FeedConnection -> TickerFeed
               -> future Level2Feed
               -> future TradesFeed
```

### Connection scaling

Current:

```text
one TickerFeed
one FeedConnection
many symbols
```

Future:

```text
FeedManager
  +-- TickerFeed
  +-- TickerFeed
  +-- TickerFeed
```

### CPU scaling

Current:

```text
one io_context thread
```

Future only if profiling justifies it:

```text
I/O event loop
      |
      v
partitioned workers / processing
```

---

## 14. Desired architectural properties

After the refactor, these statements should be true:

1. `run_application()` assumes it receives a validated `Config`.
2. All configuration validation rules live in the config/module-validation layer, not application orchestration.
3. Replacing CSV with another sink does not require modifying statistics code.
4. `ApplicationFeedHandler` does not depend specifically on `CsvSink`.
5. Output failure is reported upward; the output layer does not stop the feed directly.
6. `Application` owns lifecycle and shutdown policy.
7. `FeedConnection` contains transport logic only.
8. `TickerFeed` contains Coinbase ticker protocol logic.
9. Statistics code knows nothing about network or output details.
10. Adding multiple connections later does not require changing statistics/output.
11. Every long-lived resource has an obvious owner.
12. No synchronization machinery exists unless multiple threads actually require it.

---

## Implementation priority

Apply changes in roughly this order:

1. Remove redundant `validate_config()` from `run_application()` and establish the validated-config precondition.
2. Make `ApplicationFeedHandler` generic over `OutputSink`.
3. Move application-lifecycle state/cancellation responsibilities out of `ApplicationFeedHandler`.
4. Remove the `CsvSink -> feed.stop()` dependency and route fatal sink errors through application orchestration.
5. Preserve `TickerFeed` / `FeedConnection` separation.
6. Preserve `StatisticsProcessor` / `SlidingWindow`.
7. Reassess timed flushing only if simplification materially improves the design.
8. Update tests affected by lifecycle/composition changes.
9. Update README/design documentation with ownership rules and future-extension paths.

This is a targeted architectural refactor, not a minimal-change patch. Make substantial changes where they improve ownership, failure propagation, or extensibility, but avoid unrelated rewrites and speculative abstractions.
