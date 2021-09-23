cmake_minimum_required(VERSION 3.12)
project(hgdb)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# global options
# set -fPIC everywhere
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(CheckCXXCompilerFlag)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

# extern dependencies
add_subdirectory(extern/fmt)
add_subdirectory(extern/sqlite)
add_subdirectory(extern/PEGTL)

# system dependencies
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
if (DEFINED ENV{VERDI_HOME})
    find_package(FSDB REQUIRED)
endif()

# set checker flags
include(cmake/SetFlags.cmake)

# main library
add_subdirectory(src)
# tools
add_subdirectory(tools)

# tests
add_subdirectory(extern/googletest)
include(GoogleTest)
include (CTest)
enable_testing()
add_subdirectory(tests)