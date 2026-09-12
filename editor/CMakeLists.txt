add_library(editor_core STATIC
        src/editor_assets.cpp
        src/command_history.cpp
        src/scene_commands.cpp
        src/transform_gizmo.cpp
        src/camera_controller.cpp
        src/editor_state.cpp
        src/editor_scene_session.cpp
        src/scene_document.cpp
        src/viewport_layout.cpp
)
target_link_libraries(editor_core PUBLIC engine)
target_include_directories(editor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if (NOT COMET_BUILD_EDITOR)
    return()
endif ()

add_library(editor_ui STATIC
        src/scene_file_dialog.cpp
        src/imgui_context.cpp
        src/menu_bar.cpp
        src/shortcuts.cpp
        src/property_editor_registry.cpp
        src/panels/inspector.cpp
        src/panels/console.cpp
        src/panels/view.cpp
        src/panels/hierarchy.cpp
        src/panels/project.cpp
)
target_link_libraries(editor_ui PUBLIC editor_core imgui PRIVATE yaml-cpp)
target_compile_definitions(editor_ui PRIVATE
        COMET_EDITOR_RESOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources"
)

add_executable(editor editor.cpp)
if (APPLE)
    set_target_properties(editor PROPERTIES
            MACOSX_BUNDLE TRUE
            OUTPUT_NAME CometEditor
            MACOSX_BUNDLE_BUNDLE_NAME "Comet Editor"
            MACOSX_BUNDLE_GUI_IDENTIFIER org.comet.editor
            MACOSX_BUNDLE_ICON_FILE editor.icns
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in")
    target_sources(editor PRIVATE resources/icons/editor.icns)
    set_source_files_properties(resources/icons/editor.icns PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources)
elseif (WIN32)
    target_sources(editor PRIVATE resources/editor.rc)
    target_include_directories(editor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    set_source_files_properties(resources/editor.rc PROPERTIES
            OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/editor.ico")
endif ()
target_link_libraries(editor PRIVATE editor_ui)
target_compile_definitions(editor PRIVATE
        COMET_CONFIG_DIRECTORY="${CMAKE_SOURCE_DIR}/config"
        COMET_CONFIG_PROFILE="${COMET_CONFIG_PROFILE}"
)
