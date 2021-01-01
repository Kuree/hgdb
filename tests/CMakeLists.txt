function(add_include target)
    target_include_directories(${target} PRIVATE
            ../include
            ../extern/googletest/googletest/include
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

function(add_mock target)
    add_executable(${target} ${target}.cc vpi_dummy.cc)
    target_link_libraries(${target} PRIVATE hgdb)
    add_include(${target})
endfunction()

add_mock(test_ws_server)
add_mock(test_debug_server)

add_test(test_schema)
add_test(test_db)
add_test_vpi(test_rtl)
add_test(test_eval)
add_test(test_proto)
add_test(test_thread)
add_test_vpi(test_sim)