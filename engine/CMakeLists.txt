cmake_minimum_required(VERSION 3.31)
project(Comet)

set(CMAKE_CXX_STANDARD 20)

find_program(glslangValidator_executable glslangValidator)
if (NOT glslangValidator_executable)
    message(FATAL_ERROR "glslangValidator not found, please install it to compile shaders")
endif ()
set(SHADER_COMPILE_TARGET shader_compile)
add_subdirectory(assets/shaders)

file(GLOB SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/common/log_system/log_system.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/window/window.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/engine.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/context.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/device.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/queue.cpp"
)

find_package(Vulkan REQUIRED)
add_library(engine SHARED ${SOURCES})
target_link_libraries(engine PUBLIC spdlog::spdlog glm ${Vulkan_LIBRARIES} stb_image glfw)
target_include_directories(engine
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${Vulkan_INCLUDE_DIRS}
)
target_precompile_headers(engine PRIVATE "src/pch.h")
target_compile_definitions(engine PRIVATE COMET_EXPORTS)
