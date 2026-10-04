# Design decisions and tradeoffs

## Architecture and ownership

```text
Application — lifecycle, signals, RunControl and ExecutionContext
    |
    +— TickerFeed — FeedConnection — Beast/Asio
    |
    +— ApplicationFeedHandler
            |— StatisticsProcessor — per-symbol SlidingWindow
            |— OutputSink — CsvSink — CsvWriter
```

One thread runs one Asio event loop. `FeedConnection` owns DNS, TCP, verified
TLS, WebSocket operations and deadlines. `TickerFeed` owns subscription encoding,
JSON decoding, filtering and input counters. The application handler routes each
typed update through statistics to the sink synchronously. This preserves order
and gives mutable state one owner without locks.

The feed root exposes `TickerFeed` and its typed handler contract. `parser/`
contains decoding, while `transport/` contains the reusable connection and its
settings. Tests mirror these responsibilities.

`StatisticsProcessor` has no networking, logging or output dependencies.
`OutputSink` is a compile-time contract requiring only
`write_statistics(update) -> Result<void>`; another sink can reuse the processor
and routing without adopting CSV lifecycle methods. Templates express these
small boundaries. The transport retains private `std::function` callbacks to
keep Beast implementation types out of consumer headers.

The application owns every runtime object. Borrowed handlers, the sink and the
execution context remain alive while I/O drains; member destruction order also
keeps context/control alive until abandoned handlers are destroyed. Raw message
views and typed updates are borrowed only for synchronous delivery.

`ExecutionContext` exposes failure reporting, stop requests and completion state.
It contains no feed, sink or configuration services. `RunControl` preserves the
first failure and sets stopping state before invoking application policy, since
completion can reenter synchronously. One application-supplied stop action
avoids templating run control and CSV output over the concrete feed type.

## Configuration boundary

`load_config()` parses, validates and resolves paths before returning settings.
Validation composes policies owned by feed, statistics and output. Direct C++
construction or later edits require `validate_config()` before running; the
configuration remains a simple aggregate rather than an immutable wrapper.

`symbols` and `output.path` are required. Operational defaults and ranges are
listed in [Configuration](CONFIGURATION.md). Strict conversion rejects numeric
coercion and overflow. Unknown keys are ignored and duplicate keys use the last
value; full schema policing is outside scope. nlohmann's DOM and small startup
copies favor readable validation over allocation efficiency.

## Sliding-window algorithm

Each symbol has an independent exchange-time window `(t-duration, t]`. A deque
expires observations; two ordered multisets maintain the median, minimum and
maximum. A retained-ID index filters duplicates. Updating a window that expires
K observations costs O((K+1) log N); snapshots cost O(1), and storage is O(N).
Tree allocation is a deliberate simplicity and locality tradeoff.

Retained duplicates do not advance time or mutate the window. IDs may be reused
after expiration. Decreasing timestamps are fatal, and arithmetic is checked
before mutating live state. Idle symbols emit no synthetic rows.

