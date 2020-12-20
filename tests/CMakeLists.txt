function(add_include target)
    target_include_directories(${target} PRIVATE
            ../include
            ../src)
endfunction()

function(setup_target target)
    target_link_libraries(${target} gtest hgdb gtest_main)
    add_include(${target})
    gtest_discover_tests(${target})
endfunction()

function(add_test target)
    add_executable(${target} ${target}.cc)
    setup_target(${target})
endfunction()

function(add_test_vpi target)
    add_executable(${target} ${target}.cc vpi_dummy.cc)
    setup_target(${target})
endfunction()

add_executable(test_ws_server test_ws_server.cc)
target_link_libraries(test_ws_server PRIVATE hgdb)
add_include(test_ws_server)

add_test(test_schema)
add_test(test_db)
add_test_vpi(test_rtl)
add_test(test_eval)
add_test(test_proto)