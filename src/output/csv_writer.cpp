#include "output/csv_writer.hpp"
#include "output/format_fields.hpp"
#include <common/format_timestamp.hpp>

#include <ios>
#include <string>
#include <string_view>
#include <utility>

namespace coinbase_ticker_statistics {
namespace {

constexpr std::string_view csv_header = "time,symbol,trade_id,trade_price,count,mean,median,low,high\n";

/** Handle both ostream failure modes at the synchronous output boundary. */
Result<void> run_output_operation(std::ostream &stream, const char *failure_message, auto operation) {
    try {
        operation();
    } catch (const std::ios_base::failure &error) {
        return fail(ErrorCode::OutputIo, std::string(failure_message) + ": " + error.what());
    }
    if (!stream)
        return fail(ErrorCode::OutputIo, failure_message);
    return {};
}

} // namespace

Result<void> CsvWriter::write_header() {
    return write_text(csv_header).and_then([this] {
        return flush();
    });
}

void CsvWriter::append_field(std::string &row, std::string_view field) const {
    const bool needs_quotes = field.find_first_of(",\"\r\n") != std::string_view::npos;
    if (!needs_quotes) {
        row += field;
        return;
    }
    row += '"';
    for (const char character : field) {
        if (character == '"') {
            row += '"';
        }
        row += character;
    }
    row += '"';
}

Result<void> CsvWriter::write_text(std::string_view text) {
    if (!std::in_range<std::streamsize>(text.size())) {
        return fail(ErrorCode::OutOfRange, "CSV row exceeds the stream write size limit");
    }
    return run_output_operation(stream_, "Writing CSV output failed", [this, text] {
        stream_.write(text.data(), static_cast<std::streamsize>(text.size()));
    });
}

Result<void> CsvWriter::write_statistics(const StatisticsUpdate &update) {
    row_.clear(); // Retain storage between updates.
    const auto timestamp = format_utc_timestamp(update.ticker_update.exchange_time);
    if (!timestamp.has_value())
        return std::unexpected(timestamp.error());
    row_ += timestamp.value();
    row_ += ',';
    append_field(row_, update.ticker_update.symbol);
    const auto append_numeric_field = [this](const auto &value) -> Result<void> {
        row_ += ',';
        return append_number(row_, value);
    };
    if (auto result = append_numeric_field(update.ticker_update.trade_id); !result.has_value())
        return result;
    if (auto result = append_numeric_field(update.ticker_update.price); !result.has_value())
        return result;
    if (auto result = append_numeric_field(update.statistics.count); !result.has_value())
        return result;
    const auto &statistics = update.statistics;
    for (const auto &value : {statistics.mean, statistics.median}) {
        if (auto result = append_numeric_field(value); !result.has_value())
            return result;
    }
    for (const auto value : {statistics.low, statistics.high}) {
        if (auto result = append_numeric_field(value); !result.has_value())
            return result;
    }
    row_ += '\n';
    // Formatting errors never publish a partial row; the next call clears scratch storage.
    return write_text(row_);
}

Result<void> CsvWriter::flush() {
    return run_output_operation(stream_, "Flushing CSV output failed", [this] {
        stream_.flush();
    });
}

} // namespace coinbase_ticker_statistics
