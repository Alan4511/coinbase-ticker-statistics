target_sources(coinbase_ticker_statistics_tests PRIVATE
    parser/test_ticker_parser.cpp
    transport/test_feed_connection.cpp
    transport/test_ticker_feed.cpp
    parser/test_parse_fields.cpp
    test_subscription.cpp
)
