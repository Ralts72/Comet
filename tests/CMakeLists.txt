file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
add_executable(unit_testing ${TEST_SOURCES})
target_link_libraries(unit_testing PRIVATE editor_core gtest glfw)
target_compile_definitions(unit_testing PRIVATE GLFW_INCLUDE_NONE)
comet_copy_runtime_dependencies(unit_testing)

if (COMET_BUILD_EDITOR)
    target_link_libraries(unit_testing PRIVATE editor_ui)
    target_compile_definitions(unit_testing PRIVATE COMET_TEST_EDITOR_UI)
endif ()

target_include_directories(unit_testing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME unit_testing COMMAND unit_testing)
