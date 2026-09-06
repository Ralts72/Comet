# 收集所有测试源文件
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(APPEND TEST_SOURCES
        "${CMAKE_SOURCE_DIR}/editor/src/command_history.cpp"
        "${CMAKE_SOURCE_DIR}/editor/src/scene_commands.cpp"
        "${CMAKE_SOURCE_DIR}/editor/src/transform_gizmo.cpp"
        "${CMAKE_SOURCE_DIR}/editor/src/camera_controller.cpp"
        "${CMAKE_SOURCE_DIR}/editor/src/editor_state.cpp"
        "${CMAKE_SOURCE_DIR}/editor/src/editor_scene_session.cpp"
        "${CMAKE_SOURCE_DIR}/editor/src/scene_document.cpp"
        "${CMAKE_SOURCE_DIR}/editor/src/shader_reload.cpp"
        "${CMAKE_SOURCE_DIR}/editor/src/viewport_layout.cpp"
)

# 创建单元测试可执行文件
add_executable(unit_testing ${TEST_SOURCES})

include("${CMAKE_SOURCE_DIR}/engine/cmake/compile_shader.cmake")
compile_shaders(
        TARGET test_shader_compile
        COMPILER comet_shader_compiler
        OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders"
        SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/shaders/interface_array.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/shaders/material_integer.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/shaders/runtime_array.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/shaders/pipeline_triangle.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/shaders/pipeline_color.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/shaders/specialization.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/shaders/specialization.frag"
)
add_dependencies(unit_testing shader_compile test_shader_compile)
target_include_directories(unit_testing PRIVATE
        "${CMAKE_BINARY_DIR}/engine/generated/shaders/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/include")

# 链接库，确保正确的依赖顺序
target_link_libraries(unit_testing PRIVATE
        engine
        comet_shader_tools
        gtest
        glfw
        Vulkan::Vulkan
)

# 仅启用编辑器时测试 UI 交互与数据寿命，无需窗口或 Vulkan 后端。
if (COMET_BUILD_EDITOR)
    target_sources(unit_testing PRIVATE
            "${CMAKE_SOURCE_DIR}/editor/src/panels/console.cpp"
            "${CMAKE_SOURCE_DIR}/editor/src/panels/inspector.cpp"
            "${CMAKE_SOURCE_DIR}/editor/src/panels/hierarchy.cpp"
            "${CMAKE_SOURCE_DIR}/editor/src/panels/project.cpp"
            "${CMAKE_SOURCE_DIR}/editor/src/menu_bar.cpp"
            "${CMAKE_SOURCE_DIR}/editor/src/property_editor_registry.cpp"
            "${CMAKE_SOURCE_DIR}/editor/src/panels/view.cpp")
    target_link_libraries(unit_testing PRIVATE imgui)
    target_compile_definitions(unit_testing PRIVATE COMET_TEST_EDITOR_UI)
endif ()

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
add_test(NAME shader_build_contract COMMAND "${CMAKE_COMMAND}"
        "-DCOMPILE_TOOL=$<TARGET_FILE:comet_shader_compiler>"
        "-DCOMET_ROOT=${CMAKE_SOURCE_DIR}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/shader-build-contract"
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DBUILD_CONFIG=$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/shader/test_build.cmake")
