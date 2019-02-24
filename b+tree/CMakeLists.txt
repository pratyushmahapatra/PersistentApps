cmake_minimum_required (VERSION 3.1)

add_executable(b+tree b+tree.c)

set(CMAKE_C_FLAGS "-g3 -march=native")
set(CMAKE_CXX_FLAGS "-g3 -march=native")
