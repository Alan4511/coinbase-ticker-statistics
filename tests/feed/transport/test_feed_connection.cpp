#include "test_exchange.hpp"
#include "test_result.hpp"
#include <feed/subscription.hpp>
#include <feed/transport/feed_connection.hpp>

#include <gtest/gtest.h>

#include <type_traits>
#include <vector>

namespace coinbase_ticker_statistics {
namespace {

const Symbols symbols{"BTC-USD", "ETH-USD", "SOL-USD"};
const std::string message = "raw feed message";

class ConnectionRun final {
  public:
    Result<void> configure(FeedConfig config, std::string subscription) {
        auto created = FeedConnection::create(io, std::move(config), std::move(subscription), *this);
        if (!created)
            return std::unexpected(std::move(created.error()));
        connection = std::move(*created);
        return {};
    }
    Result<void> configure(FeedConfig config) {
        return configure(std::move(config), *encode_ticker_subscription(symbols));
    }
    Result<void> on_connected() {
        ++connected;
        if (stop_when_connected)
            connection->stop();
        return {};
    }
    Result<void> on_message(std::string_view text) {
        messages.emplace_back(text);
        return {};
    }
    void on_stopped(Result<void> result) {
        ++stopped;
        outcome = std::move(result);
    }
    void run() {
        ASSERT_RESULT_OK(connection->start());
        io.run();
        EXPECT_EQ(stopped, 1U);
    }
    boost::asio::io_context io;
    std::unique_ptr<FeedConnection> connection;
    std::vector<std::string> messages;
    Result<void> outcome;
    bool stop_when_connected{};
    unsigned connected{};
    unsigned stopped{};
};

static_assert(FeedConnectionHandler<ConnectionRun>);
static_assert(!FeedConnectionHandler<int>);
static_assert(!std::is_polymorphic_v<ConnectionRun>);

struct WrongMessageHandler {
    Result<void> on_connected();
    Result<void> on_message(const TickerUpdate &);
    void on_stopped(Result<void>);
};
static_assert(!FeedConnectionHandler<WrongMessageHandler>);

TEST(FeedConnection, DeliversOpaqueSubscriptionAndMessagesOverVerifiedTls) {
    test::TestTrustStore trust;
    const std::string subscription = "subscribe to another feed";
    test::LoopbackExchange exchange(subscription, {message, "not JSON"});
    ConnectionRun client;
    ASSERT_RESULT_OK(client.configure(exchange.config(), subscription));
    client.run();
    ASSERT_RESULT_OK(client.outcome);
    EXPECT_EQ(client.connected, 1U);
    EXPECT_EQ(client.messages, (std::vector<std::string>{message, "not JSON"}));
    EXPECT_TRUE(exchange.completed_successfully());
}

TEST(FeedConnection, RejectsTransportOrProtocolViolations) {
    struct Violation {
        const char *name;
        bool trusted;
        test::ExchangeReply reply;
        std::size_t message_limit;
        ErrorCode error;
    };
    const Violation cases[]{{"untrusted certificate", false, test::ExchangeReply::Text, 1024, ErrorCode::Transport},
                            {"binary frame", true, test::ExchangeReply::Binary, 1024, ErrorCode::Protocol},
                            {"oversized frame", true, test::ExchangeReply::Text, 8, ErrorCode::Transport}};
    for (const auto &[name, trusted, reply, message_limit, error] : cases) {
        SCOPED_TRACE(name);
        test::TestTrustStore trust(test::fixture_path(trusted ? "test_tls_cert.pem" : "ticker_expected.csv"));
        test::LoopbackExchange exchange(symbols, {message}, reply);
        auto config = exchange.config();
        config.max_message_bytes = message_limit;
        ConnectionRun client;
        ASSERT_RESULT_OK(client.configure(config));
        client.run();
        ASSERT_RESULT_ERROR(client.outcome, error);
        EXPECT_TRUE(client.messages.empty());
        if (!trusted) {
            EXPECT_EQ(client.connected, 0U);
            EXPECT_NE(client.outcome.error().message.find("certificate verify failed"), std::string::npos);
        }
    }
}

TEST(FeedConnection, BoundsConnectionAndShutdown) {
    test::TestTrustStore trust;
    for (const auto reply : {test::ExchangeReply::StallTls, test::ExchangeReply::RemainIdle}) {
        SCOPED_TRACE(reply == test::ExchangeReply::StallTls ? "connection deadline" : "close deadline");
        test::LoopbackExchange exchange(symbols, {}, reply);
        auto config = exchange.config();
        config.connect_timeout = Duration{1};
        config.close_timeout = Duration{1};
        ConnectionRun client;
        client.stop_when_connected = true;
        ASSERT_RESULT_OK(client.configure(config));
        const auto started = std::chrono::steady_clock::now();
        client.run();
        EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds{3});
        ASSERT_RESULT_ERROR(client.outcome, ErrorCode::Transport);
        EXPECT_EQ(client.outcome.error().message,
                  reply == test::ExchangeReply::StallTls ? "connection/subscription deadline exceeded"
                                                         : "WebSocket close deadline exceeded");
        EXPECT_EQ(client.connected, reply == test::ExchangeReply::StallTls ? 0U : 1U);
        EXPECT_EQ(client.stopped, 1U);
    }
}

} // namespace
} // namespace coinbase_ticker_statistics
