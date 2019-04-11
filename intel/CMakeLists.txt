cmake_minimum_required (VERSION 3.8)

project(PersistentApps LANGUAGES C CXX)


set(CMAKE_C_FLAGS "-g3 -march=native")
set(CMAKE_CXX_FLAGS "-g3")

add_executable(linkedlist_p linkedlist_intel_p.c)
add_executable(linkedlist linkedlist_intel.c)


add_executable(b+tree_intel b+tree_intel.c)
add_executable(b+tree_intel_p b+tree_intel_p.c)
target_link_libraries( b+tree_intel -lm )
target_link_libraries( b+tree_intel_p -lm )

# For linking ...
# Specify target & libraries to link it with
#target_link_libraries( problem2 -lnvToolsExt )

