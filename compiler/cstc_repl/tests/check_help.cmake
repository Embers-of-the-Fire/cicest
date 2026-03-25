execute_process(
    COMMAND "${REPL_BINARY}" --help
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)

if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "cstc_repl --help exited with code ${result}\nstderr:\n${stderr}")
endif()

set(expected_usage "Usage: cstc_repl [-h|--help] [--linker <linker>]")
string(FIND "${stdout}" "${expected_usage}" usage_index)

if(usage_index EQUAL -1)
    message(
        FATAL_ERROR
        "missing expected usage banner\nexpected: ${expected_usage}\nstdout:\n${stdout}\nstderr:\n${stderr}"
    )
endif()
