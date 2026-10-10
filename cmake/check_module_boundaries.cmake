cmake_minimum_required(VERSION 3.31)

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

set(EDITOR_UI_HEADERS
    "project/(player_input_panel|input_widgets)\\.h|ui/(imgui_context|widgets|language)\\.h")
set(ENGINE_SOURCE "${COMET_SOURCE_ROOT}/engine/src")

# Check each internal module's transitive engine-owned includes.
include("${COMET_SOURCE_ROOT}/engine/cmake/module_sources.cmake")
set(FOUNDATION_HEADERS
    "common/|core/(frame_timer|geometry|math_utils|project_paths|task_scheduler)\\.h$|diagnostics/(frame_diagnostics|log_settings|logger|profiler|timing_history)\\.h$")
set(SERIALIZATION_HEADERS "${FOUNDATION_HEADERS}")
set(SHADER_HEADERS "${FOUNDATION_HEADERS}|graphics/(enums|pipeline/shader_interface)\\.h$")
set(ASSET_HEADERS
    "${SERIALIZATION_HEADERS}|${SHADER_HEADERS}|asset/(data/|serialization/|(handle|metadata|import_settings|registry|reference|script)\\.h$)")
set(INPUT_HEADERS "${SERIALIZATION_HEADERS}|input/")
set(AUDIO_DATA_HEADERS "audio/(audio_category|audio_settings)\\.h$")
set(WORLD_HEADERS
    "${ASSET_HEADERS}|audio/audio_category\\.h$|scene/(component_registry|components|entity|entity_id|entity_uuid|material_parameters|property|scene|scene_serializer|scene_settings|script_component)\\.h$")
set(PIPELINE_HEADERS "${ASSET_HEADERS}|asset/(artifact/|import/|database\\.h$)")

set(RUNTIME_HEADERS "${WORLD_HEADERS}|${INPUT_HEADERS}|audio/audio_commands\\.h$|physics/physics_commands\\.h$|scene/(scene_runtime|runtime_session|runtime_services|systems/system)\\.h$")

set(AUDIO_HEADERS "${RUNTIME_HEADERS}|audio/|scene/systems/audio_system\\.h$")

set(PHYSICS_HEADERS "${RUNTIME_HEADERS}|physics/|scene/systems/physics_system\\.h$")
set(PHYSICS_BACKEND_HEADERS "${ASSET_HEADERS}|audio/audio_category\\.h$|physics/|scene/(components|entity_id|entity_uuid)\\.h$")
set(SCRIPTING_HEADERS "${RUNTIME_HEADERS}|scripting/|scene/systems/script_system\\.h$")
set(RUNTIME_ASSET_HEADERS "${PIPELINE_HEADERS}|${AUDIO_DATA_HEADERS}|asset/(asset_manager\\.h$|runtime/)|scripting/script_compiler\\.h$|audio/audio\\.h$|graphics/error\\.h$")
set(RENDER_ASSET_HEADERS "${FOUNDATION_HEADERS}|asset/(handle|registry)\\.h$|asset/data/|asset/runtime/render_asset_publisher\\.h$|graphics/|render/(resource/|material/material\\.h$)")
set(PLATFORM_HEADERS "${INPUT_HEADERS}|core/(window|window_settings)\\.h$")
set(GRAPHICS_HEADERS "${FOUNDATION_HEADERS}|graphics/|${PLATFORM_HEADERS}")
set(RENDER_HEADERS "${GRAPHICS_HEADERS}|${WORLD_HEADERS}|render/|asset/artifact/shader_program_artifact\\.h$|asset/runtime/render_asset_publisher\\.h$")
set(GAME_UI_HEADERS "${RENDER_HEADERS}|${AUDIO_DATA_HEADERS}|config/display_settings\\.h$|ui/|core/project\\.h$")

