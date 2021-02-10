add_executable(hgdb-rewrite-vcd hgdb-rewrite-vcd.cc)
# we don't need hgdb linkage
target_link_libraries(hgdb-rewrite-vcd hgdb-vcd fmt)
target_include_directories(hgdb-rewrite-vcd PRIVATE
        ../../include
        ../../extern/fmt/include)