if(NOT DEFINED PREPARE_TOOL OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "PREPARE_TOOL and TEST_ROOT are required")
endif()

file(MAKE_DIRECTORY "${TEST_ROOT}/assets")
file(WRITE "${TEST_ROOT}/project.json" [=[
{"version":2,"id":"00000000-0000-4000-8000-000000000001",
 "name":"CLI test","startup_scene":"startup.scene",
 "input_contexts":[],"input_actions":[]}
]=])
file(WRITE "${TEST_ROOT}/assets/startup.scene" [=[{"version":2,"entities":[]}]=])

foreach(input "${TEST_ROOT}" "${TEST_ROOT}/project.json")
    execute_process(COMMAND "${PREPARE_TOOL}" "${input}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0 OR NOT output MATCHES "Project assets ready")
        message(FATAL_ERROR "Project preparation failed: ${result}\n${output}\n${error}")
    endif()
endforeach()

file(WRITE "${TEST_ROOT}/project.json" [=[
{"version":2,"id":"00000000-0000-4000-8000-000000000001",
 "name":"UI CLI test","startup_scene":"startup.scene",
 "input_contexts":[],"input_actions":[],
 "ui":{"document":"menu.rml","controller":"menu.ui.lua"}}
]=])
file(WRITE "${TEST_ROOT}/assets/menu.ui.lua" "return {}")
file(WRITE "${TEST_ROOT}/assets/menu.rml" [=[
<rml><body><div style="display: none"><img src="hidden.png" /></div></body></rml>
]=])
file(REMOVE "${TEST_ROOT}/assets/hidden.png")
execute_process(COMMAND "${PREPARE_TOOL}" "${TEST_ROOT}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 1 OR NOT error MATCHES "hidden.png")
    message(FATAL_ERROR "Missing UI resource must report failure: ${result}: ${error}")
endif()
file(WRITE "${TEST_ROOT}/assets/hidden.png" "resource bytes")
execute_process(COMMAND "${PREPARE_TOOL}" "${TEST_ROOT}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT output MATCHES "Project assets ready")
    message(FATAL_ERROR "UI preparation failed: ${result}\n${output}\n${error}")
endif()

file(WRITE "${TEST_ROOT}/assets/startup.scene" "invalid scene")
execute_process(COMMAND "${PREPARE_TOOL}" "${TEST_ROOT}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 1 OR error STREQUAL "")
    message(FATAL_ERROR "Invalid scene must report failure: ${result}: ${error}")
endif()
