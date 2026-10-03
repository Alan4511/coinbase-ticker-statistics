# Coinbase Ticker Statistics Blueprint

## Assignment

Subscribe to public Coinbase ticker data, support concurrent symbols in one
process, and calculate mean, median, low and high over a five-minute sliding
window. Longer windows must be possible. Write time, trade ID, price and
statistics to CSV and supply calculation tests with execution logs.

Balance readability, simplicity, memory and speed. Bounded-error approximation
is permitted by the assignment; this implementation retains every accepted observation
until expiration, without a sample-count cap. Reconnection/backoff, sequence
recovery, heartbeats and stale-feed watchdogs are explicitly outside scope.
Final zip/private-repository submission remains a separate action.

## Ownership and events

```text
public ticker connections (configured symbol groups)
                     |
              nlohmann decoder
                     |
              Trade
                     |
          per-symbol SlidingWindow
                     |
              StatisticsUpdate
                     |
         one concept-constrained sink
                 CSV writer
```

The private Application class owns connections, windows, output file and sink on
one Asio event-loop thread. Named methods coordinate message processing and shutdown.
The CSV writer borrows the file stream; header writing returns an explicit result.
Local connection setup precedes file truncation. Feed objects own their resolver,
TLS/WebSocket stream and buffers.
Callbacks borrow data only during invocation. No callback escapes to a worker.
A connection failure stops all connections; SIGINT/SIGTERM request bounded graceful shutdown.

The statistics module has no sockets, JSON or files. The processor's process_trade method is templated
on an OutputSink-conforming type and receives the sink by reference per call.
The application owns the sink and handles final flushing; the concept only requires
write_statistics(update) returning Result<void>. Tests substitute recording/counting sinks without inheritance.
There are no virtual application interfaces.

Root headers types.hpp and result.hpp contain shared types and errors. Incoming field parsing
belongs to feed/parser, transport lifecycle/options to feed/transport, outgoing
field formatting to output, and window options to
statistics. Subscription validation and serialization are separate from ticker
decoding, so configuration validation does not construct network messages.

Result<T> aliases std::expected<T, Error>. A stable ErrorCode distinguishes
validation, protocol, transport and output errors. Fallible initialization
uses factories. Filtering is successful nullopt/false, not an error. Exceptions
are retained only at unavoidable library/emergency boundaries.

## Window and numbers

Each symbol advances independently in nondecreasing exchange time. Default
membership is (t - 300 seconds, t], including the current event. Expiration
and publication are event-triggered; idle windows do not emit synthetic records.
Retained duplicates are ignored. Decreasing timestamps fail before duplicate
checking, and the lower boundary is always open. Malformed messages are fatal;
unrelated valid message types are ignored. These are fixed assignment policies.

A deque retains event order, two balanced multisets retain ordered prices, and
an ID index tracks retained duplicates. Updates cost O((K+1) log N) for K
expirations; snapshots are O(1); memory is O(N) per symbol and depends on arrival
rate and window duration. There is no fixed memory budget. Allocation failure is fatal.

Price/Statistic aliases use long double. A compensated running sum reduces
cancellation, and resets after complete expiration. Arithmetic is not exact
decimal arithmetic. CSV preserves max_digits10 significant digits; production
would ideally use fixed-point decimal prices and checked wider sums.
See [README](README.md#numeric-model-and-tradeoffs) for limitations.

CSV always uses exchange time. Append mode and receipt-time windows have been
removed by agreement. CSV uses a comma and there is no event-count stopping
condition. Endpoints stay configurable; custom CA-file settings are removed and
TLS uses the default trust store. Output is synchronous, with configurable per-row or row-count/timed batch flushing.
A single Asio timer publishes partial batches during quiet periods; shutdown also flushes.
Flushing adds I/O cost and does not guarantee disk durability. Existing destination files are replaced on each run.

## Configuration and verification

[CONFIGURATION.md](docs/CONFIGURATION.md) describes the supported settings.
Connection groups and the output path are required; operational settings retain
documented defaults. Explicit named handlers and individual field reads favor
readability over generic dispatch and shorter source files.
Validation focuses on known types/ranges and invariants; comprehensive schema
and deployment validation are intentionally omitted for the exercise.

Tests cover fractional prices, timestamp syntax, window boundaries, long gaps,
one-hour windows, duplicates/late events, cancellation, numeric overflow,
substitute sink implementations, JSON-to-CSV fixtures, and real loopback TLS/WebSocket operations.
An independent Decimal/sorting verifier checks captured CSV within documented
tolerances. Linux GCC/Clang jobs are defined in .gitlab-ci.yml; a configured
GitLab runner must execute them before Linux success can be claimed.
The submission retains one current report at `docs/test-results.log`; generated
CSVs and CI logs use the ignored `build/` directory.

[Coinbase overview](https://docs.cdp.coinbase.com/exchange/websocket-feed/overview)
confirms the unauthenticated endpoint. The
[ticker channel](https://docs.cdp.coinbase.com/exchange/websocket-feed/channels#ticker-channel)
uses type, product_id, time, trade_id and price. Updates can batch matches:
statistics concern accepted ticker observations rather than a complete trade tape.
