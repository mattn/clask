

cmake_minimum_required (VERSION 3.16)

set (t_ example-sqlite3)
    add_executable (${t_} main.cxx)
    target_link_libraries (${t_} PRIVATE clask-core)
    target_include_directories(${t_} PUBLIC .)
if(WIN32)
    target_link_libraries(${t_} PRIVATE sqlite3)
endif()
