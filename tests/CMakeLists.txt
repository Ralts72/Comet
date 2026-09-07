# 收集所有测试源文件
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(REMOVE_ITEM TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/graphics/pipeline_cache_probe.cpp")
list(REMOVE_ITEM TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/graphics/test_swapchain_recovery.cpp")
list(REMOVE_ITEM TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/render/render_profile.cpp")
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
        comet_demo
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
            "${CMAKE_SOURCE_DIR}/editor/src/panels/render_stats.cpp"
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

# 构建树的动态库路径交由 CMake 从链接目标生成，不使用安装目录替代。

add_test(NAME unit_testing COMMAND unit_testing)
add_test(NAME render_graph_sync_validation COMMAND unit_testing
        "--gtest_filter=RenderGraphGpuTest.*:PostProcessGpuTest.*:RenderDiagnosticsGpuTest.*")
set_tests_properties(render_graph_sync_validation PROPERTIES
        ENVIRONMENT "VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT")
add_executable(pipeline_cache_probe graphics/pipeline_cache_probe.cpp)
target_link_libraries(pipeline_cache_probe PRIVATE engine glfw)
add_test(NAME pipeline_cache_process COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:pipeline_cache_probe>"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/pipeline-cache-process"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/graphics/test_pipeline_cache_process.cmake")
# 独立进程编译真实生产源，仅重命名四个 WSI 入口；不向 engine 注入故障开关。
add_executable(swapchain_recovery
        graphics/test_swapchain_recovery.cpp
        "${CMAKE_SOURCE_DIR}/engine/src/graphics/swapchain.cpp"
        "${CMAKE_SOURCE_DIR}/engine/src/graphics/queue.cpp"
        "${CMAKE_SOURCE_DIR}/engine/src/graphics/convert.cpp"
        "${CMAKE_SOURCE_DIR}/engine/src/render/scene/scene_renderer.cpp")
target_link_libraries(swapchain_recovery PRIVATE engine gtest_main glfw Vulkan::Vulkan)
target_compile_definitions(swapchain_recovery PRIVATE
        vkCreateSwapchainKHR=comet_test_create_swapchain
        vkGetSwapchainImagesKHR=comet_test_get_swapchain_images
        vkAcquireNextImageKHR=comet_test_acquire_next_image
        vkQueuePresentKHR=comet_test_queue_present)
add_test(NAME swapchain_recovery COMMAND swapchain_recovery)
add_executable(render_profile render/render_profile.cpp)
target_link_libraries(render_profile PRIVATE engine glfw)
target_compile_definitions(render_profile PRIVATE COMET_PROFILE_BUILD_TYPE="$<CONFIG>")
add_dependencies(render_profile shader_compile)
add_test(NAME render_profile_smoke COMMAND render_profile
        "${CMAKE_CURRENT_BINARY_DIR}/render-profile-smoke.csv" 4 160 120 8 1)
set_tests_properties(unit_testing pipeline_cache_process swapchain_recovery render_graph_sync_validation render_profile_smoke
        PROPERTIES RESOURCE_LOCK comet_graphics)
add_test(NAME shader_build_contract COMMAND "${CMAKE_COMMAND}"
        "-DCOMPILE_TOOL=$<TARGET_FILE:comet_shader_compiler>"
        "-DCOMET_ROOT=${CMAKE_SOURCE_DIR}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/shader-build-contract"
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DBUILD_CONFIG=$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/shader/test_build.cmake")
