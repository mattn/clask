cmake_minimum_required (VERSION 3.15)

add_subdirectory (json)
add_subdirectory (login)
add_subdirectory (static)
add_subdirectory (upload)

find_package (SQLite3)
if (SQLITE3_FOUND)
  add_subdirectory (sqlite3)
endif (SQLITE3_FOUND)
