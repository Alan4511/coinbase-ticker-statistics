#pragma once

#include "result.hpp"
#include "types.hpp"

#include <ostream>
#include <string>
#include <string_view>

namespace coinbase_ticker_statistics {

/**
 * A synchronous CSV writer, owned and called by the application's event loop.
 * Flush after a configured number of rows; the application handles timed and final flushing.
 */
class CsvWriter final {
  public:
    /** Borrow a stream that must outlive this writer. Construction performs no I/O. */
    explicit CsvWriter(std::ostream &stream, std::size_t flush_every_rows = 1)
        : stream_(stream), flush_every_rows_(flush_every_rows) {
    }

    CsvWriter(const CsvWriter &) = delete;
    CsvWriter &operator=(const CsvWriter &) = delete;
    CsvWriter(CsvWriter &&) = delete;
    CsvWriter &operator=(CsvWriter &&) = delete;

    /** Write the CSV header; call once before delivering statistics updates. */
    [[nodiscard]] Result<void> write_header();

    /** Format one complete row and fail immediately when writing fails. */
    [[nodiscard]] Result<void> write_statistics(const StatisticsUpdate &update);

    /** Flush buffered rows and return an OutputIo error if the underlying stream fails. */
    [[nodiscard]] Result<void> flush();

  private:
    /** Escape a field when its content contains a delimiter, quote, or line ending. */
    void append_field(std::string &row, std::string_view field) const;
    /** Write already formatted text independently of the stream's locale or format flags. */
    [[nodiscard]] Result<void> write_text(std::string_view text);

    std::ostream &stream_;
    std::size_t flush_every_rows_;
    std::size_t pending_rows_{};
    std::string row_;
};

} // namespace coinbase_ticker_statistics
