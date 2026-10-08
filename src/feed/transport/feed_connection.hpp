#pragma once

#include "feed/transport/feed_config.hpp"
#include <common/result.hpp>

#include <boost/asio/io_context.hpp>

#include <concepts>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace coinbase_ticker_statistics {

/** Raw text is borrowed from the connection buffer for one synchronous handler call. */
template <typename Handler>
concept FeedConnectionHandler = requires(Handler &handler, std::string_view message, Result<void> completion) {
    { handler.on_connected() } -> std::same_as<Result<void>>;
    { handler.on_message(message) } -> std::same_as<Result<void>>;
    { handler.on_stopped(std::move(completion)) } -> std::same_as<void>;
};

/** A verified TLS WebSocket with an initial subscription write and bounded setup/shutdown. */
class FeedConnection {
  public:
    /** Owns the subscription text; borrows io and handler until all pending operations drain.
     * on_connected runs after the subscription is written, without waiting for a server acknowledgement.
     */
    template <FeedConnectionHandler Handler>
    [[nodiscard]] static Result<std::unique_ptr<FeedConnection>>
    create(boost::asio::io_context &io, FeedConfig config, std::string subscription, Handler &handler);
    ~FeedConnection();
    FeedConnection(const FeedConnection &) = delete;
    FeedConnection &operator=(const FeedConnection &) = delete;

    [[nodiscard]] Result<void> start();
    /** Call on the event-loop thread. Close an active WebSocket or cancel setup. */
    void stop();

  private:
    struct Events {
        std::function<Result<void>()> on_connected;
        std::function<Result<void>(std::string_view)> on_message;
        std::function<void(Result<void>)> on_stopped;
    };
    [[nodiscard]] static Result<std::unique_ptr<FeedConnection>>
    create_session(boost::asio::io_context &io, FeedConfig config, std::string subscription, Events events);
    class Session;
    explicit FeedConnection(std::unique_ptr<Session> session);
    std::unique_ptr<Session> session_;
};

template <FeedConnectionHandler Handler>
Result<std::unique_ptr<FeedConnection>>
FeedConnection::create(boost::asio::io_context &io, FeedConfig config, std::string subscription, Handler &handler) {
    Events events{.on_connected =
                      [&handler] {
                          return handler.on_connected();
                      },
                  .on_message =
                      [&handler](std::string_view message) {
                          return handler.on_message(message);
                      },
                  .on_stopped =
                      [&handler](Result<void> completion) {
                          handler.on_stopped(std::move(completion));
                      }};
    return create_session(io, std::move(config), std::move(subscription), std::move(events));
}

} // namespace coinbase_ticker_statistics
