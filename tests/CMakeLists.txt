cmake_minimum_required(VERSION 3.31)
project(Test)

set(CMAKE_CXX_STANDARD 20)

add_executable(test_runner test.cpp)
target_link_libraries(test_runner PRIVATE engine gtest_main)

include(GoogleTest)
gtest_discover_tests(test_runner)