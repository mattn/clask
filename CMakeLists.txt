
cmake_minimum_required (VERSION 3.17)

project (clask
         LANGUAGES CXX C)

set (t_ clask-settings)
    add_library (${t_} INTERFACE)
    target_compile_features (${t_} INTERFACE cxx_std_17)

add_subdirectory (clask)

set (t_ main)
    add_executable (${t_} main.cxx)
    target_link_libraries (${t_} PRIVATE clask-core)

set (t_ clask_test)
    add_executable (${t_} test.cxx)
    target_link_libraries (${t_} PRIVATE clask-core)
