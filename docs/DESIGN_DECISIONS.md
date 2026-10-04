# Design decisions and tradeoffs

## Module boundaries and ownership

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

`run_application()` owns the event loop, signal wait, run result, CSV sink,
ticker feed and application handler in local scope. All borrowed objects remain
alive until pending operations drain. The application opens output after the
feed connects and writes its subscription, records the first fatal error,
requests shutdown, closes output on feed completion and cancels the signal wait.
A final close also covers exceptions that stop the event loop early. Validation
or connection failures preserve previous output; later failures can leave a
partial file.

The processing path is:

```text
FeedConnection → raw text → TickerFeed → TickerUpdate → StatisticsProcessor
                                                    → StatisticsUpdate → OutputSink
```

`FeedConnection` owns DNS/TCP/TLS/WebSocket operations, the initial subscription
write, reads and setup/close deadlines. It accepts a feed-supplied subscription
and delivers borrowed text through the `FeedConnectionHandler` concept. It does
not interpret JSON or decide application shutdown policy. `TickerFeed<Handler>`
owns the connection and implements its raw transport callbacks directly. It
borrows the typed consumer, encodes the ticker subscription, uses
`feed/parser/` to decode messages, owns input counters and filters unrelated
messages. Malformed input remains fatal. Its typed consumer satisfies `FeedHandler`,
requiring `on_connected()`, `on_message(const TickerUpdate&)` and
`on_stopped(Result<void>)`.
A ready event means subscription bytes were written, not that the server
acknowledged them. Each update may represent batched matches rather than an
individual trade.

`StatisticsProcessor` owns one `SlidingWindow` per symbol and returns an optional
`StatisticsUpdate`. It has no sockets, JSON, logging or output dependencies.
Filtered symbols and retained duplicates produce an empty success; calculation
failures return an error. Its window algorithms and data structures are unchanged.

`ApplicationFeedHandler<Sink>` owns the processor and emitted-row count and
borrows an `OutputSink`. It routes typed updates and returns processing/write
failures to the feed. Its connected/stopped callbacks report to application
orchestration; the handler has no CSV, logger, signal-set or final-result
reference. Lifecycle callbacks use two owned `std::function` adapters, keeping
policy in composition rather than adding another policy template. This retains
runtime dispatch for infrequent lifecycle events; per-update sink calls use
compile-time polymorphism. Nonempty callbacks and their borrowed targets must
remain valid throughout feed operations.

`OutputSink` requires only `write_statistics(update) -> Result<void>`. It does
not require opening files, flushing, timers or closing resources. Both `CsvSink`
and `CsvWriter` satisfy it, and handler tests use sinks with no CSV lifecycle.
`CsvSink` owns its configuration, file, directory creation and batch-flush timer;
`CsvWriter` borrows the stream and handles formatting/writing. The application
installs the sink's error reporter after the feed exists and before opening
output. The sink reports timed errors upward; application policy records the
failure and stops the feed. No callback captures a feed pointer awaiting later
assignment. A read-only settings accessor supports application readiness logs
without another configuration owner. Separating construction and error-reporter
installation adds one wiring step; `open()` rejects a missing reporter.

`TickerFeed<Handler>` calls its typed consumer directly, without an `InputHandler`
object or a second set of type-erased callbacks. Its thin protocol wrapper lives
in the header; JSON parsing and subscription encoding remain ordinary `.cpp`
functions. This adds one small protocol instantiation per consumer type and
exposes parser/subscription declarations to feed users in exchange for fewer
callback layers and a simpler ownership graph.

`FeedConnection` retains one private `std::function` adapter so the Beast session
stays in `.cpp`, avoiding transport instantiation and heavy Boost headers for
every consumer. Runtime indirection and possible callback allocations remain at
that boundary. Both feed and connection have stable addresses through unique
ownership; neither object is movable. Moving their owning `unique_ptr` does not
move the objects. Text views and typed update references are valid only during
delivery, and owners must remain alive until cancelled operations drain. No
virtual application interfaces or additional feed-management machinery are added.

Each source module owns its CMake target; parser and transport share the feed
target. Tests share one executable. Template handler definitions live in headers.
The application header includes the logger directly for readability, accepting
its transitive formatting and stream dependencies.

## Configuration boundary

```text
main → load_config() → parse_config() → validate_config() → run_application()
```

