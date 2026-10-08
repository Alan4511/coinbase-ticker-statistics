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

Shared domain vocabulary, errors and JSON boundary helpers live in `src/common/`.
Header-only run coordination lives in `src/runtime/`, letting feed and output use lifecycle
operations without depending on `app/`. These folders use the existing public
include root; both remain header-only. Run-control tests live in `tests/runtime/`.

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

`load_config()` reads the file into a string, calls `parse_config(string_view)`,
then resolves output paths before returning validated settings.
Configuration and framed WebSocket messages share one direct deserialization
boundary. Holding a configuration file in memory adds a startup allocation but
avoids a separate streaming path, token-window limits and manual trailing-content
checks. WebSocket messages are decoded directly from their complete Beast buffer
view without a message copy.

`config/settings.hpp` holds the JSON-independent aggregate. `config/json_meta.hpp`
owns its aggregate mapping and composes the settings mappings from feed,
statistics and output. `config.cpp` owns validation and file/path handling.
The public parsing interface exposes Glaze headers to callers, trading compilation
cost for a single parsing interface without duplicated schemas or forwarding overloads.
Validation composes module-owned `validate()` overloads with short-circuiting
`Result::and_then()`, without a generic validation framework. Feed owns the
`validate(const Symbols&)` overload for nonempty Coinbase product IDs; symbol
uniqueness stays in configuration, while statistics owns the supported window range of
1 second to 365 days. The same duration rule applies to configuration loading
and direct window construction; its upper bound also ensures nanosecond
representability. This deliberately caps the statistics module's window range
instead of keeping a broader module range and an application-only limit.
Validation proceeds through symbols, feed, window and output.
Direct C++ construction or later edits require `validate_config()` before running; the
configuration remains a simple aggregate rather than an immutable wrapper.

`symbols` and `output.path` are required. Operational defaults and ranges are
listed in [Configuration](CONFIGURATION.md). Unknown keys are ignored, while
all encountered known fields are decoded and checked. Repeated valid values use
the last occurrence; section setters decode fresh objects so repeated sections
replace earlier sections rather than merge their fields. An invalid earlier
occurrence fails immediately, even if a later duplicate would replace it.
This differs from DOM parsing, which can discard earlier values before conversion.
Business validation still follows symbols, feed, window and output; conversion
errors follow JSON input order.

## JSON mapping

