#include "test_result.hpp"
#include <runtime/run_control_handle.hpp>

#include <gtest/gtest.h>

#include <string>

namespace coinbase_ticker_statistics {
namespace {

TEST(RunControl, PreservesFirstFailureAcrossReentrantAndCleanupFailures) {
    unsigned stop_requests{};
    RunControl control([&] {
        ++stop_requests;
        EXPECT_EQ(control.state(), RunControl::State::Stopping);
        ASSERT_RESULT_ERROR(control.result(), ErrorCode::OutputIo);
        control.complete(fail(ErrorCode::Transport, "close deadline exceeded"));
    });
    RunControlHandle control_handle(control);
    control_handle.fail(Error{ErrorCode::OutputIo, "timed flush failed"});
    control_handle.fail(Error{ErrorCode::FileIo, "cleanup failed"});
    control_handle.request_stop("SIGTERM");
    EXPECT_EQ(stop_requests, 1U);
    EXPECT_TRUE(control_handle.shutdown_started());
    EXPECT_EQ(control.state(), RunControl::State::Completed);
    ASSERT_RESULT_ERROR(control.result(), ErrorCode::OutputIo);
    EXPECT_EQ(control.result().error().message, "timed flush failed");
    EXPECT_EQ(control.stop_reason(), "fatal error");
}

TEST(RunControl, StopAndCompletionAreIdempotent) {
    for (const bool signal_requested : {false, true}) {
        SCOPED_TRACE(signal_requested ? "signal shutdown" : "peer completion");
        unsigned stop_requests{};
        RunControl control([&] {
            ++stop_requests;
        });
        RunControlHandle control_handle(control);
        EXPECT_FALSE(control_handle.shutdown_started());
        EXPECT_EQ(control.state(), RunControl::State::Running);
        if (signal_requested) {
            std::string reason = "SIGINT";
            control_handle.request_stop(reason);
            EXPECT_EQ(control.state(), RunControl::State::Stopping);
            reason = "changed";
            control_handle.request_stop("SIGTERM");
        }
        control_handle.on_stopped({});
        control_handle.on_stopped({});
        control_handle.request_stop("late signal");
        EXPECT_TRUE(control_handle.shutdown_started());
        EXPECT_EQ(control.state(), RunControl::State::Completed);
        ASSERT_RESULT_OK(control.result());
        EXPECT_EQ(control.stop_reason(), signal_requested ? "SIGINT" : "peer closed connection");
        control_handle.fail(Error{ErrorCode::OutputIo, "final flush failed"});
        ASSERT_RESULT_ERROR(control.result(), ErrorCode::OutputIo);
        EXPECT_EQ(control.result().error().message, "final flush failed");
        EXPECT_EQ(stop_requests, signal_requested ? 1U : 0U);
    }
}

} // namespace
} // namespace coinbase_ticker_statistics
