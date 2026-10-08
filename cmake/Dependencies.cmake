include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

find_package(OpenSSL 3.0 REQUIRED)
find_package(Threads REQUIRED)
find_package(Boost 1.88 QUIET CONFIG)

if(NOT TARGET Boost::headers)
    # Beast and Asio need no Boost binaries.
    FetchContent_Declare(boost_headers
        URL https://archives.boost.io/release/1.88.0/source/boost_1_88_0.tar.bz2
        URL_HASH SHA256=46d9d2c06637b219270877c9e16155cbd015b6dc84349af064c088e9b5b12f7b
        SOURCE_SUBDIR header_only
    )
    FetchContent_MakeAvailable(boost_headers)
    add_library(coinbase_boost_headers INTERFACE)
    target_include_directories(coinbase_boost_headers SYSTEM INTERFACE "${boost_headers_SOURCE_DIR}")
    add_library(Boost::headers ALIAS coinbase_boost_headers)
endif()

# Consume only Glaze's headers; its upstream build requires CMake 3.31.
FetchContent_Declare(glaze
    URL https://codeload.github.com/stephenberry/glaze/tar.gz/refs/tags/v9.0.0
    URL_HASH SHA256=dd7b033c5bf6e4308615bb7834c541291d5b20f64121d9a68222a39414635517
    SOURCE_SUBDIR header_only
)
FetchContent_MakeAvailable(glaze)
add_library(coinbase_glaze INTERFACE)
target_include_directories(coinbase_glaze SYSTEM INTERFACE "${glaze_SOURCE_DIR}/include")
target_compile_features(coinbase_glaze INTERFACE cxx_std_23)
add_library(glaze::glaze ALIAS coinbase_glaze)