[Glaze](https://github.com/stephenberry/glaze/tree/v9.0.0) decodes directly into
local typed objects using native `glz::meta<T>` mappings. `requires_key()` keeps
required fields explicit and leaves operational defaults on their owning types.
`glz::from<glz::JSON, T>` hooks reuse the exact price and strict UTC parsers.
Each module keeps its mappings and conversion hooks in one `json_meta.hpp`:
configuration owns `Config`, feed owns its settings and wire messages, statistics
owns `WindowOptions`, and output owns `CsvConfig`. `common/json.hpp` owns parsing
options and Result translation. Core domain headers retain no JSON methods or includes. The metadata
headers require Glaze at build time but introduce no runtime I/O into statistics
or output.

Native `glz::error_ctx` diagnostics are translated into our public `Result<T>`
only at the parsing boundary. Local objects may be partially modified during
reading, but are returned only after decoding and validation succeed. Custom
field errors use Glaze's owned context storage; price precision/range failures
retain `OutOfRange`. There is no separate conversion dispatcher or exception
bridge. Allocation failure may still throw and remains an emergency failure.

Read options validate ignored values, reject trailing content, comments, trailing
commas and invalid UTF-8, and safely handle nonterminated WebSocket buffers.
Glaze's native nesting limit of 256 replaces Boost.JSON's default limit of 32.
Integer members use Glaze's native checked conversion, including exact positive
exponent forms such as `3e2`. We impose no additional integer-token classification
policy. Glaze 9.0.0 rejects decimal-point and negative-exponent spellings for integer
members, even when mathematically integral; we retain that library limitation
rather than add a custom number parser. Fractional values and overflow fail without
floating-point rounding. Unsigned trade IDs reject negative values; negative/zero
durations remain errors under module validation.

Ticker routing first reads a small discriminator, then decodes ticker fields only
for ticker messages. This scans ticker messages twice, keeping control-message
handling straightforward without a DOM or a custom discriminated-message parser.
Unused fields are still validated. Prices and timestamps use temporary decoded
strings, preserving JSON escape handling rather than borrowing raw string views.
The resulting update owns its symbol; raw control-message views are consumed
synchronously. No throughput improvement is claimed without measurement.

Glaze 9.0.0 adds a pinned header-only dependency and removes the compiled
Boost.JSON target. Template metadata reduces manual field assembly but adds
compiler work and ties adapters to Glaze's customization API. CMake consumes
only its headers, retaining this project's CMake 3.20 minimum instead of adopting
Glaze's upstream build minimum of 3.31. Boost remains for transport and exact
wide sums; no Glaze networking, CSV or runtime services are introduced.

## Library reuse

Beast performs WebSocket upgrade, framing, control-frame handling and close;
Asio performs DNS, TCP, TLS I/O, signal delivery and timer scheduling. The
connection code composes these operations and retains application-specific
cancellation and first-error policy. Beast's
[WebSocket timeouts](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/using_websocket/timeouts.html)
cover upgrade and close, but do not span DNS, TCP, TLS and subscription writes.
One application timer bounds that entire setup sequence and is reused for
shutdown. Replacing only its close phase would add a second timeout mechanism.

UTC output uses standard chrono formatting through `std::format`. Incoming
timestamps retain a small strict parser for the accepted UTC grammar,
nanosecond precision and calendar checks. Prices retain direct checked
decimal-to-tick conversion because floating-point conversion would compromise
exact representability checks. Wide sums already use Boost.Multiprecision.

CSV escaping, a one-option CLI and lifecycle logging remain small local helpers.
A CSV, CLI or logging dependency would need to remove substantial machinery or
provide a required feature to justify its build and configuration cost. Exact
exchange-time expiration and retained-ID filtering remain application logic;
the median partitions use standard containers rather than a custom tree.

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

A single-producer/single-consumer (SPSC) queue and a dedicated writer thread could
isolate disk I/O from feed processing. Omitting them keeps ownership and error
propagation on one thread, but slow output delays reads, signals and timers.
Adding them would require a bounded-queue saturation policy, cross-thread error
reporting and shutdown draining; batching reduces I/O frequency without providing
that isolation.

Immediate write and threshold-flush failures return `Result<void>`. Timer flush
failures occur after `write_statistics()` has returned, so they call
`ExecutionContext::fail()` and application policy initiates shutdown. The sink
has no feed dependency. Batching delays visibility and error detection; a failed
flush can leave fewer rows than the emitted counter reports. Flush does not
ensure disk durability, and crashes or SIGKILL can lose pending rows.

## Numeric model

Prices use signed 64-bit integer ticks at a fixed scale of eight decimal places:
`1 tick = 0.00000001`. The supported range is `0..92233720368.54775807`.
Decimal and scientific-notation input is parsed directly into ticks without
floating-point conversion. Values outside that range or off that grid fail;
extra trailing zeros are accepted, but input prices are never silently rounded.

The window maintains an exact 128-bit unsigned sum using Boost's fixed-width,
allocation-free multiprecision integer, already available through the Boost
headers. A maximum nonnegative 64-bit price multiplied by any supported sample
count (at most 64 bits) fits in 127 bits, so accepted prices cannot overflow the
sum. Expiration subtracts exact ticks, without compensation or rounding history.
Mean is retained as sum/count; even-count median as the two middle prices/2.
These fractions reach the sink without premature division or rounding.

CSV formatting rounds mean and median to the nearest tick, with ties going to
even. Their absolute error relative to the exact statistic is at most half a
tick (`0.000000005`), independent of window size and stream history. Low, high
and observed prices are exact. The bound applies to statistics of received ticker
updates, not to omitted trades or statistical sampling error.

Locale-independent output uses ordinary decimal notation with up to eight
fractional digits, omitting insignificant trailing zeros. The independent Decimal
verifier checks equality with the specified rounded result instead of relative
or absolute floating-point tolerances.

A common fixed scale avoids fetching product metadata or adding per-symbol scale
configuration. Coinbase documents each product's price grid through
[`quote_increment`](https://docs.cdp.coinbase.com/api-reference/exchange-api/rest-api/products/get-single-product),
which is distinct from the quantity's `base_increment`. A public metadata check
on 2026-10-07 found `0.01` for BTC-USD, ETH-USD and SOL-USD; all 839 listed
products' quote increments were representable at our eight-decimal scale. This
supports the current choice, but is not a permanent exchange-wide precision
guarantee. Additional trailing zeros in price text do not affect representability.

The tradeoff is an explicit range and precision restriction:
products needing finer prices are unsupported and fail validation. Supporting
those products would require a different scale or numeric representation. Exact
fractions and 128-bit division add some type and formatting work, while integer
price storage replaces platform-dependent floating point. No performance claim
is made without measurement. Every accepted observation is still retained until
expiration; this change does not bound memory use or approximate the window.

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

The [test report](../logs/test-results.log) records Release and UBSan runs.
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
