#include "feed/subscription.hpp"
#include "feed/json_meta.hpp"

#include <glaze/json.hpp>

#include <algorithm>
#include <array>
#include <string_view>

namespace coinbase_ticker_statistics {
namespace {

bool is_valid_product_id(std::string_view symbol) {
    const auto is_product_character = [](char character) {
        return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '-';
    };
    return !symbol.empty() && symbol.front() != '-' && symbol.back() != '-' && symbol.contains('-') &&
           !symbol.contains("--") && std::ranges::all_of(symbol, is_product_character);
}

} // namespace

Result<void> validate(const Symbols &symbols) {
    if (symbols.empty()) {
        return fail(ErrorCode::InvalidConfiguration, "ticker subscription requires at least one product ID");
    }
    // Product IDs are protocol identifiers, not arbitrary text. This also guarantees JSON encoding
    // cannot encounter invalid UTF-8 while serializing a caller-provided subscription.
    for (const auto &symbol : symbols) {
        if (!is_valid_product_id(symbol))
            return fail(ErrorCode::InvalidConfiguration, "invalid ticker product ID: " + symbol);
    }
    return {};
}

Result<std::string> encode_ticker_subscription(const Symbols &symbols) {
    if (auto valid = validate(symbols); !valid.has_value())
        return std::unexpected(valid.error());
    using namespace feed_json;
    const SubscriptionRequest request{key::subscribe, symbols, std::array{std::string_view{key::ticker}}};
    auto encoded = glz::write_json(request);
    if (!encoded.has_value())
        return fail(ErrorCode::Protocol, "cannot encode ticker subscription: " + glz::format_error(encoded.error()));
    return std::move(encoded.value());
}

} // namespace coinbase_ticker_statistics
