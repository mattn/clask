cmake_minimum_required (VERSION 3.15)

project (clask
         LANGUAGES CXX C)

find_package(Threads REQUIRED)

set (t_ clask-settings)
    add_library (${t_} INTERFACE)
    target_compile_features (${t_} INTERFACE cxx_std_17)
if(WIN32)
    target_link_libraries(${t_} INTERFACE ws2_32)
endif()
    target_link_libraries(${t_} INTERFACE Threads::Threads)

add_subdirectory (clask)

set (t_ main)
    add_executable (${t_} main.cxx)
    target_link_libraries (${t_} PRIVATE clask-core)

set (t_ clask_test)
    add_executable (${t_} test.cxx picotest/picotest.c)
    target_link_libraries (${t_} PRIVATE clask-core)

add_subdirectory (example)
