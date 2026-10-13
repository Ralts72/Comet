include("${CMAKE_SOURCE_DIR}/engine/cmake/compile_shader.cmake")
set(COMET_UI_SHADER_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src/ui/generated/shaders")
compile_shaders(
        TARGET comet_game_ui_shaders
        COMPILER comet_shader_compiler
        OUTPUT_DIRECTORY "${COMET_UI_SHADER_DIRECTORY}"
        SOURCES
        "${CMAKE_SOURCE_DIR}/engine/shaders/ui/rml_ui.vert"
        "${CMAKE_SOURCE_DIR}/engine/shaders/ui/rml_ui.frag")

set(COMET_UI_FONT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src/ui/fonts")
set(COMET_UI_RESOURCE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/resources/ui")
add_custom_target(comet_game_ui_resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${CMAKE_SOURCE_DIR}/engine/resources/fonts" "${COMET_UI_FONT_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${CMAKE_SOURCE_DIR}/engine/resources/ui" "${COMET_UI_RESOURCE_DIRECTORY}"
        VERBATIM)
comet_add_module(game_ui)
set(COMET_UI_SETTINGS_FILE "${CMAKE_SOURCE_DIR}/engine/src/ui/settings.lua")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${COMET_UI_SETTINGS_FILE}")
file(READ "${COMET_UI_SETTINGS_FILE}" COMET_UI_SETTINGS_SOURCE)
set(COMET_UI_LUA_INCLUDE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src/ui/generated/include")
file(CONFIGURE OUTPUT "${COMET_UI_LUA_INCLUDE_DIRECTORY}/ui/settings_source.h"
        CONTENT "#pragma once\nnamespace Comet::Ui::Detail {\ninline constexpr char settings_source[] = R\"comet_ui(@COMET_UI_SETTINGS_SOURCE@)comet_ui\";\n}\n"
        @ONLY)
target_include_directories(comet_game_ui PRIVATE "${COMET_UI_LUA_INCLUDE_DIRECTORY}")
add_dependencies(comet_game_ui comet_game_ui_shaders comet_game_ui_resources)
target_include_directories(comet_game_ui PRIVATE "${COMET_UI_SHADER_DIRECTORY}/include")
target_include_directories(comet_game_ui SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/3rdparty/stb_image")
target_link_libraries(comet_game_ui PRIVATE comet_render comet_platform RmlUi::Core comet_lua)
target_compile_definitions(comet_game_ui PRIVATE
        COMET_UI_FONT_DIRECTORY="${COMET_UI_FONT_DIRECTORY}"
        COMET_UI_RESOURCE_DIRECTORY="${COMET_UI_RESOURCE_DIRECTORY}")
target_compile_options(comet_game_ui PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
