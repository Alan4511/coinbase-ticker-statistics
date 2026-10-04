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
        return connected_result;
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
    Result<void> connected_result;
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

TEST(FeedConnection, SendsOpaqueSubscriptionAndDeliversUnparsedText) {
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

TEST(FeedConnection, PreservesConnectedHandlerFailureWithoutReadingMessages) {
    test::TestTrustStore trust;
    test::LoopbackExchange exchange(symbols, {}, test::ExchangeReply::WaitForClientClose);
    ConnectionRun client;
    client.connected_result = fail(ErrorCode::OutputIo, "consumer initialization failed");
    ASSERT_RESULT_OK(client.configure(exchange.config()));
    client.run();
    ASSERT_RESULT_ERROR(client.outcome, ErrorCode::OutputIo);
    EXPECT_EQ(client.outcome.error().message, "consumer initialization failed");
    EXPECT_EQ(client.connected, 1U);
    EXPECT_TRUE(client.messages.empty());
    EXPECT_TRUE(exchange.completed_successfully());
}

TEST(FeedConnection, RejectsInvalidConfigurationBeforeConnecting) {
    FeedConfig config;
    config.host.clear();
    ConnectionRun client;
    ASSERT_RESULT_ERROR(client.configure(config), ErrorCode::InvalidConfiguration);
    config = FeedConfig{};
    config.max_message_bytes = 0;
    ASSERT_RESULT_ERROR(client.configure(config), ErrorCode::InvalidConfiguration);
}

TEST(FeedConnection, RejectsAnUntrustedServerCertificate) {
    test::TestTrustStore trust(test::fixture_path("ticker_expected.csv"));
    test::LoopbackExchange exchange(symbols, {message});
    ConnectionRun client;
    ASSERT_RESULT_OK(client.configure(exchange.config()));
    client.run();
    ASSERT_RESULT_ERROR(client.outcome, ErrorCode::Transport);
    EXPECT_NE(client.outcome.error().message.find("certificate verify failed"), std::string::npos);
    EXPECT_EQ(client.connected, 0U);
    EXPECT_TRUE(client.messages.empty());
}

TEST(FeedConnection, RejectsBinaryMessages) {
    test::TestTrustStore trust;
    test::LoopbackExchange exchange(symbols, {message}, test::ExchangeReply::Binary);
    ConnectionRun client;
    ASSERT_RESULT_OK(client.configure(exchange.config()));
    client.run();
    ASSERT_RESULT_ERROR(client.outcome, ErrorCode::Protocol);
    EXPECT_TRUE(client.messages.empty());
}

TEST(FeedConnection, EnforcesMessageSizeLimit) {
    test::TestTrustStore trust;
    test::LoopbackExchange exchange(symbols, {message});
    auto config = exchange.config();
    config.max_message_bytes = 8;
    ConnectionRun client;
    ASSERT_RESULT_OK(client.configure(config));
    client.run();
    ASSERT_RESULT_ERROR(client.outcome, ErrorCode::Transport);
}

TEST(FeedConnection, StopBeforeStartCompletesOnce) {
    ConnectionRun client;
    ASSERT_RESULT_OK(client.configure(FeedConfig{}));
    client.connection->stop();
    client.connection->stop();
    client.io.run();
    EXPECT_EQ(client.stopped, 1U);
    ASSERT_RESULT_OK(client.outcome);
    ASSERT_RESULT_ERROR(client.connection->start(), ErrorCode::InvalidState);
}

TEST(FeedConnection, ValidatesDirectDeadlineSettingsBeforeCreatingTheConnection) {
    ConnectionRun client;
    for (const auto timeout : {Duration{0}, Duration{-1}, Duration{31'536'001}, Duration::max()}) {
        FeedConfig config;
        config.connect_timeout = timeout;
        ASSERT_RESULT_ERROR(client.configure(config), ErrorCode::InvalidConfiguration);
        config = FeedConfig{};
        config.close_timeout = timeout;
        ASSERT_RESULT_ERROR(client.configure(config), ErrorCode::InvalidConfiguration);
    }
    FeedConfig config;
    config.connect_timeout = std::chrono::days{365};
    config.close_timeout = std::chrono::days{365};
    ASSERT_RESULT_OK(client.configure(config));
}

TEST(FeedConnection, ConnectionDeadlineInterruptsPendingTlsHandshake) {
    test::TestTrustStore trust;
    test::LoopbackExchange exchange(symbols, {}, test::ExchangeReply::StallTls);
    auto config = exchange.config();
    config.connect_timeout = Duration{1};
    ConnectionRun client;
    ASSERT_RESULT_OK(client.configure(config));
    client.run();
    ASSERT_RESULT_ERROR(client.outcome, ErrorCode::Transport);
    EXPECT_EQ(client.outcome.error().message, "connection/subscription deadline exceeded");
}

} // namespace
} // namespace coinbase_ticker_statistics
