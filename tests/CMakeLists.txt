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

add_executable(test_mock test_mock.cc)
target_link_libraries(test_mock PRIVATE hgdb)
add_include(test_mock)

add_test(test_schema)