`config/` owns strict JSON decoding, application-wide validation and relative
path resolution. `validate_config()` composes symbol/subscription, feed, window
and CSV policy helpers from their owning modules. Module-specific rules remain
reusable without making lower-level modules depend on the JSON loader.

`run_application()` requires a validated, subsequently unmodified `Config` and
does not repeat composition validation. Successful `parse_config()` and
`load_config()` calls satisfy this precondition. Direct C++ construction or
mutation requires calling `validate_config()` before running; application tests
follow that same boundary. `Config` remains a readable aggregate rather than an
immutable validated wrapper, accepting a documented caller precondition. Module
factories retain their own invariant checks for independent use.

## Extension paths

- **Output:** `StatisticsUpdate → CsvSink`, or a future `BrokerSink`, `KafkaSink`
  or `DatabaseSink`. Instantiate `ApplicationFeedHandler<NewSink>` and wire that
  sink's construction, readiness, cleanup and error reporting in application
  composition. Statistics and event routing do not change; runtime sink
  selection is not implemented.
- **Protocol:** `FeedConnection → TickerFeed`, or a future `Level2Feed` or
  `TradesFeed`. New protocols supply subscription text, decoding and a typed
  consumer. Text WebSocket messages and one initial subscription write are
  supported; another transport or ongoing writes may require extending this
  boundary.
- **Connections:** today one `TickerFeed` owns one `FeedConnection` carrying many
  symbols. A future `FeedManager` could own several ticker feeds partitioned by
  symbol, coordinate startup/errors/shutdown, and close shared output after all
  feeds finish. No manager or connection-group configuration exists today;
  statistics and output need no connection-specific logic.
- **CPU:** one `io_context` thread owns all state, preserving observed order
  without locks. Partitioned workers would require explicit ownership and
  ordering decisions and should be considered only after profiling demonstrates
  a CPU bottleneck. They are outside the assignment scope.

## Naming and interface conventions

Quoted includes refer to headers in the same CMake target. Angle brackets refer
to other targets, shared vocabulary headers, standard-library and third-party
headers. Feed parser and transport share one target; all test files and helpers
share the test target. The executable includes the application library through
angle brackets even though both live in `app/`. This makes target boundaries
visible, at the cost of updating include delimiters if ownership changes.

