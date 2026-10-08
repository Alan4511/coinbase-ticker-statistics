#pragma once

#include <common/format_timestamp.hpp>

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

/** Borrow one diagnostic stream. INFO and ERROR share it; failures are best effort. */
class Logger {
  public:
    explicit Logger(std::ostream &stream) : stream_(stream) {
    }

    template <typename... Parts>
    void log(LogLevel level, const Parts &...parts) const noexcept {
        try {
            const auto now = std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now());
            const auto timestamp = format_utc_timestamp(now);
            if (!timestamp.has_value())
                return;
            std::ostringstream message;
            message.imbue(std::locale::classic());
            (message << ... << parts);
            std::string line = timestamp.value() + (level == LogLevel::Info ? " INFO " : " ERROR ");
            // Exchange errors and paths may contain line breaks; keep one record per line.
            for (const char character : message.str()) {
                if (character == '\n')
                    line += "\\n";
                else if (character == '\r')
                    line += "\\r";
                else
                    line += character;
            }
            stream_ << line << '\n';
            stream_.flush();
        } catch (const std::exception &) {
            // Reporting through the same failed destination cannot help.
        }
    }

  private:
    std::ostream &stream_;
};

} // namespace coinbase_ticker_statistics
