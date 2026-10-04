include(FetchContent)

# Keep the test dependency pinned so a fresh configure is reproducible.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.18.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googletest)

add_executable(coinbase_ticker_statistics_tests)

add_subdirectory(statistics)
add_subdirectory(feed)
add_subdirectory(output)
add_subdirectory(config)
add_subdirectory(app)

target_include_directories(coinbase_ticker_statistics_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(coinbase_ticker_statistics_tests PRIVATE
    coinbase_ticker_statistics_application
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    Threads::Threads
)

target_compile_definitions(coinbase_ticker_statistics_tests PRIVATE
    COINBASE_TICKER_STATISTICS_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

coinbase_ticker_statistics_enable_warnings(coinbase_ticker_statistics_tests)

include(GoogleTest)
gtest_discover_tests(coinbase_ticker_statistics_tests NO_PRETTY_VALUES PROPERTIES TIMEOUT 15)
