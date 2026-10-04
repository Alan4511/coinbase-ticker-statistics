## Goal

Refactor the current application lifecycle plumbing so that application-wide execution concerns are passed through a narrow execution context rather than through multiple ad-hoc `std::function` callbacks.

The purpose is to simplify lifecycle/error propagation while preserving the existing separation of concerns:

```text
Application
  ├─ TickerFeed -> FeedConnection
  ├─ StatisticsProcessor
  └─ OutputSink -> CsvSink
```

The refactor should not change the domain architecture or make synchronous modules artificially asynchronous.

## Current problem

The current code already has good subsystem boundaries, but application lifecycle concerns are spread across several callback paths.

Examples include:

- `ApplicationFeedHandler` stores `on_connected` and `on_stopped` callbacks.
- `CsvSink` stores a flush-error callback configured via `set_flush_error_handler()`.
- `application.cpp` owns `RunState`, signal handling, feed shutdown policy, sink cleanup, and conversion of output errors into `feed.stop()`.

This is functionally correct, but lifecycle policy is represented through several independent callback relationships.

In particular, asynchronous CSV flush failures require a separate callback because the failure may occur after `write_statistics()` has returned.

## Proposed model

Introduce a narrow application-owned execution/lifecycle context that can be passed through event-processing paths.

The context should represent cross-cutting execution capabilities, not application dependencies.

Conceptually:

```cpp
class ExecutionContext {
public:
    Logger& logger() noexcept;

    void fail(Error error);
    void request_stop(std::string_view reason);

    [[nodiscard]] bool stopping() const noexcept;

private:
    // Application-owned state/control only.
};
```

The context must not become a service locator.

Do not put objects such as these directly into it:

```cpp
TickerFeed&
CsvSink&
StatisticsProcessor&
Config&
boost::asio::signal_set&
```

Statistics, output sinks, feeds, and configuration should remain explicit dependencies owned by their existing layers.

## Intended event flow

Prefer event handlers of the form:

```cpp
handler.on_connected(ctx);
handler.on_message(ctx, update);
handler.on_stopped(ctx, completion);
```

and, where useful:

```cpp
sink.write_statistics(ctx, statistics_update);
```

The context should carry lifecycle/error-reporting capability through the event pipeline.

The handler should continue to own or explicitly reference its actual processing dependencies:

```cpp
template <OutputSink Sink>
class ApplicationFeedHandler {
    StatisticsProcessor processor_;
    Sink& sink_;
};
```

Do not move `StatisticsProcessor` or `Sink` into `ExecutionContext`.

## Async output errors

Keep the current buffered/timed CSV flush design.

Do not replace timed/threshold flushing with flush-per-row merely to simplify error handling.

A successful:

```cpp
write_statistics(...)
```

can only report failures that occur synchronously during that call.

A timer-triggered flush may fail later, after the originating call has returned, so that failure still requires an asynchronous reporting path.

With the proposed context, `CsvSink` should no longer need a separately registered application-specific callback such as:

```cpp
set_flush_error_handler(...)
```

Instead, the context available to the asynchronous flush operation should provide the reporting capability:

```cpp
ctx.fail(error);
```

Conceptually:

```cpp
void CsvSink::schedule_flush(ExecutionContext& ctx) {
    flush_timer_.async_wait(
        [this, &ctx](boost::system::error_code ec) {
            if (ec)
                return;

            if (auto flushed = flush_pending(); !flushed)
                ctx.fail(std::move(flushed.error()));
        });
}
```

The exact implementation may differ if required for safe lifetime management, but preserve this ownership direction:

```text
CsvSink
    |
    | reports failure
    v
ExecutionContext / application run control
    |
    | applies application shutdown policy
    v
TickerFeed::stop()
```

`CsvSink` must not know about `TickerFeed`.

## Run control

Separate the public context capability from the concrete mechanism used to stop the application.

A small application-owned `RunControl` or equivalent is acceptable.

For example:

```cpp
class RunControl {
public:
    void fail(Error error);
    void request_stop(std::string_view reason);
    bool stopping() const noexcept;
};
```

`fail()` should:

1. preserve first-failure-wins semantics,
2. record the fatal error in application run state,
3. initiate graceful application shutdown.

`request_stop()` should be idempotent.

The actual stop implementation remains application policy.

