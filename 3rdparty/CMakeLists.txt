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
add_subdirectory(glfw)