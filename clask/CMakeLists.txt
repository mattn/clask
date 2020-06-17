
cmake_minimum_required (VERSION 3.17)

set (t_ clask-core)
    add_library (${t_} INTERFACE)
    target_include_directories (${t_} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries (${t_} INTERFACE clask-settings)
