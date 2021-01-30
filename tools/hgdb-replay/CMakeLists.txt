add_executable(hgdb-replay hgdb-replay vcd.cc)
target_link_libraries(hgdb-replay hgdb)
target_include_directories(hgdb-replay PRIVATE ../../src)