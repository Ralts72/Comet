cmake_minimum_required(VERSION 3.31)
project(Comet)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Os -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Os -Wall")
find_program(glslangValidator_executable glslangValidator)
if (NOT glslangValidator_executable)
    message(FATAL_ERROR "glslangValidator not found, please install it to compile shaders")
endif ()
set(SHADER_COMPILE_TARGET shader_compile)
add_subdirectory(assets/shaders)

file(GLOB SOURCES
        "src/common/geometry_utils.cpp"
        "src/common/profiler.cpp"
        "src/common/logger.cpp"
        "src/common/config.cpp"
        "src/core/window.cpp"
        "src/core/timer.cpp"
        "src/core/engine.cpp"
        "src/graphics/convert.cpp"
        "src/graphics/context.cpp"
        "src/graphics/device.cpp"
        "src/graphics/queue.cpp"
        "src/graphics/swapchain.cpp"
        "src/graphics/render_pass.cpp"
        "src/graphics/image.cpp"
        "src/graphics/image_view.cpp"
        "src/graphics/frame_buffer.cpp"
        "src/graphics/pipeline.cpp"
        "src/graphics/shader.cpp"
        "src/graphics/command_buffer.cpp"
        "src/graphics/fence.cpp"
        "src/graphics/semaphore.cpp"
        "src/graphics/vertex_description.cpp"
        "src/graphics/buffer.cpp"
        "src/graphics/descriptor_set.cpp"
        "src/graphics/sampler.cpp"
        "src/render/renderer.cpp"
        "src/render/render_target.cpp"
        "src/render/mesh.cpp"
        "src/render/texture.cpp"
        "src/render/render_context.cpp"
        "src/render/scene_renderer.cpp"
        "src/render/resource_manager.cpp"
        "src/render/frame_manager.cpp"
)

find_package(Vulkan REQUIRED)
add_library(engine SHARED ${SOURCES})
target_link_libraries(engine PUBLIC spdlog::spdlog glm ${Vulkan_LIBRARIES} stb_image glfw yaml-cpp)
target_include_directories(engine
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/include
        ${Vulkan_INCLUDE_DIRS}
)
target_precompile_headers(engine PRIVATE "src/pch.h")
target_compile_definitions(engine PRIVATE COMET_EXPORTS)
target_compile_definitions(engine PRIVATE PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}")
if (NOT WIN32)
    target_compile_options(engine PRIVATE -fvisibility=hidden)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(engine PRIVATE -O3 -DNDEBUG)
    target_compile_options(engine PRIVATE -march=native)
else ()
    # Enable profiler in Debug builds
    target_compile_definitions(engine PRIVATE ENABLE_PROFILER)
endif ()