#pragma once

#include "result.hpp"

#include "feed/transport/feed_options.hpp"
#include "types.hpp"

#include <boost/asio/io_context.hpp>

#include <functional>
#include <memory>
#include <string_view>

namespace coinbase_ticker_statistics {

/** Callbacks borrow data only for their invocation on the io_context thread. */
struct FeedCallbacks {
    std::function<Result<void>(std::string_view)> message;
    std::function<Result<void>()> connected;
    std::function<void(Result<void>)> stopped;
};

/** One asynchronous TLS WebSocket; connection ownership is independent of symbol state. */
class TickerFeed {
  public:
    /** io_context and callback targets must outlive this connection's pending handlers. */
    [[nodiscard]] static Result<std::unique_ptr<TickerFeed>>
    create(boost::asio::io_context &io, FeedConfig config, Symbols symbols, FeedCallbacks callbacks);
    ~TickerFeed();
    TickerFeed(const TickerFeed &) = delete;
    TickerFeed &operator=(const TickerFeed &) = delete;
    TickerFeed(TickerFeed &&) = delete;
    TickerFeed &operator=(TickerFeed &&) = delete;
    /** Begin DNS, TCP, TLS, WebSocket and subscription operations, each asynchronously. */
    [[nodiscard]] Result<void> start();
    /** Request bounded close on the io_context thread; completion reports errors via stopped. */
    void stop();

  private:
    class Session;
    explicit TickerFeed(std::shared_ptr<Session> session);
    std::shared_ptr<Session> session_;
};

} // namespace coinbase_ticker_statistics
