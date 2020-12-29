add_library(hgdb db.cc debug.cc server.cc util.cc rtl.cc eval.cc
        proto.cc log.cc thread.cc)

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
        ../extern/exprtk)

target_link_libraries(hgdb fmt sqlite3 Threads::Threads)

# check if the compiler support attributes like [[likely]]
set(CMAKE_REQUIRED_FLAGS "-Wall -Werror")
check_cxx_source_compiles(
        "int main() { if (1) [[likely]] {}}"
        SUPPORT_LIKELY
)
unset(CMAKE_REQUIRED_FLAGS)
if (NOT SUPPORT_LIKELY)
    set(EXTRA_WNO_FLAGS "-Wno-attributes")
endif()

# turn on as many warning flags as possible
target_compile_options(hgdb PRIVATE -Wall -Werror -Wpedantic ${EXTRA_WNO_FLAGS})
