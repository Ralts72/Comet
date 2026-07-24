cmake_minimum_required(VERSION 3.31)
project(Comet)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(COMET_BUILD_APP "Build the runtime application" ON)
option(COMET_BUILD_EDITOR "Build the ImGui editor" OFF)
option(COMET_BUILD_TESTS "Build unit tests" OFF)

find_package(Vulkan REQUIRED)

add_subdirectory(3rdparty)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(BUILD_TYPE_DEBUG)
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(BUILD_TYPE_RELEASE)
endif ()
add_compile_definitions(PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}")

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
