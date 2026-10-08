add_library(coinbase_ticker_statistics_config config.cpp)

target_link_libraries(coinbase_ticker_statistics_config PUBLIC
    coinbase_ticker_statistics_feed
    glaze::glaze
)

target_include_directories(coinbase_ticker_statistics_config PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)

coinbase_ticker_statistics_enable_warnings(coinbase_ticker_statistics_config)
