cmake_minimum_required(VERSION 3.31)
project(App)

set(CMAKE_CXX_STANDARD 20)

set(SOURCES main.cpp)

add_executable(app ${SOURCES})
target_link_libraries(app PRIVATE engine)
target_compile_definitions(app PRIVATE
        COMET_CONFIG_DIRECTORY="${CMAKE_SOURCE_DIR}/engine/assets/config"
        COMET_DEFAULT_CONFIG_PROFILE="${COMET_CONFIG_PROFILE}"
)
