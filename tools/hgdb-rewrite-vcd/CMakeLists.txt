add_executable(hgdb-rewrite-vcd hgdb-rewrite-vcd.cc)
# we don't need hgdb linkage
target_link_libraries(hgdb-rewrite-vcd hgdb-vcd fmt sqlite3 Threads::Threads ${STATIC_GCC_FLAG} ${STATIC_CXX_FLAG})
target_include_directories(hgdb-rewrite-vcd PRIVATE
        ../../include
        ../../extern/fmt/include
        ../../extern/sqlite_orm/include
        ../../extern/sqlite/include
        ../../extern/argparse/include)
target_compile_definitions(hgdb-rewrite-vcd PUBLIC VERSION_NUMBER=${VERSION_NUMBER})
