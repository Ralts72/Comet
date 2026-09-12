# 独立临时工程验证真实 CLI、CMake depfile 重建与失败保留，不启动图形窗口。
string(RANDOM LENGTH 12 RANDOM_SUFFIX)
set(ROOT "${TEST_ROOT}/${RANDOM_SUFFIX}")
file(MAKE_DIRECTORY "${ROOT}/shared")
file(WRITE "${ROOT}/shared/value #$.glsl" "#define VALUE 2.0\n")
file(WRITE "${ROOT}/source.vert" [=[#version 450
#extension GL_GOOGLE_include_directive : require
#include "value #$.glsl"
void main(){gl_Position=vec4(VALUE*SCALE);}
]=])
file(WRITE "${ROOT}/CMakeLists.txt" "
cmake_minimum_required(VERSION 3.31)
project(ShaderContract NONE)
add_executable(compiler IMPORTED)
set_target_properties(compiler PROPERTIES IMPORTED_LOCATION [==[${COMPILE_TOOL}]==])
include([==[${COMET_ROOT}/engine/cmake/compile_shader.cmake]==])
compile_shaders(TARGET shaders COMPILER compiler
    INCLUDE_DIRECTORY [==[${ROOT}/shared]==]
    OUTPUT_DIRECTORY [==[${ROOT}/generated]==]
    ENTRY_POINT vertex_entry TARGET_ENVIRONMENT vulkan1.3 DEFINES SCALE=3.0
    SOURCES [==[${ROOT}/source.vert]==])
")
function(run_checked)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    if (NOT RESULT EQUAL 0)
        message(FATAL_ERROR "${ARGV}\n${OUTPUT}\n${ERROR}")
    endif ()
endfunction()
run_checked("${CMAKE_COMMAND}" -S "${ROOT}" -B "${ROOT}/build" -G "${GENERATOR}")
set(BUILD_COMMAND "${CMAKE_COMMAND}" --build "${ROOT}/build" --target shaders --config "${BUILD_CONFIG}")
run_checked(${BUILD_COMMAND})
set(SPV "${ROOT}/generated/spv/source.vert.spv")
file(SHA256 "${SPV}" ORIGINAL)
file(READ "${SPV}.d" ORIGINAL_DEPFILE)
# 超过文件系统时间戳粒度，确保测试的是依赖追踪而非偶然的 mtime。
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
file(WRITE "${ROOT}/shared/value #$.glsl" "#define VALUE 4.0\n")
run_checked(${BUILD_COMMAND})
file(SHA256 "${SPV}" CHANGED)
if (ORIGINAL STREQUAL CHANGED)
    message(FATAL_ERROR "Included header did not rebuild the shader")
endif ()
file(READ "${ROOT}/generated/include/source_vert.h" EMBEDDED)
if (NOT EMBEDDED MATCHES "SOURCE_VERT")
    message(FATAL_ERROR "Missing generated C++ bytecode")
endif ()
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
file(WRITE "${ROOT}/shared/value #$.glsl" "#error intentional compilation failure\n")
execute_process(COMMAND ${BUILD_COMMAND} RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if (RESULT EQUAL 0 OR NOT "${OUTPUT}${ERROR}" MATCHES "intentional compilation failure")
    message(FATAL_ERROR "Compile error was not reported: ${OUTPUT}\n${ERROR}")
endif ()
file(SHA256 "${SPV}" AFTER_FAILURE)
file(READ "${SPV}.d" AFTER_DEPFILE)
if (NOT CHANGED STREQUAL AFTER_FAILURE OR NOT ORIGINAL_DEPFILE STREQUAL AFTER_DEPFILE)
    message(FATAL_ERROR "Failed compilation replaced the previous artifact or depfile")
endif ()
execute_process(COMMAND "${COMPILE_TOOL}" --source "${ROOT}/source.vert"
        --stage unknown --output "${SPV}" RESULT_VARIABLE RESULT)
if (RESULT EQUAL 0)
    message(FATAL_ERROR "Invalid compiler arguments were accepted")
endif ()
# ROOT 是本次随机创建的独立测试目录，失败时保留它以便诊断。
file(REMOVE_RECURSE "${ROOT}")
