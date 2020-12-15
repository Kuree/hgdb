cmake_minimum_required(VERSION 3.8)
project(hgdb)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(src)

# tests
add_subdirectory(tests)