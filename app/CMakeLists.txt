add_library(comet_demo_ui STATIC src/player_input_menu.cpp)
target_include_directories(comet_demo_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(comet_demo_ui PUBLIC comet_game_ui PRIVATE RmlUi::Core)
target_compile_definitions(comet_demo_ui PRIVATE
        COMET_SAMPLE_UI_RESOURCE_ROOT="${CMAKE_SOURCE_DIR}/demo/assets")

if(NOT COMET_BUILD_APP)
    return()
endif()

set(SOURCES main.cpp)

add_executable(app ${SOURCES})
target_link_libraries(app PRIVATE engine comet_demo_ui)
comet_copy_runtime_dependencies(app)
target_compile_definitions(app PRIVATE
        COMET_CONFIG_DIRECTORY="${CMAKE_SOURCE_DIR}/config"
        COMET_CONFIG_PROFILE="${COMET_CONFIG_PROFILE}"
)
if (APPLE)
    set_target_properties(app PROPERTIES
            MACOSX_BUNDLE TRUE
            OUTPUT_NAME Comet
            MACOSX_BUNDLE_BUNDLE_NAME Comet
            MACOSX_BUNDLE_GUI_IDENTIFIER org.comet.app
            MACOSX_BUNDLE_ICON_FILE app.icns
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in")
    target_sources(app PRIVATE resources/icons/app.icns)
    set_source_files_properties(resources/icons/app.icns PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources)
elseif (WIN32)
    target_sources(app PRIVATE resources/app.rc)
    target_include_directories(app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    set_source_files_properties(resources/app.rc PROPERTIES
            OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.ico")
endif ()

if(APPLE)
    set(COMET_APP_RESOURCE_DIRECTORY "$<TARGET_BUNDLE_CONTENT_DIR:app>/Resources")
else()
    set(COMET_APP_RESOURCE_DIRECTORY "$<TARGET_FILE_DIR:app>/resources")
endif()
add_custom_target(comet_app_ui_resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${CMAKE_SOURCE_DIR}/demo/assets/ui" "${COMET_APP_RESOURCE_DIRECTORY}/ui"
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${CMAKE_SOURCE_DIR}/engine/resources/fonts" "${COMET_APP_RESOURCE_DIRECTORY}/fonts"
        VERBATIM)
add_dependencies(app comet_app_ui_resources)
