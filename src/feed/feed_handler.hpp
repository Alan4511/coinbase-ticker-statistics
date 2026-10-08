#pragma once

#include <common/result.hpp>
#include <common/types.hpp>

#include <concepts>
#include <utility>

namespace coinbase_ticker_statistics {

/** Compile-time event contract. Handlers bind run dependencies when constructed.
 * Each decoded ticker message is borrowed for synchronous delivery.
 */
template <typename Handler>
concept TickerEventHandler = requires(Handler &handler, const TickerUpdate &ticker_update, Result<void> completion) {
    { handler.on_connected() } -> std::same_as<Result<void>>;
    { handler.on_message(ticker_update) } -> std::same_as<Result<void>>;
    { handler.on_stopped(std::move(completion)) } -> std::same_as<void>;
};

} // namespace coinbase_ticker_statistics
