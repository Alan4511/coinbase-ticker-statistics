#include "config/config.hpp"
#include "feed/subscription.hpp"

#include <nlohmann/json.hpp>

#include <concepts>
#include <fstream>
#include <iterator>
#include <ranges>
#include <set>
#include <utility>

namespace coinbase_ticker_statistics {
namespace {

using Json = nlohmann::json;

namespace key {
constexpr auto symbols = "symbols";
constexpr auto connections = "connections";
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
            constexpr auto maximum = std::same_as<T, Duration> ? maximum_duration_seconds : 86'400'000ULL;
            if (count == 0 || count > maximum)
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

Result<Connections> read_connections(const Json &connection_groups) {
    if (!connection_groups.is_array() || connection_groups.empty()) {
        return fail(ErrorCode::InvalidConfiguration, "connections must be a nonempty array");
    }
    Connections connections;
    std::set<Symbol> subscribed_symbols;
    for (const auto &item : connection_groups) {
        const auto products = item.find(key::symbols);
        if (products == item.end() || !products->is_array()) {
            return fail(ErrorCode::InvalidConfiguration, "each connection requires a symbols array");
        }
        Symbols symbols;
        for (const auto &product_id : *products) {
            if (!product_id.is_string() || product_id.get_ref<const Symbol &>().empty())
                return fail(ErrorCode::InvalidConfiguration, "symbols must be nonempty strings");
            const auto &symbol = product_id.get_ref<const Symbol &>();
            const auto [symbol_position, is_new_symbol] = subscribed_symbols.insert(symbol);
            if (!is_new_symbol)
                return fail(ErrorCode::InvalidConfiguration, "symbols must be unique across connections");
            symbols.push_back(*symbol_position);
        }
        if (auto valid = validate_subscription(symbols); !valid)
            return std::unexpected(valid.error());
        connections.push_back({std::move(symbols)});
    }
    return connections;
}

Result<FeedConfig> read_feed(const Json &root) {
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
    if (feed.max_message_bytes == 0 || feed.target.front() != '/')
        return fail(ErrorCode::InvalidConfiguration, "message size must be positive and target must start with /");
    return feed;
}

Result<WindowOptions> read_window(const Json &root) {
    auto object = read_config_section(root, key::window);
    if (!object)
        return std::unexpected(object.error());
    WindowOptions window;
    if (auto result = apply_setting_override(*object, key::duration_seconds, window.duration); !result)
        return std::unexpected(result.error());
    return window;
}

Result<CsvConfig> read_output(const Json &root) {
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
    if (output.flush_every_rows == 0)
        return fail(ErrorCode::InvalidConfiguration, "flush_every_rows must be positive");
    return output;
}

} // namespace

Symbols Config::symbols() const {
    const auto symbol_groups = connections | std::views::transform(&ConnectionConfig::symbols);
    return symbol_groups | std::views::join | std::ranges::to<Symbols>();
}

Result<Config> parse_config(std::string_view text) {
    const auto document = Json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object())
        return fail(ErrorCode::InvalidConfiguration, "expected a valid JSON configuration object");
    auto connections = read_field<Json>(document, key::connections).and_then(read_connections);
    if (!connections)
        return std::unexpected(connections.error());
    auto feed = read_feed(document);
    if (!feed)
        return std::unexpected(feed.error());
    auto window = read_window(document);
    if (!window)
        return std::unexpected(window.error());
    auto output = read_output(document);
    if (!output)
        return std::unexpected(output.error());
    return Config{std::move(*connections), std::move(*feed), *window, std::move(*output)};
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
