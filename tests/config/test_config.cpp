#include "test_files.hpp"
#include "test_json.hpp"
#include "test_result.hpp"
#include <common/json.hpp>
#include <config/config.hpp>

#include <glaze/json.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>

namespace coinbase_ticker_statistics {
namespace {

using namespace std::chrono_literals;

/** Supply explicit required settings so partial inputs still exercise their intended validation. */
std::string with_required_fields(std::string_view settings) {
    const auto provided = json_utils::read_json<test::JsonFields>(settings);
    if (!provided.has_value())
        return std::string(settings);
    auto document = glz::read_json<test::JsonFields>(R"({
        "symbols":["BTC-USD","ETH-USD","SOL-USD"],
        "output":{"path":"test.csv"}
    })")
                        .value();
    for (const auto &[name, value] : provided.value()) {
        if (name == "output") {
            auto changes = glz::read_json<test::JsonFields>(value.str);
            if (changes.has_value()) {
                auto output = glz::read_json<test::JsonFields>(document[name].str).value();
                for (const auto &[field, setting] : changes.value())
                    output[field] = setting;
                document[name].str = glz::write_json(output).value();
                continue;
            }
        }
        document[name] = value;
    }
    return glz::write_json(document).value();
}

Result<Config> parse_optional_settings(std::string_view settings) {
    return parse_and_validate_config(with_required_fields(settings));
}

TEST(Config, ParsesRequiredDefaultsAndOverrides) {
    const auto required_config = with_required_fields("{}");
    ASSERT_RESULT_VALUE(defaults, parse_and_validate_config(std::string_view{required_config}));
    EXPECT_EQ(defaults.symbols, (std::vector<std::string>{"BTC-USD", "ETH-USD", "SOL-USD"}));
    EXPECT_EQ(defaults.feed.host, "ws-feed.exchange.coinbase.com");
    EXPECT_EQ(defaults.feed.port, "443");
    EXPECT_EQ(defaults.feed.target, "/");
    EXPECT_EQ(defaults.feed.max_message_bytes, 1'048'576U);
    EXPECT_EQ(defaults.feed.connect_timeout, 15s);
    EXPECT_EQ(defaults.feed.close_timeout, 5s);
    EXPECT_EQ(defaults.window.duration, 300s);
    EXPECT_EQ(defaults.window.max_observations_per_symbol, 100'000U);
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

    const auto maximum_count = std::numeric_limits<std::size_t>::max();
    const auto count = std::to_string(maximum_count);
    ASSERT_RESULT_VALUE(large,
                        parse_optional_settings("{\"feed\":{\"max_message_bytes\":" + count +
                                                "},\"output\":{\"flush_every_rows\":" + count + "}}"));
    EXPECT_EQ(large.feed.max_message_bytes, maximum_count);
    EXPECT_EQ(large.output.flush_every_rows, maximum_count);

    ASSERT_RESULT_VALUE(exponents, parse_optional_settings(R"({
        "feed":{"connect_timeout_seconds":1e1,"max_message_bytes":1e6},
        "window":{"duration_seconds":3e2},
        "output":{"flush_every_rows":1e2,"flush_interval_ms":25e1}
    })"));
    EXPECT_EQ(exponents.feed.connect_timeout, 10s);
    EXPECT_EQ(exponents.feed.max_message_bytes, 1'000'000U);
    EXPECT_EQ(exponents.window.duration, 300s);
    EXPECT_EQ(exponents.output.flush_every_rows, 100U);
    EXPECT_EQ(exponents.output.flush_interval, 250ms);
    ASSERT_RESULT_VALUE(limited, parse_optional_settings(R"({"window":{"max_observations_per_symbol":42}})"));
    EXPECT_EQ(limited.window.max_observations_per_symbol, 42U);
}

TEST(Config, PreservesUnknownAndDuplicateKeyPolicies) {
    ASSERT_RESULT_VALUE(config, parse_and_validate_config(R"({
        "symbols":["BTC-USD"], "unknown":{"ignored":true},
        "feed":{"port":"8443","port":"443"},
        "output":{"path":"first.csv","path":"last.csv"}
    })"));
    EXPECT_EQ(config.feed.port, "443");
    EXPECT_EQ(config.output.path, "last.csv");
    ASSERT_RESULT_VALUE(sections, parse_and_validate_config(R"({
        "symbols":["BTC-USD"], "feed":{"host":"localhost"}, "feed":{},
        "window":{"duration_seconds":42}, "window":{},
        "output":{"path":"first.csv","flush_every_rows":1}, "output":{"path":"last.csv"}
    })"));
    EXPECT_EQ(sections.feed.host, "ws-feed.exchange.coinbase.com");
    EXPECT_EQ(sections.window.duration, 300s);
    EXPECT_EQ(sections.output.path, "last.csv");
    EXPECT_EQ(sections.output.flush_every_rows, 100U);
    ASSERT_RESULT_OK(parse_and_validate_config(R"({
        "symbols":["BTC-USD"], "feed":{"host":""}, "feed":{"host":"localhost"},
        "window":{"duration_seconds":0}, "window":{}, "output":{"path":"test.csv"}
    })")); // Domain validation applies to the effective settings after replacement.
    ASSERT_RESULT_ERROR(parse_and_validate_config(R"({
        "symbols":["BTC-USD"], "feed":{"host":1}, "feed":{"host":"localhost"},
        "output":{"path":"test.csv"}
    })"),
                        ErrorCode::InvalidConfiguration);
}

TEST(Config, RejectsInvalidConfiguration) {
    for (const auto *missing_required : {R"({})",
                                         R"({"output":{"path":"test.csv"}})",
                                         R"({"symbols":["BTC-USD"]})",
                                         R"({"symbols":["BTC-USD"],"output":{}})"}) {
        SCOPED_TRACE(missing_required);
        ASSERT_RESULT_ERROR(parse_and_validate_config(missing_required), ErrorCode::InvalidConfiguration);
    }
    struct InvalidCase {
        std::string_view name;
        std::string_view json;
    };
    constexpr InvalidCase cases[]{
        {"invalid JSON", "not json"},
        {"non-object root", "[]"},
        {"trailing input", "{} trailing"},
        {"empty symbols", R"({"symbols":[]})"},
        {"symbols type", R"({"symbols":null})"},
        {"duplicate symbols", R"({"symbols":["BTC-USD","BTC-USD"]})"},
        {"symbol type", R"({"symbols":[1]})"},
        {"symbol syntax", R"({"symbols":["btc-usd"]})"},
        {"feed section", R"({"feed":null})"},
        {"empty host", R"({"feed":{"host":""}})"},
        {"NUL host", R"({"feed":{"host":"localhost\u0000ignored"}})"},
        {"NUL port", R"({"feed":{"port":"443\u0000ignored"}})"},
        {"NUL target", R"({"feed":{"target":"/\u0000ignored"}})"},
        {"port type", R"({"feed":{"port":443}})"},
        {"target syntax", R"({"feed":{"target":"ticker"}})"},
        {"message limit zero", R"({"feed":{"max_message_bytes":0}})"},
        {"message limit overflow", R"({"feed":{"max_message_bytes":18446744073709551616}})"},
        {"connection timeout zero", R"({"feed":{"connect_timeout_seconds":0}})"},
        {"timeout fraction", R"({"feed":{"connect_timeout_seconds":1.5}})"},
        {"timeout decimal spelling", R"({"feed":{"connect_timeout_seconds":1.0}})"},
        {"negative timeout", R"({"feed":{"close_timeout_seconds":-1}})"},
        {"timeout policy limit", R"({"feed":{"close_timeout_seconds":31536001}})"},
        {"timeout representation overflow", R"({"feed":{"connect_timeout_seconds":18446744073709551615}})"},
        {"window section", R"({"window":[]})"},
        {"window zero", R"({"window":{"duration_seconds":0}})"},
        {"observation limit zero", R"({"window":{"max_observations_per_symbol":0}})"},
        {"observation limit negative", R"({"window":{"max_observations_per_symbol":-1}})"},
        {"observation limit fraction", R"({"window":{"max_observations_per_symbol":2.5}})"},
        {"observation limit overflow", R"({"window":{"max_observations_per_symbol":18446744073709551616}})"},
        {"window negative", R"({"window":{"duration_seconds":-1}})"},
        {"window numeric coercion", R"({"window":{"duration_seconds":300.0}})"},
        {"window boolean", R"({"window":{"duration_seconds":true}})"},
        {"window policy limit", R"({"window":{"duration_seconds":31536001}})"},
        {"window representation overflow", R"({"window":{"duration_seconds":9223372036854775808}})"},
        {"output section", R"({"output":[]})"},
        {"empty path", R"({"output":{"path":""}})"},
        {"NUL path", R"({"output":{"path":"test.csv\u0000ignored"}})"},
        {"row threshold zero", R"({"output":{"flush_every_rows":0}})"},
        {"row threshold overflow", R"({"output":{"flush_every_rows":18446744073709551616}})"},
        {"flush interval zero", R"({"output":{"flush_interval_ms":0}})"},
        {"flush interval negative", R"({"output":{"flush_interval_ms":-1}})"},
        {"flush interval fraction", R"({"output":{"flush_interval_ms":2.5}})"},
        {"flush interval boolean", R"({"output":{"flush_interval_ms":true}})"},
        {"flush interval representation overflow", R"({"output":{"flush_interval_ms":18446744073709551615}})"},
        {"flush interval policy limit", R"({"output":{"flush_interval_ms":31536000001}})"}};
    for (const auto &[name, json] : cases) {
        SCOPED_TRACE(name);
        ASSERT_RESULT_ERROR(parse_optional_settings(json), ErrorCode::InvalidConfiguration);
    }
    const auto first_error = parse_optional_settings(R"({"feed":{"host":1,"port":false}})");
    ASSERT_RESULT_ERROR(first_error, ErrorCode::InvalidConfiguration);
    EXPECT_TRUE(first_error.error().message.contains("host"));
}

TEST(Config, ValidatesUnusedFieldsAndEntireInput) {
    for (const auto *input : {R"({"unused":[1,]})",
                              R"({"unused":1e})",
                              R"({"unused":"\q"})",
                              R"({"unused":01})",
                              R"({"unused":true,})",
                              R"({/*comment*/"unused":true})"}) {
        SCOPED_TRACE(input);
        ASSERT_RESULT_ERROR(parse_optional_settings(input), ErrorCode::InvalidConfiguration);
    }
    auto invalid_utf8 = with_required_fields("{}");
    invalid_utf8.insert(1, "\"unused\":\"" + std::string(1, static_cast<char>(0xff)) + "\",");
    ASSERT_RESULT_ERROR(parse_and_validate_config(invalid_utf8), ErrorCode::InvalidConfiguration);

    const auto contents = with_required_fields("{}");
    ASSERT_RESULT_OK(parse_and_validate_config(contents + " \r\n\t"));
    ASSERT_RESULT_OK(parse_and_validate_config(contents + std::string(100'000, ' ')));
    ASSERT_RESULT_ERROR(parse_and_validate_config(contents + std::string(100'000, ' ') + "{}"),
                        ErrorCode::InvalidConfiguration);
}

TEST(Config, ValidatesDirectConstructionWithModuleAndApplicationPolicies) {
    ASSERT_RESULT_VALUE(valid, parse_optional_settings("{}"));
    struct InvalidSetting {
        std::string_view name;
        void (*change)(Config &);
    };
    const InvalidSetting cases[]{{"feed host",
                                  [](Config &config) {
                                      config.feed.host.clear();
                                  }},
                                 {"feed deadline",
                                  [](Config &config) {
                                      config.feed.close_timeout = 366 * 24h;
                                  }},
                                 {"window duration overflow",
                                  [](Config &config) {
                                      config.window.duration = Duration::max();
                                  }},
                                 {"window duration limit",
                                  [](Config &config) {
                                      config.window.duration = 366 * 24h;
                                  }},
                                 {"output destination",
                                  [](Config &config) {
                                      config.output.path.clear();
                                  }},
                                 {"output row threshold",
                                  [](Config &config) {
                                      config.output.flush_every_rows = 0;
                                  }},
                                 {"output interval",
                                  [](Config &config) {
                                      config.output.flush_interval = 366 * 24h;
                                  }},
                                 {"duplicate symbols", [](Config &config) {
                                      config.symbols.push_back(config.symbols[0]);
                                  }}};
    for (const auto &[name, change] : cases) {
        SCOPED_TRACE(name);
        Config config = valid;
        change(config);
        ASSERT_RESULT_ERROR(validate_config(config), ErrorCode::InvalidConfiguration);
    }
}

TEST(Config, ReportsFirstValidationFailure) {
    ASSERT_RESULT_VALUE(config, parse_optional_settings("{}"));
    const auto valid_feed = config.feed;
    const auto valid_window = config.window;
    config.symbols.push_back(config.symbols.front());
    config.feed.host.clear();
    config.window.duration = Duration::max();
    config.output.path.clear();

    const auto expect_first_error = [&](std::string_view diagnostic) {
        const auto result = validate_config(config);
        ASSERT_RESULT_ERROR(result, ErrorCode::InvalidConfiguration);
        EXPECT_TRUE(result.error().message.contains(diagnostic)) << result.error().message;
    };
    expect_first_error("symbols must be unique");
    config.symbols.pop_back();
    expect_first_error("feed host and port");
    config.feed = valid_feed;
    expect_first_error("between 1 second and 365 days");
    config.window.duration = 366 * 24h;
    expect_first_error("between 1 second and 365 days");
    config.window = valid_window;
    expect_first_error("output path");
}

TEST(Config, LoadsFilesAndResolvesOutputPath) {
    test::TemporaryDirectory fixture;
    test::write_file(fixture.file("config.json"), with_required_fields(R"({"output":{"path":"build/../prices.csv"}})"));
    ASSERT_RESULT_VALUE(config, load_config(fixture.file("config.json")));
    EXPECT_EQ(config.output.path, (fixture.file("prices.csv")).lexically_normal());
    // Large files still require a valid complete document.
    const auto large = with_required_fields(R"({"unused":")" + std::string(100'000, 'x') + R"("})");
    test::write_file(fixture.file("large.json"), large + std::string(100'000, ' '));
    ASSERT_RESULT_OK(load_config(fixture.file("large.json")));
    ASSERT_RESULT_ERROR(load_config(fixture.file("missing.json")), ErrorCode::FileIo);
    for (const auto &contents : {std::string{},
                                 std::string{"{"},
                                 std::string{"[]"},
                                 std::string{"{}"},
                                 with_required_fields("{}") + " trailing",
                                 large + std::string(100'000, ' ') + "{}",
                                 with_required_fields(R"({"output":{"flush_every_rows":0}})")}) {
        SCOPED_TRACE(contents);
        test::write_file(fixture.file("invalid.json"), contents);
        ASSERT_RESULT_ERROR(load_config(fixture.file("invalid.json")), ErrorCode::InvalidConfiguration);
    }
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
