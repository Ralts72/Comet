cmake_minimum_required(VERSION 3.31)
project(App)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES main.cpp)

add_executable(app ${SOURCES})
target_link_libraries(app PUBLIC engine)