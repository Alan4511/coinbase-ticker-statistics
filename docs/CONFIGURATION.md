# Configuration

Pass JSON with `--config PATH`. [example.json](../config/example.json) shows all
current options. `connections` (including each `symbols` array) and `output.path`
are required. Other settings use the defaults listed below. Known fields have type/range
validation; unknown keys are ignored and duplicate JSON keys use their last value.
Full schema validation is deliberately outside this take-home implementation.

## Minimal configuration

```json
{
  "connections": [{"symbols": ["BTC-USD", "ETH-USD", "SOL-USD"]}],
  "output": {"path": "../build/ticker_statistics.csv"}
}
```

Omitting a required field is an error. An empty string or empty connection group
cannot substitute for it.

## Connections

```json
{
  "connections": [
    {"symbols": ["BTC-USD"]},
    {"symbols": ["ETH-USD", "SOL-USD"]}
  ]
}
```

Each entry creates a separate public ticker WebSocket using the shared `feed`
settings. Groups must be nonempty; product IDs must be unique across all groups.
There is no implicit symbol group: subscriptions must be supplied explicitly.
All connections share one event loop and one set of independently owned symbol
windows. A connection failure stops the complete run instead of silently
continuing with an incomplete symbol set.

The channel is always `ticker`; no authentication configuration exists.
The former root-level `symbols` setting has been replaced by these groups.

## Feed

| Field | Default | Meaning |
| --- | --- | --- |
| `host` | `ws-feed.exchange.coinbase.com` | Host without scheme or port. |
| `port` | `"443"` | Service/port string used by the resolver. |
| `target` | `"/"` | WebSocket HTTP target, beginning with /. |
| `connect_timeout_seconds` | `15` | DNS through subscription deadline. |
| `close_timeout_seconds` | `5` | Graceful close deadline. |
| `max_message_bytes` | `1048576` | Positive maximum incoming message size. |

TLS certificate and hostname verification are mandatory. Trust comes from OpenSSL’s
default trust store; there is no application setting for a custom CA file. A CA
(Certificate Authority) is a trusted certificate issuer. Host, port and target
remain configurable; a custom endpoint must present a certificate trusted by
that default store. Host/port syntax is delegated to the networking library;
there is no exhaustive DNS grammar validator.

## Window

| Field | Default | Meaning |
| --- | --- | --- |
| `duration_seconds` | `300` | Positive integer; use 3600 for one hour. |

Window time always comes from Coinbase. Membership is always `(t-duration,t]`;
the lower boundary is open. Equal timestamps are valid; decreasing timestamps
fail explicitly before duplicate checking. Retained duplicate trade IDs are
always ignored, considering the candidate window after prospective expiration.
Ignored duplicates do not mutate state, advance time, or emit rows. IDs are
forgotten after expiration. All accepted samples are retained until expiration;
there is no sample-count cap. Memory depends on arrival rate and window duration.
Idle windows expire on the next event. The removed `max_observations_per_symbol`
key has no effect if left in an older configuration, like other unknown keys.

Prices/statistics use long double and full round-trip CSV precision. There are
no fixed-point scale, decimal-place, or rounding-mode settings; see the
[README numeric tradeoff](../README.md#numeric-model-and-tradeoffs).
Mean weights each accepted ticker equally, not by volume or elapsed time.

## Process lifetime and malformed messages

Malformed messages, Coinbase error messages and processing/output failures stop
the run. Unrelated valid message types are ignored. This fail-fast behavior
makes data problems visible instead of silently producing incomplete statistics.

The process runs until SIGINT or SIGTERM requests bounded graceful shutdown. There is no configured run duration or event-count limit.

## Output

One `output` object configures the CSV writer, for example
`"output": {"path": "../build/ticker_statistics.csv"}`. Alternative sink types are
selected in code through the sink concept; there is no runtime sink registry.

| Field | Default | Meaning |
| --- | --- | --- |
| `path` | Required; no default | Nonempty path, relative to configuration directory or absolute. |
| `flush_every_rows` | `1` | Positive integer. 1 publishes each row; larger values batch rows across all symbols. |
| `flush_interval_ms` | `1000` | Integer in 1..86400000; periodic flush interval when batching. |

Parent directories are created automatically. Every run writes a fresh file,
replacing existing content. Append mode is not supported. Use different paths to retain separate runs.

Window/routing validation and local connection configuration complete before the
destination is opened. These startup failures preserve an existing file; network
failures after opening can still leave a fresh partial output.

CSV columns are `time,symbol,trade_id,trade_price,count,mean,median,low,high`.
Time is UTC exchange time with nine fractional digits. Floating-point numbers
use max_digits10 significant digits, possibly scientific notation, independently
of the stream locale. The delimiter is always a comma; fields with commas,
quotes or newlines are escaped.

The sink receives borrowed events synchronously. With `flush_every_rows: 1`,
each row is flushed for immediate visibility. For batches, set a larger threshold:

```json
"output": {
  "path": "../build/ticker_statistics.csv",
  "flush_every_rows": 128,
  "flush_interval_ms": 1000
}
```

Rows are written as observations arrive; flushing occurs at the row threshold
or the periodic timer, whichever comes first. Each successful flush resets the
row counter. The timer runs on the existing event loop and flushes even when
no new ticker arrives. The stream can also publish earlier when its buffer fills.
The interval is a scheduling target, not a hard latency bound: synchronous I/O
or busy callbacks can delay it. It is ignored in per-row mode.

Batching reduces flush overhead but delays visibility and detection of buffered
I/O failures. Write/flush failures stop the application. Shutdown flushes any
remaining partial batch. Flush does not guarantee durability on disk.
