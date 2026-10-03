#include "output/csv_writer.hpp"
#include "output/format_fields.hpp"
#include "test_files.hpp"
#include "test_result.hpp"

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

constexpr std::string_view kHeader = "time,symbol,trade_id,trade_price,count,mean,median,low,high\n";
constexpr std::string_view kRoundedRow = "2026-01-02T03:04:05.123456789Z,BTC-USD,42,1.25,2,1.5,1.5,1.25,1.75\n";

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
    EXPECT_EQ(stream.str(), std::string(kHeader) + std::string(kRoundedRow));
}

TEST(CsvWriter, WritesAllAvailableFloatingPointDigits) {
    std::ostringstream stream;
    CsvWriter writer(stream);
    ASSERT_RESULT_OK(writer.write_header());
    auto event = sample_update();
    event.trade.price = 12345.67890123456789L;
    ASSERT_RESULT_OK(writer.write_statistics(event));
    ASSERT_RESULT_VALUE(price_text, format_price(event.trade.price));
    EXPECT_NE(stream.str().find("," + price_text + ","), std::string::npos);
}

TEST(CsvWriter, EscapesDelimiterQuotesAndLineEndings) {
    std::ostringstream stream;
    CsvWriter writer(stream);
    ASSERT_RESULT_OK(writer.write_header());
    auto event = sample_update();
    event.trade.symbol = "BTC,\"USD\"\r\n";
    ASSERT_RESULT_OK(writer.write_statistics(event));
    EXPECT_EQ(stream.str(),
              std::string(kHeader) +
                  "2026-01-02T03:04:05.123456789Z,\"BTC,\"\"USD\"\"\r\n\",42,1.25,2,1.5,1.5,1.25,1.75\n");
}

TEST(CsvWriter, DoesNotDependOnTheBorrowedStreamsLocaleOrFormattingFlags) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::hex << std::showbase;
    CsvWriter writer(stream);
    ASSERT_RESULT_OK(writer.write_header());
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    EXPECT_EQ(stream.str(), std::string(kHeader) + std::string(kRoundedRow));
}

TEST(CsvWriter, EachRowIsVisibleBeforeShutdown) {
    test::TemporaryDirectory directory;
    const auto path = directory.file("statistics.csv");
    std::ofstream stream(path);
    ASSERT_TRUE(stream);
    CsvWriter writer(stream);
    ASSERT_RESULT_OK(writer.write_header());
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    EXPECT_EQ(test::read_file(path), std::string(kHeader) + std::string(kRoundedRow));
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    EXPECT_EQ(test::read_file(path), std::string(kHeader) + std::string(kRoundedRow) + std::string(kRoundedRow));
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

TEST(CsvWriter, DiscardsInvalidRowAndReusesBufferWithoutAffectingBatchCount) {
    CountingBuffer buffer;
    std::ostream stream(&buffer);
    CsvWriter writer(stream, 2);
    auto invalid = sample_update();
    invalid.trade.symbol = std::string(1024, 'X');
    invalid.statistics.high = std::numeric_limits<Price>::infinity();
    ASSERT_RESULT_ERROR(writer.write_statistics(invalid), ErrorCode::InvalidInput);
    EXPECT_TRUE(buffer.str().empty());
    EXPECT_EQ(buffer.flushes, 0U);
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    EXPECT_EQ(buffer.str(), kRoundedRow);
    EXPECT_EQ(buffer.flushes, 0U);
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    EXPECT_EQ(buffer.str(), std::string(kRoundedRow) + std::string(kRoundedRow));
    EXPECT_EQ(buffer.flushes, 1U);
}

TEST(CsvWriter, PreservesFullWidthIntegerFields) {
    std::ostringstream stream;
    CsvWriter writer(stream);
    auto update = sample_update();
    update.trade.trade_id = std::numeric_limits<TradeId>::max();
    update.statistics.count = std::numeric_limits<SampleCount>::max();
    ASSERT_RESULT_OK(writer.write_statistics(update));
    const auto integer_fields =
        "," + std::to_string(update.trade.trade_id) + ",1.25," + std::to_string(update.statistics.count) + ",";
    EXPECT_NE(stream.str().find(integer_fields), std::string::npos);
}

TEST(CsvWriter, FlushesAtBatchBoundaryAndResetsAfterExplicitFlush) {
    CountingBuffer buffer;
    std::ostream stream(&buffer);
    CsvWriter writer(stream, 3);
    ASSERT_RESULT_OK(writer.write_header());
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    EXPECT_EQ(buffer.flushes, 0U);
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    EXPECT_EQ(buffer.flushes, 1U);
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    ASSERT_RESULT_OK(writer.flush());
    EXPECT_EQ(buffer.flushes, 2U);
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    EXPECT_EQ(buffer.flushes, 2U);
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    EXPECT_EQ(buffer.flushes, 3U);
}

TEST(CsvWriter, BatchIsVisibleBeforeShutdownAndFinalPartialBatchIsFlushed) {
    test::TemporaryDirectory directory;
    const auto path = directory.file("statistics.csv");
    std::ofstream stream(path);
    CsvWriter writer(stream, 2);
    ASSERT_RESULT_OK(writer.write_header());
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    EXPECT_EQ(test::read_file(path), std::string(kHeader) + std::string(kRoundedRow) + std::string(kRoundedRow));
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    ASSERT_RESULT_OK(writer.flush());
    EXPECT_EQ(test::read_file(path),
              std::string(kHeader) + std::string(kRoundedRow) + std::string(kRoundedRow) + std::string(kRoundedRow));
}

TEST(CsvWriter, ReportsBatchFlushFailure) {
    CountingBuffer buffer;
    std::ostream stream(&buffer);
    CsvWriter writer(stream, 2);
    ASSERT_RESULT_OK(writer.write_statistics(sample_update()));
    buffer.fail_flush = true;
    ASSERT_RESULT_ERROR(writer.write_statistics(sample_update()), ErrorCode::OutputIo);
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
TEST(CsvWriter, ReportsPerRowFlushFailureWhenDeviceIsFull) {
    std::ofstream full("/dev/full");
    ASSERT_TRUE(full);
    CsvWriter writer(full);
    const auto header = writer.write_header();
    if (!header) {
        EXPECT_EQ(header.error().code, ErrorCode::OutputIo);
        return;
    }
    ASSERT_RESULT_ERROR(writer.write_statistics(sample_update()), ErrorCode::OutputIo);
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
