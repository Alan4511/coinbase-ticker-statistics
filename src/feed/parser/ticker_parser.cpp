#include "feed/parser/ticker_parser.hpp"
#include "feed/parser/parse_fields.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>

namespace coinbase_ticker_statistics {
namespace {

using Json = nlohmann::json;

namespace protocol {
constexpr auto type = "type";
constexpr auto ticker = "ticker";
constexpr auto error = "error";
constexpr auto message = "message";
constexpr auto time = "time";
constexpr auto product_id = "product_id";
constexpr auto trade_id = "trade_id";
constexpr auto price = "price";
} // namespace protocol

Result<std::string_view> read_string_field(const Json &document, const char *name) {
    const auto field = document.find(name);
    if (field == document.end() || !field->is_string() || field->get_ref<const std::string &>().empty()) {
        return fail(ErrorCode::InvalidInput, std::string("ticker field '") + name + "' must be a nonempty string");
    }
    return field->get_ref<const std::string &>();
}

Result<TradeId> read_trade_id(const Json &document) {
    const auto field = document.find(protocol::trade_id);
    if (field == document.end()) {
        return fail(ErrorCode::InvalidInput, "ticker is missing trade_id");
    }
    if (field->is_number_unsigned()) {
        return field->get<TradeId>();
    }
    if (field->is_number_integer()) {
        const auto signed_id = field->get<std::int64_t>();
        if (signed_id >= 0) {
            return static_cast<TradeId>(signed_id);
        }
    }
    return fail(ErrorCode::InvalidInput, "ticker trade_id must be a nonnegative 64-bit integer");
}

} // namespace

Result<std::optional<Trade>> parse_ticker_message(std::string_view message) {
    // A readable DOM decoder costs allocations and throughput versus SAX/simdjson. Keeping it
    // behind this boundary lets the assignment favor auditability and replace it after profiling.
    // Discarded JSON is a normal validation result; malformed network data never uses exceptions.
    const auto document = Json::parse(message, nullptr, false);
    if (document.is_discarded()) {
        return fail(ErrorCode::InvalidInput, "invalid feed JSON");
    }
    if (!document.is_object()) {
        return fail(ErrorCode::InvalidInput, "feed message must be a JSON object");
    }
    const auto message_type = read_string_field(document, protocol::type);
    if (!message_type) {
        return std::unexpected(message_type.error());
    }
    if (*message_type == protocol::error) {
        const auto description = document.find(protocol::message);
        const std::string details = description != document.end() && description->is_string()
                                        ? description->get<std::string>()
                                        : "unspecified exchange error";
        return fail(ErrorCode::Protocol, "Coinbase feed error: " + details);
    }
    if (*message_type != protocol::ticker) {
        return std::nullopt;
    }
    const auto symbol = read_string_field(document, protocol::product_id);
    if (!symbol) {
        return std::unexpected(symbol.error());
    }
    const auto exchange_time = read_string_field(document, protocol::time).and_then(parse_utc_timestamp);
    if (!exchange_time) {
        return std::unexpected(exchange_time.error());
    }
    const auto price = read_string_field(document, protocol::price).and_then(parse_price);
    if (!price) {
        return std::unexpected(price.error());
    }
    const auto trade_id = read_trade_id(document);
    if (!trade_id) {
        return std::unexpected(trade_id.error());
    }
    return Trade{*exchange_time, std::string(*symbol), *trade_id, *price};
}

} // namespace coinbase_ticker_statistics
