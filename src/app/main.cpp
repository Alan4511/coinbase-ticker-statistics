#include "app/application.hpp"
#include "app/logger.hpp"
#include "config/config.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

namespace {
constexpr std::string_view config_option = "--config";
constexpr std::string_view help_option = "--help";
constexpr std::string_view usage = "Usage: coinbase_ticker_statistics_app --config <file.json>\n";
constexpr int help_argument_count = 2;
constexpr int config_argument_count = 3;
} // namespace

int main(int argc, char **argv) {
    if (argc == help_argument_count && std::string_view(argv[1]) == help_option) {
        std::cout << usage;
        return EXIT_SUCCESS;
    }
    if (argc != config_argument_count || std::string_view(argv[1]) != config_option) {
        std::cerr << usage;
        return EXIT_FAILURE;
    }
    try {
        const auto result = coinbase_ticker_statistics::load_config(argv[2]).and_then([](const auto &config) {
            return coinbase_ticker_statistics::run_application(config, std::cerr);
        });
        if (!result) {
            coinbase_ticker_statistics::log_message(std::cerr,
                                                    coinbase_ticker_statistics::LogLevel::Error,
                                                    "application failed: ",
                                                    result.error().message);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        // Last-resort containment for allocation/library exceptions, not ordinary
        // parsing, validation, statistics, or I/O error handling.
        coinbase_ticker_statistics::log_message(std::cerr,
                                                coinbase_ticker_statistics::LogLevel::Error,
                                                "unexpected failure: ",
                                                error.what());
        return EXIT_FAILURE;
    }
}
