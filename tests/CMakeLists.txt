file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(REMOVE_ITEM TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/graphics/pipeline_cache_probe.cpp")
list(REMOVE_ITEM TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/graphics/test_swapchain_recovery.cpp")
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

target_link_libraries(unit_testing PRIVATE editor_core comet_shader_tools gtest glfw)
target_compile_definitions(unit_testing PRIVATE GLFW_INCLUDE_NONE)
comet_copy_runtime_dependencies(unit_testing)

if (COMET_BUILD_EDITOR)
    target_link_libraries(unit_testing PRIVATE editor_ui)
    target_compile_definitions(unit_testing PRIVATE COMET_TEST_EDITOR_UI)
endif ()

target_include_directories(unit_testing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# AppKit animations can occupy dispatch workers needed by Metal compilation.
set(TEST_PLATFORM_ARGS)
if (APPLE)
    list(APPEND TEST_PLATFORM_ARGS -NSAutomaticWindowAnimationsEnabled NO)
endif ()
add_test(NAME unit_testing COMMAND unit_testing ${TEST_PLATFORM_ARGS})
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
set_tests_properties(unit_testing pipeline_cache_process swapchain_recovery
        PROPERTIES RESOURCE_LOCK comet_gpu)
add_test(NAME render_graph_sync_validation COMMAND unit_testing
        --gtest_filter=RenderGraphGpuTest.* ${TEST_PLATFORM_ARGS})
set_tests_properties(render_graph_sync_validation PROPERTIES
        ENVIRONMENT "VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT"
        RESOURCE_LOCK comet_gpu TIMEOUT 90)
