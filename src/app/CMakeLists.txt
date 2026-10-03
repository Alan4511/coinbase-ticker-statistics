add_library(coinbase_ticker_statistics_application
    application.cpp
    statistics_processor.cpp
)

target_link_libraries(coinbase_ticker_statistics_application PUBLIC
    coinbase_ticker_statistics_statistics
    coinbase_ticker_statistics_feed
    coinbase_ticker_statistics_output
    coinbase_ticker_statistics_config
)

target_include_directories(coinbase_ticker_statistics_application PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)

coinbase_ticker_statistics_enable_warnings(coinbase_ticker_statistics_application)

add_executable(coinbase_ticker_statistics_app main.cpp)

set_target_properties(coinbase_ticker_statistics_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_link_libraries(coinbase_ticker_statistics_app PRIVATE
    coinbase_ticker_statistics_application
)

coinbase_ticker_statistics_enable_warnings(coinbase_ticker_statistics_app)
