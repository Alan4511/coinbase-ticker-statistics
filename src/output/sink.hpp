#pragma once

#include <result.hpp>

#include <types.hpp>

#include <concepts>

namespace coinbase_ticker_statistics {

/** Compile-time output contract. Updates are borrowed only for synchronous delivery. */
template <typename Sink>
concept OutputSink = requires(Sink &sink, const StatisticsUpdate &update) {
    { sink.write_statistics(update) } -> std::same_as<Result<void>>;
};

} // namespace coinbase_ticker_statistics
