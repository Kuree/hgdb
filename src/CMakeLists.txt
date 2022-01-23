add_library(hgdb SHARED db.cc debug.cc server.cc util.cc rtl.cc eval.cc
        proto.cc log.cc thread.cc sim.cc monitor.cc scheduler.cc symbol.cc)

target_compile_definitions(hgdb PUBLIC ASIO_STANDALONE)

target_include_directories(hgdb PUBLIC
        ../include
        ../extern/fmt/include
        ../extern/sqlite/include
        ../extern/sqlite_orm/include
        ../extern/rapidjson/include
        ../extern/asio/asio/include/
        ../extern/websocketpp
        ../extern/vlstd
        ../extern/exprtk
        ../extern/PEGTL/include
        ../extern/valijson/include
        ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(hgdb fmt sqlite3 Threads::Threads ${STATIC_GCC_FLAG} ${STATIC_CXX_FLAG} taocpp::pegtl)

# turn on as many warning flags as possible
target_compile_options(hgdb PRIVATE -Wall -Werror -Wpedantic ${EXTRA_FLAGS})

# okay here is the part that's always painful for me to debug since I don't have access to macos
if (APPLE)
    set_target_properties(hgdb PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
endif ()

# clang-tidy
if (${USE_CLANG_TIDY})
    MESSAGE("-- Running clang-tidy")
    set_target_properties(hgdb PROPERTIES CXX_CLANG_TIDY "clang-tidy;-extra-arg=-Wno-unknown-attributes")
endif ()

# read out the json schema as a variable
file (READ ${CMAKE_CURRENT_SOURCE_DIR}/../include/schema.json JSON_SCHEMA)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/jschema.hh.in ${CMAKE_CURRENT_BINARY_DIR}/jschema.hh)
