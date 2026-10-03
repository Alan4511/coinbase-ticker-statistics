#pragma once

#include "result.hpp"

#include <gtest/gtest.h>

namespace coinbase_ticker_statistics::test {

/** Produce a useful assertion diagnostic without accessing the inactive expected alternative. */
template <typename T>
::testing::AssertionResult result_ok(const Result<T> &result) {
    if (result.has_value()) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << "error " << static_cast<int>(result.error().code) << ": "
                                         << result.error().message;
}

/** Check both the failure alternative and its stable category. */
template <typename T>
::testing::AssertionResult result_error(const Result<T> &result, ErrorCode expected) {
    if (result.has_value()) {
        return ::testing::AssertionFailure() << "expected an error, but the operation succeeded";
    }
    if (result.error().code == expected) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << "expected error " << static_cast<int>(expected) << ", received "
                                         << static_cast<int>(result.error().code) << ": " << result.error().message;
}

} // namespace coinbase_ticker_statistics::test

#define ASSERT_RESULT_OK(expression) ASSERT_TRUE(::coinbase_ticker_statistics::test::result_ok((expression)))
#define EXPECT_RESULT_OK(expression) EXPECT_TRUE(::coinbase_ticker_statistics::test::result_ok((expression)))
#define ASSERT_RESULT_ERROR(expression, code)                                                                          \
    ASSERT_TRUE(::coinbase_ticker_statistics::test::result_error((expression), (code)))

// Bind the value only after a fatal assertion succeeds; the named result owns its lifetime.
#define ASSERT_RESULT_VALUE(name, expression)                                                                          \
    auto name##_result = (expression);                                                                                 \
    ASSERT_RESULT_OK(name##_result);                                                                                   \
    auto &name = *name##_result
