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

if (COMET_BUILD_EDITOR)
    add_subdirectory(imgui EXCLUDE_FROM_ALL)
endif ()

if (COMET_BUILD_TESTS)
    option(INSTALL_GTEST OFF)
    option(BUILD_GMOCK OFF)
    add_subdirectory(googletest EXCLUDE_FROM_ALL)

    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(gtest PRIVATE -Wno-character-conversion)
        target_compile_options(gtest_main PRIVATE -Wno-character-conversion)
    endif ()
endif ()
