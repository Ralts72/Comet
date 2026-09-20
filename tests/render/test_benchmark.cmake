set(platform_args)
if(APPLE)
    list(APPEND platform_args -NSAutomaticWindowAnimationsEnabled NO)
endif()
execute_process(COMMAND "${BENCHMARK}" --help
        RESULT_VARIABLE result OUTPUT_VARIABLE help ERROR_VARIABLE error TIMEOUT 5)
if(NOT result STREQUAL "0" OR NOT help MATCHES "Usage: render_benchmark OUTPUT.csv")
    message(FATAL_ERROR "Invalid benchmark help: ${result} ${help} ${error}")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
foreach(bloom 0 1)
    set(output "${OUTPUT_DIRECTORY}/bloom-${bloom}.csv")
    execute_process(COMMAND "${BENCHMARK}" "${output}" 8 160 120 16 ${bloom} ${platform_args}
            RESULT_VARIABLE result OUTPUT_VARIABLE report ERROR_VARIABLE error TIMEOUT 30)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "Benchmark failed: ${result}\n${report}\n${error}")
    endif()
    file(READ "${output}" csv)
    foreach(metric cpu_wall cpu_events cpu_update cpu_prepare cpu_render_submit cpu_graph
            "cpu_directional shadow" cpu_scene cpu_display)
        if(NOT csv MATCHES "\n${metric},16,[0-9.eE+-]+,[0-9.eE+-]+")
            message(FATAL_ERROR "Missing complete metric: ${metric}\n${csv}")
        endif()
    endforeach()
    if(NOT csv MATCHES "scene_draws=9 lights=3 msaa=4 bloom=${bloom}"
            OR NOT csv MATCHES "pipeline_binds=1 material_binds=1 cached_material_versions=1"
            OR NOT csv MATCHES "gpu_samples=[0-9]+ gpu_status=(complete|partial|unsupported|degraded)")
        message(FATAL_ERROR "Invalid scene or GPU coverage metadata\n${csv}")
    endif()
    if(bloom)
        foreach(pass extract horizontal vertical)
            if(NOT csv MATCHES "\ncpu_bloom ${pass},16,")
                message(FATAL_ERROR "Missing Bloom pass: ${pass}")
            endif()
        endforeach()
    elseif(csv MATCHES "\n(cpu|gpu)_bloom")
        message(FATAL_ERROR "Disabled Bloom was measured")
    endif()
endforeach()

# 参数失败必须发生在启动前，并且不能覆盖上一次有效报告。
file(SHA256 "${output}" original_hash)
foreach(objects -1 0 4097 999999999999999999999999999 8x)
    execute_process(COMMAND "${BENCHMARK}" "${output}" ${objects} 160 120 16 1
            RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error TIMEOUT 5)
    if(NOT result STREQUAL "2" OR NOT error MATCHES "Invalid benchmark argument")
        message(FATAL_ERROR "Invalid object count was accepted: ${objects}: ${result} ${error}")
    endif()
endforeach()
execute_process(COMMAND "${BENCHMARK}" "${output}" 8 160 120 16 2
        RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET TIMEOUT 5)
if(NOT result STREQUAL "2")
    message(FATAL_ERROR "Invalid Bloom switch was accepted")
endif()

# 临时目录不可用时在启动引擎前失败，不覆盖已有报告。
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "TMPDIR=${output}" "TMP=${output}" "TEMP=${output}"
        "${BENCHMARK}" "${output}" 8 160 120 16 1
        RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error TIMEOUT 5)
if(NOT result STREQUAL "1" OR NOT error MATCHES "Cannot locate temporary directory")
    message(FATAL_ERROR "Invalid temporary directory was not rejected: ${result} ${error}")
endif()
file(SHA256 "${output}" final_hash)
if(NOT final_hash STREQUAL original_hash)
    message(FATAL_ERROR "Invalid invocation modified the previous report")
endif()
