add_library(coinbase_ticker_statistics_feed
    transport/ticker_feed.cpp
    parser/ticker_parser.cpp
    parser/parse_fields.cpp
    subscription.cpp
)

target_link_libraries(coinbase_ticker_statistics_feed PUBLIC
    Boost::headers
    PRIVATE nlohmann_json::nlohmann_json OpenSSL::SSL OpenSSL::Crypto Threads::Threads
)

target_include_directories(coinbase_ticker_statistics_feed PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)

coinbase_ticker_statistics_enable_warnings(coinbase_ticker_statistics_feed)
