file(GLOB COINBASE_TICKER_STATISTICS_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp"
)

if(NOT COINBASE_TICKER_STATISTICS_TEST_SOURCES)
    return()
endif()

include(FetchContent)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.18.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googletest)

add_executable(coinbase_ticker_statistics_tests
    ${COINBASE_TICKER_STATISTICS_TEST_SOURCES}
)

target_link_libraries(coinbase_ticker_statistics_tests PRIVATE
    coinbase_ticker_statistics_application
    GTest::gtest_main
)

target_compile_definitions(coinbase_ticker_statistics_tests PRIVATE
    COINBASE_TICKER_STATISTICS_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

coinbase_ticker_statistics_enable_warnings(coinbase_ticker_statistics_tests)

include(GoogleTest)
gtest_discover_tests(coinbase_ticker_statistics_tests)
