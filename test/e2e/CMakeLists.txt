add_executable(e2e_test_runner runner.cpp)
target_link_libraries(e2e_test_runner
    PRIVATE
        cstc_end_to_end
        cicest_options
        cicest_warnings
)
target_compile_definitions(e2e_test_runner PRIVATE
    CSTC_BINARY_PATH="$<TARGET_FILE:cstc>"
)

add_test(NAME e2e_pass
    COMMAND e2e_test_runner --test-dir ${CMAKE_CURRENT_SOURCE_DIR}/pass --kind pass
)
add_test(NAME e2e_fail_compile
    COMMAND e2e_test_runner --test-dir ${CMAKE_CURRENT_SOURCE_DIR}/fail_compile --kind fail_compile
)
add_test(NAME e2e_fail_runtime
    COMMAND e2e_test_runner --test-dir ${CMAKE_CURRENT_SOURCE_DIR}/fail_runtime --kind fail_runtime
)
