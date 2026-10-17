if(NOT DEFINED COMET_SOURCE_ROOT OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "COMET_SOURCE_ROOT and TEST_ROOT are required")
endif()

# Test forbidden dependencies in a disposable copy, never in the working tree.
file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/engine/cmake" "${TEST_ROOT}/editor" "${TEST_ROOT}/app")
file(COPY "${COMET_SOURCE_ROOT}/engine/src" DESTINATION "${TEST_ROOT}/engine")
file(COPY "${COMET_SOURCE_ROOT}/editor/src" DESTINATION "${TEST_ROOT}/editor")
file(GLOB app_sources "${COMET_SOURCE_ROOT}/app/*.cpp" "${COMET_SOURCE_ROOT}/app/*.h")
file(COPY ${app_sources} DESTINATION "${TEST_ROOT}/app")
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
# 覆盖规则类别；完整模块清单由实际仓库的 module_boundaries 检查。
probe_dependency("src/core/task_scheduler.cpp" "\"config/config.h\""
    "Foundation violates module dependencies")
probe_dependency("src/core/task_scheduler.cpp" "\"common/json.h\""
    "Foundation must not depend on Serialization")
probe_dependency("src/asset/database.cpp" "<vulkan/vulkan_core.h>"
    "AssetPipeline includes a runtime backend")
probe_dependency("src/graphics/context.cpp" "<GLFW/glfw3.h>"
    "Graphics includes a runtime backend")
probe_dependency("src/scene/scene.cpp" "\"scene/runtime_session.h\""
    "World violates module dependencies")
probe_dependency("src/gameplay/character_controller.cpp" "\"physics/physics_service.h\""
    "Gameplay violates module dependencies")
probe_dependency("src/animation/animation_system.cpp" "\"gameplay/character_controller.h\""
    "Runtime violates module dependencies")
probe_dependency("src/config/player_settings.cpp" "\"runtime/player_settings_owner.h\""
    "PlayerSettings violates module dependencies")
probe_dependency("src/core/task_scheduler.cpp" "<glm/glm.hpp>"
    "Use core/math_utils.h instead of GLM directly")
probe_dependency("src/scene/scene.cpp" "<simdjson.h>"
    "Use common/json.h instead of simdjson directly")