## Graceful shutdown

Do not use `io_context::stop()` as the normal failure path.

Normal shutdown should remain:

```text
failure / signal
    ↓
RunControl::request_stop()
    ↓
TickerFeed::stop()
    ↓
FeedConnection cancels/closes active transport work
    ↓
feed completion
    ↓
output close
signal cancellation
    ↓
io_context naturally drains
    ↓
io.run() returns
```

`io_context::stop()` may remain an emergency containment mechanism for unexpected exceptions.

This preserves the existing bounded/graceful transport shutdown behavior.

## Lifetime requirements

Be careful with asynchronous handlers that retain access to `ExecutionContext`.

The context must outlive every pending operation that may use it.

The application already owns the event loop and all major runtime objects for the duration of `io.run()`. Preserve or strengthen that lifetime relationship.

Do not capture temporary contexts or objects whose lifetime ends before pending timer/socket handlers drain.

If passing `ExecutionContext&` directly into a scheduled timer would make lifetime guarantees unclear, restructure ownership so the context is application-owned and stable for the entire run.

## Separation of concerns to preserve

After the refactor:

```text
FeedConnection
    Transport/session mechanics only:
    DNS, TCP, TLS, WebSocket, async reads/writes, shutdown.

TickerFeed
    Coinbase ticker protocol:
    subscription and raw-message-to-TickerUpdate translation.

ApplicationFeedHandler
    Event processing:
    TickerUpdate -> StatisticsProcessor -> StatisticsUpdate -> OutputSink.

StatisticsProcessor
    Pure statistics/domain state.
    No Asio, feed, output, or application lifecycle dependencies.

CsvSink
    CSV buffering, formatting coordination, flush timer, file I/O.
    Reports asynchronous failure through execution context.
    Does not control the feed.

ExecutionContext / RunControl
    Cross-cutting execution state:
    logging where appropriate,
    first-failure recording,
    stop requests,
    application-level lifecycle reporting.

Application
    Composition root and owner of lifecycle/shutdown policy.
```

## Desired simplifications

The refactor should aim to remove or reduce plumbing such as:

```cpp
CsvSink::set_flush_error_handler(...)
```

and application-specific lifecycle `std::function` members where the execution context makes them unnecessary.

Do not remove callbacks that are inherent to Boost.Asio itself, such as timer, signal, or transport completion handlers.

The goal is not to eliminate asynchronous callbacks altogether.

The goal is to eliminate redundant application-level callback wiring.

## Important semantic distinction

`ExecutionContext::fail()` is asynchronous failure reporting, not `std::expected` propagation.

Synchronous code should continue to return:

```cpp
Result<void>
```

and propagate errors normally:

```cpp
return std::unexpected(error);
```

For asynchronous work, where the original call stack no longer exists:

```cpp
ctx.fail(error);
```

reports the error into application lifecycle state.

Keep both mechanisms.

## Scope

This is a targeted lifecycle and event-propagation refactor.

Do not:

- rewrite `SlidingWindow`,
- alter the statistical algorithms,
- merge `TickerFeed` and `FeedConnection`,
- add `FeedManager`,
- add reconnect/backoff/heartbeat/sequence-gap recovery,
- add worker threads,
- introduce an event bus,
- introduce a generic service locator,
- replace the current output abstraction,
- force coroutines into unrelated modules.

Make all changes necessary to establish the execution-context model cleanly, but avoid unrelated refactors.

## Acceptance criteria

After the refactor:

1. Application remains the lifecycle owner.
2. `ExecutionContext` contains only cross-cutting execution capabilities.
3. Statistics and output remain explicit dependencies rather than members of the context.
4. `CsvSink` does not know about `TickerFeed`.
5. Async CSV flush failures can report through application context/run control.
6. `set_flush_error_handler()` is removed if the context fully replaces it.
7. Immediate sink failures still use `Result<void>`.
8. Timed flush failures still work correctly.
9. Feed shutdown remains graceful and bounded.
10. `io_context::stop()` is not used as the ordinary shutdown policy.
11. `TickerFeed`/`FeedConnection` separation remains unchanged.
12. Single-threaded Asio execution remains unchanged.
13. Context lifetime is valid for every pending async operation.
14. Tests are updated to verify first-failure-wins behavior, async output failure handling, signal shutdown, and normal feed completion.
