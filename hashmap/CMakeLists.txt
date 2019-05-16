cmake_minimum_required (VERSION 3.8)

add_executable(hashmap hashmap.c)
add_executable(hashmap_pp hashmap_pp.c)
add_executable(hashmap_pp_perf hashmap_pp_perf.c)
add_executable(hashmap_pp_perf_appendonly hashmap_pp_perf_appendonly.c)
add_executable(hashmap_fp hashmap_fp.c)
add_executable(hashmap_fp_perf hashmap_fp_perf.c)
add_executable(hashmap_fp_perf_appendonly hashmap_fp_perf_appendonly.c)

set(CMAKE_C_FLAGS "-g3 -march=native")
set(CMAKE_CXX_FLAGS "-g3 -march=native")
