#pragma once

#include <glaze/json.hpp>

#include <map>
#include <string>

namespace coinbase_ticker_statistics::test {

// Keep fixture numeric spellings (1.0, exponents, uint64 maxima) intact when editing fields.
using JsonFields = std::map<std::string, glz::raw_json, std::less<>>;

} // namespace coinbase_ticker_statistics::test
