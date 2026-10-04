#include "config/config.hpp"
#include <feed/subscription.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <concepts>
#include <fstream>
#include <iterator>
#include <set>
#include <utility>

namespace coinbase_ticker_statistics {
namespace {

using Json = nlohmann::json;

namespace key {
constexpr auto symbols = "symbols";
constexpr auto feed = "feed";
constexpr auto window = "window";
constexpr auto output = "output";
constexpr auto host = "host";
constexpr auto port = "port";
constexpr auto target = "target";
constexpr auto connect_timeout_seconds = "connect_timeout_seconds";
constexpr auto close_timeout_seconds = "close_timeout_seconds";
constexpr auto max_message_bytes = "max_message_bytes";
constexpr auto duration_seconds = "duration_seconds";
constexpr auto path = "path";
constexpr auto flush_every_rows = "flush_every_rows";
constexpr auto flush_interval_ms = "flush_interval_ms";
} // namespace key

constexpr std::uint64_t maximum_duration_seconds = 365U * 24U * 60U * 60U;

/** Read an optional section; missing sections use their documented defaults. */
Result<Json> read_config_section(const Json &root, const char *name) {
    const auto item = root.find(name);
    if (item == root.end())
        return Json::object();
    if (!item->is_object())
        return fail(ErrorCode::InvalidConfiguration, std::string(name) + ": expected an object");
    return *item;
}

/** Strict conversion before get<T>: nlohmann arithmetic conversions can otherwise narrow/coerce. */
template <typename T>
Result<T> decode_config_value(const Json &value, const char *name) {
    const auto invalid_field = [name](const char *description) -> Result<T> {
        return fail(ErrorCode::InvalidConfiguration, std::string(name) + ": " + description);
    };
    if constexpr (std::same_as<T, Json>) {
        return value;
    }
    else if constexpr (std::same_as<T, std::string>) {
        if (!value.is_string() || value.get_ref<const std::string &>().empty())
            return invalid_field("expected nonempty text");
        return value.get<T>();
    }
    else if constexpr (std::unsigned_integral<T>) {
        std::uint64_t count{};
        if (value.is_number_unsigned())
            count = value.get<std::uint64_t>();
        else if (value.is_number_integer() && value.get<std::int64_t>() >= 0)
            count = static_cast<std::uint64_t>(value.get<std::int64_t>());
        else
            return invalid_field("expected a nonnegative integer");
        if (!std::in_range<T>(count))
            return invalid_field("integer exceeds the destination range");
        return static_cast<T>(count);
    }
    else if constexpr (std::same_as<T, std::filesystem::path>) {
        return decode_config_value<std::string>(value, name).transform([](const auto &text) {
            return T{text};
        });
    }
    else if constexpr (std::same_as<T, Duration> || std::same_as<T, std::chrono::milliseconds>) {
        return decode_config_value<std::uint64_t>(value, name).and_then([invalid_field](auto count) -> Result<T> {
            const auto maximum_count = std::chrono::duration_cast<T>(Duration{maximum_duration_seconds}).count();
            if (count == 0 || count > static_cast<std::uint64_t>(maximum_count))
                return invalid_field("duration must be positive and within the supported range");
            return T{count};
        });
    }
    else {
        static_assert(sizeof(T) == 0, "unsupported configuration field type");
    }
}

/** Required fields never fall back to an implicit subscription or destination. */
template <typename T>
Result<T> read_field(const Json &object, const char *name) {
    const auto field = object.find(name);
    if (field == object.end())
        return fail(ErrorCode::InvalidConfiguration, std::string(name) + ": required field is missing");
    return decode_config_value<T>(*field, name);
}

/** Optional operational fields retain their documented defaults. */
template <typename T>
Result<void> apply_setting_override(const Json &object, const char *name, T &destination) {
    const auto field = object.find(name);
    if (field == object.end())
        return {};
    auto parsed_value = decode_config_value<T>(*field, name);
    if (!parsed_value)
        return std::unexpected(parsed_value.error());
    destination = std::move(*parsed_value);
    return {};
}

Result<Symbols> read_symbols(const Json &products) {
    if (!products.is_array() || products.empty())
        return fail(ErrorCode::InvalidConfiguration, "symbols must be a nonempty array");
    Symbols symbols;
    for (const auto &product : products) {
        auto symbol = decode_config_value<Symbol>(product, key::symbols);
        if (!symbol)
            return std::unexpected(symbol.error());
        symbols.push_back(std::move(*symbol));
    }
    return symbols;
}

Result<FeedConfig> read_feed_config(const Json &root) {
    auto object = read_config_section(root, key::feed);
    if (!object)
        return std::unexpected(object.error());
    FeedConfig feed;
    if (auto result = apply_setting_override(*object, key::host, feed.host); !result)
        return std::unexpected(result.error());
    if (auto result = apply_setting_override(*object, key::port, feed.port); !result)
        return std::unexpected(result.error());
    if (auto result = apply_setting_override(*object, key::target, feed.target); !result)
        return std::unexpected(result.error());
    if (auto result = apply_setting_override(*object, key::connect_timeout_seconds, feed.connect_timeout); !result)
        return std::unexpected(result.error());
    if (auto result = apply_setting_override(*object, key::close_timeout_seconds, feed.close_timeout); !result)
        return std::unexpected(result.error());
    if (auto result = apply_setting_override(*object, key::max_message_bytes, feed.max_message_bytes); !result)
        return std::unexpected(result.error());
    return feed;
}

Result<WindowOptions> read_window_options(const Json &root) {
    auto object = read_config_section(root, key::window);
    if (!object)
        return std::unexpected(object.error());
    WindowOptions window;
    if (auto result = apply_setting_override(*object, key::duration_seconds, window.duration); !result)
        return std::unexpected(result.error());
    return window;
}

Result<CsvConfig> read_csv_config(const Json &root) {
    auto object = read_field<Json>(root, key::output);
    if (!object)
        return std::unexpected(object.error());
    if (!object->is_object())
        return fail(ErrorCode::InvalidConfiguration, "output: expected an object");
    auto path = read_field<std::filesystem::path>(*object, key::path);
    if (!path)
        return std::unexpected(path.error());
    CsvConfig output{std::move(*path)};
    if (auto result = apply_setting_override(*object, key::flush_every_rows, output.flush_every_rows); !result)
        return std::unexpected(result.error());
    if (auto result = apply_setting_override(*object, key::flush_interval_ms, output.flush_interval); !result)
        return std::unexpected(result.error());
    return output;
}

} // namespace

Result<void> validate_config(const Config &config) {
    if (auto valid = validate_product_ids(config.symbols); !valid)
        return valid;
    std::set<Symbol> subscribed_symbols;
    for (const auto &symbol : config.symbols) {
        if (!subscribed_symbols.insert(symbol).second)
            return fail(ErrorCode::InvalidConfiguration, "symbols must be unique");
    }
    if (auto valid = validate_feed_config(config.feed); !valid)
        return valid;
    if (auto valid = validate_window_options(config.window); !valid)
        return valid;
    if (config.window.duration > Duration{maximum_duration_seconds})
        return fail(ErrorCode::InvalidConfiguration, "window.duration_seconds must be between 1 and 31536000");
    return validate_csv_config(config.output);
}

Result<Config> parse_config(std::string_view text) {
    const auto document = Json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object())
        return fail(ErrorCode::InvalidConfiguration, "expected a valid JSON configuration object");
    auto symbols = read_field<Json>(document, key::symbols).and_then(read_symbols);
    if (!symbols)
        return std::unexpected(symbols.error());
    auto feed = read_feed_config(document);
    if (!feed)
        return std::unexpected(feed.error());
    auto window = read_window_options(document);
    if (!window)
        return std::unexpected(window.error());
    auto output = read_csv_config(document);
    if (!output)
        return std::unexpected(output.error());
    Config config{std::move(*symbols), std::move(*feed), *window, std::move(*output)};
    if (auto valid = validate_config(config); !valid)
        return std::unexpected(std::move(valid.error()));
    return config;
}

Result<Config> load_config(const std::filesystem::path &path) {
    std::ifstream stream(path);
    if (!stream)
        return fail(ErrorCode::FileIo, "cannot open configuration file: " + path.string());
    const std::string contents{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (stream.bad())
        return fail(ErrorCode::FileIo, "cannot read configuration file: " + path.string());
    auto config = parse_config(contents);
    if (!config)
        return config;
    std::error_code error;
    const auto directory = std::filesystem::absolute(path, error).parent_path();
    if (error)
        return fail(ErrorCode::FileIo, "cannot resolve configuration path: " + error.message());
    config->output.path = (directory / config->output.path).lexically_normal();
    return config;
}

} // namespace coinbase_ticker_statistics
