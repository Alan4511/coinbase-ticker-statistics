#include "test_result.hpp"
#include <execution_context.hpp>

#include <gtest/gtest.h>

#include <string>

namespace coinbase_ticker_statistics {
namespace {

TEST(RunControl, FirstFailureSurvivesReentrantCompletionAndCleanupFailures) {
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

TEST(RunControl, StopRequestsAreIdempotentAndOwnTheirReason) {
    unsigned stop_requests{};
    RunControl control([&] {
        ++stop_requests;
    });
    ExecutionContext context(control);
    EXPECT_FALSE(context.stopping());
    std::string reason = "SIGINT";
    context.request_stop(reason);
    reason = "changed";
    context.request_stop("SIGTERM");
    context.on_stopped({});
    EXPECT_EQ(stop_requests, 1U);
    EXPECT_EQ(control.stop_reason(), "SIGINT");
    ASSERT_RESULT_OK(control.result());
}

TEST(RunControl, FailureAfterSignalStillDeterminesTheExitResult) {
    unsigned stop_requests{};
    RunControl control([&] {
        ++stop_requests;
    });
    ExecutionContext context(control);
    context.request_stop("SIGTERM");
    context.fail(Error{ErrorCode::OutputIo, "pending output failed"});
    context.on_stopped(fail(ErrorCode::Transport, "close also failed"));
    EXPECT_EQ(stop_requests, 1U);
    ASSERT_RESULT_ERROR(control.result(), ErrorCode::OutputIo);
    EXPECT_EQ(control.stop_reason(), "SIGTERM");
}

TEST(RunControl, NormalCompletionDoesNotRequestAnotherFeedStop) {
    RunControl control([] {
        ADD_FAILURE() << "completed feed must not be stopped again";
    });
    ExecutionContext context(control);
    context.on_stopped({});
    EXPECT_TRUE(context.stopping());
    ASSERT_RESULT_OK(control.result());
    EXPECT_EQ(control.stop_reason(), "peer closed connection");
    context.request_stop("late signal");
    context.fail(Error{ErrorCode::OutputIo, "final flush failed"});
    ASSERT_RESULT_ERROR(control.result(), ErrorCode::OutputIo);
}

TEST(RunControl, FeedCompletionFailureSurvivesFinalOutputFailure) {
    RunControl control([] {
        ADD_FAILURE() << "completed feed must not be stopped again";
    });
    ExecutionContext context(control);
    context.on_stopped(fail(ErrorCode::Protocol, "malformed ticker"));
    context.fail(Error{ErrorCode::OutputIo, "final flush failed"});
    ASSERT_RESULT_ERROR(control.result(), ErrorCode::Protocol);
    EXPECT_EQ(control.result().error().message, "malformed ticker");
}

} // namespace
} // namespace coinbase_ticker_statistics
