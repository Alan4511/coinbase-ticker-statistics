#include "test_result.hpp"
#include <runtime/execution_context.hpp>

#include <gtest/gtest.h>

#include <string>

namespace coinbase_ticker_statistics {
namespace {

TEST(RunControl, PreservesFirstFailureAcrossReentrantAndCleanupFailures) {
    unsigned stop_requests{};
    RunControl control([&] {
        ++stop_requests;
        EXPECT_TRUE(control.stopping());
        ASSERT_RESULT_ERROR(control.result(), ErrorCode::OutputIo);
        control.complete(fail(ErrorCode::Transport, "close deadline exceeded"));
    });
    ExecutionContext context(control);
    context.fail(Error{ErrorCode::OutputIo, "timed flush failed"});
    context.fail(Error{ErrorCode::FileIo, "cleanup failed"});
    context.request_stop("SIGTERM");
    EXPECT_EQ(stop_requests, 1U);
    EXPECT_TRUE(context.stopping());
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
        ExecutionContext context(control);
        EXPECT_FALSE(context.stopping());
        if (signal_requested) {
            std::string reason = "SIGINT";
            context.request_stop(reason);
            reason = "changed";
            context.request_stop("SIGTERM");
        }
        context.on_stopped({});
        context.on_stopped({});
        context.request_stop("late signal");
        EXPECT_TRUE(context.stopping());
        ASSERT_RESULT_OK(control.result());
        EXPECT_EQ(control.stop_reason(), signal_requested ? "SIGINT" : "peer closed connection");
        context.fail(Error{ErrorCode::OutputIo, "final flush failed"});
        ASSERT_RESULT_ERROR(control.result(), ErrorCode::OutputIo);
        EXPECT_EQ(control.result().error().message, "final flush failed");
        EXPECT_EQ(stop_requests, signal_requested ? 1U : 0U);
    }
}

} // namespace
} // namespace coinbase_ticker_statistics
