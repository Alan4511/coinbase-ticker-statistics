#include "test_files.hpp"
#include "test_result.hpp"
#include <output/csv_writer.hpp>
#include <output/format_fields.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

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

TEST(CsvWriter, WritesExactHeaderExchangeTimeAndUnroundedPrices) {
    std::ostringstream stream;
    CsvWriter writer(stream);
    ASSERT_RESULT_OK(writer.write_header());
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    ASSERT_RESULT_OK(writer.flush());
    EXPECT_EQ(stream.str(), std::string(expected_header) + std::string(expected_row));
}

TEST(CsvWriter, WritesAllAvailableFloatingPointDigits) {
    std::ostringstream stream;
    CsvWriter writer(stream);
    ASSERT_RESULT_OK(writer.write_header());
    auto event = sample_update();
    event.ticker_update.price = 12345.67890123456789L;
    ASSERT_RESULT_OK(writer.write_statistics(event));
    ASSERT_RESULT_VALUE(price_text, format_price(event.ticker_update.price));
    EXPECT_NE(stream.str().find("," + price_text + ","), std::string::npos);
}

TEST(CsvWriter, EscapesDelimiterQuotesAndLineEndings) {
    std::ostringstream stream;
    CsvWriter writer(stream);
    ASSERT_RESULT_OK(writer.write_header());
    auto event = sample_update();
    event.ticker_update.symbol = "BTC,\"USD\"\r\n";
    ASSERT_RESULT_OK(writer.write_statistics(event));
    EXPECT_EQ(stream.str(),
              std::string(expected_header) +
                  "2026-01-02T03:04:05.123456789Z,\"BTC,\"\"USD\"\"\r\n\",42,1.25,2,1.5,1.5,1.25,1.75\n");
}

TEST(CsvWriter, DoesNotDependOnTheBorrowedStreamsLocaleOrFormattingFlags) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::hex << std::showbase;
    CsvWriter writer(stream);
    ASSERT_RESULT_OK(writer.write_header());
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    EXPECT_EQ(stream.str(), std::string(expected_header) + std::string(expected_row));
}

TEST(CsvWriter, ExplicitFlushPublishesBufferedRows) {
    test::TemporaryDirectory directory;
    const auto path = directory.file("statistics.csv");
    std::ofstream stream(path);
    ASSERT_TRUE(stream);
    CsvWriter writer(stream);
    ASSERT_RESULT_OK(writer.write_header());
    EXPECT_EQ(test::read_file(path), expected_header);
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    ASSERT_RESULT_OK(writer.flush());
    EXPECT_EQ(test::read_file(path), std::string(expected_header) + std::string(expected_row));
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    ASSERT_RESULT_OK(writer.flush());
    EXPECT_EQ(test::read_file(path),
              std::string(expected_header) + std::string(expected_row) + std::string(expected_row));
}

class CountingBuffer : public std::stringbuf {
  public:
    int sync() override {
        ++flushes;
        return fail_flush ? -1 : 0;
    }
    unsigned flushes{};
    bool fail_flush{};
};

TEST(CsvWriter, DiscardsInvalidRowAndReusesBuffer) {
    CountingBuffer buffer;
    std::ostream stream(&buffer);
    CsvWriter writer(stream);
    auto invalid = sample_update();
    invalid.ticker_update.symbol = std::string(1024, 'X');
    invalid.statistics.high = std::numeric_limits<Price>::infinity();
    ASSERT_RESULT_ERROR(writer.write_statistics(invalid), ErrorCode::InvalidInput);
    EXPECT_TRUE(buffer.str().empty());
    EXPECT_EQ(buffer.flushes, 0U);
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    EXPECT_EQ(buffer.str(), expected_row);
    EXPECT_EQ(buffer.flushes, 0U);
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    EXPECT_EQ(buffer.str(), std::string(expected_row) + std::string(expected_row));
    EXPECT_EQ(buffer.flushes, 0U);
}

TEST(CsvWriter, PreservesFullWidthIntegerFields) {
    std::ostringstream stream;
    CsvWriter writer(stream);
    auto update = sample_update();
    update.ticker_update.trade_id = std::numeric_limits<TradeId>::max();
    update.statistics.count = std::numeric_limits<SampleCount>::max();
    ASSERT_RESULT_OK(writer.write_statistics(update));
    const auto integer_fields =
        "," + std::to_string(update.ticker_update.trade_id) + ",1.25," + std::to_string(update.statistics.count) + ",";
    EXPECT_NE(stream.str().find(integer_fields), std::string::npos);
}

TEST(CsvWriter, LeavesFlushPolicyToTheSink) {
    CountingBuffer buffer;
    std::ostream stream(&buffer);
    CsvWriter writer(stream);
    buffer.fail_flush = true;
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    EXPECT_EQ(buffer.flushes, 0U);
    ASSERT_RESULT_ERROR(writer.flush(), ErrorCode::OutputIo);
    EXPECT_EQ(buffer.flushes, 1U);
}

TEST(CsvWriter, ReportsHeaderWriteFailure) {
    std::ostringstream stream;
    stream.setstate(std::ios::badbit);
    CsvWriter writer(stream);
    ASSERT_RESULT_ERROR(writer.write_header(), ErrorCode::OutputIo);
}

TEST(CsvWriter, ReportsRowWriteFailure) {
    std::ostringstream stream;
    CsvWriter writer(stream);
    ASSERT_RESULT_OK(writer.write_header());
    stream.setstate(std::ios::badbit);
    ASSERT_RESULT_ERROR(writer.write_statistics(sample_update()), ErrorCode::OutputIo);
}

TEST(CsvWriter, ReportsFlushFailure) {
    std::ostringstream stream;
    CsvWriter writer(stream);
    ASSERT_RESULT_OK(writer.write_header());
    stream.setstate(std::ios::badbit);
    ASSERT_RESULT_ERROR(writer.flush(), ErrorCode::OutputIo);
}

#if defined(__linux__)
TEST(CsvWriter, ReportsOutputFailureWhenDeviceIsFull) {
    std::ofstream full("/dev/full");
    ASSERT_TRUE(full);
    CsvWriter writer(full);
    const auto header = writer.write_header();
    if (!header) {
        EXPECT_EQ(header.error().code, ErrorCode::OutputIo);
        return;
    }
    const auto written = writer.write_statistics(sample_update());
    if (!written)
        EXPECT_EQ(written.error().code, ErrorCode::OutputIo);
    else
        ASSERT_RESULT_ERROR(writer.flush(), ErrorCode::OutputIo);
}
#endif

TEST(CsvWriter, ConvertsExceptionEnabledLibraryStreamFailures) {
    std::ofstream unopened;
    unopened.exceptions(std::ios::badbit | std::ios::failbit);
    CsvWriter writer(unopened);
    ASSERT_RESULT_ERROR(writer.write_header(), ErrorCode::OutputIo);
}

} // namespace
} // namespace coinbase_ticker_statistics
