add_test_cwd(test_hgdb_replay ${CMAKE_CURRENT_SOURCE_DIR}/vectors)
target_link_libraries(test_hgdb_replay hgdb-replay)