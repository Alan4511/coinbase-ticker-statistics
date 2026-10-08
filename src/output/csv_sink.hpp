#pragma once

#include "output/csv_config.hpp"
#include "output/csv_writer.hpp"
#include "output/sink.hpp"
#include <runtime/execution_context.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <cstddef>
#include <fstream>

namespace coinbase_ticker_statistics {

/**
 * Own the file and batch-flush timer; CsvWriter formats stream output.
 * Call on the event-loop thread and keep this sink alive until cancelled handlers drain.
 */
class CsvSink {
  public:
    CsvSink(boost::asio::io_context &io, CsvConfig config, ExecutionContext &context);
    CsvSink(const CsvSink &) = delete;
    CsvSink &operator=(const CsvSink &) = delete;

    [[nodiscard]] const CsvConfig &config() const noexcept;

    /** Create directories, replace the file and publish its header. */
    [[nodiscard]] Result<void> open();
    [[nodiscard]] Result<void> write_statistics(const StatisticsUpdate &update);
    /** Flush and close explicitly to report errors. Safe before open or after close. */
    [[nodiscard]] Result<void> close();

  private:
    void schedule_flush();
    [[nodiscard]] Result<void> flush_pending();

    CsvConfig config_;
    ExecutionContext &context_;
    boost::asio::steady_timer flush_timer_;
    // The writer borrows the stream, which must outlive it.
    std::ofstream stream_;
    CsvWriter writer_;
    std::size_t pending_rows_{};
};

static_assert(OutputSink<CsvSink>);

} // namespace coinbase_ticker_statistics
