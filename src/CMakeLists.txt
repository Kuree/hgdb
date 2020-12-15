add_library(hgdb db.cc)

target_include_directories(hgdb PRIVATE
        ../include
        ../extern/sqlite_orm/include
        ../extern/rapidjson/include
        ../extern/websocketpp)
