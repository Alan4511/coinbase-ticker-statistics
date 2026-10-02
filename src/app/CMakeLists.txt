add_library(coinbase_ticker_statistics_application INTERFACE)

target_link_libraries(coinbase_ticker_statistics_application INTERFACE
    coinbase_ticker_statistics_statistics
    coinbase_ticker_statistics_feed
    coinbase_ticker_statistics_output
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
    add_executable(coinbase_ticker_statistics_app main.cpp)

    set_target_properties(coinbase_ticker_statistics_app PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    target_link_libraries(coinbase_ticker_statistics_app PRIVATE
        coinbase_ticker_statistics_application
    )

    coinbase_ticker_statistics_enable_warnings(coinbase_ticker_statistics_app)
endif()
