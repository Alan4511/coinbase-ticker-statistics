file(GLOB SPARKLAND_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp"
)

if(NOT SPARKLAND_TEST_SOURCES)
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

add_executable(sparkland_tests
    ${SPARKLAND_TEST_SOURCES}
)

target_link_libraries(sparkland_tests PRIVATE
    sparkland_application
    GTest::gtest_main
)

target_compile_definitions(sparkland_tests PRIVATE
    SPARKLAND_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

sparkland_enable_warnings(sparkland_tests)

include(GoogleTest)
gtest_discover_tests(sparkland_tests)
