# all tool binaries can access the version
file (STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION VERSION_NUMBER)

add_subdirectory(vcd)
add_subdirectory(fsdb)
add_subdirectory(hgdb-replay)
add_subdirectory(hgdb-rewrite-vcd)