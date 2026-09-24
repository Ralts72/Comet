# 按运行依赖显式分组；扫描只检查遗漏，不替新测试猜测执行环境。
set(UNIT_TEST_SOURCES
        audio/test_audio.cpp
        asset/test_asset_manager.cpp
        asset/test_asset_publication.cpp
        asset/test_asset_task_queue.cpp
        asset/test_database.cpp
        asset/test_environment_artifact.cpp
        asset/test_environment_importer.cpp
        asset/test_external_file_import.cpp
        asset/test_handle.cpp
        asset/test_import_input_snapshot.cpp
        asset/test_material_serializer.cpp
        asset/test_mesh_artifact.cpp
        asset/test_mesh_data.cpp
        asset/test_mesh_importer.cpp
        asset/test_metadata.cpp
        asset/test_project_assets.cpp
        asset/test_registry.cpp
        asset/test_source_monitor.cpp
        asset/test_texture_importer.cpp
        common/test_file_io.cpp
        common/test_result.cpp
        common/test_retry_backoff.cpp
        common/test_scope_exit.cpp
        config/test_config.cpp
        core/test_geometry.cpp
        core/test_math_utils.cpp
        core/test_project.cpp
        core/test_project_paths.cpp
        core/test_task_scheduler.cpp
        core/test_timer.cpp
        diagnostics/test_frame_diagnostics.cpp
        diagnostics/test_logger.cpp
        diagnostics/test_profiler.cpp
        diagnostics/test_timing_history.cpp
        editor/core/test_camera_controller.cpp
        editor/core/test_command_history.cpp
        editor/core/test_editor_assets.cpp
        editor/core/test_editor_scene_session.cpp
        editor/core/test_material_editing.cpp
        editor/core/test_render_view.cpp
        editor/core/test_scene_commands.cpp
        editor/core/test_scene_document.cpp
        editor/core/test_selection.cpp
        editor/core/test_shader_reload.cpp
        editor/core/test_shader_program_import.cpp
        editor/core/test_transform_gizmo.cpp
        editor/core/test_viewport_layout.cpp
        graphics/test_device_capability.cpp
        graphics/test_resource_state.cpp
        graphics/test_viewport.cpp
        graphics/test_vulkan_raii_ownership.cpp
        input/test_input.cpp
        input/test_input_actions.cpp
        input/test_runtime_input.cpp
        render/test_lighting.cpp
        render/test_line_draw_list.cpp
        render/test_material.cpp
        render/test_material_shader.cpp
        render/test_render_graph.cpp
        render/test_scene_extractor.cpp
        render/test_scene_picking.cpp
        scene/test_camera_controller.cpp
        scene/test_component_registry.cpp
        scene/test_entity_uuid.cpp
        scene/test_physics_system.cpp
        scene/test_scene.cpp
        scene/test_scene_runtime.cpp
        scene/test_script_system.cpp
        scripting/test_script.cpp
        scene/test_scene_serializer.cpp
        shader/test_compiler.cpp)
set(INTEGRATION_SOURCES
        core/test_window.cpp
        editor/integration/test_material_editing.cpp
        editor/ui/test_asset_editing.cpp
        editor/ui/test_console_panel.cpp
        editor/ui/test_editing_ui.cpp
        editor/ui/test_language.cpp
        editor/ui/test_project_panel.cpp
        editor/ui/test_property_editor_registry.cpp
        editor/ui/test_scene_file_dialog.cpp
        editor/ui/test_shortcuts.cpp
        editor/ui/test_unsaved_scene_dialog.cpp
        editor/ui/test_viewport.cpp
        editor/ui/test_viewport_gizmo_ui.cpp
        editor/ui/test_viewport_play_ui.cpp
        graphics/test_allocator.cpp
        graphics/test_barrier.cpp
        graphics/test_creation.cpp
        graphics/test_descriptor_set.cpp
        graphics/test_pipeline_cache.cpp
        graphics/test_queue.cpp
        graphics/test_render_pass.cpp
        graphics/test_sampler.cpp
        graphics/test_shader_interface.cpp
        graphics/test_swapchain.cpp
        graphics/test_upload_manager.cpp
        render/test_bloom.cpp
        render/test_debug_renderer.cpp
        render/test_frame_edit_order.cpp
        render/test_frame_scheduler.cpp
        render/test_material_rendering.cpp
        render/test_material_runtime.cpp
        render/test_offscreen_resize.cpp
        render/test_render_diagnostics.cpp
        render/test_render_graph_gpu.cpp
        render/test_render_pipeline_gpu.cpp
        render/test_scene_resolver.cpp
        runtime/test_engine.cpp
        runtime/test_entry.cpp)
set(ISOLATED_TEST_SOURCES
        graphics/pipeline_cache_probe.cpp
        graphics/test_swapchain_recovery.cpp
        render/test_post_process_recovery.cpp)

file(GLOB_RECURSE DISCOVERED_TEST_SOURCES CONFIGURE_DEPENDS
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
set(ASSIGNED_TEST_SOURCES test.cpp ${UNIT_TEST_SOURCES}
        ${INTEGRATION_SOURCES} ${ISOLATED_TEST_SOURCES})
list(LENGTH ASSIGNED_TEST_SOURCES ASSIGNED_COUNT)
list(REMOVE_DUPLICATES ASSIGNED_TEST_SOURCES)
list(LENGTH ASSIGNED_TEST_SOURCES UNIQUE_COUNT)
if(NOT ASSIGNED_COUNT EQUAL UNIQUE_COUNT)
    message(FATAL_ERROR "Test source assigned to more than one execution group")
endif()
list(REMOVE_ITEM DISCOVERED_TEST_SOURCES ${ASSIGNED_TEST_SOURCES})
if(DISCOVERED_TEST_SOURCES)
    message(FATAL_ERROR "Assign new test sources to an execution group: ${DISCOVERED_TEST_SOURCES}")
endif()
add_executable(unit_testing test.cpp ${UNIT_TEST_SOURCES})
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
add_test(NAME module_boundaries COMMAND "${CMAKE_COMMAND}"
        "-DCOMET_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/cmake/check_module_boundaries.cmake")

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
