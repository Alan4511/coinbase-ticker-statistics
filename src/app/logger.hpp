#pragma once

#include "output/format_fields.hpp"

#include <chrono>
#include <exception>
#include <locale>
#include <ostream>
#include <sstream>
#include <string>

namespace coinbase_ticker_statistics {

enum class LogLevel {
    Info,
    Error
};

/** Small, synchronous lifecycle logger. Diagnostic failures never replace application errors. */
template <typename... Parts>
void log_message(std::ostream &stream, LogLevel level, const Parts &...parts) noexcept {
    try {
        const auto now = std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now());
        const auto timestamp = format_utc_timestamp(now);
        if (!timestamp)
            return;
        std::ostringstream message;
        message.imbue(std::locale::classic());
        (message << ... << parts);
        std::string line = *timestamp + (level == LogLevel::Info ? " INFO " : " ERROR ");
        // Exchange errors and paths may contain line breaks; keep each record on one line.
        for (const char character : message.str()) {
            if (character == '\n')
                line += "\\n";
            else if (character == '\r')
                line += "\\r";
            else
                line += character;
        }
        stream << line << '\n';
        stream.flush();
    } catch (const std::exception &) {
        // Best effort: reporting through the same failed destination cannot help.
    }
}

} // namespace coinbase_ticker_statistics
