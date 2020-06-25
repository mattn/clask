cmake_minimum_required (VERSION 3.15)

set (t_ example-upload)
    add_executable (${t_} main.cxx)
    target_link_libraries (${t_} PRIVATE clask-core)
    target_include_directories(${t_} PUBLIC .)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/files DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/public DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
