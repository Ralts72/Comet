include(cmake/modules.cmake)

set(SHADER_COMPILE_TARGET shader_compile)
set(SHADER_GENERATED_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
add_subdirectory(shaders)

set(SOURCES
    src/core/project.cpp
    src/config/config_loader.cpp
    src/config/display_settings.cpp
    src/config/player_display_settings.cpp
    src/config/player_quality_settings.cpp
    src/diagnostics/diagnostics.cpp
    src/runtime/entry.cpp
    src/runtime/application.cpp
    src/core/engine.cpp
    src/scene/systems/camera_controller.cpp
)

add_library(engine SHARED ${SOURCES})
add_library(Comet::Engine ALIAS engine)
add_dependencies(comet_render ${SHADER_COMPILE_TARGET})
target_include_directories(comet_render PRIVATE "${SHADER_GENERATED_DIRECTORY}/include")
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
)
target_include_directories(engine
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
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
