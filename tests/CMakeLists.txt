file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
add_executable(unit_testing ${TEST_SOURCES})
target_link_libraries(unit_testing PRIVATE editor_core gtest)

if (COMET_BUILD_EDITOR)
    target_link_libraries(unit_testing PRIVATE editor_ui)
    target_compile_definitions(unit_testing PRIVATE COMET_TEST_EDITOR_UI)
endif ()

target_include_directories(unit_testing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 设置运行时库路径
if (APPLE)
    set_target_properties(unit_testing PROPERTIES
            INSTALL_RPATH "@executable_path/../engine;@loader_path/../engine;/usr/local/lib"
            BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif (UNIX)
    set_target_properties(unit_testing PROPERTIES
            INSTALL_RPATH "$ORIGIN/../engine:$ORIGIN/../lib:/usr/local/lib"
            BUILD_WITH_INSTALL_RPATH TRUE
    )
endif ()

add_test(NAME unit_testing COMMAND unit_testing)
