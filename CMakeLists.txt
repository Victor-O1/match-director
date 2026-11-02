cmake_minimum_required(VERSION 3.25)
project(match_director VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MD_BUILD_TESTS "Build unit tests" ON)
option(MD_BUILD_BENCH "Build benchmarks" ON)
option(MD_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
set(MD_SANITIZER "" CACHE STRING "Sanitizer build: '', 'asan' (ASAN+UBSAN) or 'tsan'")

include(cmake/Warnings.cmake)
include(cmake/Sanitizers.cmake)

add_subdirectory(src)

if(MD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(MD_BUILD_BENCH)
  add_subdirectory(bench)
endif()
