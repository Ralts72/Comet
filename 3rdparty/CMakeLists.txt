# SDL3
set(SDL_CAMERA OFF)
set(SDL_EXAMPLES OFF)
set(SDL_GPU OFF)
set(SDL_RENDER OFF)
set(SDL_HAPTIC OFF)
set(SDL_AUDIO OFF)
set(SDL_TESTS OFF)
set(SDL_TEST_LIBRARY OFF)
set(SDL_INSTALL OFF)
set(SDL_DISABLE_INSTALL ON)
set(SDL_DISABLE_UNINSTALL ON)
set(SDL_SHARED OFF)
set(SDL_STATIC ON)
add_subdirectory(SDL3)

# spdlog
set(SPDLOG_ENABLE_PCH ON)
add_subdirectory(spdlog)

# gtest
add_subdirectory(googletest)

# glm
add_subdirectory(glm)