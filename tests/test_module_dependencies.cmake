if(NOT DEFINED COMET_SOURCE_ROOT OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "COMET_SOURCE_ROOT and TEST_ROOT are required")
endif()

# Test forbidden dependencies in a disposable copy, never in the working tree.
file(MAKE_DIRECTORY "${TEST_ROOT}/engine/cmake" "${TEST_ROOT}/editor" "${TEST_ROOT}/app")
file(COPY "${COMET_SOURCE_ROOT}/engine/src" DESTINATION "${TEST_ROOT}/engine")
file(COPY "${COMET_SOURCE_ROOT}/editor/src" DESTINATION "${TEST_ROOT}/editor")
file(COPY "${COMET_SOURCE_ROOT}/app/main.cpp" DESTINATION "${TEST_ROOT}/app")
file(COPY "${COMET_SOURCE_ROOT}/engine/cmake/module_sources.cmake"
    DESTINATION "${TEST_ROOT}/engine/cmake")
set(checker "${COMET_SOURCE_ROOT}/cmake/check_module_boundaries.cmake")

function(expect_boundary expected)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DCOMET_SOURCE_ROOT=${TEST_ROOT}" -P "${checker}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(expected STREQUAL "")
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Baseline must pass: ${output}\n${error}")
        endif()
    elseif(result EQUAL 0 OR NOT "${output}${error}" MATCHES "${expected}")
        message(FATAL_ERROR "Expected ${expected}, got ${result}: ${output}\n${error}")
    endif()
endfunction()

expect_boundary("")
set(editor_adapter "${TEST_ROOT}/editor/src/project/game_ui.cpp")
file(READ "${editor_adapter}" original_adapter)
file(APPEND "${editor_adapter}" "\n#include \"core/engine.h\"\n")
expect_boundary("Editor features must use explicit module workflows")
file(WRITE "${editor_adapter}" "${original_adapter}")

function(probe_dependency source header expected)
    set(path "${TEST_ROOT}/engine/${source}")
    file(READ "${path}" original)
    file(APPEND "${path}" "\n#include \"common/dependency_probe.h\"\n")
    file(WRITE "${TEST_ROOT}/engine/src/common/dependency_probe.h" "#include ${header}\n")
    expect_boundary("${expected}")
    file(WRITE "${path}" "${original}")
    file(REMOVE "${TEST_ROOT}/engine/src/common/dependency_probe.h")
endfunction()
probe_dependency("src/core/task_scheduler.cpp" "\"config/config.h\""
    "Foundation violates module dependencies")
probe_dependency("src/core/task_scheduler.cpp" "\"common/json.h\""
    "Foundation must not depend on Serialization")
probe_dependency("src/asset/database.cpp" "<vulkan/vulkan_core.h>"
    "AssetPipeline includes a runtime backend")
probe_dependency("src/input/input.cpp" "\"scene/scene.h\""
    "Input violates module dependencies")
probe_dependency("src/asset/script.h" "\"scene/entity.h\""
    "AssetData violates module dependencies")
probe_dependency("src/asset/script.h" "\"scripting/script_instance.h\""
    "AssetData violates module dependencies")
probe_dependency("src/common/parameters.h" "\"scene/property.h\""
    "Foundation violates module dependencies")
probe_dependency("src/asset/runtime/script_loader.cpp" "\"scripting/script_instance.h\""
    "RuntimeAssets violates module dependencies")
probe_dependency("src/asset/data/material_data.cpp" "\"asset/database.h\""
    "AssetData violates module dependencies")
probe_dependency("src/scene/scene_runtime.cpp" "\"core/engine.h\""
    "Runtime violates module dependencies")
probe_dependency("src/scene/systems/system.h" "\"scene/systems/physics_system.h\""
    "Runtime violates module dependencies")
probe_dependency("src/scene/scene_runtime.h" "<RmlUi/Core.h>"
    "Runtime includes a runtime backend")
probe_dependency("src/scene/scene.cpp" "\"scene/scene_runtime.h\""
    "World violates module dependencies")
probe_dependency("src/scene/scene.h" "\"scene/runtime_session.h\""
    "World violates module dependencies")
probe_dependency("src/scene/scene.cpp" "\"input/input_actions.h\""
    "World violates module dependencies")
