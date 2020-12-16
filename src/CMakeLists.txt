add_library(hgdb db.cc debug.cc server.cc)

target_include_directories(hgdb PUBLIC
        ../include
        ../extern/fmt/include
        ../extern/sqlite/include
        ../extern/sqlite_orm/include
        ../extern/rapidjson/include
        ../extern/websocketpp)

target_link_libraries(hgdb fmt sqlite3 Threads::Threads)
