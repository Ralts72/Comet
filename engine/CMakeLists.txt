find_program(glslangValidator_executable glslangValidator)
if (NOT glslangValidator_executable)
    message(FATAL_ERROR "glslangValidator not found, please install it to compile shaders")
endif ()
set(SHADER_COMPILE_TARGET shader_compile)
set(SHADER_GENERATED_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
add_subdirectory(shaders)

find_package(Threads REQUIRED)

set(SOURCES
        src/asset/cache/mesh_import_cache.cpp
        src/asset/database.cpp
        src/asset/handle.cpp
        src/asset/import_settings.cpp
        src/asset/import/mesh_importer.cpp
        src/asset/import/texture_importer.cpp
        src/asset/manager.cpp
        src/asset/material_data.cpp
        src/asset/metadata.cpp
        src/asset/registry.cpp
        src/asset/serialization/material_serializer.cpp
        src/asset/serialization/metadata_serializer.cpp
        src/render/resource/mesh_primitives.cpp
        src/common/file_io.cpp
        src/config/config_loader.cpp
        src/diagnostics/diagnostics.cpp
        src/diagnostics/profiler.cpp
        src/diagnostics/logger.cpp
        src/core/window.cpp
        src/core/timer.cpp
        src/core/project_paths.cpp
        src/core/task_scheduler.cpp
        src/core/engine.cpp
        src/graphics/convert.cpp
        src/graphics/context.cpp
        src/graphics/device.cpp
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
        src/graphics/pipeline/shader.cpp
        src/graphics/command/command_buffer.cpp
        src/graphics/command/command_context.cpp
        src/graphics/command/upload_manager.cpp
        src/graphics/synchronization/fence.cpp
        src/graphics/synchronization/semaphore.cpp
        src/graphics/pipeline/vertex_description.cpp
        src/graphics/resource/allocator.cpp
        src/graphics/resource/buffer.cpp
        src/graphics/resource/resource_result.cpp
        src/graphics/pipeline/descriptor_set.cpp
        src/graphics/resource/sampler.cpp
        src/render/renderer.cpp
        src/render/render_target.cpp
        src/render/resource/mesh.cpp
        src/render/resource/texture.cpp
        src/render/render_context.cpp
        src/render/scene/scene_resolver.cpp
        src/render/scene/scene_extractor.cpp
        src/render/scene/scene_renderer.cpp
        src/render/resource/resource_manager.cpp
        src/render/frame_scheduler.cpp
        src/render/material.cpp
        src/scene/entity.cpp
        src/scene/entity_uuid.cpp
        src/scene/component_registry.cpp
        src/scene/scene.cpp
        src/scene/scene_serializer.cpp
)

add_library(engine SHARED ${SOURCES})
add_dependencies(engine ${SHADER_COMPILE_TARGET})
target_link_libraries(engine
        PUBLIC
        spdlog::spdlog
        glm
        Vulkan::Vulkan
        glfw
        GPUOpen::VulkanMemoryAllocator
        EnTT::EnTT
        PRIVATE
        Threads::Threads
        stb_image
        fastgltf::fastgltf
        yaml-cpp
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
target_compile_definitions(engine PRIVATE COMET_EXPORTS)
if (NOT WIN32)
    target_compile_options(engine PRIVATE -Wall -fvisibility=hidden)
endif ()

if (COMET_NATIVE_OPTIMIZATION AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(engine PRIVATE -march=native)
endif ()
