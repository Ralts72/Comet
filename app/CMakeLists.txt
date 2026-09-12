set(SOURCES main.cpp)

add_executable(app ${SOURCES})
target_link_libraries(app PRIVATE engine)
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
