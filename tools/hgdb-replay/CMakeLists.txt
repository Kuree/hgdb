add_library(hgdb-replay vcd_db.cc vpi.cc engine.cc)
target_link_libraries(hgdb-replay hgdb hgdb-vcd)
target_include_directories(hgdb-replay PUBLIC ../../src
        ../vcd)

add_executable(hgdb-replay-bin hgdb-replay.cc vpi_dummy.cc)
target_link_libraries(hgdb-replay-bin hgdb-replay ${STATIC_GCC_FLAG} ${STATIC_CXX_FLAG})
set_target_properties(hgdb-replay-bin PROPERTIES OUTPUT_NAME hgdb-replay)