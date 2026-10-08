add_library(coinbase_ticker_statistics_statistics
    sliding_window.cpp
    statistics_processor.cpp
)

target_link_libraries(coinbase_ticker_statistics_statistics PUBLIC Boost::headers glaze::glaze)

target_include_directories(coinbase_ticker_statistics_statistics PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)
coinbase_ticker_statistics_enable_warnings(coinbase_ticker_statistics_statistics)
