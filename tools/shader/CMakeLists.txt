add_library(comet_shader_tools STATIC compiler.cpp)
target_include_directories(comet_shader_tools PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CMAKE_SOURCE_DIR}/engine/src")
target_link_libraries(comet_shader_tools PRIVATE
        glslang::glslang glslang::glslang-default-resource-limits)

add_executable(comet_shader_compiler main.cpp
        "${CMAKE_SOURCE_DIR}/engine/src/common/file_io.cpp")
target_link_libraries(comet_shader_compiler PRIVATE comet_shader_tools)
target_compile_definitions(comet_shader_compiler PRIVATE COMET_EXPORTS)
