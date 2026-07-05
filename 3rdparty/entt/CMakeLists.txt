cmake_minimum_required(VERSION 3.31)
project(EnTT)

add_library(entt INTERFACE)
target_include_directories(entt INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(EnTT::EnTT ALIAS entt)
