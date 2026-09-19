file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(REMOVE_ITEM TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/graphics/pipeline_cache_probe.cpp")
list(REMOVE_ITEM TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/graphics/test_swapchain_recovery.cpp")
file(GLOB_RECURSE INTEGRATION_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/render/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cpp")
list(APPEND INTEGRATION_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/core/test_window.cpp")
list(REMOVE_ITEM INTEGRATION_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/pipeline_cache_probe.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/graphics/test_swapchain_recovery.cpp")
foreach(scope command_history scene_commands scene_document editor_assets editor_scene_session
        material_editing selection shader_reload camera_controller transform_gizmo viewport_layout)
    list(REMOVE_ITEM INTEGRATION_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/editor/test_${scope}.cpp")
endforeach()
list(REMOVE_ITEM TEST_SOURCES ${INTEGRATION_SOURCES})
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

# AppKit animations can occupy dispatch workers needed by Metal compilation.
set(TEST_PLATFORM_ARGS)
if (APPLE)
    list(APPEND TEST_PLATFORM_ARGS -NSAutomaticWindowAnimationsEnabled NO)
endif ()
add_test(NAME unit_testing COMMAND unit_testing
        --gtest_filter=-*/ProjectAssetIntegrationTest.* ${TEST_PLATFORM_ARGS})
set_tests_properties(unit_testing PROPERTIES LABELS "unit")
add_test(NAME integration_testing COMMAND integration_testing ${TEST_PLATFORM_ARGS})
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
# Isolated WSI fault injection: compile the actual consumers with renamed Vulkan entry points.
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
        --gtest_filter=RenderGraphGpuTest.*:BloomGpuTest.* ${TEST_PLATFORM_ARGS})
set_tests_properties(render_graph_sync_validation PROPERTIES
        ENVIRONMENT "VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT"
        RESOURCE_LOCK comet_gpu TIMEOUT 90 LABELS "integration")