Every accepted ticker update contributes one equally weighted price. Coinbase's
[ticker channel](https://docs.cdp.coinbase.com/exchange/websocket-feed/channels#ticker-channel)
can batch cascading matches, so these statistics describe received observations,
not a complete trade tape or a volume-weighted average.

The assignment permits bounded-error approximation, but this implementation
keeps every accepted observation until expiration. There is no sample-count cap
or approximation mode: memory grows with arrival rate and window duration.
Allocation failure is fatal rather than silently dropping data.

## Asynchronous transport and shutdown

Asynchronous operations let signals interrupt connection setup or an idle read,
and let CSV timers run without another ticker arriving. A simple blocking loop
would need a separate mechanism to wake a pending TLS/WebSocket read. The
additional callback and lifetime coordination supports bounded shutdown on one
thread; parsing, calculations and formatting remain synchronous.

TLS verifies both the certificate chain and hostname against the environment's
trust store. Public access removes authentication, not TLS negotiation. Readiness
means the subscription was written, without waiting for its acknowledgement.

On SIGINT/SIGTERM or a fatal failure, application policy stops the feed. Setup is
cancelled or a normal WebSocket close begins, with a deadline forcing transport
closure if the peer does not respond. Feed completion closes output and cancels
the signal wait; the event loop then drains naturally. `io_context::stop()` is
reserved for unexpected exceptions. The original failure survives later cleanup
errors. Closing the connection requires no separate unsubscribe.

One connection carries all configured symbols. Coinbase's
[best practices](https://docs.cdp.coinbase.com/exchange/websocket-feed/best-practices)
recommend distributing subscriptions across connections to spread inbound load,
especially for the full channel. A single connection keeps this assignment
small; the transport/protocol boundary allows reuse without implementing a
connection manager now.

## CSV batching and errors

`CsvSink` owns the file, buffering policy and flush timer. `CsvWriter` formats
rows into a reusable string and borrows the stream. Output opens only after
connection and subscription succeed, preserving previous output on startup
failures. Each successful run opening replaces its destination file.

Rows flush after a configurable threshold or interval from the first pending
row, whichever comes first. Later rows do not postpone that deadline. The header
flushes immediately, and shutdown flushes the remaining batch. The stream's
buffer holds rows without an additional queue.

Batching reduces synchronous flush frequency on the event-loop thread while
keeping output continuous, including when the feed is idle. Writes and flushes
still block that thread; the interval and transport deadlines are scheduling
bounds, not hard real-time guarantees. No latency or throughput claim is made.

Immediate write and threshold-flush failures return `Result<void>`. Timer flush
failures occur after `write_statistics()` has returned, so they call
`ExecutionContext::fail()` and application policy initiates shutdown. The sink
has no feed dependency. Batching delays visibility and error detection; a failed
flush can leave fewer rows than the emitted counter reports. Flush does not
ensure disk durability, and crashes or SIGKILL can lose pending rows.

## Numeric model

Prices and statistics use native `long double`, parsed directly without narrowing
through `double`. Precision depends on the platform. This is a take-home
simplicity choice, not a promise of exact decimal arithmetic. Fixed-point prices
at a known product scale would be preferable for exact financial calculations.

Mean uses compensated addition/subtraction to limit cancellation during
expiration; the sum resets after complete expiration. Even-count median uses
`std::midpoint`. Nonfinite or negative prices, conversion overflow/underflow to
zero, and nonfinite accumulated sums are rejected. Representable subnormal
prices are accepted.

CSV uses locale-independent `max_digits10` significant digits to preserve stored
values on round-trip. It may use scientific notation and does not preserve a
fixed decimal scale or trailing zeros. Platforms without floating-point
`from_chars` use a classic-locale stream fallback.

There is no universal floating-point error bound for arbitrary input histories.
The independent Decimal verifier uses `1e-12` relative and `1e-15` absolute
tolerances by default; these are verification thresholds, not mathematical
guarantees. No observations are approximated to reduce memory use.

## Explicit scope

Malformed ticker data, out-of-order timestamps, and transport/output failures
end the run. Unrelated valid messages and retained duplicates are filtered; a
normal peer close succeeds. There are no reconnects, sequence recovery,
heartbeats, stale-feed monitoring, worker threads, output queues, runtime reload,
or crash-durability machinery. This is not a production trading system.

Logging is synchronous and best effort: UTC lifecycle records and the final
summary go to stderr for both INFO and ERROR. Per-ticker logging is omitted
because CSV already records accepted updates. Collection and rotation belong to
the environment. The logger reuses output timestamp formatting; a separate
utility module would add little value here.

## Verification approach

Tests are organized around observable contracts, with table-driven invalid
inputs and independent references instead of a separate test for each branch.
Randomized windows are checked against a sorted reference; a deterministic
JSON-lines fixture verifies the complete parse/statistics/CSV path. Tests also
cover cancellation, first-error preservation, TLS verification, signal shutdown
and partial-batch visibility. Local TLS servers run in bounded child processes;
unit and integration tests need loopback access but no external feed.

The [test report](../logs/test-results.log) records clean Release and UBSan runs.
AddressSanitizer could not initialize on this host, including for an empty program;
no ASan pass is claimed. [Live diagnostics](../logs/live-run.log) and the
[independent live verification](../logs/live-verification.log) record a public
feed capture, continuous output and graceful shutdown. The short live capture
does not cover five-minute expiration; deterministic tests do. Linux CI is
configured, but its results must be confirmed separately.

```sh
ctest --test-dir build --output-on-failure
python3 tools/verify_csv.py --config data/fixture_config.json --require-expiration
./build/bin/coinbase_ticker_statistics_app --config config/live_verification.json
# Stop with Ctrl-C, then verify:
python3 tools/verify_csv.py --config config/live_verification.json
```

See [Build setup](BUILDING.md) for sanitizer options. Generated CSVs and build/CI
artifacts stay under ignored `build/`; `logs/` retains only current execution
evidence. The certificate and key in `data/` are public local-test fixtures.