function(check_module_closure module sources allowed)
    set(pending ${sources})
    set(visited)
    while(pending)
        list(POP_FRONT pending path)
        string(REGEX REPLACE "^src/" "" path "${path}")
        if(path IN_LIST visited)
            continue()
        endif()
        list(APPEND visited "${path}")
        file(STRINGS "${ENGINE_SOURCE}/${path}" includes
            REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")
        foreach(line IN LISTS includes)
            if(line MATCHES "[<\"]([Vv]ulkan/|GLFW/|RmlUi/|imgui|(lua|lauxlib|lualib)\\.h|Jolt/|miniaudio\\.h)")
                if(NOT ((module MATCHES "^(Graphics|Render|RenderAssetPublication|GameUi)$"
                    AND line MATCHES "[<\"][Vv]ulkan/") OR
                    (module MATCHES "^(Platform|Graphics|Render|GameUi)$"
                    AND path MATCHES "^(core/window|graphics/window_surface)\\.cpp$"
                    AND line MATCHES "[<\"]GLFW/") OR
                    (module STREQUAL "GameUi" AND path MATCHES "^ui/"
                    AND line MATCHES "[<\"]RmlUi/") OR
                    (module STREQUAL "GameUi" AND path MATCHES "^ui/(lua_controller|project_ui)\\.cpp$"
                    AND line MATCHES "[<\"](lua|lauxlib|lualib)\\.h[>\"]") OR
                    (module STREQUAL "Audio" AND path STREQUAL "audio/audio.cpp"
                    AND line MATCHES "[<\"]miniaudio\\.h[>\"]") OR
                    (module MATCHES "^Physics" AND path STREQUAL "physics/physics_service.cpp"
                    AND line MATCHES "[<\"]Jolt/") OR
                    (module STREQUAL "Scripting" AND
                    path MATCHES "^scripting/(script_instance|lua_bindings)\\.cpp$" AND
                    line MATCHES "[<\"](lua|lauxlib|lualib)\\.h[>\"]")))
                    message(FATAL_ERROR "${module} includes a runtime backend: ${path}: ${line}")
                endif()
            endif()
            if(NOT line MATCHES "[<\"]([^>\"]+)[>\"]")
                continue()
            endif()
            set(header "${CMAKE_MATCH_1}")
            if(NOT EXISTS "${ENGINE_SOURCE}/${header}")
                get_filename_component(directory "${path}" DIRECTORY)
                set(header "${directory}/${header}")
                if(NOT EXISTS "${ENGINE_SOURCE}/${header}")
                    continue()
                endif()
            endif()
            cmake_path(NORMAL_PATH header)
            if(NOT header MATCHES "^(${allowed})")
                message(FATAL_ERROR "${module} violates module dependencies: ${path}: ${line}")
            endif()
            if(module MATCHES "^(Foundation|ShaderContracts)$" AND header STREQUAL "common/json.h")
                message(FATAL_ERROR "${module} must not depend on Serialization: ${path}: ${line}")
            endif()
            list(APPEND pending "${header}")
        endforeach()
    endwhile()
endfunction()
check_module_closure(Foundation "${COMET_FOUNDATION_SOURCES};src/common/result.h;src/common/scope_exit.h"
    "${FOUNDATION_HEADERS}")
check_module_closure(Serialization "${COMET_SERIALIZATION_SOURCES}" "${SERIALIZATION_HEADERS}")
check_module_closure(ShaderContracts "${COMET_SHADER_CONTRACTS_SOURCES}" "${SHADER_HEADERS}")
check_module_closure(AssetData "${COMET_ASSET_DATA_SOURCES}" "${ASSET_HEADERS}")
check_module_closure(Input "${COMET_INPUT_SOURCES}" "${INPUT_HEADERS}")
check_module_closure(World "${COMET_WORLD_SOURCES}" "${WORLD_HEADERS}")
check_module_closure(Runtime "${COMET_RUNTIME_SOURCES}" "${RUNTIME_HEADERS}")
check_module_closure(Audio "${COMET_AUDIO_SOURCES}" "${AUDIO_HEADERS}")
check_module_closure(Physics "${COMET_PHYSICS_SOURCES}" "${PHYSICS_HEADERS}")
check_module_closure(PhysicsBackend "src/physics/physics_service.cpp;src/physics/physics_service.h"
    "${PHYSICS_BACKEND_HEADERS}")
check_module_closure(Scripting "${COMET_SCRIPTING_SOURCES}" "${SCRIPTING_HEADERS}")
check_module_closure(AssetPipeline "${COMET_ASSET_PIPELINE_SOURCES}" "${PIPELINE_HEADERS}")
check_module_closure(RuntimeAssets "${COMET_RUNTIME_ASSETS_SOURCES}" "${RUNTIME_ASSET_HEADERS}")
check_module_closure(Platform "${COMET_PLATFORM_SOURCES}" "${PLATFORM_HEADERS}")
check_module_closure(Graphics "${COMET_GRAPHICS_SOURCES}" "${GRAPHICS_HEADERS}")
check_module_closure(RenderAssetPublication "src/render/resource/render_asset_publisher.cpp"
    "${RENDER_ASSET_HEADERS}")
check_module_closure(Render "${COMET_RENDER_SOURCES}" "${RENDER_HEADERS}")
check_module_closure(GameUi "${COMET_GAME_UI_SOURCES}" "${GAME_UI_HEADERS}")

file(GLOB_RECURSE ENGINE_FILES RELATIVE "${ENGINE_SOURCE}"
    "${ENGINE_SOURCE}/*.h" "${ENGINE_SOURCE}/*.cpp")
check_includes("${ENGINE_SOURCE}" "${ENGINE_FILES}"
    "editor/|imgui|player_input_menu\\.h|player_input_panel\\.h|input_widgets\\.h|${EDITOR_UI_HEADERS}"
    "Engine must not include Editor or ImGui")
set(ENGINE_CORE_FILES ${ENGINE_FILES})
list(FILTER ENGINE_CORE_FILES EXCLUDE REGEX "^ui/")
check_includes("${ENGINE_SOURCE}" "${ENGINE_CORE_FILES}" "RmlUi/|ui/rml_"
    "Only the Engine UI module may depend on RmlUi")

set(LOW_LEVEL_FILES)
foreach(directory common input scene scripting audio physics)
    file(GLOB_RECURSE files RELATIVE "${ENGINE_SOURCE}"
        "${ENGINE_SOURCE}/${directory}/*.h" "${ENGINE_SOURCE}/${directory}/*.cpp")
    list(APPEND LOW_LEVEL_FILES ${files})
endforeach()
check_includes("${ENGINE_SOURCE}" "${LOW_LEVEL_FILES}" "render/|graphics/|[Vv]ulkan|GLFW/"
    "Scene/Input/Scripting/Audio/Physics/Common must not include rendering or platform backends")

file(GLOB_RECURSE ASSET_FILES RELATIVE "${ENGINE_SOURCE}"
    "${ENGINE_SOURCE}/asset/*.h" "${ENGINE_SOURCE}/asset/*.cpp")
# 资产代码只使用 CPU 数据和不含后端头的渲染发布契约。
check_includes("${ENGINE_SOURCE}" "${ASSET_FILES}" "render/|graphics/|[Vv]ulkan|GLFW/"
    "CPU asset code must not include rendering or platform backends" "graphics/(enums|error)\\.h")

set(EDITOR_SOURCE "${COMET_SOURCE_ROOT}/editor/src")
file(GLOB_RECURSE EDITOR_FILES RELATIVE "${EDITOR_SOURCE}"
    "${EDITOR_SOURCE}/*.h" "${EDITOR_SOURCE}/*.cpp")
set(EDITOR_IMGUI_FILES ui/imgui_context.h ui/imgui_context.cpp)
list(REMOVE_ITEM EDITOR_FILES ${EDITOR_IMGUI_FILES})
check_includes("${EDITOR_SOURCE}" "${EDITOR_FILES}"
    "render/(scene/scene_renderer|render_context|frame_scheduler|presentation)\\.h|[Vv]ulkan|GLFW/"
    "Editor features must use Renderer workflows, not rendering internals")

# ImGui 呈现适配单独编译，允许连接图形后端，不依赖编辑器工作流。
check_includes("${EDITOR_SOURCE}" "${EDITOR_IMGUI_FILES}"
    "assets/|project/|inspector/|viewport/|ui/(language|menu_bar|dialogs|shortcuts)\\.h|editor_state\\.h"
    "Editor ImGui backend must not depend on Editor workflows")

set(RUNTIME_UI_SOURCE "${COMET_SOURCE_ROOT}/engine/src/ui")
file(GLOB_RECURSE RUNTIME_UI_FILES RELATIVE "${RUNTIME_UI_SOURCE}"
    "${RUNTIME_UI_SOURCE}/*.h" "${RUNTIME_UI_SOURCE}/*.cpp")
check_includes("${RUNTIME_UI_SOURCE}" "${RUNTIME_UI_FILES}"
    "editor/|imgui|app/|player_input_panel\\.h|${EDITOR_UI_HEADERS}|render/frame_scheduler\\.h"
    "Engine UI must not depend on game menus, Editor, ImGui or frame scheduling internals")
file(GLOB_RECURSE APP_FILES RELATIVE "${COMET_SOURCE_ROOT}/app"
    "${COMET_SOURCE_ROOT}/app/*.h" "${COMET_SOURCE_ROOT}/app/*.cpp")
check_includes("${COMET_SOURCE_ROOT}/app" "${APP_FILES}"
    "editor/|imgui|player_input_panel\\.h|imgui_context\\.h|${EDITOR_UI_HEADERS}"
    "Runtime app must not depend on Editor or ImGui")
