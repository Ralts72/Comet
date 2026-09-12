set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "Enable -fPIC for all 3rd party libraries")

# spdlog
set(SPDLOG_ENABLE_PCH ON)
option(SPDLOG_BUILD_EXAMPLE OFF)
option(SPDLOG_BUILD_TESTS OFF)
option(SPDLOG_BUILD_BENCH OFF)
option(SPDLOG_INSTALL OFF)
add_subdirectory(spdlog EXCLUDE_FROM_ALL)

# glm
option(GLM_BUILD_TESTS OFF)
option(GLM_BUILD_INSTALL OFF)
option(GLM_BUILD_LIBRARY OFF)  # glm is header-only, no need to build library
add_subdirectory(glm EXCLUDE_FROM_ALL)

# stb_image
add_subdirectory(stb_image EXCLUDE_FROM_ALL)

# glfw
option(GLFW_BUILD_DOCS OFF)
option(GLFW_BUILD_TESTS OFF)
option(GLFW_BUILD_EXAMPLES OFF)
option(GLFW_INSTALL OFF)
add_subdirectory(glfw EXCLUDE_FROM_ALL)

# yaml-cpp
option(YAML_CPP_BUILD_TESTS OFF)
option(YAML_CPP_BUILD_TOOLS OFF)
option(YAML_CPP_BUILD_CONTRIB OFF)
option(YAML_CPP_INSTALL OFF)
add_subdirectory(yaml-cpp EXCLUDE_FROM_ALL)

# Vulkan Memory Allocator
add_subdirectory(VulkanMemoryAllocator EXCLUDE_FROM_ALL)

# EnTT
add_subdirectory(entt EXCLUDE_FROM_ALL)

# simdjson
set(SIMDJSON_BUILD_STATIC_LIB OFF CACHE BOOL "" FORCE)
set(SIMDJSON_ENABLE_THREADS ON CACHE BOOL "" FORCE)
set(SIMDJSON_INSTALL ON)
add_subdirectory(simdjson EXCLUDE_FROM_ALL)

# fastgltf
set(FASTGLTF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_DOCS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_COMPILE_AS_CPP20 ON CACHE BOOL "" FORCE)
add_subdirectory(fastgltf EXCLUDE_FROM_ALL)

set(SPIRV_REFLECT_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_STATIC_LIB ON CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_REFLECT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(spirv-reflect EXCLUDE_FROM_ALL)
set_target_properties(spirv-reflect-static PROPERTIES C_VISIBILITY_PRESET hidden)

block(SCOPE_FOR VARIABLES)
    set(ENABLE_GLSLANG_BINARIES OFF)
    set(ENABLE_SPIRV ON)
    set(GLSLANG_TESTS OFF)
    set(GLSLANG_ENABLE_INSTALL OFF)
    set(ENABLE_HLSL OFF)
    set(ENABLE_OPT OFF)
    set(BUILD_EXTERNAL OFF)
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory(glslang EXCLUDE_FROM_ALL)
endblock()

# GCC PR110498: false positive in libstdc++ vector<bool>::reserve.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
    AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13
    AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13.4)
    target_compile_options(glslang PRIVATE -Wno-array-bounds)
endif()

if (COMET_BUILD_EDITOR)
    add_subdirectory(imgui EXCLUDE_FROM_ALL)
endif ()

if (COMET_BUILD_TESTS)
    option(INSTALL_GTEST OFF)
    option(BUILD_GMOCK OFF)
    add_subdirectory(googletest EXCLUDE_FROM_ALL)
endif ()
