#include "feed/subscription.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string_view>

namespace coinbase_ticker_statistics {
namespace {
using Json = nlohmann::json;

namespace protocol {
constexpr auto type = "type";
constexpr auto subscribe = "subscribe";
constexpr auto product_ids = "product_ids";
constexpr auto channels = "channels";
constexpr auto ticker = "ticker";
} // namespace protocol

bool is_valid_product_id(std::string_view symbol) {
    const auto is_product_character = [](char character) {
        return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '-';
    };
    return !symbol.empty() && symbol.front() != '-' && symbol.back() != '-' && symbol.contains('-') &&
           !symbol.contains("--") && std::ranges::all_of(symbol, is_product_character);
}

} // namespace

Result<void> validate_subscription(const Symbols &symbols) {
    if (symbols.empty()) {
        return fail(ErrorCode::InvalidConfiguration, "ticker subscription requires at least one product ID");
    }
    // Product IDs are protocol identifiers, not arbitrary text. This also guarantees dump()
    // cannot encounter invalid UTF-8 while serializing a caller-provided subscription.
    for (const auto &symbol : symbols) {
        if (!is_valid_product_id(symbol))
            return fail(ErrorCode::InvalidConfiguration, "invalid ticker product ID: " + symbol);
    }
    return {};
}

Result<std::string> make_subscription(const Symbols &symbols) {
    if (auto valid = validate_subscription(symbols); !valid)
        return std::unexpected(valid.error());
    return Json{{protocol::type, protocol::subscribe},
                {protocol::product_ids, symbols},
                {protocol::channels, Json::array({protocol::ticker})}}
        .dump();
}

} // namespace coinbase_ticker_statistics
