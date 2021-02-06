add_library(hgdb SHARED db.cc debug.cc server.cc util.cc rtl.cc eval.cc
        proto.cc log.cc thread.cc sim.cc monitor.cc scheduler.cc)

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
        ../extern/PEGTL/include)

# check if the compiler support static lib gcc
# we need this because most
CHECK_CXX_COMPILER_FLAG(-static-libgcc COMPILER_STATIC_LIBGCC)
if (${COMPILER_STATIC_LIBGCC})
    set(STATIC_GCC_FLAG "-static-libgcc")
endif ()
CHECK_CXX_COMPILER_FLAG(-static-libstdc++ COMPILER_STATIC_LIBCXX)
if (${COMPILER_STATIC_LIBCXX})
    set(STATIC_CXX_FLAG "-static-libstdc++")
endif ()

target_link_libraries(hgdb fmt sqlite3 Threads::Threads ${STATIC_GCC_FLAG} ${STATIC_CXX_FLAG} taocpp::pegtl)

# check if the compiler support attributes like [[likely]]
set(CMAKE_REQUIRED_FLAGS "-Wall -Werror")
check_cxx_source_compiles(
        "int main() { if (1) [[likely]] {}}"
        SUPPORT_LIKELY
)
unset(CMAKE_REQUIRED_FLAGS)
if (NOT SUPPORT_LIKELY)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(EXTRA_FLAGS -Wno-unknown-attributes"")
    else ()
        set(EXTRA_FLAGS "-Wno-attributes")
    endif ()
endif ()

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