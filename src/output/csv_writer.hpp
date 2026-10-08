#pragma once

#include "output/sink.hpp"
#include <common/result.hpp>
#include <common/types.hpp>

#include <ostream>
#include <string>
#include <string_view>

namespace coinbase_ticker_statistics {

/**
 * A synchronous CSV writer borrowing an output stream.
 * Publish the header immediately; the sink controls row flushing.
 */
class CsvWriter final {
  public:
    /** Borrow a stream that must outlive this writer. Construction performs no I/O. */
    explicit CsvWriter(std::ostream &stream) : stream_(stream) {
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
    void append_field(std::string &row, std::string_view field) const;
    /** Write already formatted text independently of the stream's locale or format flags. */
    [[nodiscard]] Result<void> write_text(std::string_view text);

    std::ostream &stream_;
    std::string row_;
};

static_assert(OutputSink<CsvWriter>);

} // namespace coinbase_ticker_statistics
