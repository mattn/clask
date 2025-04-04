cmake_minimum_required (VERSION 3.10)

set (t_ example-file)
    add_executable (${t_} main.cxx)
    target_link_libraries (${t_} PRIVATE clask-core)
