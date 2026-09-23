if(NOT DEFINED COMET_SOURCE_ROOT)
    message(FATAL_ERROR "COMET_SOURCE_ROOT is required")
endif()

set(ENGINE_SOURCE "${COMET_SOURCE_ROOT}/engine/src")

function(check_includes paths forbidden rule)
    foreach(path IN LISTS paths)
        file(STRINGS "${ENGINE_SOURCE}/${path}" includes
            REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")
        foreach(line IN LISTS includes)
            if(line MATCHES "^[ \t]*#[ \t]*include[ \t]*[<\"](${forbidden})")
                message(FATAL_ERROR "${rule}: ${path}: ${line}")
            endif()
        endforeach()
    endforeach()
endfunction()

file(GLOB_RECURSE ENGINE_FILES RELATIVE "${ENGINE_SOURCE}"
    "${ENGINE_SOURCE}/*.h" "${ENGINE_SOURCE}/*.cpp")
check_includes("${ENGINE_FILES}" "editor/|imgui" "Engine must not include Editor or ImGui")

set(LOW_LEVEL_FILES)
foreach(directory common input scene scripting)
    file(GLOB_RECURSE files RELATIVE "${ENGINE_SOURCE}"
        "${ENGINE_SOURCE}/${directory}/*.h" "${ENGINE_SOURCE}/${directory}/*.cpp")
    list(APPEND LOW_LEVEL_FILES ${files})
endforeach()
check_includes("${LOW_LEVEL_FILES}" "render/|graphics/|[Vv]ulkan|GLFW/"
    "Scene/Input/Scripting/Common must not include rendering or platform backends")
