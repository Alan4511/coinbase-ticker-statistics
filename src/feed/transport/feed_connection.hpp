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

/** A feed protocol adapter receives raw connection events and decodes them.
 * Raw text is borrowed from the connection buffer for one synchronous feed call.
 */
template <typename FeedType>
concept Feed = requires(FeedType &feed, std::string_view message, Result<void> completion) {
    { feed.on_connected() } -> std::same_as<Result<void>>;
    { feed.on_message(message) } -> std::same_as<Result<void>>;
    { feed.on_stopped(std::move(completion)) } -> std::same_as<void>;
};

/** A verified TLS WebSocket with an initial subscription write and bounded setup/shutdown. */
class FeedConnection {
  public:
    /** Owns the subscription text; borrows io and feed until all pending operations drain.
     * on_connected runs after the subscription is written, without waiting for a server acknowledgement.
     */
    template <Feed FeedType>
    [[nodiscard]] static Result<std::unique_ptr<FeedConnection>>
    create(boost::asio::io_context &io, FeedConfig config, std::string subscription, FeedType &feed);
    ~FeedConnection();
    FeedConnection(const FeedConnection &) = delete;
    FeedConnection &operator=(const FeedConnection &) = delete;

    [[nodiscard]] Result<void> start();
    /** Call on the event-loop thread. Close an active WebSocket or cancel setup. */
    void stop();

  private:
    struct ConnectionCallbacks {
        std::function<Result<void>()> on_connected;
        std::function<Result<void>(std::string_view)> on_message;
        std::function<void(Result<void>)> on_stopped;
    };
    [[nodiscard]] static Result<std::unique_ptr<FeedConnection>> create_session(boost::asio::io_context &io,
                                                                                FeedConfig config,
                                                                                std::string subscription,
                                                                                ConnectionCallbacks callbacks);
    class Session;
    explicit FeedConnection(std::unique_ptr<Session> session);
    std::unique_ptr<Session> session_;
};

template <Feed FeedType>
Result<std::unique_ptr<FeedConnection>>
FeedConnection::create(boost::asio::io_context &io, FeedConfig config, std::string subscription, FeedType &feed) {
    ConnectionCallbacks callbacks{.on_connected =
                                      [&feed] {
                                          return feed.on_connected();
                                      },
                                  .on_message =
                                      [&feed](std::string_view message) {
                                          return feed.on_message(message);
                                      },
                                  .on_stopped =
                                      [&feed](Result<void> completion) {
                                          feed.on_stopped(std::move(completion));
                                      }};
    return create_session(io, std::move(config), std::move(subscription), std::move(callbacks));
}

} // namespace coinbase_ticker_statistics
