# Design decisions and tradeoffs

## Architecture and ownership

```text
Application — lifecycle, signals, RunControl and RunControlHandle
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

`StatisticsProcessor` has no networking, logging or output dependencies.
`OutputSink` is a compile-time contract requiring only
`write_statistics(update) -> Result<void>`, so other sinks can reuse statistics
and routing without implementing CSV lifecycle methods. Templates express these
boundaries; private transport callbacks keep Beast types out of consumer headers.

The application owns runtime objects and keeps borrowed handlers, the sink and
run-control handle alive until I/O drains. Message views and typed updates are
borrowed only for synchronous delivery. `RunControlHandle` is retained as a
borrowed reporting interface so reusable modules do not depend on `Application`.
It owns no state or executor; `RunControl` owns run state and the stop policy.
Feed callbacks carry only ticker updates and completion results; the feed and
sink retain their run-control handles for failure reporting, while the application
uses its own handle for lifecycle coordination. Handlers bind dependencies at
construction, trading per-callback handle flexibility for a smaller event contract.
`RunControl` preserves the first failure, including through reentrant shutdown
and subsequent cleanup errors. Its running, stopping and completed states
distinguish a stop request from completion; completion ends event processing,
while resource cleanup and destruction may still follow.

Shared vocabulary and JSON helpers live in `common/`; lifecycle coordination
lives in `runtime/`. The feed root exposes its typed protocol interface,
`parser/` decodes messages and `transport/` owns the reusable connection. Tests
mirror these responsibilities.

## Extensibility

- **Additional feeds:** reuse `FeedConnection` for transport, with a separate
  subscription encoder, parser and protocol adapter. The current `FeedHandler`
  contract delivers `TickerUpdate`; other payloads need a corresponding typed
  contract or conversion into that model.
- **Alternative output:** implement `OutputSink::write_statistics()` and reuse
  the templated application handler. Application wiring must still provide the
  destination's configuration, startup and cleanup; CSV lifecycle operations
  are not required by the delivery contract.
- **Multiple connections:** a future feed manager can own connections for symbol
  groups on the same event loop. It must add group configuration, track individual
  feed readiness/completion, open shared output once, and wait for all feeds to
  stop before completing the run and closing output. Each handler remains bound
  to its dependencies at construction.

Components may share one `RunControlHandle`, or borrow separate handles backed
by the same `RunControl`. They share run state and shutdown policy; handles do
not identify feeds or aggregate completion. That coordination belongs to the
application or future manager. The shipped application uses one ticker connection
and one CSV sink.

## Configuration and JSON boundary

`load_config()` reads the file into a string, calls `parse_and_validate_config(string_view)`,
then resolves output paths before returning validated settings. Configurations
and complete WebSocket message views use the same direct deserialization
boundary. The file string costs a startup allocation but avoids a separate
streaming path; feed decoding needs no copy of the framed message.

[Glaze](https://github.com/stephenberry/glaze/tree/v9.0.0) maps JSON into local
objects. Each module owns its mappings in `json_meta.hpp`; domain types remain
independent of JSON. `common/json.hpp` translates parsing errors into `Result<T>`.
Only successful values escape the boundary. Price and UTC timestamp conversion
reuse domain parsers and preserve detailed errors, including `OutOfRange`.
Allocation failure remains an emergency exception.

Length-delimited input is parsed without assuming a null terminator. Unknown
keys are ignored, but their values must contain valid JSON. Trailing content,
comments, trailing commas and invalid UTF-8 are rejected; JSON nesting is limited
to 256 levels. Integer conversion checks range without floating-point rounding
and accepts positive-exponent forms such as `3e2`. Glaze's native integer reader
rejects decimal-point and negative-exponent spellings, even when mathematically
integral; no custom number parser is added.

Ticker routing reads the message type before decoding ticker fields. This scans
ticker messages twice but keeps control-message filtering simple without a DOM.
The extra O(message-size) scan costs CPU; price/timestamp conversion and statistics
run only once. Its impact on total processing time has not been measured.
The pinned tagged-variant reader changes required-tag and dispatch semantics,
so separate dispatch preserves the current protocol behavior.
Decoded strings preserve JSON escapes, and the resulting update owns its symbol.
Glaze's header-only mappings reduce manual field assembly at the cost of template
compilation. Configuration parsing and mappings stay in the implementation, so
including the public config API does not instantiate or expose Glaze parsing.
No parsing throughput improvement is claimed without measurement.

`symbols` and `output.path` are required. Defaults and ranges are listed in
[Configuration](CONFIGURATION.md). Repeated valid keys use their last value;
repeated sections replace earlier sections rather than merge fields. An invalid
earlier field fails even if a later occurrence would replace it. Conversion
errors follow input order; business validation proceeds through symbols, feed,
window and output.

Validation belongs to each settings module, with symbol uniqueness coordinated
by config. Supported windows span 1 second to 365 days, also safely representable
in nanoseconds. Direct C++ construction or later edits require `validate_config()`
before running; settings remain a simple aggregate rather than an immutable wrapper.

## Sliding-window algorithm

Each symbol has an independent exchange-time window `(t-duration, t]`. A deque
expires observations; two ordered multisets maintain the median, minimum and
maximum. A retained-ID index filters duplicates. Updating a window that expires
K observations costs O((K+1) log N); snapshots cost O(1), and storage is O(N).
Tree allocation trades speed and cache locality for readable invariants. Bulk
expiration restores partition sizes once before insertion, avoiding intermediate
node transfers after each removal; asymptotic complexity remains unchanged.

Retained duplicates do not advance time or mutate the window. IDs may be reused
after expiration. Decreasing timestamps are fatal, and arithmetic is checked
before mutating live state. Idle symbols emit no synthetic rows.

Every accepted ticker update contributes one equally weighted price. Coinbase's
[ticker channel](https://docs.cdp.coinbase.com/exchange/websocket-feed/channels#ticker-channel)
can batch cascading matches, so these statistics describe received observations,
not a complete trade tape or a volume-weighted average.

The assignment permits bounded-error approximation, but this implementation
keeps every accepted observation until expiration, subject to a configurable
per-symbol limit (100,000 by default). Capacity is checked after prospective
expiration; overflow fails before mutation rather than silently dropping data.
The default is an operational starting point, not a measured exchange-rate budget.
This bounds retained sample count, not total process bytes or allocation latency.
Node containers remain dynamically allocated; no pool or full-capacity reservation
is added without evidence that its complexity or upfront memory cost is worthwhile.
Allocation failure is fatal and reported explicitly; under memory exhaustion even
diagnostic construction is best effort, so recovery is not promised.

## Asynchronous transport and shutdown

Asynchronous operations let signals interrupt connection setup or an idle read,
and let CSV timers run without another ticker arriving. A blocking loop would
need another mechanism to wake a pending TLS/WebSocket read. The additional
lifetime coordination supports bounded shutdown on one thread; parsing,
calculations and formatting remain synchronous.

Beast handles WebSocket framing and close; Asio handles network I/O, signals and
timers. One deadline spans DNS, TCP, TLS, upgrade and subscription writing, then
bounds shutdown. Beast's [WebSocket timeouts](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/using_websocket/timeouts.html)
do not cover the entire connection sequence. TLS verifies the certificate chain
and hostname against the environment's trust store. Public access removes
authentication, not TLS negotiation. Readiness means the subscription was
written, without waiting for its acknowledgement.

On SIGINT/SIGTERM or a fatal failure, application policy stops the feed. Setup is
cancelled or a normal WebSocket close begins; the deadline forces transport
closure if the peer does not respond. Feed completion closes output and cancels
the signal wait, then the event loop drains. The original failure survives
cleanup errors; final CSV cleanup failures are also logged separately. Closing
the connection requires no separate unsubscribe.

Normal completion closes output before the event loop drains; final reporting
does not close it again. Unexpected exceptions use an emergency abort path that
stops the event loop and closes output even if feed completion never runs.
This can abandon an in-progress WebSocket close handshake.

One connection carries all configured symbols. Coinbase's
[best practices](https://docs.cdp.coinbase.com/exchange/websocket-feed/best-practices)
recommend distributing subscriptions across connections to spread inbound load,
especially for the full channel. A single connection keeps this assignment
small; the transport/protocol boundary allows reuse without a connection manager.

## CSV batching and errors

`CsvSink` owns the file, buffering policy and flush timer. `CsvWriter` formats
rows into a reusable string and borrows the stream. Output opens only after
connection and subscription succeed, preserving previous output on startup
failures. Each run that opens output replaces its destination file.

Rows flush after a configurable threshold or interval from the first pending
row, whichever comes first. Later rows do not postpone that deadline. The header
flushes immediately, and shutdown flushes the remaining batch. The stream's
buffer holds rows without an additional queue.

Batching reduces flush frequency while keeping output continuous, including
when the feed is idle. Writes and flushes still block the event-loop thread;
slow output delays reads, signals and timers. The flush interval and transport
deadlines are scheduling bounds, not hard real-time guarantees.

An SPSC queue and dedicated writer thread could isolate disk I/O, but would need
a saturation policy, cross-thread error reporting and shutdown draining. Omitting
them keeps ownership and error propagation on one thread. Batching reduces I/O
frequency without providing that isolation; no latency or throughput claim is made.

Immediate write and threshold-flush failures return `Result<void>`. Timer flush
failures occur after `write_statistics()` returns, so they report asynchronously
through `RunControlHandle`; the application owns fatal-error and shutdown policy.
The sink has no feed dependency. Batching delays visibility and error detection;
a failed flush can leave fewer rows than the emitted counter reports. Flush does
not ensure disk durability, and crashes or SIGKILL can lose pending rows.

## Numeric model

Prices use signed 64-bit integer ticks at a fixed scale of eight decimal places:
`1 tick = 0.00000001`. The supported range is `0..92233720368.54775807`.
Decimal and scientific-notation input is parsed directly into ticks without
floating-point conversion. Values outside that range or off that grid fail;
extra trailing zeros are accepted, but input prices are never silently rounded.

The window maintains an exact 128-bit unsigned sum using Boost's fixed-width,
allocation-free multiprecision integer. A maximum nonnegative 64-bit price
multiplied by a sample count of at most 64 bits fits in 127 bits. Expiration
subtracts exact ticks. Mean is retained as sum/count; even-count median as the two
middle prices/2. These fractions reach the sink without premature rounding.

CSV rounds mean and median to the nearest tick, with ties going to even. Their
absolute error relative to the exact statistic is at most half a tick
(`0.000000005`), independent of window size and stream history. Low, high and
observed prices are exact. The bound applies to statistics of received ticker
updates, not to omitted trades or sampling error.

Locale-independent output uses ordinary decimal notation with up to eight
fractional digits, omitting insignificant trailing zeros. The independent Decimal
verifier checks equality with the specified rounded result.

A common fixed scale avoids fetching product metadata or configuring per-symbol
scales. Coinbase documents the price grid through
[`quote_increment`](https://docs.cdp.coinbase.com/api-reference/exchange-api/rest-api/products/get-single-product).
The example symbols' increments were representable in the 2026-10-07 metadata
check; this is not a permanent exchange-wide precision guarantee. Products
requiring finer prices are unsupported. Exact fractions and wide integer division
add formatting work in exchange for portable, exact stored prices and statistics.
The observation limit bounds sample count separately from this numeric policy;
it does not approximate the window.

## Explicit scope

Malformed ticker data, out-of-order timestamps and transport/output failures
end the run. Unrelated valid messages and retained duplicates are filtered; a
normal peer close succeeds. There are no reconnects, sequence recovery,
heartbeats, stale-feed monitoring, worker threads, output queues, runtime reload
or crash-durability machinery. This is not a production trading system.

Logging is synchronous and best effort: UTC lifecycle records and the final
summary go to stderr for both INFO and ERROR. Per-ticker logging is omitted
because CSV already records accepted updates. Collection and rotation belong to
the environment.

Logging and CSV share UTC formatting in `common/`; numeric CSV formatting stays
in `output/`. A small common library keeps the formatter implementation out of
consumer headers and avoids a logging dependency on the output module.

## Verification approach

Tests exercise production entry points with table-driven inputs and independent
references. Randomized windows are checked against a sorted reference; a
JSON-lines fixture covers parsing, statistics and CSV together. Local TLS servers
verify cancellation during setup, TLS trust, signal shutdown, first-failure
preservation across close/CSV failures, and partial-batch visibility.
Tests need loopback access but no external feed.

The [test report](../logs/test-results.log) records Release and UBSan runs.
AddressSanitizer could not initialize on this host, including for an empty program;
no ASan pass is claimed. The empty-program check was repeated on 2026-10-08
inside and outside the sandbox with the same initialization failure.
[Live diagnostics](../logs/live-run.log) and
[independent live verification](../logs/live-verification.log) record continuous
output and graceful shutdown. The short capture does not cover five-minute
expiration; deterministic tests do. Linux CI defines GCC/Clang Release and Clang
ASan/UBSan jobs, but runner results must be confirmed separately; no Linux runtime is available on this development host.

See [Build setup](BUILDING.md) for sanitizer options and the README for commands.
Generated CSVs and build/CI artifacts stay under ignored `build/`; `logs/` retains
current execution evidence. The certificate and key in `data/` are public fixtures.
