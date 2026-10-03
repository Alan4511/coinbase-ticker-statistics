#include "feed/transport/ticker_feed.hpp"
#include "feed/subscription.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <openssl/ssl.h>

#include <boost/system/system_error.hpp>
#include <chrono>
#include <utility>

namespace coinbase_ticker_statistics {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace ssl = net::ssl;
namespace websocket = beast::websocket;
using Tcp = net::ip::tcp;

class TickerFeed::Session : public std::enable_shared_from_this<TickerFeed::Session> {
  public:
    Session(net::io_context &io, FeedConfig config, std::string subscription, FeedCallbacks callbacks)
        : config_(std::move(config)), callbacks_(std::move(callbacks)), tls_(ssl::context::tls_client), resolver_(io),
          socket_(io, tls_), deadline_(io), subscription_(std::move(subscription)) {
    }

    Result<void> configure() {
        // Prefer the library's error_code APIs; do not unwind on trust/configuration errors.
        beast::error_code error;
        tls_.set_default_verify_paths(error);
        if (error) {
            return fail(ErrorCode::Transport, "TLS default trust paths: " + error.message());
        }
        socket_.next_layer().set_verify_mode(ssl::verify_peer, error);
        if (error) {
            return fail(ErrorCode::Transport, "TLS verification mode: " + error.message());
        }
        socket_.next_layer().set_verify_callback(ssl::host_name_verification(config_.host), error);
        if (error) {
            return fail(ErrorCode::Transport, "TLS hostname verification: " + error.message());
        }
        if (SSL_set_tlsext_host_name(socket_.next_layer().native_handle(), config_.host.c_str()) != 1) {
            return fail(ErrorCode::Transport, "cannot configure TLS server name");
        }
        if (SSL_set_min_proto_version(socket_.next_layer().native_handle(), TLS1_2_VERSION) != 1) {
            return fail(ErrorCode::Transport, "cannot configure minimum TLS version");
        }
        socket_.read_message_max(config_.max_message_bytes);
        // A connection deadline is distinct from the out-of-scope feed watchdog.
        websocket::stream_base::timeout timeouts;
        timeouts.handshake_timeout = config_.connect_timeout;
        timeouts.idle_timeout = websocket::stream_base::none();
        timeouts.keep_alive_pings = false;
        socket_.set_option(timeouts);
        socket_.text(true);
        return {};
    }

    Result<void> start() {
        if (state_ != State::Created) {
            return fail(ErrorCode::InvalidState, "a feed can only be started once");
        }
        state_ = State::Connecting;
        arm_deadline(config_.connect_timeout, "connection/subscription deadline exceeded");
        const auto connect_resolved_endpoints = [self = shared_from_this()](beast::error_code error,
                                                                            Tcp::resolver::results_type endpoints) {
            if (self->check_transport_result(error, "DNS resolution"))
                beast::get_lowest_layer(self->socket_)
                    .async_connect(endpoints, [self](beast::error_code connect_error, const Tcp::endpoint &) {
                        if (self->check_transport_result(connect_error, "TCP connect"))
                            self->on_tcp_connected();
                    });
        };
        resolver_.async_resolve(config_.host, config_.port, connect_resolved_endpoints);
        return {};
    }

    void stop() {
        if (state_ == State::Stopped || state_ == State::Closing) {
            return;
        }
        if (state_ != State::Reading) {
            // Never overlap async_close with an outstanding subscription write.
            complete_connection({});
            return;
        }
        state_ = State::Closing;
        arm_deadline(config_.close_timeout, "WebSocket close deadline exceeded");
        const auto complete_close = [self = shared_from_this()](beast::error_code error) {
            if (self->state_ == State::Stopped)
                return;
            if (error && error != websocket::error::closed)
                self->complete_connection(fail(ErrorCode::Transport, "WebSocket close: " + error.message()));
            else
                self->complete_connection({});
        };
        socket_.async_close(websocket::close_code::normal, complete_close);
    }

  private:
    enum class State {
        Created,
        Connecting,
        Subscribing,
        Reading,
        Closing,
        Stopped
    };

    bool check_transport_result(beast::error_code error, std::string_view operation) {
        if (state_ == State::Stopped) {
            return false;
        }
        if (error) {
            complete_connection(fail(ErrorCode::Transport, std::string(operation) + ": " + error.message()));
            return false;
        }
        return true;
    }

    void arm_deadline(Duration timeout, std::string_view failure_message) {
        static_cast<void>(deadline_.expires_after(timeout));
        // Both messages are static string literals; captures cannot dangle while waiting.
        const auto fail_on_timeout = [self = shared_from_this(), failure_message](beast::error_code error) {
            if (!error)
                self->complete_connection(fail(ErrorCode::Transport, std::string(failure_message)));
        };
        deadline_.async_wait(fail_on_timeout);
    }

