#include "feed/transport/feed_connection.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/system/system_error.hpp>
#include <openssl/ssl.h>

#include <utility>

namespace coinbase_ticker_statistics {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace ssl = net::ssl;
namespace websocket = beast::websocket;
using Tcp = net::ip::tcp;

class FeedConnection::Session {
  public:
    Session(net::io_context &io, FeedConfig config, std::string subscription, Events events)
        : config_(std::move(config)), subscription_(std::move(subscription)), events_(std::move(events)), resolver_(io),
          socket_(io, tls_), deadline_(io) {
    }

    Result<void> configure() {
        tls_.set_default_verify_paths();
        socket_.next_layer().set_verify_mode(ssl::verify_peer);
        socket_.next_layer().set_verify_callback(ssl::host_name_verification(config_.host));
        if (SSL_set_tlsext_host_name(socket_.next_layer().native_handle(), config_.host.c_str()) != 1)
            return fail(ErrorCode::Transport, "cannot configure TLS server name");
        if (SSL_set_min_proto_version(socket_.next_layer().native_handle(), TLS1_2_VERSION) != 1)
            return fail(ErrorCode::Transport, "cannot configure minimum TLS version");
        socket_.read_message_max(config_.max_message_bytes);
        socket_.text(true);
        // Our timer spans DNS through subscription and also bounds shutdown.
        // Idle-feed monitoring and automatic pings remain outside scope.
        return {};
    }

    Result<void> start() {
        if (state_ != State::Created)
            return fail(ErrorCode::InvalidState, "a feed can only be started once");
        try {
            state_ = State::Connecting;
            arm_deadline(config_.connect_timeout, "connection/subscription deadline exceeded");
            const auto on_resolved = [this](beast::error_code error, Tcp::resolver::results_type endpoints) {
                if (!check_io_result(error, "DNS resolution"))
                    return;
                const auto on_tcp_connected = [this](beast::error_code connect_error, const Tcp::endpoint &) {
                    if (check_io_result(connect_error, "TCP connect"))
                        start_tls_handshake();
                };
                net::async_connect(beast::get_lowest_layer(socket_), endpoints, on_tcp_connected);
            };
            resolver_.async_resolve(config_.host, config_.port, on_resolved);
            return {};
        } catch (const boost::system::system_error &error) {
            complete(fail(ErrorCode::Transport, "feed start: " + std::string(error.what())));
            return std::unexpected(result_.error());
        }
    }

    void stop() {
        begin_close({});
    }

  private:
    enum class State {
        Created,
        Connecting,
        Reading,
        Closing,
        Stopped
    };

    /** Stop on an I/O failure; ignore handlers after the session has stopped. */
    bool check_io_result(beast::error_code error, const char *operation) {
        if (state_ == State::Stopped)
            return false;
        if (error) {
            complete(fail(ErrorCode::Transport, std::string(operation) + ": " + error.message()));
            return false;
        }
        return true;
    }

    void arm_deadline(std::chrono::seconds timeout, const char *description) {
        static_cast<void>(deadline_.expires_after(timeout));
        const auto expected_state = state_;
        const auto on_deadline = [this, expected_state, description](beast::error_code error) {
            // A ready handler from an earlier deadline must not end a later phase.
            if (!error && state_ == expected_state)
                complete(fail(ErrorCode::Transport, description));
        };
        deadline_.async_wait(on_deadline);
    }

    void start_tls_handshake() {
        beast::error_code error;
        beast::get_lowest_layer(socket_).set_option(Tcp::no_delay(true), error);
        if (!check_io_result(error, "TCP_NODELAY"))
            return;
        const auto on_tls_handshake = [this](beast::error_code tls_error) {
            if (check_io_result(tls_error, "TLS handshake"))
                start_websocket_handshake();
        };
        socket_.next_layer().async_handshake(ssl::stream_base::client, on_tls_handshake);
    }

