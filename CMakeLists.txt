cmake_minimum_required(VERSION 3.20)

project(coinbase_ticker_statistics VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(COINBASE_TICKER_STATISTICS_BUILD_TESTS "Build unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

option(COINBASE_TICKER_STATISTICS_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
set(COINBASE_TICKER_STATISTICS_SANITIZER_SET "address,undefined" CACHE STRING "Comma-separated compiler sanitizers")
if(COINBASE_TICKER_STATISTICS_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fsanitize=${COINBASE_TICKER_STATISTICS_SANITIZER_SET} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${COINBASE_TICKER_STATISTICS_SANITIZER_SET})
endif()

add_subdirectory(src)

if(COINBASE_TICKER_STATISTICS_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
