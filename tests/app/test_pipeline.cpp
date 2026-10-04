#include "test_files.hpp"
#include "test_result.hpp"
#include <config/config.hpp>
#include <feed/parser/ticker_parser.hpp>
#include <output/csv_writer.hpp>
#include <output/sink.hpp>
#include <statistics/statistics_processor.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace coinbase_ticker_statistics {
namespace {

TEST(Pipeline, JsonThroughRoutingAndWindowsMatchesIndependentCsvFixture) {
    const std::filesystem::path fixture_directory =
        std::filesystem::path{COINBASE_TICKER_STATISTICS_SOURCE_DIR} / "data";
    std::ifstream input(fixture_directory / "ticker_fixture.jsonl");
    ASSERT_TRUE(input.good());
    const auto expected = test::read_file(fixture_directory / "ticker_expected.csv");
    ASSERT_RESULT_VALUE(config, parse_config(R"({"symbols":["BTC-USD","ETH-USD"],"output":{"path":"unused.csv"}})"));
    std::ostringstream output;
    CsvWriter writer(output);
    ASSERT_RESULT_OK(writer.write_header());
    ASSERT_RESULT_VALUE(processor, StatisticsProcessor::create(config.symbols, config.window));
    std::size_t emitted{};
    std::string message;
    while (std::getline(input, message)) {
        ASSERT_RESULT_VALUE(event, parse_ticker_message(message));
        if (event) {
            ASSERT_RESULT_VALUE(update, processor.on_update(*event));
            if (update) {
                ASSERT_RESULT_OK(writer.write_statistics(*update));
                ++emitted;
            }
        }
    }
    ASSERT_RESULT_OK(writer.flush());
    EXPECT_EQ(emitted, 7U);
    EXPECT_EQ(output.str(), expected);
}

} // namespace
} // namespace coinbase_ticker_statistics