    void on_tcp_connected() {
        beast::error_code option_error;
        beast::get_lowest_layer(socket_).socket().set_option(Tcp::no_delay(true), option_error);
        if (!check_transport_result(option_error, "TCP_NODELAY"))
            return;
        const auto complete_tls_handshake = [self = shared_from_this()](beast::error_code error) {
            if (self->check_transport_result(error, "TLS handshake"))
                self->on_tls_handshake_completed();
        };
        socket_.next_layer().async_handshake(ssl::stream_base::client, complete_tls_handshake);
    }

    void on_tls_handshake_completed() {
        const auto complete_websocket_handshake = [self = shared_from_this()](beast::error_code error) {
            if (self->check_transport_result(error, "WebSocket handshake"))
                self->on_websocket_handshake_completed();
        };
        socket_.async_handshake(config_.host + ':' + config_.port, config_.target, complete_websocket_handshake);
    }

    void on_websocket_handshake_completed() {
        state_ = State::Subscribing;
        // The owned string remains alive until async_write completes.
        const auto complete_subscription_write = [self = shared_from_this()](beast::error_code error, std::size_t) {
            if (self->check_transport_result(error, "subscription write"))
                self->on_subscription_written();
        };
        socket_.async_write(net::buffer(subscription_), complete_subscription_write);
    }

    void on_subscription_written() {
        static_cast<void>(deadline_.cancel());
        state_ = State::Reading;
        auto connected = callbacks_.connected();
        if (!connected) {
            complete_connection(std::move(connected));
            return;
        }
        // User callbacks can synchronously request stop().
        if (state_ == State::Reading)
            read_next_message();
    }

    void read_next_message() {
        const auto deliver_text_message = [self = shared_from_this()](beast::error_code error, std::size_t) {
            if (self->state_ == State::Closing || self->state_ == State::Stopped) {
                return;
            }
            if (!self->check_transport_result(error, "WebSocket read")) {
                return;
            }
            if (!self->socket_.got_text()) {
                self->complete_connection(fail(ErrorCode::Protocol, "expected a text WebSocket message"));
                return;
            }
            const auto bytes = self->buffer_.data();
            const std::string_view message(static_cast<const char *>(bytes.data()), bytes.size());
            auto delivered = self->callbacks_.message(message);
            if (!delivered) {
                self->complete_connection(std::move(delivered));
                return;
            }
            self->buffer_.consume(self->buffer_.size());
            if (self->state_ == State::Reading) {
                self->read_next_message();
            }
        };
        socket_.async_read(buffer_, deliver_text_message);
    }

    void complete_connection(Result<void> result) {
        if (state_ == State::Stopped) {
            return;
        }
        state_ = State::Stopped;
        resolver_.cancel();
        static_cast<void>(deadline_.cancel());
        beast::error_code ignored;
        auto &transport = beast::get_lowest_layer(socket_).socket();
        transport.cancel(ignored);
        transport.close(ignored);
        callbacks_.stopped(std::move(result));
    }

    FeedConfig config_;
    FeedCallbacks callbacks_;
    ssl::context tls_;
    Tcp::resolver resolver_;
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> socket_;
    net::steady_timer deadline_;
    beast::flat_buffer buffer_;
    std::string subscription_;
    State state_{State::Created};
};

Result<std::unique_ptr<TickerFeed>>
TickerFeed::create(net::io_context &io, FeedConfig config, Symbols symbols, FeedCallbacks callbacks) {
    if (!callbacks.message || !callbacks.connected || !callbacks.stopped) {
        return fail(ErrorCode::InvalidConfiguration, "all feed callbacks are required");
    }
    if (config.host.empty() || config.port.empty() || config.target.empty() ||
        config.connect_timeout <= std::chrono::seconds::zero() ||
        config.close_timeout <= std::chrono::seconds::zero() || config.max_message_bytes == 0) {
        return fail(ErrorCode::InvalidConfiguration, "feed endpoint and positive resource bounds are required");
    }
    auto subscription = make_subscription(symbols);
    if (!subscription) {
        return std::unexpected(std::move(subscription.error()));
    }
    // Asio's SSL context/stream constructors have no error_code overload. Keep
    // their system_error conversion at this boundary, not in normal event flow.
    try {
        auto session = std::make_shared<Session>(io, std::move(config), std::move(*subscription), std::move(callbacks));
        auto configured = session->configure();
        if (!configured) {
            return std::unexpected(std::move(configured.error()));
        }
        // make_unique cannot access this private constructor; ownership is immediate.
        return std::unique_ptr<TickerFeed>(new TickerFeed(std::move(session)));
    } catch (const boost::system::system_error &error) {
        return fail(ErrorCode::Transport, "feed initialization: " + std::string(error.what()));
    }
}

TickerFeed::TickerFeed(std::shared_ptr<Session> session) : session_(std::move(session)) {
}

TickerFeed::~TickerFeed() = default;

Result<void> TickerFeed::start() {
    // Timer/async initiation APIs can throw system_error even though their
    // eventual completions report error_code. Allocation failures remain fatal.
    try {
        return session_->start();
    } catch (const boost::system::system_error &error) {
        return fail(ErrorCode::Transport, "feed start: " + std::string(error.what()));
    }
}

void TickerFeed::stop() {
    session_->stop();
}

} // namespace coinbase_ticker_statistics
