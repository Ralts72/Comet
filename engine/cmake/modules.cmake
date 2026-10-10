include("${CMAKE_CURRENT_LIST_DIR}/module_sources.cmake")
find_package(Threads REQUIRED)

# Object targets constrain build dependencies and feed the single engine library.
function(comet_add_module name)
    string(TOUPPER "${name}" source_group)
    add_library(comet_${name} OBJECT ${COMET_${source_group}_SOURCES})
    set_target_properties(comet_${name} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(comet_${name} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(comet_${name} PRIVATE COMET_EXPORTS)
    target_precompile_headers(comet_${name} PRIVATE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h>")
    if(NOT WIN32)
        target_compile_options(comet_${name} PRIVATE -Wall -fvisibility=hidden)
    endif()
    if(COMET_NATIVE_OPTIMIZATION AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(comet_${name} PRIVATE -march=native)
    endif()
endfunction()

comet_add_module(foundation)
comet_add_module(serialization)
comet_add_module(shader_contracts)
comet_add_module(asset_data)
comet_add_module(input)
comet_add_module(world)
comet_add_module(runtime)
comet_add_module(audio)
comet_add_module(physics)
comet_add_module(scripting)
comet_add_module(asset_pipeline)
comet_add_module(runtime_assets)
comet_add_module(platform)
comet_add_module(graphics)
comet_add_module(render)

target_link_libraries(comet_foundation PUBLIC glm spdlog::spdlog Threads::Threads)
target_link_libraries(comet_serialization PUBLIC comet_foundation simdjson::simdjson yaml-cpp)
target_link_libraries(comet_shader_contracts PUBLIC comet_foundation PRIVATE spirv-reflect-static)
target_link_libraries(comet_asset_data PUBLIC comet_serialization comet_shader_contracts)
target_link_libraries(comet_input PUBLIC comet_serialization)
target_link_libraries(comet_world PUBLIC comet_asset_data EnTT::EnTT)
target_link_libraries(comet_runtime PUBLIC comet_world comet_input)
target_link_libraries(comet_audio PUBLIC comet_runtime PRIVATE comet_miniaudio)
target_link_libraries(comet_physics PUBLIC comet_runtime PRIVATE Jolt::Jolt)
target_link_libraries(comet_scripting PUBLIC comet_runtime PRIVATE comet_lua)
target_link_libraries(comet_asset_pipeline PUBLIC comet_asset_data PRIVATE stb_image fastgltf::fastgltf)
target_link_libraries(comet_runtime_assets PUBLIC comet_asset_pipeline PRIVATE comet_audio comet_scripting)

target_link_libraries(comet_platform PUBLIC comet_input PRIVATE glfw)
target_compile_definitions(comet_platform PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(comet_graphics
    PUBLIC comet_shader_contracts Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator
    PRIVATE comet_platform glfw)
target_compile_definitions(comet_graphics PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(comet_render PUBLIC comet_graphics comet_world PRIVATE comet_platform)

set(COMET_ENGINE_MODULES
    comet_foundation comet_serialization comet_shader_contracts comet_asset_data
    comet_input comet_world comet_runtime comet_audio comet_physics comet_scripting comet_asset_pipeline
    comet_runtime_assets comet_platform comet_graphics comet_render)

include("${CMAKE_CURRENT_LIST_DIR}/game_ui.cmake")
list(APPEND COMET_ENGINE_MODULES comet_game_ui)
