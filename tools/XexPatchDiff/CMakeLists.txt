cmake_minimum_required(VERSION 3.25)
project(xex_patch_diff LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(rexglue CONFIG REQUIRED)

add_executable(xex_patch_diff xex_patch_diff.cpp)
target_link_libraries(xex_patch_diff PRIVATE rex::runtime)
