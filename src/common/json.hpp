#pragma once

#include "result.hpp"

#include <glaze/json.hpp>

#include <string_view>
#include <utility>

namespace coinbase_ticker_statistics::json_utils {

// Validate unused values and reject non-whitespace after the root document.
// Beast supplies length-delimited frames, so parsing must not require a null terminator.
struct ReadOptions : glz::opts {
    bool validate_skipped = true;
    bool validate_trailing_whitespace = true;
};
inline constexpr auto read_options = [] {
    ReadOptions options;
    options.null_terminated = false;
    options.error_on_unknown_keys = false;
    options.error_on_missing_keys = true;
    return options;
}();

// Keep Glaze diagnostics behind the project's Result/ErrorCode boundary;
// callers receive a complete value or an error, never a partially decoded object.
template <typename T>
Result<T> read_json(std::string_view input, ErrorCode category = ErrorCode::InvalidInput) {
    T value{};
    glz::context context;
    if (const auto error = glz::read<read_options>(value, input, context); error) {
        if (category == ErrorCode::InvalidInput && error.ec == glz::error_code::constraint_violated)
            category = ErrorCode::OutOfRange;
        return fail(category, glz::format_error(error, input));
    }
    return value;
}

// Carry domain-parser diagnostics through Glaze until read_json translates them.
// The context owns the message so its diagnostic view remains valid during parsing.
template <typename T>
void assign_parsed(T &destination, Result<T> parsed, glz::context &context) {
    if (parsed) {
        destination = std::move(*parsed);
        return;
    }
    context.error = parsed.error().code == ErrorCode::OutOfRange ? glz::error_code::constraint_violated
                                                                 : glz::error_code::syntax_error;
    context.scratch = std::move(parsed.error().message);
    context.custom_error_message = context.scratch;
}

} // namespace coinbase_ticker_statistics::json_utils
