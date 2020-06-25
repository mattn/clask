cmake_minimum_required (VERSION 3.15)

set (t_ example-static)
    add_executable (${t_} main.cxx)
    target_link_libraries (${t_} PRIVATE clask-core)
    file(COPY ${CMAKE_SOURCE_DIR}/example/static/public DESTINATION ${CMAKE_BINARY_DIR}/example/static)
