# Coinbase Ticker Statistics

A C++23 application that consumes Coinbase's public ticker feed and writes
mean, median, low and high to CSV for each subscribed symbol. Each symbol has
an independent sliding window, defaulting to five minutes with a configurable
limit of 100,000 retained observations per symbol. Prices use exact
integer ticks; mean and median round to eight decimal places in CSV. No API key
is required.

- **Simplicity:** one asynchronous event-loop thread; independently testable parsing/statistics and an output interface supporting alternative sinks.
- **Speed:** typed JSON decoding without a DOM, O(log N) work per inserted/expired sample, O(1) snapshots and batched CSV flushing.
- **Memory:** O(N) retained storage per symbol; expired samples leave all indexes, and a configurable cap fails explicitly.

See [design tradeoffs](docs/DESIGN_DECISIONS.md) for allocation costs and window-capacity sizing.

## Build

Requires a C++23 toolchain, CMake 3.20+ and OpenSSL 3 development files.
See [build setup](docs/BUILDING.md) for compiler and platform requirements.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Run

```sh
./build/bin/coinbase_ticker_statistics_app --config config/example.json
```

The example subscribes to BTC-USD, ETH-USD and SOL-USD, writing one combined file
at `build/ticker_statistics.csv`. Configure the symbols, output path and window
length in the JSON file; see the [configuration reference](docs/CONFIGURATION.md).

Each accepted ticker update produces a statistics row. CSV output flushes every
100 rows or after 250 ms by default; both are configurable, with immediate flushing
available. Each run replaces the destination file. INFO and ERROR diagnostics go
to stderr. Ctrl-C/SIGTERM requests graceful shutdown and flushes remaining rows.

```csv
time,symbol,trade_id,trade_price,count,mean,median,low,high
```

## Test

```sh
ctest --test-dir build --output-on-failure
```

Tests cover window calculations, parsing, CSV output and local TLS/WebSocket
integration, including shutdown. They require loopback access but no external feed.
See [verification details](docs/DESIGN_DECISIONS.md#verification-approach) for fixtures,
independent CSV checks and the scope of the recorded results.

## Documentation

- [Configuration](docs/CONFIGURATION.md)
- [Design decisions and tradeoffs](docs/DESIGN_DECISIONS.md)
- [Test results](logs/test-results.log)
- [Live application log](logs/live-run.log)
- [Live CSV verification](logs/live-verification.log)
