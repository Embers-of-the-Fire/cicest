cmake_minimum_required(VERSION 3.21)

foreach(required_var IN ITEMS E2E_RUNNER E2E_SOURCE_DIR E2E_BINARY_DIR)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "Missing required variable ${required_var}")
    endif()
endforeach()

set(e2e_suite_kinds
    pass
    fail_compile
    fail_runtime
)

set(e2e_report_dir "${E2E_BINARY_DIR}/reports")
file(MAKE_DIRECTORY "${e2e_report_dir}")

set(e2e_total_tests 0)
set(e2e_total_passed 0)
set(e2e_total_failed 0)
set(e2e_has_failures FALSE)

set(e2e_summary "End-to-end summary\n")
list(LENGTH e2e_suite_kinds e2e_suite_count)
string(APPEND e2e_summary "  Suites: ${e2e_suite_count}\n")

set(e2e_failures "")

foreach(kind IN LISTS e2e_suite_kinds)
    set(test_dir "${E2E_SOURCE_DIR}/${kind}")
    set(report_path "${e2e_report_dir}/${kind}.json")
    file(REMOVE "${report_path}")

    execute_process(
        COMMAND
            "${E2E_RUNNER}"
            --quiet
            --test-dir "${test_dir}"
            --kind "${kind}"
            --report "${report_path}"
        RESULT_VARIABLE suite_exit_code
        OUTPUT_VARIABLE suite_stdout
        ERROR_VARIABLE suite_stderr
    )

    if(NOT EXISTS "${report_path}")
        string(STRIP "${suite_stderr}" suite_stderr)
        if(suite_stderr STREQUAL "")
            set(suite_stderr "runner did not produce a report")
        endif()
        message(FATAL_ERROR "Failed to run '${kind}' suite: ${suite_stderr}")
    endif()

    file(READ "${report_path}" suite_report_json)
    string(JSON suite_total GET "${suite_report_json}" total)
    string(JSON suite_passed GET "${suite_report_json}" passed)
    string(JSON suite_failed GET "${suite_report_json}" failed)

    math(EXPR e2e_total_tests "${e2e_total_tests} + ${suite_total}")
    math(EXPR e2e_total_passed "${e2e_total_passed} + ${suite_passed}")
    math(EXPR e2e_total_failed "${e2e_total_failed} + ${suite_failed}")

    string(APPEND e2e_summary "  ${kind}: ${suite_passed}/${suite_total} passed")
    if(NOT suite_failed EQUAL 0)
        string(APPEND e2e_summary " (${suite_failed} failed)")
        set(e2e_has_failures TRUE)
    endif()
    string(APPEND e2e_summary "\n")

    if(NOT suite_exit_code EQUAL 0)
        set(e2e_has_failures TRUE)
    endif()

    string(JSON suite_test_count LENGTH "${suite_report_json}" tests)
    if(suite_test_count GREATER 0)
        math(EXPR suite_last_index "${suite_test_count} - 1")
        foreach(test_index RANGE ${suite_last_index})
            string(JSON test_status GET "${suite_report_json}" tests ${test_index} status)
            if(test_status STREQUAL "passed")
                continue()
            endif()

            string(JSON test_name GET "${suite_report_json}" tests ${test_index} name)
            string(JSON test_path GET "${suite_report_json}" tests ${test_index} path)
            string(JSON test_error GET "${suite_report_json}" tests ${test_index} error)

            if(e2e_failures STREQUAL "")
                set(e2e_failures "Failures:\n")
            endif()

            string(APPEND e2e_failures "  [${kind}] ${test_name}\n")
            string(APPEND e2e_failures "    ${test_path}\n")
            if(NOT test_error STREQUAL "")
                string(REPLACE "\n" "\n    " test_error "${test_error}")
                string(APPEND e2e_failures "    ${test_error}\n")
            endif()
        endforeach()
    endif()
endforeach()

string(APPEND e2e_summary
    "  Tests: ${e2e_total_tests} total, ${e2e_total_passed} passed, ${e2e_total_failed} failed")
message("${e2e_summary}")

if(NOT e2e_failures STREQUAL "")
    message("${e2e_failures}")
endif()

if(e2e_has_failures)
    message(FATAL_ERROR "End-to-end test suite failed")
endif()
