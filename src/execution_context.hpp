#pragma once

#include "run_control.hpp"

namespace coinbase_ticker_statistics {

/** Borrow application run control, without exposing feed, sink or other services.
 * Keep this context and its control alive until pending I/O handlers drain.
 */
class ExecutionContext {
  public:
    explicit ExecutionContext(RunControl &control) : control_(control) {
    }

    void fail(Error error) {
        control_.fail(std::move(error));
    }
    void request_stop(std::string_view reason) {
        control_.request_stop(reason);
    }
    [[nodiscard]] bool stopping() const noexcept {
        return control_.stopping();
    }
    void on_stopped(Result<void> completion) {
        control_.complete(std::move(completion));
    }

    ExecutionContext(const ExecutionContext &) = delete;
    ExecutionContext &operator=(const ExecutionContext &) = delete;

  private:
    RunControl &control_;
};

} // namespace coinbase_ticker_statistics
