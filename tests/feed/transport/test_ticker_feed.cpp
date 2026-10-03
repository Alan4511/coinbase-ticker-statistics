#include "feed/transport/ticker_feed.hpp"
#include "test_result.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace coinbase_ticker_statistics {
namespace {

namespace net = boost::asio;
namespace ssl = net::ssl;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using Tcp = net::ip::tcp;

constexpr std::chrono::seconds kTestDeadline{5};
constexpr std::chrono::seconds kConnectionTimeout{2};
constexpr std::chrono::seconds kCloseTimeout{1};
constexpr std::string_view kTickerMessage =
    R"({"type":"ticker","product_id":"BTC-USD","trade_id":42,"price":"123.45","time":"2026-01-02T03:04:05Z"})";
const std::vector<std::string> kSymbols{"BTC-USD", "ETH-USD", "SOL-USD"};

std::filesystem::path fixture_path(std::string_view name) {
    return std::filesystem::path(COINBASE_TICKER_STATISTICS_SOURCE_DIR) / "data" / name;
}

ssl::context test_server_context() {
    ssl::context context(ssl::context::tls_server);
    // This publicly committed private key exists only for isolated tests.
    context.use_certificate_chain_file(fixture_path("test_tls_cert.pem").string());
    context.use_private_key_file(fixture_path("test_tls_key.pem").string(), ssl::context::pem);
    return context;
}

enum class ReplyKind {
    Text,
    Binary
};

/**
 * A one-client TLS/WebSocket exchange on the client's own event loop.
 * No background threads or external network are involved. run_for() in the
 * fixture bounds every accept, handshake, read, write, and close operation.
 */
class LoopbackExchange final : public std::enable_shared_from_this<LoopbackExchange> {
  public:
    explicit LoopbackExchange(net::io_context &io, ReplyKind reply_kind = ReplyKind::Text)
        : tls_(test_server_context()), acceptor_(io, Tcp::endpoint(net::ip::address_v4::loopback(), 0)),
          socket_(io, tls_), reply_kind_(reply_kind) {
    }

    [[nodiscard]] std::string port() const {
        return std::to_string(acceptor_.local_endpoint().port());
    }

    void start() {
        acceptor_.async_accept(beast::get_lowest_layer(socket_).socket(),
                               [self = shared_from_this()](beast::error_code error) {
                                   if (!self->check(error, "accept")) {
                                       return;
                                   }
                                   self->socket_.next_layer().async_handshake(
                                       ssl::stream_base::server,
                                       [self](beast::error_code handshake_error) {
                                           if (!self->check(handshake_error, "TLS handshake")) {
                                               return;
                                           }
                                           self->socket_.async_accept([self](beast::error_code websocket_error) {
                                               if (self->check(websocket_error, "WebSocket handshake")) {
                                                   self->read_subscription();
                                               }
                                           });
                                       });
                               });
    }

    void stop() {
        beast::error_code ignored;
        acceptor_.cancel(ignored);
        acceptor_.close(ignored);
        auto &transport = beast::get_lowest_layer(socket_).socket();
        transport.cancel(ignored);
        transport.close(ignored);
    }

    std::string subscription;
    std::optional<std::string> failure;
    bool saw_graceful_close{};

  private:
    bool check(beast::error_code error, std::string_view stage) {
        if (error) {
            failure = std::string(stage) + ": " + error.message();
            return false;
        }
        return true;
    }

    void read_subscription() {
        socket_.async_read(buffer_, [self = shared_from_this()](beast::error_code error, std::size_t) {
            if (!self->check(error, "subscription read")) {
                return;
            }
            self->subscription = beast::buffers_to_string(self->buffer_.data());
            self->buffer_.consume(self->buffer_.size());
            self->socket_.text(self->reply_kind_ == ReplyKind::Text);
            self->socket_.async_write(
                net::buffer(kTickerMessage.data(), kTickerMessage.size()),
                [self](beast::error_code write_error, std::size_t) {
                    if (!self->check(write_error, "ticker write")) {
                        return;
                    }
                    self->socket_.async_read(self->buffer_, [self](beast::error_code close_error, std::size_t) {
                        if (close_error == websocket::error::closed) {
                            self->saw_graceful_close = true;
                        }
                        else {
                            static_cast<void>(self->check(close_error, "close read"));
                        }
                    });
                });
        });
    }

