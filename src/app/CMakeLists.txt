add_library(sparkland_application INTERFACE)

target_link_libraries(sparkland_application INTERFACE
    sparkland_statistics
    sparkland_feed
    sparkland_output
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
    add_executable(sparkland_app main.cpp)

    set_target_properties(sparkland_app PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    target_link_libraries(sparkland_app PRIVATE
        sparkland_application
    )

    sparkland_enable_warnings(sparkland_app)
endif()
