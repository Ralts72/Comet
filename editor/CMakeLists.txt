if(APPLE)
    set(COMET_SYSTEM_TRASH_SOURCE src/assets/system_trash_mac.mm)
    set(COMET_SYSTEM_TEXT_EDITOR_SOURCE src/assets/system_text_editor_mac.mm)
else()
    set(COMET_SYSTEM_TRASH_SOURCE src/assets/system_trash.cpp)
    set(COMET_SYSTEM_TEXT_EDITOR_SOURCE src/assets/system_text_editor.cpp)
endif()

add_library(editor_core STATIC
        src/editor_state.cpp
        src/file_recheck_trigger.cpp
        src/project/recent_projects.cpp
        src/project/editor_paths.cpp
        src/project/project_session.cpp
        src/project/project_creation.cpp
        src/project/asset_operations.cpp
        src/assets/editor_assets.cpp
        src/assets/scene_asset_references.cpp
        src/assets/source_operations.cpp
        ${COMET_SYSTEM_TRASH_SOURCE}
        ${COMET_SYSTEM_TEXT_EDITOR_SOURCE}
        src/assets/source_monitor.cpp
        src/assets/material_editing.cpp
        src/render/shader_reload.cpp
        src/render/material_shader_reload.cpp
        src/scene/command_history.cpp
        src/scene/editor_scene_session.cpp
        src/scene/scene_commands.cpp
        src/scene/scene_editor.cpp
        src/scene/scene_document.cpp
        src/viewport/camera_controller.cpp
        src/viewport/transform_gizmo.cpp
        src/viewport/viewport_layout.cpp
)
target_link_libraries(editor_core PUBLIC engine comet_asset_tools comet_shader_tools PRIVATE yaml-cpp fastgltf::fastgltf simdjson)
if(APPLE)
    target_link_libraries(editor_core PRIVATE
        "-framework CoreServices" "-framework Foundation" "-framework AppKit")
elseif(WIN32)
    target_link_libraries(editor_core PRIVATE ole32 shell32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
    target_link_libraries(editor_core PRIVATE PkgConfig::GIO)
endif()
target_include_directories(editor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if (NOT COMET_BUILD_EDITOR)
    return()
endif ()

include("${CMAKE_SOURCE_DIR}/engine/cmake/compile_shader.cmake")
set(COMET_EDITOR_SHADER_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
compile_shaders(
        TARGET editor_imgui_shaders
        COMPILER comet_shader_compiler
        OUTPUT_DIRECTORY "${COMET_EDITOR_SHADER_DIRECTORY}"
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/shaders/imgui_hdr.frag"
)

add_library(editor_imgui STATIC src/ui/imgui_context.cpp)
add_dependencies(editor_imgui editor_imgui_shaders)
target_include_directories(editor_imgui
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
        PRIVATE "${COMET_EDITOR_SHADER_DIRECTORY}/include")
target_link_libraries(editor_imgui PUBLIC engine imgui)
target_compile_definitions(editor_imgui PRIVATE
        COMET_EDITOR_FONT_DIRECTORY="${CMAKE_SOURCE_DIR}/engine/resources/fonts")
target_compile_options(editor_imgui PUBLIC "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

add_library(editor_ui STATIC
        src/project/player_input_panel.cpp
        src/project/input_widgets.cpp
        src/project/project_name_dialog.cpp
        src/project/input_settings_panel.cpp
        src/project/project_settings.cpp
        src/assets/asset_reference.cpp
        src/assets/project_panel.cpp
        src/inspector/inspector.cpp
        src/inspector/asset_inspector.cpp
        src/inspector/property_editor_registry.cpp
        src/scene/hierarchy.cpp
        src/scene/entity_reference.cpp
        src/ui/path_dialog.cpp
        src/render/render_stats.cpp
        src/ui/dialogs.cpp
        src/ui/console.cpp
        src/ui/menu_bar.cpp
        src/ui/language.cpp
        src/ui/shortcuts.cpp
        src/ui/shortcut_settings_dialog.cpp
        src/ui/widgets.cpp
        src/viewport/viewport_panel.cpp
        src/viewport/viewport.cpp
)
target_link_libraries(editor_ui PUBLIC editor_core editor_imgui PRIVATE yaml-cpp simdjson)
target_compile_options(editor_ui PUBLIC "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
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
comet_copy_runtime_dependencies(editor)
target_compile_definitions(editor PRIVATE
        COMET_BUILTIN_SHADER_DIRECTORY="${CMAKE_SOURCE_DIR}/engine/shaders"
        COMET_CONFIG_DIRECTORY="${CMAKE_SOURCE_DIR}/config"
        COMET_CONFIG_PROFILE="${COMET_CONFIG_PROFILE}"
)
