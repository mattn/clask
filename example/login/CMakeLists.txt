cmake_minimum_required (VERSION 3.15)

set (t_ example-login)
    add_executable (${t_} main.cxx)
    target_link_libraries (${t_} PRIVATE clask-core)
    target_include_directories(${t_} PUBLIC .)
    file(COPY ${CMAKE_SOURCE_DIR}/example/sqlite3/index.html DESTINATION ${CMAKE_BINARY_DIR}/example/sqlite3)
    file(COPY ${CMAKE_SOURCE_DIR}/example/sqlite3/schema.sql DESTINATION ${CMAKE_BINARY_DIR}/example/sqlite3)
