cmake_minimum_required(VERSION 3.31)
project(Test)

set(CMAKE_CXX_STANDARD 20)

add_executable(unit_testing test.cpp)
target_link_libraries(unit_testing PRIVATE engine gtest_main)

include(GoogleTest)
gtest_discover_tests(unit_testing)