add_library(hgdb-replay vcd.cc vpi.cc)
target_link_libraries(hgdb-replay hgdb)
target_include_directories(hgdb-replay PRIVATE ../../src)

add_executable(hgdb-replay-bin hgdb-replay.cc)
target_link_libraries(hgdb-replay-bin hgdb-replay)
set_target_properties(hgdb-replay-bin PROPERTIES OUTPUT_NAME hgdb-replay)