cmake_minimum_required(VERSION 3.31)
project(Editor)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES
    editor.cpp
    src/imgui_context.cpp
)

add_executable(editor ${SOURCES})
target_link_libraries(editor PUBLIC engine imgui)
target_include_directories(editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)