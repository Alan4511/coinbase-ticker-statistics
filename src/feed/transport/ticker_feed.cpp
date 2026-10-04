#include "feed/transport/ticker_feed.hpp"
#include "feed/parser/ticker_parser.hpp"
#include "feed/subscription.hpp"
#include "feed/transport/feed_connection.hpp"

#include <utility>

namespace coinbase_ticker_statistics {

// The connection borrows this stable object until its pending operations drain.
class TickerFeed::InputHandler {
  public:
    explicit InputHandler(Events events) : events_(std::move(events)) {
    }

    Result<void> on_connected() {
        return events_.on_connected();
    }

    Result<void> on_message(std::string_view message) {
        ++counts.received_messages;
        auto parsed_update = parse_ticker_message(message);
        if (!parsed_update)
            return std::unexpected(std::move(parsed_update.error()));
        if (!*parsed_update)
            return {};
        ++counts.ticker_updates;
        return events_.on_message(**parsed_update);
    }

    void on_stopped(Result<void> completion) {
        events_.on_stopped(std::move(completion));
    }

    FeedCounts counts;

  private:
    Events events_;
};

Result<std::unique_ptr<TickerFeed>>
TickerFeed::create_feed(boost::asio::io_context &io, FeedConfig config, const Symbols &symbols, Events events) {
    auto subscription = encode_ticker_subscription(symbols);
    if (!subscription)
        return std::unexpected(std::move(subscription.error()));
    auto input = std::make_unique<InputHandler>(std::move(events));
    auto connection = FeedConnection::create(io, std::move(config), std::move(*subscription), *input);
    if (!connection)
        return std::unexpected(std::move(connection.error()));
    // make_unique cannot call the private constructor; ownership is immediate.
    return std::unique_ptr<TickerFeed>(new TickerFeed(std::move(input), std::move(*connection)));
}

TickerFeed::TickerFeed(std::unique_ptr<InputHandler> input, std::unique_ptr<FeedConnection> connection)
    : input_(std::move(input)), connection_(std::move(connection)) {
}

TickerFeed::~TickerFeed() = default;

Result<void> TickerFeed::start() {
    return connection_->start();
}

void TickerFeed::stop() {
    connection_->stop();
}

const FeedCounts &TickerFeed::counts() const noexcept {
    return input_->counts;
}

} // namespace coinbase_ticker_statistics
