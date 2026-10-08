# Configuration

- Pass JSON with `--config PATH`; see [example.json](../config/example.json).
- Required: root `symbols` and `output.path`. Other settings use the defaults below.
- `parse_and_validate_config()` / `load_config()` return validated settings. Direct C++ construction/edits require `validate_config()` before running.

## Minimal configuration

```json
{
  "symbols": ["BTC-USD", "ETH-USD", "SOL-USD"],
  "output": {"path": "../build/ticker_statistics.csv"}
}
```

## Symbols

- Nonempty array of distinct product IDs; one public ticker connection, independent windows.
- IDs allow uppercase ASCII letters, digits and separated hyphens; at least one hyphen, no leading/trailing or repeated hyphens.
- No implicit subscription, authentication or connection grouping.
- Older configurations: replace `connections` with root `symbols`.

## Feed

| Field | Default | Meaning |
| --- | --- | --- |
| `host` | `ws-feed.exchange.coinbase.com` | Host without scheme or port. |
| `port` | `"443"` | Resolver service/port string. |
| `target` | `"/"` | WebSocket HTTP target, beginning with /. |
| `connect_timeout_seconds` | `15` | Integer in 1..31536000; DNS through subscription deadline. |
| `close_timeout_seconds` | `5` | Integer in 1..31536000; maximum WebSocket close time. |
| `max_message_bytes` | `1048576` | Positive integer maximum incoming message size. |

- Mandatory certificate/hostname verification using OpenSSL's default trust store; trust changes belong in the environment.
- Host/port syntax is delegated to networking libraries.
- Endpoint fields must not contain NUL characters.
- Asynchronous setup/reads on one event loop; setup/close deadlines, no idle timeout.

## Window

| Field | Default | Meaning |
| --- | --- | --- |
| `duration_seconds` | `300` | Integer in 1..31536000; 3600 for one hour. |
| `max_observations_per_symbol` | `100000` | Positive integer retained-observation limit per symbol. |

- Membership: `(t-duration,t]`, using exchange time. Equal timestamps allowed; decreasing timestamps fail before duplicate checking.
- Retained duplicate IDs are ignored after considering prospective expiration; no state/time change or row. Expired IDs may be reused; idle windows expire on the next accepted event.
- Capacity is checked after prospective expiration and duplicate filtering. Overflow returns `OutOfRange` before mutation; no samples are dropped/approximated.
- Limit bounds samples, not process memory/allocation latency; raise it for longer/busier windows.
- Mean weights ticker updates equally, not by volume/time.
- Eight-decimal price ticks: `0..92233720368.54775807`; finer nonzero digits rejected.
- Exact mean/median round only in CSV, nearest ties-to-even. Fixed scale/error bound: [numeric model](DESIGN_DECISIONS.md#numeric-model).

## Output

| Field | Default | Meaning |
| --- | --- | --- |
| `path` | Required | Nonempty path; relative to the configuration file's directory. |
| `flush_every_rows` | `100` | Positive integer across all symbols; `1` flushes each row. |
| `flush_interval_ms` | `250` | Integer in 1..31536000000; partial-batch deadline from its first row. |

- Parent directories are created; every run replaces existing content.
- NUL paths are rejected. Output/input-file aliases are not checked; use a CSV destination distinct from the configuration file, including symlinks/hard links.
- Validation and subscription writing precede opening; header flushes immediately.
- Flush at count/interval, whichever comes first; later rows do not postpone the timer. Timer works while idle; shutdown flushes the remainder. Stream buffering may publish earlier.
- Writes/flushes remain synchronous: slow I/O delays timers. Interval is a scheduling bound, not hard real-time; batching delays visibility/error detection.
- Timer-flush failure stops the feed with failure exit status. Flush does not guarantee durability; abrupt termination can lose rows.
- Alternative sinks implement `write_statistics()` and require application configuration/lifecycle wiring; no runtime registry/factory.

```csv
time,symbol,trade_id,trade_price,count,mean,median,low,high
```

- UTC exchange timestamps: nine fractional digits.
- Locale-independent decimals: up to eight fractional digits; insignificant zeros omitted.
- Comma delimiter; quotes, commas and line endings escaped.

## JSON policies

- Unknown keys ignored but syntactically validated; known fields have strict types/ranges.
- Malformed/trailing input rejected; nesting limited to 256 levels. Files and message buffers use the same whole-document boundary; config files have no byte-size limit and must fit in memory.
- Native integers, including ticker `trade_id`, accept exact positive-exponent forms such as `3e2`; fractional values/overflow rejected. Decimal-point and negative-exponent spellings unsupported.
- Repeated keys use the last value; sections replace rather than merge. Earlier syntax/type errors fail; domain validation checks the final settings.

## Lifetime and diagnostics

- Malformed data, Coinbase errors and processing/output failures stop the run; unrelated valid messages are ignored. Peer close codes 1000, 1001 or no code succeed; other codes and unexpected transport failures return errors. No reconnect.
- Ctrl-C/SIGTERM requests normal WebSocket close, flushes CSV and logs final counts. Successful close exits successfully; close failure/timeout forces transport closure and returns failure.
- During setup, signals cancel pending operations without opening CSV. No unsubscribe required; synchronous work can delay signals/deadlines.
- Peer-initiated close also uses the close deadline, including stalled TLS teardown.
- SIGKILL/crashes bypass cleanup. No run-duration/event-count limit.
- INFO and ERROR share stderr; capture with `2> application.log`. CSV uses its own file; log rotation/collection is external.
- Final counts: received messages, decoded tickers, emitted rows. Emitted rows include buffered writes and do not prove persistence after a later flush failure.
- No periodic/per-ticker diagnostic logging.
