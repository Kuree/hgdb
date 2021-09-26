add_library(hgdb-replay vcd_db.cc vpi.cc engine.cc)
target_link_libraries(hgdb-replay hgdb hgdb-vcd)

target_include_directories(hgdb-replay PUBLIC ../../src
        ../vcd)

add_executable(hgdb-replay-bin hgdb-replay.cc vpi_dummy.cc)
target_include_directories(hgdb-replay-bin PRIVATE ../../extern/argparse/include)

if (DEFINED ENV{VERDI_HOME})
    target_link_libraries(hgdb-replay-bin hgdb-replay fsdb ${STATIC_GCC_FLAG} ${STATIC_CXX_FLAG})
    target_compile_definitions(hgdb-replay PUBLIC USE_FSDB VERSION_NUMBER=${VERSION_NUMBER})
else()
    target_link_libraries(hgdb-replay-bin hgdb-replay ${STATIC_GCC_FLAG} ${STATIC_CXX_FLAG})
    target_compile_definitions(hgdb-replay PUBLIC VERSION_NUMBER=${VERSION_NUMBER})
endif()

set_target_properties(hgdb-replay-bin PROPERTIES OUTPUT_NAME hgdb-replay)