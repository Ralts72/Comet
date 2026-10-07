if(NOT DEFINED COMET_SOURCE_ROOT)
    message(FATAL_ERROR "COMET_SOURCE_ROOT is required")
endif()

function(check_includes root paths forbidden rule)
    set(allowed "${ARGV4}")
    foreach(path IN LISTS paths)
        file(STRINGS "${root}/${path}" includes
            REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")
        foreach(line IN LISTS includes)
            if(allowed AND line MATCHES "^[ \t]*#[ \t]*include[ \t]*[<\"](${allowed})[>\"]")
                continue()
            endif()
            if(line MATCHES "^[ \t]*#[ \t]*include[ \t]*[<\"](${forbidden})")
                message(FATAL_ERROR "${rule}: ${path}: ${line}")
            endif()
        endforeach()
    endforeach()
endfunction()

set(ENGINE_SOURCE "${COMET_SOURCE_ROOT}/engine/src")
file(GLOB_RECURSE ENGINE_FILES RELATIVE "${ENGINE_SOURCE}"
    "${ENGINE_SOURCE}/*.h" "${ENGINE_SOURCE}/*.cpp")
check_includes("${ENGINE_SOURCE}" "${ENGINE_FILES}"
    "editor/|imgui|player_input_menu\\.h|player_input_panel\\.h|input_widgets\\.h"
    "Engine must not include Editor or shared UI")
set(ENGINE_CORE_FILES ${ENGINE_FILES})
list(FILTER ENGINE_CORE_FILES EXCLUDE REGEX "^ui/")
check_includes("${ENGINE_SOURCE}" "${ENGINE_CORE_FILES}" "RmlUi/|ui/rml_"
    "Only the optional Engine UI module may depend on RmlUi")

set(LOW_LEVEL_FILES)
foreach(directory common input scene scripting audio)
    file(GLOB_RECURSE files RELATIVE "${ENGINE_SOURCE}"
        "${ENGINE_SOURCE}/${directory}/*.h" "${ENGINE_SOURCE}/${directory}/*.cpp")
    list(APPEND LOW_LEVEL_FILES ${files})
endforeach()
check_includes("${ENGINE_SOURCE}" "${LOW_LEVEL_FILES}" "render/|graphics/|[Vv]ulkan|GLFW/"
    "Scene/Input/Scripting/Audio/Common must not include rendering or platform backends")

file(GLOB_RECURSE ASSET_FILES RELATIVE "${ENGINE_SOURCE}"
    "${ENGINE_SOURCE}/asset/*.h" "${ENGINE_SOURCE}/asset/*.cpp")
# 运行时加载桥接只允许在 AssetManager 实现中依赖 Render。
list(REMOVE_ITEM ASSET_FILES asset/asset_manager.cpp asset/asset_manager_async.cpp
    asset/data/texture_data.h)
check_includes("${ENGINE_SOURCE}" "${ASSET_FILES}" "render/|graphics/|[Vv]ulkan|GLFW/"
    "CPU asset code must not include rendering or platform backends")
check_includes("${ENGINE_SOURCE}" "asset/data/texture_data.h"
    "render/|graphics/|[Vv]ulkan|GLFW/"
    "TextureData may only use backend-free graphics enums" "graphics/enums\\.h")

set(EDITOR_SOURCE "${COMET_SOURCE_ROOT}/editor/src")
file(GLOB_RECURSE EDITOR_FILES RELATIVE "${EDITOR_SOURCE}"
    "${EDITOR_SOURCE}/*.h" "${EDITOR_SOURCE}/*.cpp")
check_includes("${EDITOR_SOURCE}" "${EDITOR_FILES}"
    "render/(scene/scene_renderer|render_context|frame_scheduler|presentation)\\.h|[Vv]ulkan|GLFW/"
    "Editor features must use Renderer workflows, not rendering internals")

set(UI_SOURCE "${COMET_SOURCE_ROOT}/ui/src")
file(GLOB_RECURSE UI_FILES RELATIVE "${UI_SOURCE}"
    "${UI_SOURCE}/*.h" "${UI_SOURCE}/*.cpp")
check_includes("${UI_SOURCE}" "${UI_FILES}" "editor/|project/editor_|ui/language\\.h|editor_state\\.h"
    "Shared UI must not depend on Editor state or resources")

set(RUNTIME_UI_SOURCE "${COMET_SOURCE_ROOT}/engine/src/ui")
file(GLOB_RECURSE RUNTIME_UI_FILES RELATIVE "${RUNTIME_UI_SOURCE}"
    "${RUNTIME_UI_SOURCE}/*.h" "${RUNTIME_UI_SOURCE}/*.cpp")
check_includes("${RUNTIME_UI_SOURCE}" "${RUNTIME_UI_FILES}"
    "editor/|imgui|app/|input/player_input_edit\\.h|player_input_panel\\.h|render/frame_scheduler\\.h"
    "Engine UI must not depend on game menus, Editor, ImGui or frame scheduling internals")
file(GLOB_RECURSE APP_FILES RELATIVE "${COMET_SOURCE_ROOT}/app"
    "${COMET_SOURCE_ROOT}/app/*.h" "${COMET_SOURCE_ROOT}/app/*.cpp")
check_includes("${COMET_SOURCE_ROOT}/app" "${APP_FILES}"
    "editor/|imgui|player_input_panel\\.h|imgui_context\\.h"
    "Runtime app must not depend on Editor or ImGui")
