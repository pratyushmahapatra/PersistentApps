cmake_minimum_required (VERSION 3.8)

project(PersistentApps LANGUAGES C CXX)


set(CMAKE_C_FLAGS "-g3 -march=native")
set(CMAKE_CXX_FLAGS "-g3")

add_executable(linkedlist_p linkedlist_intel_p.c)
add_executable(linkedlist linkedlist_intel.c)
add_executable(linkedlist_perf linkedlist_perf.c)
add_executable(linkedlist_perf_p linkedlist_perf_p.c)
add_executable(linkedlist_perf_appendonly linkedlist_perf_appendonly.c)
add_executable(linkedlist_perf_p_appendonly linkedlist_perf_p_appendonly.c)
