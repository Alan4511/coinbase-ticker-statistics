#include "config/config.hpp"
#include <feed/subscription.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <utility>

namespace coinbase_ticker_statistics {
Result<void> validate_config(const Config &config) {
    if (auto valid = validate(config.symbols); !valid)
        return valid;
    std::set<Symbol> subscribed_symbols;
    for (const auto &symbol : config.symbols) {
        if (!subscribed_symbols.insert(symbol).second)
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
        auto config = parse_config(contents);
        if (!config)
            return config;
        std::error_code error;
        const auto directory = std::filesystem::absolute(path, error).parent_path();
        if (error)
            return fail(ErrorCode::FileIo, "cannot resolve configuration path: " + error.message());
        config->output.path = (directory / config->output.path).lexically_normal();
        return config;
    } catch (const std::ios_base::failure &error) {
        return fail(ErrorCode::FileIo, "cannot read configuration file: " + path.string() + ": " + error.what());
    }
}

} // namespace coinbase_ticker_statistics
