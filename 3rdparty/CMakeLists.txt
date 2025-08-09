set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "Enable -fPIC for all 3rd party libraries")

# spdlog
set(SPDLOG_ENABLE_PCH ON)
add_subdirectory(spdlog)

# gtest
add_subdirectory(googletest)

# glm
add_subdirectory(glm)

# stb_image
add_subdirectory(stb_image)

# glfw
option(GLFW_BUILD_DOCS OFF)
option(GLFW_BUILD_TESTS OFF)
option(GLFW_BUILD_EXAMPLES OFF)
option(GLFW_BUILD_TESTS OFF)
option(GLFW_INSTALL OFF)
add_subdirectory(glfw)