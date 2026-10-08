#pragma once

#include <runtime/execution_context.hpp>
#include <common/result.hpp>
#include <common/types.hpp>

#include <concepts>
#include <utility>

namespace coinbase_ticker_statistics {

/** Compile-time event contract. Each decoded ticker message is borrowed for synchronous delivery. */
template <typename Handler>
concept FeedHandler =
    requires(Handler &handler, ExecutionContext &context, const TickerUpdate &ticker_update, Result<void> completion) {
        { handler.on_connected(context) } -> std::same_as<Result<void>>;
        { handler.on_message(context, ticker_update) } -> std::same_as<Result<void>>;
        { handler.on_stopped(context, std::move(completion)) } -> std::same_as<void>;
    };

} // namespace coinbase_ticker_statistics
