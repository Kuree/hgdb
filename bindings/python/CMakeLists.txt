cmake_minimum_required(VERSION 3.4)
project(hgdb_python)

add_subdirectory(extern/pybind11)
# check if the system has sqlite installed already
find_package(SQLite3)
if (NOT SQLite3_FOUND)
    add_subdirectory(../../extern/sqlite sqlite3)
    set(SQLite3_LIBRARIES sqlite3)
    set(SQLite3_INCLUDE_DIRS ../../extern/sqlite/include)
endif()

pybind11_add_module(_hgdb src/_hgdb.cc)
target_include_directories(_hgdb PRIVATE ../../extern/sqlite_orm/include
        ${SQLite3_INCLUDE_DIRS}
        ../../include)
target_link_libraries(_hgdb PRIVATE ${SQLite3_LIBRARIES})