#pragma once

#include <expected>
#include <string>
#include <utility>

namespace coinbase_ticker_statistics {

/** Stable error categories; callers branch on codes, never on diagnostic text. */
enum class ErrorCode {
    InvalidConfiguration,
    InvalidInput,
    OutOfRange,
    OutOfOrderTimestamp,
    FileIo,
    OutputIo,
    Protocol,
    Transport,
    InvalidState,
    UnexpectedFailure
};

/** An operational failure with a typed category and human-readable context. */
struct Error {
    ErrorCode code;
    std::string message;
};

/** Recoverable operations return errors explicitly; allocation failure may still throw. */
template <typename T>
using Result = std::expected<T, Error>;

/** Construct a failure that can be returned from any Result specialization. */
[[nodiscard]] inline std::unexpected<Error> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

} // namespace coinbase_ticker_statistics
