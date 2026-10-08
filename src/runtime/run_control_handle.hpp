#pragma once

#include "run_control.hpp"

#include <string_view>
#include <utility>

namespace coinbase_ticker_statistics {

/** Borrowed reporting interface backed by the application's RunControl.
 * Reusable feed/output modules can report failures without depending on Application.
 * Owns no run state, executor or services; handle and control must outlive pending callbacks.
 */
class RunControlHandle {
  public:
    explicit RunControlHandle(RunControl &control) : control_(control) {
    }

    void fail(Error error) {
        control_.fail(std::move(error));
    }
    void request_stop(std::string_view reason) {
        control_.request_stop(reason);
    }
    [[nodiscard]] bool shutdown_started() const noexcept {
        return control_.shutdown_started();
    }
    // The application/coordinator reports whole-run completion, not an individual feed's stop.
    void on_stopped(Result<void> completion) {
        control_.complete(std::move(completion));
    }

    RunControlHandle(const RunControlHandle &) = delete;
    RunControlHandle &operator=(const RunControlHandle &) = delete;

  private:
    RunControl &control_;
};

} // namespace coinbase_ticker_statistics
