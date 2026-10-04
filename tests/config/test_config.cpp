#include "test_files.hpp"
#include "test_result.hpp"
#include <config/config.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace coinbase_ticker_statistics {
namespace {

using namespace std::chrono_literals;

/** Supply explicit required settings so partial inputs still exercise their intended validation. */
std::string with_required_fields(std::string_view settings) {
    const auto provided = nlohmann::json::parse(settings, nullptr, false);
    if (provided.is_discarded() || !provided.is_object())
        return std::string(settings);
    auto document = nlohmann::json::parse(R"({
        "symbols":["BTC-USD","ETH-USD","SOL-USD"],
        "output":{"path":"test.csv"}
    })");
    for (const auto &[name, value] : provided.items()) {
        if (name == "output" && value.is_object())
            document[name].update(value);
        else
            document[name] = value;
    }
    return document.dump();
}

Result<Config> parse_optional_settings(std::string_view settings) {
    return parse_config(with_required_fields(settings));
}

TEST(Config, RejectsMissingRequiredSettings) {
    for (const auto *configuration : {R"({})",
                                      R"({"output":{"path":"test.csv"}})",
                                      R"({"symbols":["BTC-USD"]})",
                                      R"({"symbols":["BTC-USD"],"output":{}})",
                                      R"({"connections":[{}],"output":{"path":"test.csv"}})"}) {
        ASSERT_RESULT_ERROR(parse_config(configuration), ErrorCode::InvalidConfiguration);
    }
}

TEST(Config, SuppliesDefaultsOnlyForOptionalSettings) {
    ASSERT_RESULT_VALUE(config, parse_optional_settings("{}"));
    EXPECT_EQ(config.symbols, (std::vector<std::string>{"BTC-USD", "ETH-USD", "SOL-USD"}));
    EXPECT_EQ(config.feed.host, "ws-feed.exchange.coinbase.com");
    EXPECT_EQ(config.feed.port, "443");
    EXPECT_EQ(config.feed.target, "/");
    EXPECT_EQ(config.feed.max_message_bytes, 1'048'576U);
    EXPECT_EQ(config.feed.connect_timeout, 15s);
    EXPECT_EQ(config.feed.close_timeout, 5s);
    EXPECT_EQ(config.window.duration, 300s);
    EXPECT_EQ(config.output.path, "test.csv");
    EXPECT_EQ(config.output.flush_every_rows, 100U);
    EXPECT_EQ(config.output.flush_interval, 250ms);
}

TEST(Config, ParsesConfigurableEndpointLimitsAndOutput) {
    constexpr auto configuration = R"({
        "symbols": ["ETH-USD", "BTC-USD"],
        "feed": {
            "host": "localhost", "port": "8443", "target": "/ticker?version=1",
            "max_message_bytes": 65536, "connect_timeout_seconds": 30, "close_timeout_seconds": 2
        },
        "window": {
            "duration_seconds": 3600
        },
        "output": {"path": "build/first.csv", "flush_every_rows": 128, "flush_interval_ms": 500}
    })";
    ASSERT_RESULT_VALUE(config, parse_optional_settings(configuration));
    EXPECT_EQ(config.symbols, (std::vector<std::string>{"ETH-USD", "BTC-USD"}));
    EXPECT_EQ(config.feed.host, "localhost");
    EXPECT_EQ(config.feed.port, "8443");
    EXPECT_EQ(config.feed.target, "/ticker?version=1");
    EXPECT_EQ(config.feed.max_message_bytes, 65'536U);
    EXPECT_EQ(config.feed.connect_timeout, 30s);
    EXPECT_EQ(config.feed.close_timeout, 2s);
    EXPECT_EQ(config.window.duration, 3600s);
    EXPECT_EQ(config.output.path, "build/first.csv");
    EXPECT_EQ(config.output.flush_every_rows, 128U);
    EXPECT_EQ(config.output.flush_interval, 500ms);
}

TEST(Config, ReportsFirstInvalidFieldInReadingOrder) {
    const auto feed = parse_optional_settings(R"({"feed":{"host":false,"port":false}})");
    ASSERT_FALSE(feed);
    EXPECT_NE(feed.error().message.find("host:"), std::string::npos);
    const auto window = parse_optional_settings(R"({"window":{"duration_seconds":false}})");
    ASSERT_FALSE(window);
    EXPECT_NE(window.error().message.find("duration_seconds:"), std::string::npos);
    const auto output = parse_optional_settings(R"({"output":{"path":false}})");
    ASSERT_FALSE(output);
    EXPECT_NE(output.error().message.find("path:"), std::string::npos);
}

TEST(Config, AcceptsIntegerPolicyBoundaries) {
    ASSERT_RESULT_OK(parse_optional_settings(R"({
        "feed": {"port": "65535", "max_message_bytes": 65536},
        "window": {"duration_seconds": 1},
        "output": {"flush_every_rows": 1, "flush_interval_ms": 1}
    })"));
    ASSERT_RESULT_OK(parse_optional_settings(R"({"output":{"flush_interval_ms":31536000000}})"));
}

