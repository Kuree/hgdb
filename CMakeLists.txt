cmake_minimum_required(VERSION 3.8)
project(hgdb)

set(CMAKE_CXX_STANDARD 17)

# extern dependencies
add_subdirectory(extern/fmt)
add_subdirectory(extern/sqlite)
add_subdirectory(extern/spdlog)

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