add_executable(hgdb-db hgdb-db.cc)
target_link_libraries(hgdb-db hgdb vpi_dummy ${STATIC_GCC_FLAG} ${STATIC_CXX_FLAG})
target_include_directories(hgdb-db PRIVATE
        ../../extern/fmt/include
        ../../extern/cli/include)
target_compile_definitions(hgdb-db PUBLIC VERSION_NUMBER=${VERSION_NUMBER})