class InvalidConfiguration : public testing::TestWithParam<const char *> {};

TEST_P(InvalidConfiguration, RejectsWithoutCoercionOrSilentFallback) {
    ASSERT_RESULT_ERROR(parse_optional_settings(GetParam()), ErrorCode::InvalidConfiguration) << GetParam();
}

INSTANTIATE_TEST_SUITE_P(Schema,
                         InvalidConfiguration,
                         testing::Values("not json",
                                         "[]",
                                         "null",
                                         "{} trailing",
                                         R"({"symbols":[]})",
                                         R"({"symbols":null})",
                                         R"({"symbols":{}})",
                                         R"({"symbols":["BTC-USD","BTC-USD"]})",
                                         R"({"symbols":[1]})",
                                         R"({"symbols":["btc-usd"]})",
                                         R"({"feed":null})",
                                         R"({"feed":{"host":""}})",
                                         R"({"feed":{"port":443}})",
                                         R"({"feed":{"target":"ticker"}})",
                                         R"({"feed":{"max_message_bytes":0}})",
                                         R"({"feed":{"connect_timeout_seconds":0}})",
                                         R"({"feed":{"connect_timeout_seconds":1.5}})",
                                         R"({"feed":{"close_timeout_seconds":-1}})",
                                         R"({"feed":{"close_timeout_seconds":true}})",
                                         R"({"feed":{"close_timeout_seconds":31536001}})",
                                         R"({"window":[]})",
                                         R"({"window":{"duration_seconds":-1}})",
                                         R"({"window":{"duration_seconds":300.0}})",
                                         R"({"window":{"duration_seconds":true}})",
                                         R"({"window":{"duration_seconds":31536001}})",

                                         R"({"output":[]})",
                                         R"({"output":{"path":""}})",
                                         R"({"output":{"flush_every_rows":0}})",
                                         R"({"output":{"flush_every_rows":-1}})",
                                         R"({"output":{"flush_every_rows":1.5}})",
                                         R"({"output":{"flush_every_rows":true}})",
                                         R"({"output":{"flush_every_rows":18446744073709551616}})",
                                         R"({"output":{"flush_interval_ms":0}})",
                                         R"({"output":{"flush_interval_ms":-1}})",
                                         R"({"output":{"flush_interval_ms":2.5}})",
                                         R"({"output":{"flush_interval_ms":true}})",
                                         R"({"output":{"flush_interval_ms":31536000001}})",
                                         R"({"feed":{"max_message_bytes":-1}})",
                                         R"({"feed":{"max_message_bytes":2.5}})",
                                         R"({"feed":{"max_message_bytes":true}})",
                                         R"({"feed":{"max_message_bytes":18446744073709551616}})"));

TEST(Config, ResolvesFilePathsRelativeToConfigurationDirectory) {
    test::TemporaryDirectory fixture;
    test::write_file(fixture.file("config.json"), with_required_fields(R"({"output":{"path":"build/../prices.csv"}})"));
    ASSERT_RESULT_VALUE(config, load_config(fixture.file("config.json")));
    EXPECT_EQ(config.output.path, (fixture.file("prices.csv")).lexically_normal());
}

TEST(Config, MissingOutputPathCannotSelectAnImplicitFile) {
    test::TemporaryDirectory fixture;
    test::write_file(fixture.file("config.json"), R"({"symbols":["BTC-USD"],"output":{}})");
    ASSERT_RESULT_ERROR(load_config(fixture.file("config.json")), ErrorCode::InvalidConfiguration);
    EXPECT_FALSE(std::filesystem::exists(fixture.file("ticker_statistics.csv")));
}

TEST(Config, ReportsMissingConfigurationFile) {
    test::TemporaryDirectory fixture;
    ASSERT_RESULT_ERROR(load_config(fixture.file("config.json")), ErrorCode::FileIo);
}

TEST(Config, CheckedInExamplesAreValidAndUseProjectRelativeBuildPaths) {
    const std::filesystem::path project_directory(COINBASE_TICKER_STATISTICS_SOURCE_DIR);
    for (const auto *filename : {"example.json", "live_verification.json"}) {
        ASSERT_RESULT_VALUE(config, load_config(project_directory / "config" / filename));
        EXPECT_EQ(config.symbols, (std::vector<std::string>{"BTC-USD", "ETH-USD", "SOL-USD"}));
        EXPECT_EQ(config.window.duration, 300s);
        EXPECT_EQ(config.output.path.parent_path(), project_directory / "build");
    }
}

TEST(Config, KeepsTakeHomeParsingSimpleAndDocumentsLastKeyWins) {
    ASSERT_RESULT_VALUE(
        config,
        parse_config(
            R"({"symbols":["BTC-USD"],"output":{"path":"test.csv"},"unknown":1,"window":{"duration_seconds":2,"duration_seconds":4}})"));
    EXPECT_EQ(config.window.duration, 4s);
}

} // namespace
} // namespace coinbase_ticker_statistics
