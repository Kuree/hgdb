cmake_minimum_required(VERSION 3.2)
project(hgdb_python)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(extern/pybind11)
# check if the system has sqlite installed already
find_package(SQLite3)
if (NOT SQLite3_FOUND)
    add_subdirectory(../../extern/sqlite sqlite3)
    set(SQLite3_LIBRARIES sqlite3)
    set(SQLite3_INCLUDE_DIRS ../../extern/sqlite/include)
endif()

# check if the compiler support static lib gcc
# we need this because most
# only used for Linux
if(UNIX AND NOT APPLE)
    CHECK_CXX_COMPILER_FLAG(-static-libgcc COMPILER_STATIC_LIBGCC)
    if (${COMPILER_STATIC_LIBGCC})
        set(STATIC_GCC_FLAG "-static-libgcc")
    endif ()
    CHECK_CXX_COMPILER_FLAG(-static-libstdc++ COMPILER_STATIC_LIBCXX)
    if (${COMPILER_STATIC_LIBCXX})
        set(STATIC_CXX_FLAG "-static-libstdc++")
    endif ()
endif()

pybind11_add_module(_hgdb src/_hgdb.cc)
target_include_directories(_hgdb PRIVATE ../../extern/sqlite_orm/include
        ${SQLite3_INCLUDE_DIRS}
        ../../include)

target_link_libraries(_hgdb PRIVATE ${SQLite3_LIBRARIES} ${STATIC_GCC_FLAG} ${STATIC_CXX_FLAG})

