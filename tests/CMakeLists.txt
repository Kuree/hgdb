function(add_include target)
    target_include_directories(${target} PRIVATE
            ../include
            ../extern/googletest/googletest/include
            ../extern/vlstd/
            ../src)
endfunction()

function(setup_target target)
    target_link_libraries(${target} gtest hgdb gtest_main)
    add_include(${target})
    gtest_discover_tests(${target})
endfunction()

function(setup_target_cwd target cwd)
    target_link_libraries(${target} gtest hgdb gtest_main)
    add_include(${target})
    gtest_discover_tests(${target} WORKING_DIRECTORY ${cwd})
endfunction()

function(add_test target)
    add_executable(${target} ${target}.cc $<TARGET_OBJECTS:vpi_dummy>)
    setup_target(${target})
endfunction()

function(add_test_cwd target cwd)
    add_executable(${target} ${target}.cc $<TARGET_OBJECTS:vpi_dummy>)
    setup_target_cwd(${target} ${cwd})
endfunction()

function(add_mock target)
    add_executable(${target} ${target}.cc $<TARGET_OBJECTS:vpi_dummy>)
    target_link_libraries(${target} PRIVATE hgdb)
    add_include(${target})
endfunction()

add_library(vpi_dummy OBJECT vpi_dummy.cc)
add_include(vpi_dummy)

add_mock(test_ws_server)
add_mock(test_debug_server)

add_test(test_schema)
add_test(test_db)
add_test(test_debug)
add_test(test_rtl)
add_test(test_eval)
add_test(test_proto)
add_test(test_thread)
add_test(test_sim)
add_test(test_monitor)
add_test(test_scheduler)

# other tests
add_subdirectory(tools)
