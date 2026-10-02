# Coinbase Ticker Statistics

Project blueprint for the Coinbase ticker statistics assignment. Implementation and dependency
choices will be developed after discussing the design.

## Requirements

- Subscribe to Coinbase Exchange's public `ticker` WebSocket channel.
- Support multiple symbols concurrently in one process, such as BTC-USD,
  ETH-USD, and SOL-USD.
- Parse the trade price from ticker JSON messages.
- Calculate mean, median, high, and low over a sliding window per symbol.
- Default to a five-minute window and support longer durations such as one hour.
- Write timestamp, symbol, trade ID, trade price, and calculated statistics to CSV.
- Include calculation tests and logs from tests and a live-feed run for verification.
- Provide build/run instructions and deliver as a zip or private GitHub repository.

Third-party WebSocket and JSON libraries are permitted. Reconnection with
backoff, sequence-gap detection, heartbeat monitoring, and stale-feed watchdogs
are outside the assignment's scope.

## Proposed layout

```text
coinbase-ticker-statistics/
  README.md
  src/
    app/          # Configuration, orchestration, and executable entry point
    feed/         # WebSocket subscription and ticker JSON parsing
    statistics/   # Sliding-window calculations, independent of networking
    output/       # CSV formatting and writing
  tests/          # Calculation, parsing, and output tests
  data/           # Deterministic input fixtures and expected results
  logs/           # Test results and live-run evidence for submission
```

## Build

Requires CMake 3.20 or later and a C++23-capable compiler. Configure and build
from the project root:

```sh
cmake -S . -B build
cmake --build build
```

The component targets are scaffolded; the executable and test targets will be
added as their implementation and test sources are introduced. The project is
self-contained and can be packaged and built from the repository root.

## Proposed processing flow

Subscribe to the configured symbols in one connection, parse each ticker into
a trade observation, update that symbol's window, and append a CSV row with the
resulting statistics. Each symbol has independent state; supporting multiple
symbols does not necessarily require one thread per symbol.

Proposed CSV columns:

```csv
time,symbol,trade_id,trade_price,count,mean,median,low,high
```

The count column makes the number of observations contributing to each result
visible. Mean and median are based on observed prices, without volume weighting.

## Statistics design to discuss

An initial exact approach would use a time-ordered queue for expiration, a
running sum for the mean, and two balanced multisets for median and extrema.
Insertion and removal would cost O(log N) per observation, with O(N) memory per
symbol, where N is the number of observations in its current window. Readout of
the maintained statistics would take O(1).

Before choosing the implementation, agree on:

- Exchange timestamps versus local receipt time for window membership.
- The exact window boundary, for example `(current_time - duration, current_time]`.
- Handling of out-of-order observations and repeated trade IDs.
- Price representation, precision, and rounding of mean and even-count median.
- Whether results update only on ticker arrival or also during idle periods.
- Memory requirements for busy symbols and a one-hour window.
- WebSocket/JSON libraries, buffering, CSV flushing, and shutdown behavior.

Exact statistics are the starting proposal. If memory constraints require an
approximation, document and test its error bound before adopting it.

## Verification plan

- Known sequences with independently calculated mean, median, high, and low.
- Odd/even observation counts, duplicate prices, and a single observation.
- Expiration at window boundaries and gaps longer than the window.
- Independent results for interleaved symbols and configurable window durations.
- Invalid ticker fields and non-ticker messages.
- CSV values, column order, and row counts against deterministic fixtures.
- A timed public-feed capture across multiple symbols, preserving CSV output,
  run configuration, and test logs for the submission.

## Coinbase reference

Subscribe using `product_ids` and `channels: ["ticker"]`. Relevant ticker fields
are `product_id`, `time`, `trade_id`, and `price`; the price is a JSON string.
The channel can batch cascading matches, so the statistics describe observations
received through the ticker feed and should not claim to include every individual
exchange trade. Coinbase's 24-hour high/low fields do not represent our window.

[Coinbase Exchange ticker channel documentation](https://docs.cdp.coinbase.com/exchange/websocket-feed/channels#ticker-channel)
