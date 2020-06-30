cmake_minimum_required (VERSION 3.10)

set (t_ example-sqlite3)
    add_executable (${t_} main.cxx)
    target_link_libraries (${t_} PRIVATE clask-core)
    target_include_directories(${t_} PUBLIC . ${SQLITE3_INCLUDE_DIR})
    target_link_libraries(${t_} PRIVATE ${SQLITE3_LIBRARIES})
