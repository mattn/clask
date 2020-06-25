cmake_minimum_required (VERSION 3.15)

set (t_ example-json)
    add_executable (${t_} main.cxx)
    target_link_libraries (${t_} PRIVATE clask-core)
    target_include_directories(${t_} PUBLIC .)
    file(COPY ${CMAKE_SOURCE_DIR}/example/json/public DESTINATION ${CMAKE_BINARY_DIR}/example/json)
