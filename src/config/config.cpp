#include "config/config.hpp"
#include "config/json_meta.hpp"
#include <feed/subscription.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <utility>

namespace coinbase_ticker_statistics {
Result<Config> parse_and_validate_config(std::string_view input) {
    return json_utils::read_json<Config>(input, ErrorCode::InvalidConfiguration).and_then([](Config config) {
        return validate_config(config).transform([&] {
            return std::move(config);
        });
    });
}

Result<void> validate_config(const Config &config) {
    if (auto valid = validate(config.symbols); !valid.has_value())
        return valid;
    std::set<Symbol> subscribed_symbols;
    for (const auto &symbol : config.symbols) {
        const auto [symbol_position, is_new_symbol] = subscribed_symbols.insert(symbol);
        if (!is_new_symbol)
            return fail(ErrorCode::InvalidConfiguration, "symbols must be unique");
    }
    return validate(config.feed)
        .and_then([&] {
            return validate(config.window);
        })
        .and_then([&] {
            return validate(config.output);
        });
}

Result<Config> load_config(const std::filesystem::path &path) {
    std::ifstream stream(path);
    if (!stream)
        return fail(ErrorCode::FileIo, "cannot open configuration file: " + path.string());
    try {
        const std::string contents{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        if (stream.bad())
            return fail(ErrorCode::FileIo, "cannot read configuration file: " + path.string());
        auto config = parse_and_validate_config(contents);
        if (!config.has_value())
            return config;
        std::error_code error;
        const auto directory = std::filesystem::absolute(path, error).parent_path();
        if (error)
            return fail(ErrorCode::FileIo, "cannot resolve configuration path: " + error.message());
        config.value().output.path = (directory / config.value().output.path).lexically_normal();
        return config;
    } catch (const std::ios_base::failure &error) {
        return fail(ErrorCode::FileIo, "cannot read configuration file: " + path.string() + ": " + error.what());
    }
}

} // namespace coinbase_ticker_statistics
