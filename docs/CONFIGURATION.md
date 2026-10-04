# Configuration

Pass JSON with `--config PATH`. [example.json](../config/example.json) shows all
current options. Root `symbols` and `output.path` are required. Other settings
use the defaults below. Known fields have strict type/range validation; unknown
keys are ignored and duplicate JSON keys use their last value.

`parse_config()` and `load_config()` return validated settings. `run_application()`
requires that validated configuration; direct C++ construction or changes require
`validate_config()` before running.

## Minimal configuration

```json
{
  "symbols": ["BTC-USD", "ETH-USD", "SOL-USD"],
  "output": {"path": "../build/ticker_statistics.csv"}
}
```

## Symbols

`symbols` must be a nonempty array of distinct product IDs such as `BTC-USD`.
All products use one public ticker WebSocket; each has an independent window.
There is no implicit subscription, authentication setting or connection grouping.

Older configurations must replace `connections` with the root `symbols` array.

## Feed

| Field | Default | Meaning |
| --- | --- | --- |
| `host` | `ws-feed.exchange.coinbase.com` | Host without scheme or port. |
| `port` | `"443"` | Service/port string used by the resolver. |
| `target` | `"/"` | WebSocket HTTP target, beginning with /. |
| `connect_timeout_seconds` | `15` | Integer in 1..31536000; deadline from DNS through subscription. |
| `close_timeout_seconds` | `5` | Integer in 1..31536000; maximum WebSocket close time. |
| `max_message_bytes` | `1048576` | Positive integer maximum incoming message size. |

TLS certificate and hostname verification are mandatory, using OpenSSL's default
trust store. Deployment-specific trust configuration belongs to the environment.
Host/port syntax is delegated to the networking library. DNS, connection setup
and reads are asynchronous on one event-loop thread. Connection and close
deadlines are enforced; there is no idle-feed timeout.

## Window

| Field | Default | Meaning |
| --- | --- | --- |
| `duration_seconds` | `300` | Integer in 1..31536000; use 3600 for one hour. |

Window membership is `(t-duration,t]`, using each symbol's exchange time. Equal
timestamps are valid; decreasing timestamps fail before duplicate checking.
Retained duplicate trade IDs are ignored, considering prospective expiration;
ignored duplicates do not mutate state, advance time or emit rows. IDs are
forgotten after expiration. Idle windows expire on the next accepted event.

All accepted samples remain until expiration. There is no sample-count cap;
memory depends on arrival rate and window duration. The old
`max_observations_per_symbol` key has no effect. Mean weights each ticker equally,
not by volume or elapsed time. Prices/statistics use `long double`, with no
scale or rounding settings; see the [numeric tradeoff](DESIGN_DECISIONS.md#numeric-model-and-tradeoffs).

## Output

`output.path` is a required nonempty path, absolute or relative to the configuration
file's directory. Parent directories are created automatically. Every run replaces
existing content; use separate paths to retain previous runs. Window validation
and connection/subscription complete before the destination is opened.

| Field | Default | Meaning |
| --- | --- | --- |
| `flush_every_rows` | `100` | Positive integer; flush after this many rows across all symbols. Set `1` for immediate flushing. |
| `flush_interval_ms` | `250` | Integer in 1..31536000000; flush a partial batch after this many milliseconds from its first row. |

The header is flushed immediately. Rows flush when the count or interval is
reached, whichever comes first; additional rows do not postpone the deadline.
The timer runs even when the feed is idle, and shutdown flushes any remaining
rows. The stream may publish data earlier when its internal buffer fills.

Flushing still uses synchronous I/O on the event loop. Slow processing/writes can
delay the timer, so its interval is a scheduling bound rather than a hard
real-time guarantee. Batching reduces explicit flush frequency but delays
visibility and potentially detection of output errors. A timed flush failure
stops the feed and yields a failure exit status. Flush does not guarantee disk
durability; abrupt termination can lose buffered rows.

Production uses `ApplicationFeedHandler<CsvSink>`. Another destination supplies
`write_statistics()` and changes application construction/lifecycle wiring;
statistics and the generic event handler stay unchanged. `OutputSink` does not
provide a runtime sink registry or configurable sink factory.

Columns are `time,symbol,trade_id,trade_price,count,mean,median,low,high`.
Time is UTC exchange time with nine fractional digits. Numbers use `max_digits10`
significant digits, possibly scientific notation, independent of the stream locale.
The delimiter is a comma; quotes, commas and line endings are escaped.

## Process lifetime and malformed messages

Malformed data, Coinbase error messages and processing/output failures stop the
run. Unrelated valid message types are ignored. A normal peer close ends the run
successfully; unexpected transport failures return an error. There is no reconnect.

Ctrl-C/SIGTERM stops processing and attempts a normal WebSocket close. If the peer
responds, the application flushes CSV, logs the reason and final counts, and exits
successfully. If closing fails or exceeds `close_timeout_seconds`, the transport
is closed forcibly, CSV is still flushed and an error is reported with a failure
exit status. During connection setup, the signal cancels pending operations
without opening the output file. No unsubscribe is needed when closing a connection.

Signals and deadlines run on the same event loop as processing, so synchronous
CSV I/O can delay them; the deadlines are scheduling bounds, not hard real-time
guarantees. SIGKILL and process crashes bypass cleanup. There is no configured
run duration or event-count limit.

Startup, shutdown and error messages share stderr. Redirect it with
`2> application.log` to capture both INFO and ERROR records in one file. The final
summary includes received-message, decoded-ticker and emitted-row counts; there
are no periodic or per-ticker diagnostic messages. CSV output uses its own file.
Emitted-row counts include successful writes into the buffer; they do not prove
that every row reached the file when a later flush fails.
