cmake_minimum_required (VERSION 3.10)

project (clask
         LANGUAGES CXX C)

if(MSVC_VERSION GREATER_EQUAL "1900")
  include(CheckCXXCompilerFlag)
  CHECK_CXX_COMPILER_FLAG("/std:c++latest" _cpp_latest_flag_supported)
  if(_cpp_latest_flag_supported)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest")
  endif()
endif()

if(MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest -DNOMINMAX")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  endif()
elseif(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-long-long")
endif()

find_package(Threads REQUIRED)

set (t_ clask-settings)
    add_library (${t_} INTERFACE)
if(NOT MSVC)
    target_compile_features (${t_} INTERFACE cxx_std_17)
endif()
if(WIN32)
    target_link_libraries(${t_} INTERFACE ws2_32)
endif()
    target_link_libraries(${t_} INTERFACE Threads::Threads)
    target_link_libraries(${t_} INTERFACE stdc++fs)

add_subdirectory (clask)

set (t_ main)
    add_executable (${t_} main.cxx)
    target_link_libraries (${t_} PRIVATE clask-core)

set (t_ clask_test)
    add_executable (${t_} test.cxx picotest/picotest.c)
    target_link_libraries (${t_} PRIVATE clask-core)

add_subdirectory (example)
