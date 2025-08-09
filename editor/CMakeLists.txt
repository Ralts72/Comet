cmake_minimum_required(VERSION 3.31)
project(Editor)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES editor.cpp)

add_executable(editor ${SOURCES})
target_link_libraries(editor PUBLIC engine)