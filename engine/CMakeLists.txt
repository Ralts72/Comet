include(cmake/modules.cmake)

set(SHADER_COMPILE_TARGET shader_compile)
set(SHADER_GENERATED_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
add_subdirectory(shaders)

set(SOURCES
        src/core/project.cpp
        src/config/config_loader.cpp
        src/diagnostics/diagnostics.cpp
        src/audio/audio.cpp
        src/asset/asset_manager.cpp
        src/asset/asset_manager_async.cpp
        src/asset/asset_manager_scripts.cpp
        src/render/presentation.cpp
        src/render/render_graph.cpp
        src/render/render_diagnostics.cpp
        src/render/passes/output_pass.cpp
        src/render/passes/bloom_pass.cpp
        src/render/passes/skybox_pass.cpp
        src/render/passes/shadow_pass.cpp
        src/core/window.cpp
        src/runtime/entry.cpp
        src/runtime/application.cpp
        src/core/engine.cpp
        src/graphics/convert.cpp
        src/graphics/context.cpp
        src/graphics/device.cpp
        src/graphics/gpu_timer.cpp
        src/graphics/vk_capability.cpp
        src/graphics/synchronization/barrier.cpp
        src/graphics/synchronization/gpu_completion_point.cpp
        src/graphics/synchronization/resource_state.cpp
        src/graphics/queue.cpp
        src/graphics/swapchain.cpp
        src/graphics/render_pass.cpp
        src/graphics/resource/image.cpp
        src/graphics/resource/image_view.cpp
        src/graphics/frame_buffer.cpp
        src/graphics/pipeline/pipeline.cpp
        src/graphics/pipeline/pipeline_cache.cpp
        src/graphics/pipeline/pipeline_config.cpp
        src/graphics/pipeline/pipeline_key.cpp
        src/graphics/pipeline/shader.cpp
        src/graphics/command/command_buffer.cpp
        src/graphics/command/command_context.cpp
        src/graphics/command/upload_manager.cpp
        src/graphics/synchronization/fence.cpp
        src/graphics/synchronization/semaphore.cpp
        src/graphics/pipeline/vertex_description.cpp
        src/graphics/resource/allocator.cpp
        src/graphics/resource/buffer.cpp
        src/graphics/result.cpp
        src/graphics/pipeline/descriptor_set.cpp
        src/graphics/resource/sampler.cpp
        src/render/renderer.cpp
        src/render/overlay_record_context.cpp
        src/render/debug/line_draw_list.cpp
        src/render/lighting.cpp
        src/render/debug/debug_renderer.cpp
        src/render/render_target.cpp
        src/render/resource/mesh.cpp
        src/render/resource/texture.cpp
        src/render/resource/environment.cpp
        src/render/resource/sampled_image_binding.cpp
        src/render/render_context.cpp
        src/render/scene/scene_resolver.cpp
        src/render/scene/render_scene.cpp
        src/render/scene/scene_picking.cpp
        src/render/scene/scene_extractor.cpp
        src/render/scene/scene_renderer.cpp
        src/render/resource/render_resources.cpp
        src/render/frame_scheduler.cpp
        src/render/material/material.cpp
        src/render/material/material_layout.cpp
        src/render/material/material_programs.cpp
        src/render/material/material_shader.cpp
        src/render/material/material_runtime.cpp
        src/render/material/material_renderer.cpp
        src/scene/scene_runtime.cpp
        src/scene/systems/camera_controller.cpp
        src/scene/systems/physics_system.cpp
        src/scene/systems/audio_system.cpp
        src/scene/systems/script_system.cpp
        src/scripting/script.cpp
        src/scripting/lua_bindings.cpp
)

add_library(engine SHARED ${SOURCES})
add_library(Comet::Engine ALIAS engine)
add_dependencies(engine ${SHADER_COMPILE_TARGET})
target_link_libraries(engine
        PUBLIC
        glm
        spdlog::spdlog
        EnTT::EnTT
        simdjson::simdjson
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        PRIVATE
        ${COMET_ENGINE_MODULES}
        yaml-cpp
        comet_lua
        Jolt::Jolt
        comet_miniaudio
        glfw
)
target_include_directories(engine
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        PRIVATE
        ${SHADER_GENERATED_DIRECTORY}/include
)
target_precompile_headers(engine PRIVATE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h>"
)
target_compile_definitions(engine PRIVATE COMET_EXPORTS GLFW_INCLUDE_NONE)
if (NOT WIN32)
    target_compile_options(engine PRIVATE -Wall -fvisibility=hidden)
endif ()

if (COMET_NATIVE_OPTIMIZATION AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(engine PRIVATE -march=native)
endif ()

if(COMET_BUILD_GAME_UI)
    add_subdirectory(src/ui)
endif()
