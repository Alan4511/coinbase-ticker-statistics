#include "feed/parser/ticker_parser.hpp"
#include "feed/json_meta.hpp"

namespace coinbase_ticker_statistics {

Result<std::optional<TickerUpdate>> parse_ticker_message(std::string_view message) {
    using namespace feed_json;
    auto header = json_utils::read_json<MessageHeader>(message);
    if (!header.has_value())
        return std::unexpected(std::move(header.error()));
    if (header.value().type.empty())
        return fail(ErrorCode::InvalidInput, "type: expected a nonempty string");
    if (header.value().type == key::error) {
        auto description = json_utils::read_json<std::string>(header.value().message.str);
        return fail(ErrorCode::Protocol, "Coinbase feed error: " + description.value_or("unspecified exchange error"));
    }
    if (header.value().type != key::ticker)
        return std::nullopt;
    return json_utils::read_json<TickerUpdate>(message).and_then(
        [](TickerUpdate update) -> Result<std::optional<TickerUpdate>> {
            if (update.symbol.empty())
                return fail(ErrorCode::InvalidInput, "product_id: expected a nonempty string");
            return std::optional<TickerUpdate>{std::move(update)};
        });
}

} // namespace coinbase_ticker_statistics
