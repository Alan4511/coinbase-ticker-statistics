#pragma once

#include "test_files.hpp"

#include <common/types.hpp>
#include <feed/transport/feed_config.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <glaze/json.hpp>

#include <format>

#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace coinbase_ticker_statistics::test {

inline std::filesystem::path fixture_path(const char *name) {
    return std::filesystem::path(COINBASE_TICKER_STATISTICS_SOURCE_DIR) / "data" / name;
}

/** Trust only the fixture through the standard OpenSSL environment setting. */
class TestTrustStore {
  public:
    explicit TestTrustStore(const std::filesystem::path &certificate = fixture_path("test_tls_cert.pem")) {
        if (const char *original = std::getenv(variable))
            previous_ = original;
        if (::setenv(variable, certificate.c_str(), 1) != 0)
            throw std::runtime_error("cannot set test trust store");
    }
    ~TestTrustStore() {
        if (previous_.has_value())
            ::setenv(variable, previous_.value().c_str(), 1);
        else
            ::unsetenv(variable);
    }
    TestTrustStore(const TestTrustStore &) = delete;
    TestTrustStore &operator=(const TestTrustStore &) = delete;

  private:
    static constexpr auto variable = "SSL_CERT_FILE";
    std::optional<std::string> previous_;
};

enum class ExchangeReply {
    Text,
    Binary,
    RemainIdle,
    WaitForClientClose,
    StallTls
};

/** An isolated local exchange. A five-second alarm bounds every server operation,
 * and closes its socket even when the client fails to make progress.
 */
class LoopbackExchange {
  public:
    LoopbackExchange(const Symbols &symbols,
                     const std::vector<std::string> &messages,
                     ExchangeReply reply = ExchangeReply::Text,
                     const std::filesystem::path &accepted_path = {})
        : LoopbackExchange(std::format(R"({{"type":"subscribe","product_ids":{},"channels":["ticker"]}})",
                                       glz::write_json(symbols).value()),
                           messages,
                           reply,
                           accepted_path) {
    }

    LoopbackExchange(std::string expected_subscription,
                     const std::vector<std::string> &messages,
                     ExchangeReply reply = ExchangeReply::Text,
                     const std::filesystem::path &accepted_path = {}) {
        namespace net = boost::asio;
        namespace beast = boost::beast;
        using Tcp = net::ip::tcp;
        net::io_context io;
        Tcp::acceptor acceptor(io, Tcp::endpoint(net::ip::address_v4::loopback(), 0));
        config_.host = "127.0.0.1";
        config_.port = std::to_string(acceptor.local_endpoint().port());
        pid_ = ::fork();
        if (pid_ < 0)
            throw std::runtime_error("cannot fork local exchange");
        if (pid_ != 0)
            return;
        ::alarm(5);
        try {
            net::ssl::context tls(net::ssl::context::tls_server);
            tls.use_certificate_chain_file(fixture_path("test_tls_cert.pem").string());
            tls.use_private_key_file(fixture_path("test_tls_key.pem").string(), net::ssl::context::pem);
            beast::websocket::stream<beast::ssl_stream<Tcp::socket>> socket(io, tls);
            acceptor.accept(beast::get_lowest_layer(socket));
            acceptor.close();
            if (!accepted_path.empty())
                write_file(accepted_path, "TCP accepted\n");
            if (reply == ExchangeReply::StallTls)
                for (;;)
                    ::pause();
            socket.next_layer().handshake(net::ssl::stream_base::server);
            socket.accept();
            beast::flat_buffer buffer;
            socket.read(buffer);
            if (beast::buffers_to_string(buffer.data()) != expected_subscription)
                ::_exit(2);
            socket.text(reply != ExchangeReply::Binary);
            for (const auto &message : messages)
                socket.write(net::buffer(message));
            if (reply == ExchangeReply::RemainIdle)
                for (;;)
                    ::pause();
            if (reply == ExchangeReply::WaitForClientClose) {
                beast::error_code error;
                buffer.consume(buffer.size());
                socket.read(buffer, error);
                ::_exit(error == beast::websocket::error::closed ? 0 : 3);
            }
            socket.close(beast::websocket::close_code::normal);
            ::_exit(0);
        } catch (const std::exception &) {
            ::_exit(1);
        }
    }
    ~LoopbackExchange() {
        if (!status_.has_value()) {
            ::kill(pid_, SIGKILL);
            int status{};
            ::waitpid(pid_, &status, 0);
        }
    }
    LoopbackExchange(const LoopbackExchange &) = delete;
    LoopbackExchange &operator=(const LoopbackExchange &) = delete;

    FeedConfig config() const {
        return config_;
    }
    bool completed_successfully() {
        if (!status_.has_value()) {
            int status{};
            if (::waitpid(pid_, &status, 0) != pid_)
                return false;
            status_ = status;
        }
        return WIFEXITED(status_.value()) && WEXITSTATUS(status_.value()) == 0;
    }

  private:
    FeedConfig config_;
    pid_t pid_{};
    std::optional<int> status_;
};

} // namespace coinbase_ticker_statistics::test
