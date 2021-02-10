add_executable(hgdb-rewrite-vcd hgdb-rewrite-vcd.cc)
# we don't need hgdb linkage
target_link_libraries(hgdb-rewrite-vcd hgdb-vcd fmt sqlite3 Threads::Threads)
target_include_directories(hgdb-rewrite-vcd PRIVATE
        ../../include
        ../../extern/fmt/include
        ../../extern/sqlite_orm/include
        ../../extern/sqlite/include)