probe_dependency("src/scene/runtime_session.h" "<vulkan/vulkan_core.h>"
    "Runtime includes a runtime backend")
probe_dependency("src/scene/scene.h" "\"audio/audio_commands.h\""
    "World violates module dependencies")
probe_dependency("src/scene/runtime_services.h" "\"audio/audio_service.h\""
    "Runtime violates module dependencies")
probe_dependency("src/audio/audio_service.h" "<miniaudio.h>"
    "Audio includes a runtime backend")
probe_dependency("src/scene/systems/audio_system.cpp" "\"core/engine.h\""
    "Audio violates module dependencies")
probe_dependency("src/scene/scene.h" "\"physics/physics_commands.h\""
    "World violates module dependencies")
probe_dependency("src/physics/physics_commands.h" "\"physics/physics_service.h\""
    "Runtime violates module dependencies")
probe_dependency("src/physics/physics_service.h" "<Jolt/Jolt.h>"
    "Physics includes a runtime backend")
probe_dependency("src/scene/systems/physics_system.cpp" "<Jolt/Physics/PhysicsSystem.h>"
    "Physics includes a runtime backend")
probe_dependency("src/physics/physics_service.cpp" "\"scene/scene.h\""
    "PhysicsBackend violates module dependencies")
probe_dependency("src/physics/physics_service.h" "\"scene/entity.h\""
    "PhysicsBackend violates module dependencies")
probe_dependency("src/scene/script_component.h" "\"scene/systems/script_system.h\""
    "World violates module dependencies")
probe_dependency("src/scene/systems/script_system.cpp" "\"asset/asset_manager.h\""
    "Scripting violates module dependencies")
probe_dependency("src/scene/systems/script_system.h" "<lua.h>"
    "Scripting includes a runtime backend")
probe_dependency("src/scene/systems/script_system.h" "<lauxlib.h>"
    "Scripting includes a runtime backend")
probe_dependency("src/scripting/script_instance.cpp" "\"physics/physics_service.h\""
    "Scripting violates module dependencies")
probe_dependency("src/scripting/lua_bindings.cpp" "\"render/material/material_programs.h\""
    "Scripting violates module dependencies")
probe_dependency("src/asset/runtime/asset_loader.cpp" "<vulkan/vulkan_core.h>"
    "RuntimeAssets includes a runtime backend")
probe_dependency("src/asset/asset_manager_async.cpp" "\"render/resource/environment.h\""
    "RuntimeAssets violates module dependencies")
probe_dependency("src/asset/runtime/render_asset_publisher.h" "\"graphics/result.h\""
    "RuntimeAssets violates module dependencies")
probe_dependency("src/asset/import/import_service.cpp" "\"asset/runtime/render_asset_publisher.h\""
    "AssetPipeline violates module dependencies")
probe_dependency("src/render/resource/render_asset_publisher.cpp" "\"asset/database.h\""
    "RenderAssetPublication violates module dependencies")
probe_dependency("src/render/resource/render_asset_publisher.cpp" "\"asset/import/import_service.h\""
    "RenderAssetPublication violates module dependencies")
probe_dependency("src/core/window.h" "\"graphics/context.h\""
    "Platform violates module dependencies")
probe_dependency("src/core/window.h" "\"config/config.h\""
    "Platform violates module dependencies")
probe_dependency("src/graphics/device.cpp" "\"render/renderer.h\""
    "Graphics violates module dependencies")
probe_dependency("src/graphics/context.cpp" "<GLFW/glfw3.h>"
    "Graphics includes a runtime backend")
probe_dependency("src/render/renderer.h" "\"config/config.h\""
    "Render violates module dependencies")
probe_dependency("src/render/renderer.cpp" "\"asset/asset_manager.h\""
    "Render violates module dependencies")
probe_dependency("src/render/lighting.h" "\"scene/components.h\""
    "RenderSnapshot violates module dependencies")
probe_dependency("src/render/scene/render_scene.h" "<vulkan/vulkan_core.h>"
    "RenderSnapshot includes a runtime backend")
probe_dependency("src/ui/rml_context.cpp" "<GLFW/glfw3.h>"
    "GameUi includes a runtime backend")
probe_dependency("src/ui/project_ui.cpp" "\"core/engine.h\""
    "GameUi violates module dependencies")
