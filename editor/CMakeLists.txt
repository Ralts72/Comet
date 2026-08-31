set(SOURCES
        editor.cpp
        src/editor_scene_session.cpp
        src/scene_document.cpp
        src/imgui_context.cpp
        src/menu_bar.cpp
        src/property_editor_registry.cpp
        src/panels/inspector.cpp
        src/panels/console.cpp
        src/panels/view.cpp
        src/panels/hierarchy.cpp
        src/panels/project.cpp
)

add_executable(editor ${SOURCES})
target_link_libraries(editor PRIVATE engine imgui glfw)
target_include_directories(editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(editor PRIVATE
        COMET_CONFIG_DIRECTORY="${CMAKE_SOURCE_DIR}/config"
        COMET_CONFIG_PROFILE="${COMET_CONFIG_PROFILE}"
        COMET_EDITOR_RESOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources"
)
