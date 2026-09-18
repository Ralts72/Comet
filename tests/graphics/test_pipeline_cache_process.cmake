string(RANDOM LENGTH 12 RANDOM_SUFFIX)
set(ROOT "${TEST_ROOT}/${RANDOM_SUFFIX} cache")
file(MAKE_DIRECTORY "${ROOT}/config/profiles")
file(WRITE "${ROOT}/config/common.yaml" "vulkan: {msaa_samples: 1}\n")
file(WRITE "${ROOT}/config/profiles/probe.yaml"
        "diagnostics: {enable_validation: true, enable_file_logging: false, log_level: info}\n")
foreach(EXPECTED IN ITEMS missing restored)
    execute_process(COMMAND "${PROBE}" "${ROOT}/config" "${ROOT}/cache" "${EXPECTED}"
            RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR TIMEOUT 30)
    if (NOT RESULT STREQUAL "0" OR "${OUTPUT}${ERROR}" MATCHES "VUID-|Validation Error")
        message(FATAL_ERROR "Cache ${EXPECTED} process failed (${RESULT})\n${OUTPUT}\n${ERROR}")
    endif ()
endforeach()
file(REMOVE_RECURSE "${ROOT}")
