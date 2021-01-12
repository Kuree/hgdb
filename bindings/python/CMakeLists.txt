cmake_minimum_required(VERSION 3.4)
project(hgdb_python)

add_subdirectory(extern/pybind11)
add_subdirectory(../../extern/sqlite sqlite3)
pybind11_add_module(_hgdb src/_hgdb.cc)
target_include_directories(_hgdb PRIVATE ../../extern/sqlite_orm/include
        ../../extern/sqlite/include
        ../../include)
target_link_libraries(_hgdb PRIVATE sqlite3)