cmake_minimum_required(VERSION 3.31)
project(Comet)

if (WIN32)
    enable_language(RC)
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(COMET_BUILD_APP "Build the runtime application" ON)
option(COMET_BUILD_EDITOR "Build the ImGui editor" OFF)
option(COMET_BUILD_TESTS "Build unit tests" OFF)
option(COMET_NATIVE_OPTIMIZATION "Optimize engine code for the build machine CPU" OFF)
set(COMET_CONFIG_PROFILE "" CACHE STRING "Runtime configuration profile")

find_package(Vulkan REQUIRED)

set(GLFW_LIBRARY_TYPE SHARED CACHE STRING "GLFW linkage required by Comet" FORCE)
add_subdirectory(3rdparty)

function(comet_copy_runtime_dependencies executable)
    if (WIN32)
        add_custom_command(TARGET ${executable} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${executable}>
                $<TARGET_FILE_DIR:${executable}>
                COMMAND_EXPAND_LISTS)
    endif ()
endfunction()

add_compile_definitions(
    "$<$<CONFIG:Debug>:COMET_ENABLE_DEBUG_LOGS>"
    "$<$<CONFIG:Debug>:COMET_ENABLE_TRACE_LOGS>"
    "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:COMET_ENABLE_PROFILER>"
)
add_compile_definitions(
    PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}"
    COMET_SAMPLE_PROJECT_DIRECTORY="${CMAKE_SOURCE_DIR}/demo"
)

if ((COMET_BUILD_APP OR COMET_BUILD_EDITOR) AND COMET_CONFIG_PROFILE STREQUAL "")
    message(FATAL_ERROR
        "COMET_CONFIG_PROFILE is required when building app or editor; use a CMake preset or set it explicitly")
endif ()

add_subdirectory(engine)

if (COMET_BUILD_APP)
    add_subdirectory(app)
endif ()

if (COMET_BUILD_EDITOR OR COMET_BUILD_TESTS)
    add_subdirectory(editor)
endif ()

if (COMET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()