Types use PascalCase; functions, variables and constants use snake_case. Event
callbacks use `on_*`: `on_connected`, `on_message`, `on_stopped` and `on_update`.
Actions retain verbs that describe their work, such as `parse_price`,
`read_next_message`, `write_statistics` and `close`. Named transport lambdas
identify completion events without adding a separate member function per callback.
This follows the distinction used in
[Beast's asynchronous client example](https://github.com/boostorg/beast/blob/develop/example/websocket/client/async-ssl/websocket_client_async_ssl.cpp).
The [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rl-name)
recommend consistency; these conventions are a project choice, not certification.

`StatisticsProcessor` remains descriptive: it owns the symbol-to-window map,
updates windows and returns statistics. `SlidingWindow` names the calculation
structure; `CsvSink` owns file lifecycle while `CsvWriter` formats stream output.
`TickerUpdate` names one decoded ticker message, which may represent cascading
matches. `StatisticsUpdate::ticker_update` retains the source message, and window
operations use `on_update()`/`add_update()` to keep that meaning consistent.
`TradeId` and `trade_id` retain Coinbase's actual identifier vocabulary; the CSV
columns remain stable. These names describe received observations without
implying a complete trade tape. Configuration headers are
named after their types (`feed_config.hpp` and `csv_config.hpp`). `WindowOptions`
remains independent of file configuration. `OutOfOrderTimestamp` identifies the
actual failing policy rather than suggesting a network delay.

These names express responsibilities; renaming alone does not improve latency or
resource safety. Ownership through RAII, explicit error results and callbacks
that remain within their targets' lifetimes are the relevant design properties.

## Input and window policies

The assignment asks for public ticker updates and per-symbol sliding-window
statistics. Each accepted ticker update contributes one equally weighted price
sample; the mean is not volume weighted. Coinbase batches cascading matches,
so these samples need not include every individual trade. Individual trades are
not reconstructed. See the [ticker channel documentation](https://docs.cdp.coinbase.com/exchange/websocket-feed/channels#ticker-channel).

Each symbol advances independently in exchange time. Membership is
`(t-duration,t]`; equal timestamps are allowed. Decreasing timestamps fail before
duplicate checking. Retained duplicate IDs are ignored against the prospective
window after expiration, without advancing time or mutating state. IDs are
forgotten after expiration. Idle symbols emit no synthetic records.

Malformed data and exchange/transport/output errors end the run; unrelated valid
message types are ignored. A normal peer close ends successfully. These strict
policies keep data problems visible without adding recovery machinery.

## Architecture alternatives

**Selected: one asynchronous WebSocket on one event loop.** Signals can stop
an idle read and connection setup; a deadline bounds the close attempt. This adds
callback state and lifetime coordination, but provides explicit cleanup and final
logging without worker threads or connection groups.

The simpler blocking loop is suitable for basic consumption, but a signal flag
alone cannot reliably wake Beast's pending TLS/WebSocket read. An idle connection
could therefore delay cleanup indefinitely. Default signal termination closes
the socket but bypasses application cleanup and the final summary. Adding a
separate interruption mechanism would introduce its own coordination and
portability costs. We choose asynchronous operations because responsive shutdown,
a bounded close attempt, final CSV flushing and final logging are useful behavior
for a continuously running application. The additional lifecycle code serves those
requirements; processing remains on one thread, with one connection and no queues.

TLS is still required: public access removes authentication, not DNS/TCP,
certificate verification, TLS negotiation, WebSocket upgrade or subscription.

| Approach | Benefit | Tradeoff |
| --- | --- | --- |
| One blocking WebSocket | A direct read/parse/update/write loop can serve all symbols. | Responsive shutdown requires a separate way to wake blocked operations. |
| One event loop with callbacks | Coordinates reads, signals and deadlines on one thread. | Explicit callback and lifetime management adds code. |
| One event loop with coroutines | Expresses the asynchronous connection sequence in a linear form. | Cancellation, deadlines and ownership still require coordination. |

[Coinbase's best practices](https://docs.cdp.coinbase.com/exchange/websocket-feed/best-practices)
recommend spreading subscriptions across connections to distribute inbound load,
especially for the full channel. Its [subscription protocol](https://docs.cdp.coinbase.com/exchange/websocket-feed/overview#subscribe)
also supports multiple products on one connection. Choosing one connection for
this take-home is a simplicity tradeoff; it does not implement that load-distribution
recommendation. Each `TickerFeed` instance can own an independent connection and
symbol subscription, but the application currently wires one instance. Multiple
symbol groups would require coordinating startup, stopping all feeds on signals
or failure, and closing the shared sink only after all feeds finish. Extracting
`FeedConnection` provides transport reuse; it does not add group configuration
or that application lifecycle coordination.

[Beast's WebSocket timeout support](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref/boost__beast__websocket__stream_base__timeout.html)
requires asynchronous operations. Coroutines retain the same asynchronous model;
see [Asio's coroutine support](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/composition/cpp20_coroutines.html).

## Numeric model and tradeoffs

`Price` and `Statistic` use **long double**, parsed directly without going through
`double`. Precision depends on the platform: Apple Silicon commonly provides
`double` precision; x86-64 Linux commonly provides extended precision.

CSV uses `max_digits10` significant digits and locale-independent conversion for round-trip
precision. Decimal scale and trailing zeros are not preserved, and scientific
notation is possible. Mean uses compensated addition/subtraction, resetting after
complete expiration; even-count median uses `std::midpoint`. Nonfinite/negative
prices, conversion overflow/underflow to zero and nonfinite sums are rejected.
Representable subnormal prices are accepted and covered by round-trip tests.

Binary floating point is a deliberate take-home simplification, not exact decimal
arithmetic. Production financial calculations should ideally use fixed-point
prices at a known scale, checked wider sums and explicit output rounding.

The assignment permits bounded-error approximation, but this implementation
retains every accepted observation until expiration. There is no sample-count
cap or approximation mode. Memory is O(N) per symbol and grows with the arrival
rate and window duration; a time window is not a fixed memory budget. Allocation
failure remains fatal. Removing capacity configuration keeps the assignment's
window semantics and failure handling simple.

Floating-point rounding still occurs; there is no universal absolute error bound
for arbitrary streams. Tests cover fractional values, cancellation and randomized
windows. The independent Decimal verifier defaults to `1e-12` relative and
`1e-15` absolute tolerance; these are verification thresholds, not mathematical
guarantees.

## Design decisions

- **Single event loop:** one thread owns the asynchronous feed, windows and sink
  without locks. Connection/subscription has a configurable deadline; reads have
  no idle timeout. There are no worker threads, output queues, reconnects,
  heartbeat handling or stale-feed watchdogs. Synchronous CSV I/O and logging can
  delay every handler, including signals and deadlines.
- **Termination and output:** Ctrl-C/SIGTERM cancels setup or starts a normal
  WebSocket close. Application completion handling closes the sink, cancelling its timer and
  flushing pending rows; the event loop drains cancelled handlers before final logging.
  A configurable close deadline forces transport closure if the peer does not
  respond; close/flush errors yield a failure status. Processing errors retain their
  original error even if closing also fails. Completing the protocol handshake
  depends on the peer. Deadlines are event-loop scheduling bounds, not hard real-time
  guarantees; SIGKILL and crashes bypass cleanup. No unsubscribe is needed when
  closing the connection. Flush does not guarantee disk durability; each run replaces its file.
- **Batch flushing:** CSV rows are buffered because synchronous per-observation
  flushes execute on the single event-loop thread and can unnecessarily stall
  feed processing. Output flushes after either a configurable row threshold or
  time interval, whichever comes first: 100 rows or 250 ms by default. This
  amortizes filesystem I/O while targeting bounded output latency, including
  when the feed becomes idle. Flushes remain synchronous, and slow callbacks can
  delay timer dispatch; the interval is not a hard real-time guarantee. Actual
  performance is not measured. `flush_every_rows=1` enables immediate output;
  the header always flushes immediately.
  A single sink-owned Asio timer starts with the first unflushed row; later rows
  do not postpone it. Batch flushing and close cancel it, and empty sinks schedule
  no timers. The stream's existing buffer holds rows without a separate queue.
  The sink depends on Asio, while the stream formatter remains independent of it.
  Timer-triggered flush failures occur after the originating `write_statistics()`
  call has returned. They are reported asynchronously to the application, which
  preserves the first fatal error and owns shutdown policy. Failures during
  writes, row-triggered flushing or close return through `Result<void>` directly.
  Batching delays visibility and error detection; crashes/SIGKILL can lose pending
  rows. A failed flush can leave fewer complete rows than the emitted-row counter
  reports, and flushing does not guarantee disk durability.
- **TLS and ownership:** certificate and hostname verification are mandatory,
  using OpenSSL's default trust store. `FeedConnection` uniquely owns the session
  that hides Boost socket/TLS types; RAII closes the transport. `TickerFeed` owns
  the connection, which borrows the feed itself for raw event delivery. The feed
  borrows its typed consumer until pending operations drain. Text views are
  borrowed only during decoding; consumers receive a
  typed ticker update borrowed for one handler call.
  The application owns the feed and its handler until the event loop drains
  cancelled operations. This keeps ownership unique but requires callers to honor
  that lifetime contract. A constrained template adapts the handler to private
  callbacks without virtual application methods. `std::function` still introduces
  runtime indirection; no latency improvement is claimed from this interface choice.
- **Window storage:** deque expiration, two ordered multisets, a retained-ID index
  and compensated sum. An update expiring K samples costs O((K+1) log N); snapshots
  cost O(1). Tree allocations trade cache locality for readable invariants.
  Preflight arithmetic validation scans expired samples before mutation, adding a
  traversal so arithmetic errors leave the window unchanged.
- **JSON and configuration:** nlohmann's DOM and small section copies favor readable
  validation over allocation efficiency. Private templated helpers perform strict
  checks before conversion; named key constants remain. `symbols` and `output.path`
  are required. Unknown keys are ignored and duplicate keys use the last value;
  full schema policing is outside scope. Removed options can be silently ignored.
  Configuration validation composes module-owned symbol, feed, window and CSV
  helpers before returning settings to the application; orchestration consumes
  validated settings without repeating that pass. The feed's documented 365-day deadline limit also
  applies to direct C++ construction, avoiding oversized timer conversions.
  Explicit field-by-field reads retain error ordering and readable diagnostics;
  a generic field table would add machinery to this small startup path.
- **Errors and reuse:** `Result<T>` aliases `std::expected<T, Error>`. Narrow catches
  convert exceptions at library boundaries; asynchronous operations report errors
  through completion handlers with operation names for diagnostics. Feed and sink
  concepts and numeric helpers use templates, adding compilation work without
  requiring virtual application interfaces. Ordinary loops keep validation and
  mutation visible.
- **Numeric conversion:** `to_chars` appends numbers to a reusable row buffer.
  `from_chars` parses directly into `long double` where available; a compile-time
  check retains a classic-locale stream fallback for Apple libc++. Both paths need
  coverage. Timestamp formatting uses a stream. The row buffer retains its largest
  capacity; formatting completes before writing, though I/O failure can leave partial
  bytes. Benchmarking is outside scope; no measured latency or throughput is claimed.
- **Logging:** a small logger borrows one stream, bound at construction. INFO and
  ERROR both go to stderr, with UTC timestamps and one escaped record per line.
  Lifecycle messages identify the symbols, window, endpoint, verified subscription
  and CSV readiness. Shutdown records the reason and cumulative received-message,
  decoded-ticker and emitted-row counts. There is no activity timer or per-ticker
  logging: the CSV already records accepted updates, and additional messages would
  add synchronous I/O. This sacrifices ongoing activity reports for simpler
  lifecycle code. Logging is synchronous and best effort, without runtime filtering;
  rotation and collection belong to the environment.
- **Tests:** isolated temporary files and local TLS servers exercise real boundaries.
  Connection tests cover raw text, opaque subscriptions, TLS, limits and lifecycle;
  ticker tests cover decoding, filtering and typed delivery. Handler tests exercise
  multiple sinks that expose only the narrow output contract. Test servers use
  child processes with deadlines; production has no background workers. Test cleanup is best effort and can leave files after an OS-level failure.

Fixed policies: comma-only CSV, exchange-time windows with an open lower boundary,
ignored retained duplicates, and fatal malformed data/decreasing timestamps.
Sequence recovery, runtime reload and crash durability are outside scope.

## Verification scope

The recorded macOS run passed **141/141 tests**, and the independent fixture
verifier passed. The current live capture contains **96 verified rows**, with
40 rows visible before shutdown and a successful graceful SIGTERM shutdown.
Linux CI results are not yet confirmed.

`data/ticker_fixture.jsonl` contains one JSON feed message per line, with
controlled prices, duplicates and timestamps. The pipeline test compares its
output against `data/ticker_expected.csv`, including window expiration.
Refresh test results and check the fixture with:

```sh
ctest --test-dir build --output-on-failure --output-log logs/test-results.log
python3 tools/verify_csv.py --config data/fixture_config.json --require-expiration
```

To refresh live evidence:

```sh
./build/bin/coinbase_ticker_statistics_app --config config/live_verification.json 2> logs/live-run.log
# Stop with Ctrl-C, then verify the CSV:
python3 tools/verify_csv.py --config config/live_verification.json > logs/live-verification.log 2>&1
```

Calculation tests include deterministic fixtures and 16,000 randomized window
updates. Boundary tests cover numeric round trips, typed feed delivery, error
propagation, immediate/batched CSV visibility, idle timed flushing and output
failures, SIGINT/SIGTERM cleanup during idle reads and startup, close/connection
deadlines and local TLS/WebSocket operation. Public certificate/key fixtures
exist only for tests.

The independent Python verifier recomputes CSV statistics with Decimal and
sorting. The retained live capture spans 30 seconds; it demonstrates feed
operation but not five-minute expiration, which deterministic tests cover.
For live expiration evidence, collect more than five minutes of data and run:

```sh
python3 tools/verify_csv.py --config config/live_verification.json --require-expiration
```

The local macOS build exercises the stream fallback for price parsing. Native
floating-point `from_chars` and Linux-specific checks remain subject to Linux CI
verification. The GitLab pipeline defines GCC/Clang jobs on Debian, requires a
Docker/Kubernetes runner and retains JUnit/test logs under `build/`. Defining a
pipeline does not establish that it has run.

The current [test report](../logs/test-results.log), [live application log](../logs/live-run.log)
and [live verification report](../logs/live-verification.log) are retained for the assignment.
Markdown documentation stays in `docs/`; `logs/` contains only these current
execution reports, keeping the evidence separate from design and setup guidance.
Application diagnostics are separate from capture metadata and verifier output;
this keeps the live log representative of what the application actually emits.
The capture harness sends SIGTERM; a completed close and flush return exit status
zero, with a shutdown summary in the application log. Close/flush failures return
nonzero status.
Generated CSVs and intermediate/CI logs stay in ignored `build/`. This preserves
execution evidence without accumulating artifacts.

See [build setup](BUILDING.md) for compiler requirements and sanitizer options.
