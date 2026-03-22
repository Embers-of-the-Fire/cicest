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

add_test(NAME e2e
    COMMAND ${CMAKE_COMMAND}
        -DE2E_RUNNER=$<TARGET_FILE:e2e_test_runner>
        -DE2E_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DE2E_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_e2e.cmake
)
