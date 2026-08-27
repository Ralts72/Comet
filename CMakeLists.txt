cmake_minimum_required(VERSION 3.31)
project(Comet)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(COMET_BUILD_APP "Build the runtime application" ON)
option(COMET_BUILD_EDITOR "Build the ImGui editor" OFF)
option(COMET_BUILD_TESTS "Build unit tests" OFF)
option(COMET_NATIVE_OPTIMIZATION "Optimize engine code for the build machine CPU" OFF)
set(COMET_CONFIG_PROFILE "" CACHE STRING "Runtime configuration profile")

find_package(Vulkan REQUIRED)

add_subdirectory(3rdparty)

add_compile_definitions(
    "$<$<CONFIG:Debug>:COMET_ENABLE_DEBUG_LOGS>"
    "$<$<CONFIG:Debug>:COMET_ENABLE_TRACE_LOGS>"
    "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:COMET_ENABLE_PROFILER>"
)
add_compile_definitions(
    PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}"
)

if ((COMET_BUILD_APP OR COMET_BUILD_EDITOR) AND COMET_CONFIG_PROFILE STREQUAL "")
    message(FATAL_ERROR
        "COMET_CONFIG_PROFILE is required when building app or editor; use a CMake preset or set it explicitly")
endif ()

add_subdirectory(engine)

if (COMET_BUILD_APP)
    add_subdirectory(app)
endif ()

if (COMET_BUILD_EDITOR)
    add_subdirectory(editor)
endif ()

if (COMET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()