    ssl::context tls_;
    Tcp::acceptor acceptor_;
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> socket_;
    beast::flat_buffer buffer_;
    ReplyKind reply_kind_;
};

/** Supply a test-only OpenSSL trust store, restoring the process environment.
 * CTest runs these cases in separate processes; no application CA override exists.
 */
class TestTrustStore {
  public:
    explicit TestTrustStore(const std::filesystem::path &certificate) {
        if (const char *original = std::getenv(variable))
            previous_ = original;
        EXPECT_EQ(::setenv(variable, certificate.c_str(), 1), 0);
    }
    ~TestTrustStore() {
        if (previous_) {
            EXPECT_EQ(::setenv(variable, previous_->c_str(), 1), 0);
        }
        else {
            EXPECT_EQ(::unsetenv(variable), 0);
        }
    }
    TestTrustStore(const TestTrustStore &) = delete;
    TestTrustStore &operator=(const TestTrustStore &) = delete;

  private:
    static constexpr auto variable = "SSL_CERT_FILE";
    std::optional<std::string> previous_;
};

class TickerFeedTest : public testing::Test {
  protected:
    void connect_to_exchange(bool trust_fixture = true, ReplyKind reply_kind = ReplyKind::Text) {
        exchange_ = std::make_shared<LoopbackExchange>(io_, reply_kind);
        FeedConfig config;
        config.host = "127.0.0.1";
        config.port = exchange_->port();
        config.connect_timeout = kConnectionTimeout;
        config.close_timeout = kCloseTimeout;
        if (trust_fixture) {
            trust_.emplace(fixture_path("test_tls_cert.pem"));
        }
        ASSERT_NO_FATAL_FAILURE(create_feed(std::move(config)));
        exchange_->start();
        ASSERT_RESULT_OK(feed_->start());
    }

    void create_feed(FeedConfig config) {
        FeedCallbacks callbacks;
        callbacks.connected = [this]() -> Result<void> {
            ++connected_count_;
            if (connected_error_)
                return std::unexpected(*connected_error_);
            if (stop_on_connected_)
                feed_->stop();
            return {};
        };
        callbacks.message = [this](std::string_view message) -> Result<void> {
            messages_.emplace_back(message);
            if (message_error_)
                return std::unexpected(*message_error_);
            feed_->stop();
            return {};
        };
        callbacks.stopped = [this](Result<void> result) {
            ++stopped_count_;
            if (!result)
                stopped_error_ = std::move(result.error());
        };
        ASSERT_RESULT_VALUE(feed, TickerFeed::create(io_, std::move(config), kSymbols, std::move(callbacks)));
        feed_ = std::move(feed);
    }

    void run_bounded() {
        io_.run_for(kTestDeadline);
        const bool drained = io_.stopped();
        if (!drained) {
            if (exchange_) {
                exchange_->stop();
            }
            feed_->stop();
            io_.stop();
        }
        ASSERT_TRUE(drained) << "Loopback feed did not complete within its test deadline";
    }

