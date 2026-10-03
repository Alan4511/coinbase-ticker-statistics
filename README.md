# Coinbase Ticker Statistics

C++23 take-home application consuming Coinbase's public **ticker** channel,
maintaining an independent sliding window per symbol, and writing mean, median,
low and high to CSV. The default window is five minutes; longer windows are
configurable.

[BLUEPRINT.md](BLUEPRINT.md) records the assignment and architecture.
[Configuration reference](docs/CONFIGURATION.md) lists the operating options.

## Build and run

Requires CMake 3.20+, a C++23 standard library supporting `std::expected`
(including monadic operations) and `std::ranges::to`, and OpenSSL 3 development files. Use GCC 14+,
Clang 19+ with a compatible standard library, or AppleClang 16+.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/bin/coinbase_ticker_statistics_app --config config/example.json
```

On Debian install `libssl-dev`; on macOS install `openssl@3` and pass
`-DCMAKE_PREFIX_PATH="$(brew --prefix openssl@3)"` if necessary.
CMake uses installed Boost 1.83+ or fetches pinned Boost 1.88.0 headers,
nlohmann-json 3.12.0 and GoogleTest 1.18.0. First configuration needs network access.

The example groups BTC-USD on one connection and ETH-USD/SOL-USD on another.
Both run on the same event-loop thread. Change `connections[].symbols` to
partition subscriptions differently; a symbol must belong to exactly one group.
No API key, signature, or authenticated channel is used.
[Coinbase's overview](https://docs.cdp.coinbase.com/exchange/websocket-feed/overview)
identifies `wss://ws-feed.exchange.coinbase.com` as the unauthenticated endpoint.
The [ticker documentation](https://docs.cdp.coinbase.com/exchange/websocket-feed/channels#ticker-channel)
matches our subscription and decoded fields. Ticker updates can batch cascading
matches, so these statistics describe received observations, not a complete trade tape.

## Layout

- `src/types.hpp`, `src/result.hpp`: shared vocabulary and expected-based errors.
- `src/statistics/`: window options, state and calculations, independent of I/O.
- `src/feed/parser/`: JSON ticker decoding and incoming field validation, independent of sockets.
- `src/feed/transport/`: Beast TLS/WebSocket sessions and connection options.
- `src/feed/subscription.hpp/.cpp`: Coinbase subscription validation and encoding.
- `src/output/`: sink concept, field formatting, and CSV writer.
- `src/config/`: JSON configuration loading and validation; JSON keys remain private to the implementation.
- `src/app/`: templated ticker routing, connection ownership and lifecycle; concrete wiring aliases stay in `application.cpp`. `StatisticsProcessor` belongs here because it connects windows to the output sink.
- `tests/`: mirrors the source modules, with shared assertion/file helpers at the root and pipeline coverage under `app/`.
- `data/`: deterministic fixtures and public test certificates.
- `docs/test-results.log`: the current test report retained for the assignment.
- `build/`: ignored build products, generated CSVs and CI logs.

Each module owns its CMake target; tests share one executable. Feed parser and
transport directories share one target: the split clarifies responsibilities
without introducing additional libraries. Parsing and formatting
stay at their I/O boundaries. The application validates windows and configures
connections before truncating output, so local setup failures preserve previous
files. Network failures after startup can leave a partial file.

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

- **nlohmann-json:** readable DOM validation costs allocations and parses unused
  fields. Appropriate for the exercise; a measured production bottleneck could
  justify SAX/SIMD parsing. No parser benchmark is claimed.
- **Beast/Asio:** explicit async operations control TLS and session lifetimes,
  at the cost of connection-state code. Certificate/hostname verification stays on.
- **Single event loop:** preserves per-connection order and gives windows/sinks
  one owner without locks. Separate connections do not provide parallel CPU
  processing. Synchronous CSV I/O can delay all connections.
- **Logging:** a small helper writes UTC timestamps, `INFO`/`ERROR` severity and
  connection context to stderr. Connection numbers follow configuration order,
  starting at 1. Logs cover lifecycle events and failures; tickers are not logged.
  Text output keeps the implementation small but has no structured schema or
  runtime level filtering. Logging is synchronous and best effort; redirection,
  collection and rotation belong to the environment.
- **Window storage:** deque expiration, two ordered multisets, retained-ID index,
  and compensated sum. An event expiring K samples costs O((K+1) log N);
  snapshots cost O(1); memory is O(N) per symbol. Tree allocations trade speed
  and cache locality for readable invariants. Preflight validation scans expired
  samples before removing them, adding a second traversal so arithmetic failures
  leave the window unchanged.
- **Explicit errors:** recoverable operations return `Result<T>`, an alias for
  `std::expected<T, Error>`. Factories prevent exposing invalid objects;
  successful empty optionals mean filtered events. Narrow catches remain for
  library APIs that throw, plus emergency event-loop/process boundaries.
- **Sink abstraction:** `OutputSink` is a concept; `StatisticsProcessor::process_trade`
  borrows the sink per call. Tests use recording/counting sinks without inheritance.
  Templates require definitions in headers and can increase compilation work and
  generated code per type; they remain limited to small, reusable operations.
- **Output:** CSV flushes each row by default. Configurable row-count and timed
  batching reduces flush overhead while publishing continuously, including during
  quiet feeds. Both use the existing event loop; synchronous writes/flushes can
  still delay feed processing. Batching delays visibility and detection of buffered
  I/O failures. Shutdown flushes the final partial batch. Flush does not guarantee
  disk durability; each run truncates its output file.
- **Configuration:** private typed helpers use nlohmann conversion after strict
  type/range checks. Unknown keys are ignored and duplicate keys use the last value.
  Section readers copy small JSON objects to simplify ownership during startup.
  Full schema policing and deployment-specific validation are outside scope;
  misspelled or removed options can therefore be silently ignored.
- **Readability and reuse:** named connection handlers and individual configuration
  field reads keep execution order and first-error reporting visible, at the cost
  of some repeated checks. Private named JSON-key constants remain. One local
  template handles CSV write/flush errors; short `std::expected` chains handle
  dependent operations. Sharing these mechanics adds a helper call to follow,
  while keeping validation and lifecycle decisions beside their callers.
- **Test fixtures:** one RAII directory helper shares setup and cleanup across file
  tests. Setup/file-operation exceptions abort the affected test; destructor cleanup
  is best effort, so a cleanup failure can leave a temporary directory behind.
- **C++23 vocabulary:** `Trade`, `Statistics` and `StatisticsUpdate` name the input,
  calculated values and sink payload. Functions describe actions (`read_trade_id`,
  `process_trade`, `write_statistics`); optional results remain explicit in signatures.
  Structured bindings unpack insertion results, and local ranges/views express symbol
  flattening, character validation and expiration. Views borrow existing data only
  during iteration; configuration symbols are materialized before returning. Ordinary
  loops remain where validation or state changes are easier to follow.
- **Required configuration:** connection groups and `output.path` have no fallbacks.
  Missing values fail before output is opened. Endpoint, window, resource and flush
  settings retain documented operational defaults. See the
  [minimal configuration](docs/CONFIGURATION.md#minimal-configuration).
- **Connection lifecycle:** public access removes authentication, while DNS/TCP,
  verified TLS, WebSocket upgrade and subscription remain necessary. The subscription
  is sent immediately after upgrade ([Coinbase protocol](https://docs.cdp.coinbase.com/exchange/websocket-feed/overview)).
  Asynchronous stages, deadlines and cancellation keep connection groups responsive
  during startup and support bounded shutdown. These costs remain deliberate.
- **Ownership:** the application uniquely owns feed wrappers; pending handlers
  share session ownership until completion. Message views are borrowed during
  delivery. `std::function` keeps transport details out of callers at a type-erasure
  cost. Feeds and CSV writers cannot be copied or moved; the factory's explicit
  `new` immediately enters a `unique_ptr` because its constructor is private.
  Core Guidelines review focuses on RAII, capture lifetimes and checked conversions.
- **Numeric conversion and CSV storage:** `to_chars` appends prices, statistics,
  trade IDs and counts to a reusable row buffer through one constrained helper.
  A complete row is validated before writing. `from_chars` parses prices directly
  as `long double` where supported; a compile-time check retains the classic-locale
  stream fallback on libraries such as the local Apple libc++ that lack that overload.
  Supporting both paths adds maintenance and cross-platform verification work.
  No conversion narrows through `double`. Timestamp formatting still uses a stream
  for straightforward calendar formatting. The row buffer retains its largest capacity;
  fewer temporary objects are a design improvement, not a measured speedup.
  Formatting failures emit no row, but a stream write failure can leave partial bytes.
  Benchmarking is outside the assignment scope.

For batched visibility, the example uses:

```json
"output": {
  "path": "../build/ticker_statistics.csv",
  "flush_every_rows": 128,
  "flush_interval_ms": 1000
}
```

A batch flushes after 128 rows across all symbols or the periodic one-second
flush. Set `flush_every_rows` to `1` for per-row visibility. The interval is a
scheduling target, not a hard latency bound. See the
[configuration reference](docs/CONFIGURATION.md#output) for validation and failure behavior.

Fixed policies: comma-only CSV, fresh files, exchange-time windows with an open
lower boundary, ignored retained duplicates, and fatal malformed data/decreasing
timestamps. Ctrl-C/SIGTERM requests bounded shutdown; there is no run-duration limit.

The endpoint remains configurable. TLS verifies certificates and hostnames using
OpenSSL's default trust store; deployment-specific trust belongs in the environment.

Reconnect/backoff, sequence-gap recovery, heartbeat monitoring, stale-feed
watchdogs, runtime reload, output queues and crash durability remain outside scope.
We do not claim measured high-frequency trading performance.

## Verification

Current checks: **113/113 tests passed** on macOS; see the retained
[test report](docs/test-results.log). Coverage includes 16,000 randomized window
updates, deterministic JSON-to-CSV fixtures, numeric round trips, error propagation,
batched output, logging and bounded local TLS/WebSocket tests. The independent
Decimal verifier passes the fixture, including expiration for both symbols.
Local tests need loopback-port access but no external feed or credentials;
`data/test_tls_*.pem` contains public test fixtures only.

Local price parsing exercises the Apple stream fallback. Native floating-point
`from_chars` and Linux-specific checks still need verification in Linux CI.
Only the current test report is retained: generated output stays under ignored
`build/`, keeping intermediate runs and historical reports out of the submission.
Refresh the report with:

```sh
ctest --test-dir build --output-on-failure --output-log docs/test-results.log
```

`.gitlab-ci.yml` builds and tests on Debian Linux using GCC and Clang, archives
JUnit/test logs and runs the independent fixture verifier. It requires a
Docker/Kubernetes GitLab runner. Adding this pipeline does not mean it has run;
Linux results must be confirmed in GitLab.

```sh
python3 tools/verify_csv.py --config data/fixture_config.json --require-expiration
./build/bin/coinbase_ticker_statistics_app --config config/live_verification.json
python3 tools/verify_csv.py --config config/live_verification.json --require-expiration
```

Stop a live run with Ctrl-C/SIGTERM, which requests bounded shutdown. Paths are relative to the configuration file. Output columns are:

```csv
time,symbol,trade_id,trade_price,count,mean,median,low,high
```

The default window is `(t - 300 seconds, t]`, using each symbol's exchange time.
Expiration and output happen on accepted events; idle symbols emit no synthetic rows.
UTC timestamps retain nanosecond formatting.

For sanitizer builds use a separate Debug directory with
`-DCOINBASE_TICKER_STATISTICS_SANITIZERS=ON`. The default set is
`address,undefined`; select UBSan alone with
`-DCOINBASE_TICKER_STATISTICS_SANITIZER_SET=undefined`.
