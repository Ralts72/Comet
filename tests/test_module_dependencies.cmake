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
probe_dependency("src/asset/database.cpp" "<vulkan/vulkan_core.h>"
    "AssetPipeline includes a runtime backend")
probe_dependency("src/input/input.cpp" "\"scene/scene.h\""
    "Input violates module dependencies")
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
