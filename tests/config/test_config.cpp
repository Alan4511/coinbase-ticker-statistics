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

TEST(Config, ParsesRequiredDefaultsAndOverrides) {
    ASSERT_RESULT_VALUE(defaults, parse_optional_settings("{}"));
    EXPECT_EQ(defaults.symbols, (std::vector<std::string>{"BTC-USD", "ETH-USD", "SOL-USD"}));
    EXPECT_EQ(defaults.feed.host, "ws-feed.exchange.coinbase.com");
    EXPECT_EQ(defaults.feed.port, "443");
    EXPECT_EQ(defaults.feed.target, "/");
    EXPECT_EQ(defaults.feed.max_message_bytes, 1'048'576U);
    EXPECT_EQ(defaults.feed.connect_timeout, 15s);
    EXPECT_EQ(defaults.feed.close_timeout, 5s);
    EXPECT_EQ(defaults.window.duration, 300s);
    EXPECT_EQ(defaults.output.path, "test.csv");
    EXPECT_EQ(defaults.output.flush_every_rows, 100U);
    EXPECT_EQ(defaults.output.flush_interval, 250ms);
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
    ASSERT_RESULT_OK(parse_optional_settings(R"({
        "feed": {"port": "65535", "max_message_bytes": 65536},
        "window": {"duration_seconds": 1},
        "output": {"flush_every_rows": 1, "flush_interval_ms": 1}
    })"));
    ASSERT_RESULT_OK(parse_optional_settings(R"({"output":{"flush_interval_ms":31536000000}})"));
}

TEST(Config, RejectsInvalidConfiguration) {
    for (const auto *missing_required : {R"({})",
                                         R"({"output":{"path":"test.csv"}})",
                                         R"({"symbols":["BTC-USD"]})",
                                         R"({"symbols":["BTC-USD"],"output":{}})"}) {
        SCOPED_TRACE(missing_required);
        ASSERT_RESULT_ERROR(parse_config(missing_required), ErrorCode::InvalidConfiguration);
    }
    struct InvalidCase {
        std::string_view name;
        std::string_view json;
    };
    constexpr InvalidCase cases[]{{"invalid JSON", "not json"},
                                  {"non-object root", "[]"},
                                  {"trailing input", "{} trailing"},
                                  {"empty symbols", R"({"symbols":[]})"},
                                  {"symbols type", R"({"symbols":null})"},
                                  {"duplicate symbols", R"({"symbols":["BTC-USD","BTC-USD"]})"},
                                  {"symbol type", R"({"symbols":[1]})"},
                                  {"symbol syntax", R"({"symbols":["btc-usd"]})"},
                                  {"feed section", R"({"feed":null})"},
                                  {"empty host", R"({"feed":{"host":""}})"},
                                  {"port type", R"({"feed":{"port":443}})"},
                                  {"target syntax", R"({"feed":{"target":"ticker"}})"},
                                  {"message limit zero", R"({"feed":{"max_message_bytes":0}})"},
                                  {"message limit overflow", R"({"feed":{"max_message_bytes":18446744073709551616}})"},
                                  {"connection timeout zero", R"({"feed":{"connect_timeout_seconds":0}})"},
                                  {"timeout fraction", R"({"feed":{"connect_timeout_seconds":1.5}})"},
                                  {"negative timeout", R"({"feed":{"close_timeout_seconds":-1}})"},
                                  {"timeout policy limit", R"({"feed":{"close_timeout_seconds":31536001}})"},
                                  {"window section", R"({"window":[]})"},
                                  {"window zero", R"({"window":{"duration_seconds":0}})"},
                                  {"window negative", R"({"window":{"duration_seconds":-1}})"},
                                  {"window numeric coercion", R"({"window":{"duration_seconds":300.0}})"},
                                  {"window boolean", R"({"window":{"duration_seconds":true}})"},
                                  {"window policy limit", R"({"window":{"duration_seconds":31536001}})"},
                                  {"output section", R"({"output":[]})"},
                                  {"empty path", R"({"output":{"path":""}})"},
                                  {"row threshold zero", R"({"output":{"flush_every_rows":0}})"},
                                  {"row threshold overflow", R"({"output":{"flush_every_rows":18446744073709551616}})"},
                                  {"flush interval zero", R"({"output":{"flush_interval_ms":0}})"},
                                  {"flush interval negative", R"({"output":{"flush_interval_ms":-1}})"},
                                  {"flush interval fraction", R"({"output":{"flush_interval_ms":2.5}})"},
                                  {"flush interval boolean", R"({"output":{"flush_interval_ms":true}})"},
                                  {"flush interval policy limit", R"({"output":{"flush_interval_ms":31536000001}})"}};
    for (const auto &[name, json] : cases) {
        SCOPED_TRACE(name);
        ASSERT_RESULT_ERROR(parse_optional_settings(json), ErrorCode::InvalidConfiguration);
    }
}

TEST(Config, LoadsFilesAndResolvesOutputPath) {
    test::TemporaryDirectory fixture;
    test::write_file(fixture.file("config.json"), with_required_fields(R"({"output":{"path":"build/../prices.csv"}})"));
    ASSERT_RESULT_VALUE(config, load_config(fixture.file("config.json")));
    EXPECT_EQ(config.output.path, (fixture.file("prices.csv")).lexically_normal());
    ASSERT_RESULT_ERROR(load_config(fixture.file("missing.json")), ErrorCode::FileIo);
    const std::filesystem::path project_directory(COINBASE_TICKER_STATISTICS_SOURCE_DIR);
    for (const auto *filename : {"example.json", "live_verification.json"}) {
        SCOPED_TRACE(filename);
        ASSERT_RESULT_VALUE(example, load_config(project_directory / "config" / filename));
        ASSERT_RESULT_OK(validate_config(example));
        EXPECT_EQ(example.output.path.parent_path(), project_directory / "build");
    }
}

} // namespace
} // namespace coinbase_ticker_statistics
