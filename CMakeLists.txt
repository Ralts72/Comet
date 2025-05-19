cmake_minimum_required(VERSION 3.31)
project(Comet)

set(CMAKE_CXX_STANDARD 17)

option(Comet_TESTS "build tests" ON)
if (Comet_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

add_subdirectory(app)
add_subdirectory(engine)
add_subdirectory(editor)
add_subdirectory(3rdparty)