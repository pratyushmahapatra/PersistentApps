cmake_minimum_required (VERSION 3.8)

add_executable(trial_ds_64 trial_ds_64B.c)
add_executable(trial_ds_32 trial_ds_32B.c)
add_executable(trial_ds_16 trial_ds_16B.c)
add_executable(trial_ds_8 trial_ds_8B.c)

set(CMAKE_C_FLAGS "-g3 -march=native")
set(CMAKE_CXX_FLAGS "-g3 -march=native")
