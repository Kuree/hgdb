function(add_include target)
    target_include_directories(${target} PRIVATE
            ../include
            ../src)
endfunction()

function(add_test target)
    add_executable(${target} ${target}.cc)
    target_link_libraries(${target} gtest hgdb gtest_main)
    add_include(${target})
    gtest_discover_tests(${target})
endfunction()

add_executable(test_debug_server test_debug_server.cc)
target_link_libraries(test_debug_server PRIVATE hgdb)
add_include(test_debug_server)

add_test(test_schema)