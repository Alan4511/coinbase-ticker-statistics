#pragma once

#include "result.hpp"

#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace coinbase_ticker_statistics {

/** Application-owned run state. All operations execute on the event-loop thread. */
class RunControl {
  public:
    explicit RunControl(std::function<void()> stop) : stop_(std::move(stop)) {
    }

    void fail(Error error) {
        if (result_)
            result_ = std::unexpected(std::move(error));
        request_stop("fatal error");
    }

    void request_stop(std::string_view reason) {
        if (stopping_)
            return;
        // Set state before invoking policy: stop() may synchronously report completion.
        stopping_ = true;
        stop_reason_ = reason;
        stop_();
    }

    void complete(Result<void> completion) {
        // Completion suppresses further stop requests, including cleanup failures.
        stopping_ = true;
        if (result_ && !completion)
            result_ = std::move(completion);
    }

    [[nodiscard]] bool stopping() const noexcept {
        return stopping_;
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
    bool stopping_{};
};

} // namespace coinbase_ticker_statistics
