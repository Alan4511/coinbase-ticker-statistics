#include "test_files.hpp"
#include "test_result.hpp"
#include <output/csv_writer.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <ios>
#include <limits>
#include <locale>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>

namespace coinbase_ticker_statistics {
namespace {

constexpr std::string_view expected_header = "time,symbol,trade_id,trade_price,count,mean,median,low,high\n";
constexpr std::string_view expected_row = "2026-01-02T03:04:05.123456789Z,BTC-USD,42,1.25,2,1.5,1.5,1.25,1.75\n";

StatisticsUpdate sample_update() {
    const Timestamp exchange_time = std::chrono::sys_days{std::chrono::year{2026} / std::chrono::January / 2} +
                                    std::chrono::hours{3} + std::chrono::minutes{4} + std::chrono::seconds{5} +
                                    std::chrono::nanoseconds{123456789};
    return {{exchange_time, "BTC-USD", 42, Price{1.25L}}, {2, 1.5L, 1.5L, Price{1.25L}, Price{1.75L}}};
}

class DecimalComma : public std::numpunct<char> {
    char do_decimal_point() const override {
        return ',';
    }
    char do_thousands_sep() const override {
        return '.';
    }
    std::string do_grouping() const override {
        return "\3";
    }
};

TEST(CsvWriter, WritesEscapedRoundTrippableRows) {
    std::ostringstream stream;
    stream.imbue(std::locale(std::locale::classic(), new DecimalComma));
    stream << std::hex << std::showbase << std::fixed << std::setprecision(1);
    CsvWriter writer(stream);
    ASSERT_RESULT_OK(writer.write_header());
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    EXPECT_EQ(stream.str(), std::string(expected_header) + std::string(expected_row));

    auto update = sample_update();
    update.ticker_update.symbol = "BTC,\"USD\"\r\n";
    update.ticker_update.trade_id = std::numeric_limits<TradeId>::max();
    update.statistics.count = std::numeric_limits<SampleCount>::max();
    update.ticker_update.price = 12345.67890123456789L;
    std::ostringstream independent_price;
    independent_price.imbue(std::locale::classic());
    independent_price << std::setprecision(std::numeric_limits<Price>::max_digits10) << update.ticker_update.price;
    ASSERT_RESULT_OK(writer.write_statistics(update));
    ASSERT_RESULT_OK(writer.flush());
    const auto escaped_row = "2026-01-02T03:04:05.123456789Z,\"BTC,\"\"USD\"\"\r\n\"," +
                             std::to_string(update.ticker_update.trade_id) + ',' + independent_price.str() + ',' +
                             std::to_string(update.statistics.count) + ",1.5,1.5,1.25,1.75\n";
    EXPECT_EQ(stream.str(), std::string(expected_header) + std::string(expected_row) + escaped_row);
}

class FailingBuffer : public std::stringbuf {
  public:
    std::streamsize xsputn(const char *text, std::streamsize count) override {
        return reject_writes ? 0 : std::stringbuf::xsputn(text, count);
    }
    int sync() override {
        return reject_flush ? -1 : 0;
    }
    bool reject_writes{};
    bool reject_flush{};
};

TEST(CsvWriter, ReportsStreamFailures) {
    for (const bool exceptions_enabled : {false, true}) {
        SCOPED_TRACE(exceptions_enabled ? "throwing stream" : "error-state stream");
        FailingBuffer buffer;
        std::ostream stream(&buffer);
        if (exceptions_enabled)
            stream.exceptions(std::ios::badbit | std::ios::failbit);
        CsvWriter writer(stream);
        ASSERT_RESULT_OK(writer.write_header());
        buffer.reject_writes = true;
        ASSERT_RESULT_ERROR(writer.write_statistics(sample_update()), ErrorCode::OutputIo);
        stream.clear();
        ASSERT_RESULT_ERROR(writer.write_header(), ErrorCode::OutputIo);
        stream.clear();
        buffer.reject_writes = false;
        buffer.reject_flush = true;
        ASSERT_RESULT_ERROR(writer.flush(), ErrorCode::OutputIo);
    }
#if defined(__linux__)
    std::ofstream full("/dev/full");
    ASSERT_TRUE(full);
    CsvWriter writer(full);
    ASSERT_RESULT_ERROR(writer.write_header(), ErrorCode::OutputIo);
#endif
}

} // namespace
} // namespace coinbase_ticker_statistics
