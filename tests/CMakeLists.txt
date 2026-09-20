file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
set(ISOLATED_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/pipeline_cache_probe.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/test_swapchain_recovery.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/render/test_post_process_recovery.cpp")
file(GLOB_RECURSE INTEGRATION_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/render/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/editor/ui/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/editor/integration/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cpp")
list(APPEND INTEGRATION_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/core/test_window.cpp")
# 目录描述职责，运行分组按是否实际创建窗口／GPU／ImGui 区分。
set(CPU_RENDER_SOURCES
        graphics/test_device_capability.cpp
        graphics/test_resource_state.cpp
        graphics/test_viewport.cpp
        graphics/test_vulkan_raii_ownership.cpp
        render/test_lighting.cpp
        render/test_line_draw_list.cpp
        render/test_material.cpp
        render/test_material_shader.cpp
        render/test_render_graph.cpp
        render/test_scene_extractor.cpp
        render/test_scene_picking.cpp)
list(TRANSFORM CPU_RENDER_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(REMOVE_ITEM INTEGRATION_SOURCES ${ISOLATED_TEST_SOURCES} ${CPU_RENDER_SOURCES})
list(REMOVE_ITEM TEST_SOURCES ${INTEGRATION_SOURCES} ${ISOLATED_TEST_SOURCES})
add_executable(unit_testing ${TEST_SOURCES})
add_executable(integration_testing test.cpp ${INTEGRATION_SOURCES})
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
foreach(target unit_testing integration_testing)
    add_dependencies(${target} shader_compile test_shader_compile)
    target_include_directories(${target} PRIVATE
            "${CMAKE_BINARY_DIR}/engine/generated/shaders/include"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/include"
            ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${target} PRIVATE editor_core comet_shader_tools gtest glfw)
    target_compile_definitions(${target} PRIVATE GLFW_INCLUDE_NONE)
    comet_copy_runtime_dependencies(${target})
endforeach()

if (COMET_BUILD_EDITOR)
    target_link_libraries(integration_testing PRIVATE editor_ui)
    target_compile_definitions(integration_testing PRIVATE COMET_TEST_EDITOR_UI)
endif ()

# AppKit 窗口动画可能占用 Metal 编译所需的调度线程，因此仅在测试进程中关闭动画。
set(TEST_PLATFORM_ARGS)
if (APPLE)
    list(APPEND TEST_PLATFORM_ARGS -NSAutomaticWindowAnimationsEnabled NO)
endif ()
add_test(NAME unit_testing COMMAND unit_testing
        --gtest_filter=-*/ProjectAssetIntegrationTest.* ${TEST_PLATFORM_ARGS})
set_tests_properties(unit_testing PROPERTIES LABELS "unit")
set(SYNC_VALIDATION_FILTER "RenderGraphGpuTest.*:BloomGpuTest.*:RenderDiagnosticsGpuTest.*")
add_test(NAME integration_testing COMMAND integration_testing
        "--gtest_filter=-${SYNC_VALIDATION_FILTER}" ${TEST_PLATFORM_ARGS})
set_tests_properties(integration_testing PROPERTIES LABELS "integration" RESOURCE_LOCK comet_gpu)
option(COMET_TEST_DOWNLOADED_ASSETS "Enable tests of optional downloaded demo assets" OFF)
if(COMET_TEST_DOWNLOADED_ASSETS)
    add_test(NAME downloaded_assets COMMAND unit_testing
            --gtest_filter=*/ProjectAssetIntegrationTest.* ${TEST_PLATFORM_ARGS})
    set_tests_properties(downloaded_assets PROPERTIES LABELS "assets;slow")
endif()
add_test(NAME shader_build_contract COMMAND "${CMAKE_COMMAND}"
        "-DCOMPILE_TOOL=$<TARGET_FILE:comet_shader_compiler>"
        "-DCOMET_ROOT=${CMAKE_SOURCE_DIR}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/shader-build-contract"
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DBUILD_CONFIG=$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/shader/test_build.cmake")

add_executable(pipeline_cache_probe graphics/pipeline_cache_probe.cpp)
target_link_libraries(pipeline_cache_probe PRIVATE engine)
comet_copy_runtime_dependencies(pipeline_cache_probe)
add_test(NAME pipeline_cache_process COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:pipeline_cache_probe>"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/pipeline-cache-process"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/graphics/test_pipeline_cache_process.cmake")
# 仅为此测试重命名 Vulkan 入口，编译真实调用方进行隔离的 WSI 故障注入。
add_executable(swapchain_recovery
        graphics/test_swapchain_recovery.cpp
        "${CMAKE_SOURCE_DIR}/engine/src/graphics/swapchain.cpp"
        "${CMAKE_SOURCE_DIR}/engine/src/graphics/queue.cpp"
        "${CMAKE_SOURCE_DIR}/engine/src/graphics/convert.cpp"
        "${CMAKE_SOURCE_DIR}/engine/src/render/render_context.cpp"
        "${CMAKE_SOURCE_DIR}/engine/src/render/presentation.cpp")
target_link_libraries(swapchain_recovery PRIVATE engine gtest_main Vulkan::Vulkan)
target_compile_definitions(swapchain_recovery PRIVATE
        vkCreateSwapchainKHR=comet_test_create_swapchain
        vkGetSwapchainImagesKHR=comet_test_get_swapchain_images
        vkGetPhysicalDeviceSurfaceFormatsKHR=comet_test_get_surface_formats
        vkAcquireNextImageKHR=comet_test_acquire_next_image
        vkQueuePresentKHR=comet_test_queue_present)
comet_copy_runtime_dependencies(swapchain_recovery)
add_test(NAME swapchain_recovery COMMAND swapchain_recovery ${TEST_PLATFORM_ARGS})
set_tests_properties(swapchain_recovery PROPERTIES TIMEOUT 90)
set_tests_properties(pipeline_cache_process swapchain_recovery
        PROPERTIES RESOURCE_LOCK comet_gpu LABELS "integration")
add_test(NAME render_graph_sync_validation COMMAND integration_testing
        "--gtest_filter=${SYNC_VALIDATION_FILTER}" ${TEST_PLATFORM_ARGS})
set_tests_properties(render_graph_sync_validation PROPERTIES
        ENVIRONMENT "VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT"
        RESOURCE_LOCK comet_gpu TIMEOUT 90 LABELS "integration")

add_executable(post_process_recovery test.cpp render/test_post_process_recovery.cpp
        "${CMAKE_SOURCE_DIR}/engine/src/render/renderer.cpp"
        "${CMAKE_SOURCE_DIR}/engine/src/render/scene/scene_renderer.cpp"
        "${CMAKE_SOURCE_DIR}/engine/src/graphics/convert.cpp")
target_link_libraries(post_process_recovery PRIVATE engine gtest)
target_compile_definitions(post_process_recovery PRIVATE COMET_EXPORTS)
comet_copy_runtime_dependencies(post_process_recovery)
add_test(NAME post_process_recovery COMMAND post_process_recovery ${TEST_PLATFORM_ARGS})
set_tests_properties(post_process_recovery PROPERTIES
        RESOURCE_LOCK comet_gpu TIMEOUT 90 LABELS "integration")

if (TARGET render_benchmark)
    add_test(NAME render_benchmark_smoke COMMAND "${CMAKE_COMMAND}"
            "-DBENCHMARK=$<TARGET_FILE:render_benchmark>"
            "-DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/render-benchmark"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/render/test_benchmark.cmake")
    set_tests_properties(render_benchmark_smoke PROPERTIES
            RESOURCE_LOCK comet_gpu TIMEOUT 90 LABELS "integration")
endif ()
