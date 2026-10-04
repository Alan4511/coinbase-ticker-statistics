include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

find_package(OpenSSL 3.0 REQUIRED)
find_package(Threads REQUIRED)
find_package(Boost 1.83 QUIET CONFIG)

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

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)
# Keep strict project warnings from being applied to vendor implementation details.
get_target_property(json_include_dirs nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
set_property(TARGET nlohmann_json PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${json_include_dirs}")
