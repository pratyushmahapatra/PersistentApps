cmake_minimum_required (VERSION 3.1)

add_executable(linkedlist linkedlist.c)
add_executable(linkedlist_persistent linkedlist_persistent.c)
add_executable(linkedlist_persistent_msync linkedlist_persistent_msync.c)
add_executable(linkedlist_pp_msync linkedlist_pp_msync.c)
add_executable(linkedlist_copyonflush linkedlist_copyonflush.c)
add_executable(linkedlist_volatile_mmap linkedlist_volatile_mmap.c)
add_executable(linkedlist_volatile_malloc linkedlist_volatile_malloc.c)
add_executable(linkedlist_volatile_mallocall linkedlist_volatile_mallocall.c)

set(CMAKE_C_FLAGS "-g -march=native")
set(CMAKE_CXX_FLAGS "-g -march=native")
