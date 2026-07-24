cmake_minimum_required(VERSION 3.31)
project(Editor)

set(CMAKE_CXX_STANDARD 20)

set(SOURCES
        editor.cpp
        src/imgui_context.cpp
        src/menu_bar.cpp
        src/panels/inspector_panel.cpp
        src/panels/console_panel.cpp
        src/panels/view_panel.cpp
        src/panels/hierarchy_panel.cpp
        src/panels/project_panel.cpp
)

add_executable(editor ${SOURCES})
target_link_libraries(editor PRIVATE engine imgui glfw)
target_include_directories(editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(editor PRIVATE COMET_EDITOR)
