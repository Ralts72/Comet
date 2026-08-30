# 收集所有测试源文件
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(APPEND TEST_SOURCES
        "${CMAKE_SOURCE_DIR}/editor/src/editor_command_history.cpp"
        "${CMAKE_SOURCE_DIR}/editor/src/editor_scene_session.cpp"
        "${CMAKE_SOURCE_DIR}/editor/src/editor_camera_controller.cpp"
        "${CMAKE_SOURCE_DIR}/editor/src/transform_gizmo.cpp"
        "${CMAKE_SOURCE_DIR}/editor/src/viewport_layout.cpp"
)

# 创建单元测试可执行文件
add_executable(unit_testing ${TEST_SOURCES})

# 链接库，确保正确的依赖顺序
target_link_libraries(unit_testing PRIVATE
        engine
        gtest
        glfw
        Vulkan::Vulkan
)

# 设置包含目录
target_include_directories(unit_testing PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/engine/src
        ${CMAKE_SOURCE_DIR}/editor/src
)

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
