
cmake_minimum_required (VERSION 3.16)

set (t_ example-json)
    add_executable (${t_} main.cxx)
    target_link_libraries (${t_} PRIVATE clask-core)
