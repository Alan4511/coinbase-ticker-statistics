#include "output/csv_sink.hpp"

#include <filesystem>
#include <utility>

namespace coinbase_ticker_statistics {

CsvSink::CsvSink(boost::asio::io_context &io, CsvConfig config, RunControlHandle &control_handle)
    : config_(std::move(config)), control_handle_(control_handle), flush_timer_(io), writer_(stream_) {
}

const CsvConfig &CsvSink::config() const noexcept {
    return config_;
}

Result<void> CsvSink::open() {
    if (auto valid = validate(config_); !valid.has_value())
        return valid;
    if (stream_.is_open())
        return fail(ErrorCode::InvalidState, "CSV output is already open");
    const auto &path = config_.path;
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return fail(ErrorCode::FileIo, "cannot create output directory: " + error.message());
    }
    stream_.open(path);
    if (!stream_)
        return fail(ErrorCode::FileIo, "cannot open CSV output: " + path.string());
    return writer_.write_header();
}

Result<void> CsvSink::write_statistics(const StatisticsUpdate &update) {
    if (auto written = writer_.write_statistics(update); !written.has_value())
        return written;
    ++pending_rows_;
    if (pending_rows_ >= config_.flush_every_rows)
        return flush_pending();
    if (pending_rows_ == 1)
        schedule_flush();
    return {};
}

void CsvSink::schedule_flush() {
    static_cast<void>(flush_timer_.expires_after(config_.flush_interval));
    const auto deadline = flush_timer_.expiry();
    const auto on_flush_due = [this, deadline](boost::system::error_code error) {
        // Ignore cancelled waits and already-ready handlers from an earlier batch.
        if (error || deadline != flush_timer_.expiry() || pending_rows_ == 0)
            return;
        if (auto flushed = flush_pending(); !flushed.has_value())
            control_handle_.fail(std::move(flushed.error()));
    };
    flush_timer_.async_wait(on_flush_due);
}

Result<void> CsvSink::flush_pending() {
    static_cast<void>(flush_timer_.cancel());
    auto flushed = writer_.flush();
    if (flushed.has_value())
        pending_rows_ = 0;
    return flushed;
}

Result<void> CsvSink::close() {
    static_cast<void>(flush_timer_.cancel());
    if (!stream_.is_open())
        return {};
    auto flushed = writer_.flush();
    stream_.close();
    pending_rows_ = 0;
    if (!flushed.has_value())
        return flushed;
    if (stream_.fail())
        return fail(ErrorCode::OutputIo, "Closing CSV output failed");
    return {};
}

} // namespace coinbase_ticker_statistics
