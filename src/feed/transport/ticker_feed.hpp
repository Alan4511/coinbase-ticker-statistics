#pragma once

#include "feed/transport/feed_connection.hpp"
#include "feed/transport/feed_handler.hpp"
#include <result.hpp>
#include <types.hpp>

#include <boost/asio/io_context.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

namespace coinbase_ticker_statistics {

struct FeedCounts {
    std::size_t received_messages{};
    std::size_t ticker_updates{};
};

/** Receive and decode ticker updates over a verified TLS WebSocket, with bounded setup and shutdown. */
class TickerFeed {
  public:
    /** The event loop and borrowed handler must outlive all pending operations. */
    template <FeedHandler Handler>
    [[nodiscard]] static Result<std::unique_ptr<TickerFeed>>
    create(boost::asio::io_context &io, FeedConfig config, const Symbols &symbols, Handler &handler);
    ~TickerFeed();
    TickerFeed(const TickerFeed &) = delete;
    TickerFeed &operator=(const TickerFeed &) = delete;

    [[nodiscard]] Result<void> start();
    /** Call on the event-loop thread. Close an active WebSocket or cancel setup. */
    void stop();
    [[nodiscard]] const FeedCounts &counts() const noexcept;

  private:
    // Only the factory adapts the borrowed object; callers supply its typed methods.
    struct Events {
        std::function<Result<void>()> on_connected;
        std::function<Result<void>(const TickerUpdate &)> on_message;
        std::function<void(Result<void>)> on_stopped;
    };
    [[nodiscard]] static Result<std::unique_ptr<TickerFeed>>
    create_feed(boost::asio::io_context &io, FeedConfig config, const Symbols &symbols, Events events);
    class InputHandler;
    TickerFeed(std::unique_ptr<InputHandler> input, std::unique_ptr<FeedConnection> connection);
    // Destroy the connection before its borrowed input handler.
    std::unique_ptr<InputHandler> input_;
    std::unique_ptr<FeedConnection> connection_;
};

template <FeedHandler Handler>
Result<std::unique_ptr<TickerFeed>>
TickerFeed::create(boost::asio::io_context &io, FeedConfig config, const Symbols &symbols, Handler &handler) {
    Events events{.on_connected =
                      [&handler] {
                          return handler.on_connected();
                      },
                  .on_message =
                      [&handler](const TickerUpdate &ticker_update) {
                          return handler.on_message(ticker_update);
                      },
                  .on_stopped =
                      [&handler](Result<void> completion) {
                          handler.on_stopped(std::move(completion));
                      }};
    return create_feed(io, std::move(config), symbols, std::move(events));
}

} // namespace coinbase_ticker_statistics
