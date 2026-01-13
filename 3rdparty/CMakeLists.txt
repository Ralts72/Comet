set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "Enable -fPIC for all 3rd party libraries")

# spdlog
set(SPDLOG_ENABLE_PCH ON)
option(SPDLOG_BUILD_EXAMPLE OFF)
option(SPDLOG_BUILD_TESTS OFF)
option(SPDLOG_BUILD_BENCH OFF)
option(SPDLOG_INSTALL OFF)
add_subdirectory(spdlog)

# gtest
option(INSTALL_GTEST OFF)
option(BUILD_GMOCK ON)  # Keep gmock if you use it, otherwise set to OFF
add_subdirectory(googletest)

# glm
option(GLM_BUILD_TESTS OFF)
option(GLM_BUILD_INSTALL OFF)
option(GLM_BUILD_LIBRARY OFF)  # glm is header-only, no need to build library
add_subdirectory(glm)

# stb_image
add_subdirectory(stb_image)

# glfw
option(GLFW_BUILD_DOCS OFF)
option(GLFW_BUILD_TESTS OFF)
option(GLFW_BUILD_EXAMPLES OFF)
option(GLFW_INSTALL OFF)
add_subdirectory(glfw)

# yaml-cpp
option(YAML_CPP_BUILD_TESTS OFF)
option(YAML_CPP_BUILD_TOOLS OFF)
option(YAML_CPP_BUILD_CONTRIB OFF)
option(YAML_CPP_INSTALL OFF)
add_subdirectory(yaml-cpp)