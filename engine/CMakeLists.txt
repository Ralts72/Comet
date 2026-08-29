find_program(glslangValidator_executable glslangValidator)
if (NOT glslangValidator_executable)
    message(FATAL_ERROR "glslangValidator not found, please install it to compile shaders")
endif ()
set(SHADER_COMPILE_TARGET shader_compile)
set(SHADER_GENERATED_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
add_subdirectory(assets/shaders)

set(SOURCES
        src/asset/database.cpp
        src/asset/handle.cpp
        src/asset/metadata.cpp
        src/asset/registry.cpp
        src/common/geometry_utils.cpp
        src/common/diagnostics.cpp
        src/common/profiler.cpp
        src/common/logger.cpp
        src/common/config_loader.cpp
        src/core/window.cpp
        src/core/timer.cpp
        src/core/project_paths.cpp
        src/core/engine.cpp
        src/graphics/convert.cpp
        src/graphics/context.cpp
        src/graphics/device.cpp
        src/graphics/vk_capability.cpp
        src/graphics/queue.cpp
        src/graphics/swapchain.cpp
        src/graphics/render_pass.cpp
        src/graphics/image.cpp
        src/graphics/image_view.cpp
        src/graphics/frame_buffer.cpp
        src/graphics/pipeline.cpp
        src/graphics/shader.cpp
        src/graphics/command_buffer.cpp
        src/graphics/command_context.cpp
        src/graphics/fence.cpp
        src/graphics/semaphore.cpp
        src/graphics/vertex_description.cpp
        src/graphics/allocator.cpp
        src/graphics/buffer.cpp
        src/graphics/descriptor_set.cpp
        src/graphics/sampler.cpp
        src/render/renderer.cpp
        src/render/render_target.cpp
        src/render/mesh.cpp
        src/render/texture.cpp
        src/render/render_context.cpp
        src/render/scene_resolver.cpp
        src/render/scene_extractor.cpp
        src/render/scene_renderer.cpp
        src/render/resource_manager.cpp
        src/render/frame_manager.cpp
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
        stb_image
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
