cmake_minimum_required(VERSION 3.12)
project(hgdb)

set(CMAKE_CXX_STANDARD 20)

# global options
# set -fPIC everywhere
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# extern dependencies
add_subdirectory(extern/fmt)
add_subdirectory(extern/sqlite)

# system dependencies
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_subdirectory(src)

# tests
add_subdirectory(extern/googletest)
include(GoogleTest)
include (CTest)
enable_testing()
add_subdirectory(tests)