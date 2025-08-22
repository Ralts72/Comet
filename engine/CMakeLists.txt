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
        "${CMAKE_CURRENT_SOURCE_DIR}/src/common/geometry_utils.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/common/profiler.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/logger/logger.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/window/window.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/timer/timer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/engine.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/context.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/device.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/queue.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/swapchain.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/render_pass.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/image.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/image_view.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/frame_buffer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/pipeline.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/shader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/command_buffer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/fence.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/semaphore.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/vertex_description.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/buffer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/renderer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/render_target.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/render/mesh.cpp"
)

find_package(Vulkan REQUIRED)
add_library(engine SHARED ${SOURCES})
target_link_libraries(engine PUBLIC spdlog::spdlog glm ${Vulkan_LIBRARIES} stb_image glfw)
target_include_directories(engine
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/include
        ${Vulkan_INCLUDE_DIRS}
)
target_precompile_headers(engine PRIVATE "src/pch.h")
target_compile_definitions(engine PRIVATE COMET_EXPORTS)
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