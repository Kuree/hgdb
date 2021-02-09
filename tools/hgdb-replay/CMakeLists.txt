add_library(hgdb-replay vcd_db.cc vpi.cc engine.cc)
target_link_libraries(hgdb-replay hgdb)
target_include_directories(hgdb-replay PUBLIC ../../src)

add_executable(hgdb-replay-bin hgdb-replay.cc vpi_dummy.cc)
target_link_libraries(hgdb-replay-bin hgdb-replay)
set_target_properties(hgdb-replay-bin PROPERTIES OUTPUT_NAME hgdb-replay)