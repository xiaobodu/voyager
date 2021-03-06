cmake_minimum_required(VERSION 2.8.7)

if (POLICY CMP0042)
  cmake_policy(SET CMP0042 NEW)
endif()

project (Voyager C CXX)

set(VOYAGER_VERSION_MAJOR 1)
set(VOYAGER_VERSION_MINOR 0)
set(VOYAGER_VERSION_PATCH 1)
set(VOYAGER_VERSION_STRING ${VOYAGER_VERSION_MAJOR}.${VOYAGER_VERSION_MINOR}.${VOYAGER_VERSION_PATCH})

function(exclude OUTPUT INPUT)
  set(EXCLUDES ${ARGN})
  foreach(EXCLUDE ${EXCLUDES})
    list(REMOVE_ITEM INPUT "${EXCLUDE}")
  endforeach()
  set(${OUTPUT} ${INPUT} PARENT_SCOPE)
endfunction(exclude)

enable_testing()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "release")
endif()

if(NOT CMAKE_INSTALL_DIR)
  set(CMAKE_INSTALL_DIR "/usr/local")
endif()

option(BUILD_HTTP "Build voyager http" on)
option(BUILD_TESTS "Build voyager perftests and unittests" on)
option(BUILD_EXAMPLES "Build voyager examples" on)
option(BUILD_SHARED_LIBS "Build voyager shared libraries" on)

set(CXX_FLAGS
  -g
  -DCHECK_PTHREAD_RETURN_VALUE
  -D_FILE_OFFSET_BITS=64
  -Wall
  -Wextra
  -Wconversion
  -Wno-unused-parameter
  -Woverloaded-virtual
  -Wpointer-arith
  -Wwrite-strings
  -Wshadow
  -march=native
  -std=c++11
  -Wno-sign-conversion
  )

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  list(APPEND CXX_FLAGS "-rdynamic")
endif()

if(CMAKE_BUILD_BITS EQUAL 32)
  list(APPEND CXX_FLAGS "-m32")
endif()

string(REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set(CMAKE_CXX_COMPILER "g++")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(CMAKE_CXX_COMPILER "clang++")
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-O0")

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set(CMAKE_CXX_FLAGS_RELEASE "-O2 -finline-limit=1000 -DNDEBUG")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
endif()

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
set(LIBARARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

string(TOUPPER ${CMAKE_BUILD_TYPE} BUILD_TYPE)

message(STATUS "CXX_FLAGS = " ${CMAKE_CXX_FLAGS} " " ${CMAKE_CXX_FLAGS_${BUILD_TYPE}})

include_directories(${PROJECT_SOURCE_DIR})

add_subdirectory(voyager)

if (BUILD_EXAMPLES)
  add_subdirectory(examples/sudoku)
  add_subdirectory(examples/pingpong)
  add_subdirectory(benchmarks)
endif()
