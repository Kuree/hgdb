add_executable(test_mock test_mock.cc)
target_link_libraries(test_mock PRIVATE hgdb)
target_include_directories(test_mock PRIVATE
        ../include
        ../src)
