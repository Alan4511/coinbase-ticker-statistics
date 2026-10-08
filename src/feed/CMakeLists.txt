add_library(coinbase_ticker_statistics_feed
    transport/feed_connection.cpp
    parser/ticker_parser.cpp
    parser/parse_fields.cpp
    subscription.cpp
)

target_link_libraries(coinbase_ticker_statistics_feed PUBLIC
    Boost::headers
    glaze::glaze
    PRIVATE coinbase_ticker_statistics_common OpenSSL::SSL OpenSSL::Crypto Threads::Threads
)

target_include_directories(coinbase_ticker_statistics_feed PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)

coinbase_ticker_statistics_enable_warnings(coinbase_ticker_statistics_feed)
