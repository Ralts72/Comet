add_library(comet_asset_tools STATIC shader_program_import.cpp project_prepare.cpp)
target_include_directories(comet_asset_tools PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(comet_asset_tools PUBLIC engine PRIVATE comet_shader_tools)

add_executable(comet_prepare_project main.cpp)
target_link_libraries(comet_prepare_project PRIVATE comet_asset_tools)
target_compile_definitions(comet_prepare_project PRIVATE
        COMET_CONFIG_DIRECTORY="${CMAKE_SOURCE_DIR}/config"
        COMET_CONFIG_PROFILE="${COMET_CONFIG_PROFILE}")
comet_copy_runtime_dependencies(comet_prepare_project)
