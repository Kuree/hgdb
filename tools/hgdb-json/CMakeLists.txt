add_executable(hgdb-json hgdb-json.cc)
target_link_libraries(hgdb-json hgdb vpi_dummy ${STATIC_GCC_FLAG} ${STATIC_CXX_FLAG})
target_include_directories(hgdb-json PRIVATE
        ../../extern/fmt/include
        ../../extern/argparse/include)
target_compile_definitions(hgdb-json PUBLIC VERSION_NUMBER=${VERSION_NUMBER})