    std::optional<TestTrustStore> trust_;
    net::io_context io_;
    std::shared_ptr<LoopbackExchange> exchange_;
    std::unique_ptr<TickerFeed> feed_;
    std::size_t connected_count_{};
    std::size_t stopped_count_{};
    std::vector<std::string> messages_;
    std::optional<Error> stopped_error_;
    std::optional<Error> message_error_;
    std::optional<Error> connected_error_;
    bool stop_on_connected_{};
};

TEST_F(TickerFeedTest, SubscribesReceivesTextAndClosesGracefully) {
    ASSERT_NO_FATAL_FAILURE(connect_to_exchange());
    run_bounded();

    ASSERT_EQ(connected_count_, 1);
    ASSERT_EQ(messages_, std::vector<std::string>{std::string(kTickerMessage)});
    const nlohmann::json expected_subscription{{"type", "subscribe"},
                                               {"product_ids", kSymbols},
                                               {"channels", {"ticker"}}};
    EXPECT_EQ(nlohmann::json::parse(exchange_->subscription), expected_subscription);
    EXPECT_TRUE(exchange_->saw_graceful_close);
    EXPECT_FALSE(exchange_->failure);
    EXPECT_EQ(stopped_count_, 1);
    EXPECT_FALSE(stopped_error_);
    feed_->stop();
    EXPECT_EQ(stopped_count_, 1);
}

TEST_F(TickerFeedTest, RejectsAnUntrustedServerCertificate) {
    ASSERT_NO_FATAL_FAILURE(connect_to_exchange(false));
    run_bounded();
    EXPECT_EQ(connected_count_, 0);
    EXPECT_TRUE(messages_.empty());
    EXPECT_TRUE(exchange_->subscription.empty());
    EXPECT_EQ(stopped_count_, 1);
    ASSERT_TRUE(stopped_error_);
    EXPECT_NE(stopped_error_->message.find("TLS handshake"), std::string::npos);
    EXPECT_NE(stopped_error_->message.find("certificate verify failed"), std::string::npos);
}

TEST_F(TickerFeedTest, RejectsBinaryFramesWithoutDeliveringThem) {
    ASSERT_NO_FATAL_FAILURE(connect_to_exchange(true, ReplyKind::Binary));
    run_bounded();
    EXPECT_EQ(connected_count_, 1);
    EXPECT_TRUE(messages_.empty());
    EXPECT_EQ(stopped_count_, 1);
    ASSERT_TRUE(stopped_error_);
    EXPECT_EQ(stopped_error_->code, ErrorCode::Protocol);
    EXPECT_EQ(stopped_error_->message, "expected a text WebSocket message");
}

TEST_F(TickerFeedTest, StopBeforeStartIsIdempotentAndReportsCompletionOnce) {
    FeedConfig config;
    config.host = "127.0.0.1";
    create_feed(config);
    feed_->stop();
    feed_->stop();
    run_bounded();
    EXPECT_EQ(connected_count_, 0);
    EXPECT_TRUE(messages_.empty());
    EXPECT_EQ(stopped_count_, 1);
    EXPECT_FALSE(stopped_error_);
    ASSERT_RESULT_ERROR(feed_->start(), ErrorCode::InvalidState);
}

TEST_F(TickerFeedTest, PreservesTypedCallbackFailureAndStopsOnce) {
    message_error_ = Error{ErrorCode::OutOfRange, "test numeric overflow"};
    connect_to_exchange();
    run_bounded();
    ASSERT_TRUE(stopped_error_);
    EXPECT_EQ(stopped_error_->code, ErrorCode::OutOfRange);
    EXPECT_EQ(stopped_error_->message, "test numeric overflow");
    EXPECT_EQ(stopped_count_, 1U);
}

TEST_F(TickerFeedTest, ConnectedCallbackCanStopWithoutStartingARead) {
    stop_on_connected_ = true;
    connect_to_exchange();
    run_bounded();
    EXPECT_EQ(connected_count_, 1U);
    EXPECT_TRUE(messages_.empty());
    EXPECT_EQ(stopped_count_, 1U);
    EXPECT_FALSE(stopped_error_);
}

TEST_F(TickerFeedTest, PreservesConnectedCallbackFailure) {
    connected_error_ = Error{ErrorCode::OutputIo, "test connected callback failure"};
    connect_to_exchange();
    run_bounded();
    EXPECT_EQ(connected_count_, 1U);
    EXPECT_TRUE(messages_.empty());
    EXPECT_EQ(stopped_count_, 1U);
    ASSERT_TRUE(stopped_error_);
    EXPECT_EQ(stopped_error_->code, ErrorCode::OutputIo);
    EXPECT_EQ(stopped_error_->message, "test connected callback failure");
}

TEST_F(TickerFeedTest, ConnectionDeadlineStopsAStalledTlsHandshake) {
    Tcp::acceptor acceptor(io_, Tcp::endpoint(net::ip::address_v4::loopback(), 0));
    Tcp::socket pending_tls(io_);
    acceptor.async_accept(pending_tls, [](beast::error_code error) {
        EXPECT_FALSE(error);
    });
    FeedConfig config;
    config.host = "127.0.0.1";
    config.port = std::to_string(acceptor.local_endpoint().port());
    config.connect_timeout = std::chrono::seconds{1};
    create_feed(config);
    ASSERT_RESULT_OK(feed_->start());
    run_bounded();
    EXPECT_EQ(connected_count_, 0U);
    EXPECT_EQ(stopped_count_, 1U);
    ASSERT_TRUE(stopped_error_);
    EXPECT_EQ(stopped_error_->code, ErrorCode::Transport);
    EXPECT_EQ(stopped_error_->message, "connection/subscription deadline exceeded");
    feed_->stop();
    EXPECT_EQ(stopped_count_, 1U);
}

TEST_F(TickerFeedTest, FactoryRejectsMissingCallbacks) {
    ASSERT_RESULT_ERROR(TickerFeed::create(io_, {}, kSymbols, {}), ErrorCode::InvalidConfiguration);
}

} // namespace
} // namespace coinbase_ticker_statistics
