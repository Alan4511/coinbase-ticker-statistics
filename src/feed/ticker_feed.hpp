#pragma once

#include "feed/feed_handler.hpp"
#include "feed/parser/ticker_parser.hpp"
#include "feed/subscription.hpp"
#include "feed/transport/feed_connection.hpp"
#include <common/result.hpp>
#include <common/types.hpp>
#include <runtime/run_control_handle.hpp>

#include <boost/asio/io_context.hpp>

#include <cstddef>
#include <expected>
#include <memory>
#include <string_view>
#include <utility>

namespace coinbase_ticker_statistics {

struct FeedCounts {
    std::size_t received_messages{};
    std::size_t ticker_updates{};
};

/** Own the connection and receive its raw-message/lifecycle events.
 * Decode ticker messages and deliver typed updates to the borrowed ticker handler.
 */
template <TickerEventHandler TickerHandler>
class TickerFeed {
  public:
    /** The event loop, borrowed ticker handler and run-control handle must outlive all pending operations. */
    [[nodiscard]] static Result<std::unique_ptr<TickerFeed>> create(boost::asio::io_context &io,
                                                                    FeedConfig config,
                                                                    const Symbols &symbols,
                                                                    TickerHandler &ticker_handler,
                                                                    RunControlHandle &control_handle) {
        auto subscription = encode_ticker_subscription(symbols);
        if (!subscription.has_value())
            return std::unexpected(std::move(subscription.error()));
        // The connection borrows this object's stable address.
        auto feed = std::unique_ptr<TickerFeed>(new TickerFeed(ticker_handler, control_handle));
        // This adapter receives connection events; create() checks that it satisfies the Feed concept.
        auto connection = FeedConnection::create(io, std::move(config), std::move(subscription.value()), *feed);
        if (!connection.has_value())
            return std::unexpected(std::move(connection.error()));
        feed->connection_ = std::move(connection.value());
        return feed;
    }

    TickerFeed(const TickerFeed &) = delete;
    TickerFeed &operator=(const TickerFeed &) = delete;

    [[nodiscard]] Result<void> start() {
        return connection_->start();
    }

    /** Call on the event-loop thread. Close an active WebSocket or cancel setup. */
    void stop() {
        connection_->stop();
    }

    [[nodiscard]] const FeedCounts &counts() const noexcept {
        return counts_;
    }

    // Raw transport callbacks: only the connection invokes these during a normal run.
    [[nodiscard]] Result<void> on_connected() {
        return report_completion(ticker_handler_.on_connected());
    }

    [[nodiscard]] Result<void> on_message(std::string_view message) {
        ++counts_.received_messages;
        auto parsed_update = parse_ticker_message(message);
        if (!parsed_update.has_value())
            return report_completion(std::unexpected(std::move(parsed_update.error())));
        if (!parsed_update.value().has_value())
            return {};
        ++counts_.ticker_updates;
        return report_completion(ticker_handler_.on_message(parsed_update.value().value()));
    }

    void on_stopped(Result<void> completion) {
        ticker_handler_.on_stopped(std::move(completion));
    }

  private:
    Result<void> report_completion(Result<void> completion) {
        // Record failures at the async boundary, before close/timer errors can overtake them.
        // The transport still receives the Result; domain processing remains synchronous.
        if (!completion.has_value())
            control_handle_.fail(completion.error());
        return completion;
    }

    TickerFeed(TickerHandler &ticker_handler, RunControlHandle &control_handle)
        : ticker_handler_(ticker_handler), control_handle_(control_handle) {
    }

    TickerHandler &ticker_handler_;
    RunControlHandle &control_handle_;
    FeedCounts counts_;
    // Declared last so the connection is destroyed before the protocol state it borrows.
    std::unique_ptr<FeedConnection> connection_;
};

} // namespace coinbase_ticker_statistics
