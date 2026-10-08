#pragma once

#include <common/result.hpp>

#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace coinbase_ticker_statistics {

/** Own the run's lifecycle state, first failure and application-supplied stop policy.
 * The application or its coordinator declares whole-run completion.
 * All operations execute on the event-loop thread.
 */
class RunControl {
  public:
    enum class State {
        Running,
        Stopping,
        // Run coordination has ended; pending callbacks and resource cleanup may still follow.
        Completed
    };

    explicit RunControl(std::function<void()> stop) : stop_(std::move(stop)) {
    }

    void fail(Error error) {
        // For Result<void>, has_value() means no error yet; preserve the first failure.
        if (result_.has_value())
            result_ = std::unexpected(std::move(error));
        request_stop("fatal error");
    }

    void request_stop(std::string_view reason) {
        if (state_ != State::Running)
            return;
        // Set state before invoking policy: stop() may synchronously report completion.
        state_ = State::Stopping;
        stop_reason_ = reason;
        stop_();
    }

    void complete(Result<void> completion) {
        // This is a completion notification, not a stop request.
        // Suppress further stop callbacks, including those triggered by cleanup failures.
        state_ = State::Completed;
        if (result_.has_value() && !completion.has_value())
            result_ = std::move(completion);
    }

    [[nodiscard]] State state() const noexcept {
        return state_;
    }
    [[nodiscard]] bool shutdown_started() const noexcept {
        return state_ != State::Running;
    }
    [[nodiscard]] const Result<void> &result() const noexcept {
        return result_;
    }
    [[nodiscard]] std::string_view stop_reason() const noexcept {
        return stop_reason_;
    }

    RunControl(const RunControl &) = delete;
    RunControl &operator=(const RunControl &) = delete;

  private:
    std::function<void()> stop_;
    Result<void> result_;
    std::string stop_reason_{"peer closed connection"};
    State state_{State::Running};
};

} // namespace coinbase_ticker_statistics
