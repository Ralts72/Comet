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
check_includes("${ENGINE_SOURCE}" "${ENGINE_FILES}" "editor/|imgui"
    "Engine must not include Editor or ImGui")

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
list(REMOVE_ITEM EDITOR_FILES ui/imgui_context.h ui/imgui_context.cpp)
check_includes("${EDITOR_SOURCE}" "${EDITOR_FILES}"
    "render/(scene/scene_renderer|render_context|frame_scheduler|presentation)\\.h|[Vv]ulkan|GLFW/"
    "Editor features must use Renderer workflows, not rendering internals")
