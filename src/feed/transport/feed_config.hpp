#pragma once

#include <result.hpp>

#include <chrono>
#include <cstddef>
#include <string>

namespace coinbase_ticker_statistics {

/** Feed connection settings. Certificate and hostname verification are always enabled. */
struct FeedConfig {
    std::string host{"ws-feed.exchange.coinbase.com"};
    std::string port{"443"};
    std::string target{"/"};
    std::chrono::seconds connect_timeout{15};
    std::chrono::seconds close_timeout{5};
    std::size_t max_message_bytes{1'048'576};
};

/** Validate feed-owned policy for both JSON loading and direct construction. */
[[nodiscard]] inline Result<void> validate_feed_config(const FeedConfig &config) {
    if (config.host.empty() || config.port.empty())
        return fail(ErrorCode::InvalidConfiguration, "feed host and port must be nonempty");
    if (config.target.empty() || config.target.front() != '/')
        return fail(ErrorCode::InvalidConfiguration, "feed target must start with /");
    if (config.max_message_bytes == 0)
        return fail(ErrorCode::InvalidConfiguration, "feed message size must be positive");
    constexpr auto maximum_timeout = std::chrono::days{365};
    if (config.connect_timeout <= std::chrono::seconds::zero() || config.connect_timeout > maximum_timeout ||
        config.close_timeout <= std::chrono::seconds::zero() || config.close_timeout > maximum_timeout)
        return fail(ErrorCode::InvalidConfiguration, "feed deadlines must be between 1 second and 365 days");
    return {};
}

} // namespace coinbase_ticker_statistics
