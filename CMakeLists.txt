cmake_minimum_required(VERSION 3.31)
project(Comet)

set(CMAKE_CXX_STANDARD 20)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(BUILD_TYPE_DEBUG)
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(BUILD_TYPE_RELEASE)
endif ()

option(Comet_TESTS "build tests" ON)
option(Comet_ENABLE_FILE_LOGGING "enable file logging output" ON)

if (Comet_ENABLE_FILE_LOGGING)
    add_compile_definitions(COMET_ENABLE_FILE_LOGGING)
endif ()

if (Comet_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

add_subdirectory(app)
add_subdirectory(engine)
add_subdirectory(editor)
add_subdirectory(3rdparty)