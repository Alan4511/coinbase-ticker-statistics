#pragma once

#include "feed/parser/ticker_parser.hpp"
#include "feed/subscription.hpp"
#include "feed/transport/feed_connection.hpp"
#include "feed/transport/feed_handler.hpp"
#include <result.hpp>
#include <types.hpp>

#include <boost/asio/io_context.hpp>

#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>

namespace coinbase_ticker_statistics {

struct FeedCounts {
    std::size_t received_messages{};
    std::size_t ticker_updates{};
};

/** Own ticker protocol processing and one connection; borrow the typed consumer. */
template <typename Handler>
class TickerFeed {
  public:
    /** The event loop and borrowed handler must outlive all pending operations. */
    [[nodiscard]] static Result<std::unique_ptr<TickerFeed>>
    create(boost::asio::io_context &io, FeedConfig config, const Symbols &symbols, Handler &handler)
        requires FeedHandler<Handler>
    {
        auto subscription = encode_ticker_subscription(symbols);
        if (!subscription)
            return std::unexpected(std::move(subscription.error()));
        // The connection borrows this stable object, rather than a separate callback adapter.
        auto feed = std::unique_ptr<TickerFeed>(new TickerFeed(handler));
        auto connection = FeedConnection::create(io, std::move(config), std::move(*subscription), *feed);
        if (!connection)
            return std::unexpected(std::move(connection.error()));
        feed->connection_ = std::move(*connection);
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
        return handler_.on_connected();
    }

    [[nodiscard]] Result<void> on_message(std::string_view message) {
        ++counts_.received_messages;
        auto parsed_update = parse_ticker_message(message);
        if (!parsed_update)
            return std::unexpected(std::move(parsed_update.error()));
        if (!*parsed_update)
            return {};
        ++counts_.ticker_updates;
        return handler_.on_message(**parsed_update);
    }

    void on_stopped(Result<void> completion) {
        handler_.on_stopped(std::move(completion));
    }

  private:
    explicit TickerFeed(Handler &handler) : handler_(handler) {
    }

    Handler &handler_;
    FeedCounts counts_;
    // Declared last so the connection is destroyed before the protocol state it borrows.
    std::unique_ptr<FeedConnection> connection_;
};

} // namespace coinbase_ticker_statistics
