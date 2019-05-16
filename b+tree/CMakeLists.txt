cmake_minimum_required (VERSION 3.8)

project(PersistentApps LANGUAGES C CXX)


set(CMAKE_C_FLAGS "-g3 -march=native")
set(CMAKE_CXX_FLAGS "-g3")

add_executable(b+tree b+tree_intel.c)
add_executable(b+tree_appendonly b+tree_intel_appendonly.c)
add_executable(b+tree_p b+tree_intel_p.c)
add_executable(b+tree_p_appendonly b+tree_intel_p_appendonly.c)
target_link_libraries(b+tree -lm)
target_link_libraries(b+tree_appendonly -lm)
target_link_libraries(b+tree_p -lm)
target_link_libraries(b+tree_p_appendonly -lm)


set(CMAKE_C_FLAGS "-g3 -march=native")
set(CMAKE_CXX_FLAGS "-g3 -march=native")