    void start_websocket_handshake() {
        const auto on_websocket_handshake = [this](beast::error_code error) {
            if (!check_io_result(error, "WebSocket handshake"))
                return;
            const auto on_subscription_written = [this](beast::error_code write_error, std::size_t) {
                if (!check_io_result(write_error, "subscription write"))
                    return;
                static_cast<void>(deadline_.cancel());
                state_ = State::Reading;
                auto ready = events_.on_connected();
                if (!ready)
                    begin_close(std::move(ready));
                else if (state_ == State::Reading)
                    read_next_message();
            };
            socket_.async_write(net::buffer(subscription_), on_subscription_written);
        };
        socket_.async_handshake(config_.host + ':' + config_.port, config_.target, on_websocket_handshake);
    }

    void read_next_message() {
        const auto on_read = [this](beast::error_code error, std::size_t) {
            if (state_ != State::Reading)
                return;
            if (error == websocket::error::closed) {
                complete({});
                return;
            }
            if (!check_io_result(error, "WebSocket read"))
                return;
            if (!socket_.got_text()) {
                begin_close(fail(ErrorCode::Protocol, "expected a text WebSocket message"));
                return;
            }
            const auto bytes = buffer_.data();
            const std::string_view message(static_cast<const char *>(bytes.data()), bytes.size());
            auto delivered = events_.on_message(message);
            buffer_.consume(buffer_.size());
            if (!delivered)
                begin_close(std::move(delivered));
            else if (state_ == State::Reading)
                read_next_message();
        };
        socket_.async_read(buffer_, on_read);
    }

    void begin_close(Result<void> result) {
        if (state_ == State::Stopped)
            return;
        // A consumer may request stop synchronously before returning its failure.
        if (result_ && !result)
            result_ = std::move(result);
        if (state_ == State::Closing)
            return;
        if (state_ != State::Reading) {
            // No close handshake during DNS/TLS/upgrade/subscription write.
            complete({});
            return;
        }
        try {
            state_ = State::Closing;
            arm_deadline(config_.close_timeout, "WebSocket close deadline exceeded");
            const auto on_closed = [this](beast::error_code error) {
                if (state_ == State::Stopped)
                    return;
                if (error && error != websocket::error::closed)
                    complete(fail(ErrorCode::Transport, "WebSocket close: " + error.message()));
                else
                    complete({});
            };
            socket_.async_close(websocket::close_code::normal, on_closed);
        } catch (const boost::system::system_error &error) {
            complete(fail(ErrorCode::Transport, "WebSocket close: " + std::string(error.what())));
        }
    }

    void complete(Result<void> result) {
        if (state_ == State::Stopped)
            return;
        state_ = State::Stopped;
        if (result_ && !result)
            result_ = std::move(result);
        resolver_.cancel();
        static_cast<void>(deadline_.cancel());
        beast::error_code ignored;
        beast::get_lowest_layer(socket_).close(ignored);
        events_.on_stopped(result_);
    }

    FeedConfig config_;
    std::string subscription_;
    Events events_;
    ssl::context tls_{ssl::context::tls_client};
    Tcp::resolver resolver_;
    websocket::stream<beast::ssl_stream<Tcp::socket>> socket_;
    net::steady_timer deadline_;
    beast::flat_buffer buffer_;
    State state_{State::Created};
    Result<void> result_;
};

Result<std::unique_ptr<FeedConnection>>
FeedConnection::create_session(net::io_context &io, FeedConfig config, std::string subscription, Events events) {
    if (auto valid = validate(config); !valid)
        return std::unexpected(std::move(valid.error()));
    try {
        auto session = std::make_unique<Session>(io, std::move(config), std::move(subscription), std::move(events));
        if (auto configured = session->configure(); !configured)
            return std::unexpected(std::move(configured.error()));
        // make_unique cannot call the private constructor; ownership is immediate.
        return std::unique_ptr<FeedConnection>(new FeedConnection(std::move(session)));
    } catch (const boost::system::system_error &error) {
        return fail(ErrorCode::Transport, "feed initialization: " + std::string(error.what()));
    }
}

FeedConnection::FeedConnection(std::unique_ptr<Session> session) : session_(std::move(session)) {
}
FeedConnection::~FeedConnection() = default;
Result<void> FeedConnection::start() {
    return session_->start();
}
void FeedConnection::stop() {
    session_->stop();
}

} // namespace coinbase_ticker_statistics
