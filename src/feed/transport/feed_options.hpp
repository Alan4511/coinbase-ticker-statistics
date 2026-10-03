#pragma once

#include <chrono>
#include <cstddef>
#include <string>

namespace coinbase_ticker_statistics {

/** Connection settings. Certificate and hostname verification are always enabled. */
struct FeedConfig {
    std::string host{"ws-feed.exchange.coinbase.com"};
    std::string port{"443"};
    std::string target{"/"};
    std::chrono::seconds connect_timeout{15};
    std::chrono::seconds close_timeout{5};
    std::size_t max_message_bytes{1'048'576};
};

} // namespace coinbase_ticker_statistics
