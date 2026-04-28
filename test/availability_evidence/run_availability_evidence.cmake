cmake_minimum_required(VERSION 3.21)

foreach(required_var IN ITEMS
    AVAILABILITY_EVIDENCE_SOURCE_DIR
    AVAILABILITY_EVIDENCE_BINARY_DIR
    CICEST_SOURCE_DIR
    CSTC_BINARY
    CSTC_INSPECT_BINARY
)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "Missing required variable ${required_var}")
    endif()
endforeach()

function(read_pattern_spec spec_path)
    set(spec_source "")
    set(spec_out_type "tyir")
    set(spec_rule "unspecified")
    set(spec_patterns "")

    file(STRINGS "${spec_path}" spec_lines)
    foreach(spec_line IN LISTS spec_lines)
        if(spec_line MATCHES "^# source: (.+)$")
            set(spec_source "${CMAKE_MATCH_1}")
        elseif(spec_line MATCHES "^# out-type: (.+)$")
            set(spec_out_type "${CMAKE_MATCH_1}")
        elseif(spec_line MATCHES "^# rule: (.+)$")
            set(spec_rule "${CMAKE_MATCH_1}")
        elseif(spec_line MATCHES "^#" OR spec_line STREQUAL "")
            continue()
        else()
            list(APPEND spec_patterns "${spec_line}")
        endif()
    endforeach()

    if(spec_source STREQUAL "")
        message(FATAL_ERROR "Pattern spec is missing '# source: ...': ${spec_path}")
    endif()
    if(spec_patterns STREQUAL "")
        message(FATAL_ERROR "Pattern spec has no expected patterns: ${spec_path}")
    endif()

    set(PATTERN_SPEC_SOURCE "${spec_source}" PARENT_SCOPE)
    set(PATTERN_SPEC_OUT_TYPE "${spec_out_type}" PARENT_SCOPE)
    set(PATTERN_SPEC_RULE "${spec_rule}" PARENT_SCOPE)
    set(PATTERN_SPEC_PATTERNS "${spec_patterns}" PARENT_SCOPE)
endfunction()

function(resolve_source_path source_path)
    if(IS_ABSOLUTE "${source_path}")
        set(resolved_source_path "${source_path}")
    else()
        set(resolved_source_path "${CICEST_SOURCE_DIR}/${source_path}")
    endif()

    if(NOT EXISTS "${resolved_source_path}")
        message(FATAL_ERROR "Evidence source does not exist: ${resolved_source_path}")
    endif()

    set(RESOLVED_SOURCE_PATH "${resolved_source_path}" PARENT_SCOPE)
endfunction()

function(assert_ordered_patterns spec_path actual_output)
    set(search_offset 0)
    foreach(pattern IN LISTS ARGN)
        string(SUBSTRING "${actual_output}" ${search_offset} -1 remaining_output)
        string(FIND "${remaining_output}" "${pattern}" pattern_index)
        if(pattern_index EQUAL -1)
            message(FATAL_ERROR
                "Expected pattern not found in ${spec_path}\n"
                "Pattern:\n${pattern}\n")
        endif()

        string(LENGTH "${pattern}" pattern_length)
        math(EXPR search_offset "${search_offset} + ${pattern_index} + ${pattern_length}")
    endforeach()
endfunction()

file(GLOB tyir_specs "${AVAILABILITY_EVIDENCE_SOURCE_DIR}/tyir/*.patterns")
file(GLOB diagnostic_specs "${AVAILABILITY_EVIDENCE_SOURCE_DIR}/diagnostics/*.patterns")
list(SORT tyir_specs)
list(SORT diagnostic_specs)

set(total_specs 0)
set(passed_specs 0)

foreach(spec_path IN LISTS tyir_specs)
    math(EXPR total_specs "${total_specs} + 1")
    read_pattern_spec("${spec_path}")
    resolve_source_path("${PATTERN_SPEC_SOURCE}")

    execute_process(
        COMMAND "${CSTC_INSPECT_BINARY}" "${RESOLVED_SOURCE_PATH}" --out-type "${PATTERN_SPEC_OUT_TYPE}"
        RESULT_VARIABLE inspect_result
        OUTPUT_VARIABLE inspect_stdout
        ERROR_VARIABLE inspect_stderr
    )

    if(NOT inspect_result EQUAL 0)
        message(FATAL_ERROR
            "TyIR evidence failed for ${PATTERN_SPEC_RULE}: ${PATTERN_SPEC_SOURCE}\n"
            "Exit code: ${inspect_result}\n"
            "Stderr:\n${inspect_stderr}")
    endif()

    assert_ordered_patterns("${spec_path}" "${inspect_stdout}" ${PATTERN_SPEC_PATTERNS})
    math(EXPR passed_specs "${passed_specs} + 1")
endforeach()

set(diagnostic_output_dir "${AVAILABILITY_EVIDENCE_BINARY_DIR}/diagnostics")
file(MAKE_DIRECTORY "${diagnostic_output_dir}")

foreach(spec_path IN LISTS diagnostic_specs)
    math(EXPR total_specs "${total_specs} + 1")
    read_pattern_spec("${spec_path}")
    resolve_source_path("${PATTERN_SPEC_SOURCE}")

    get_filename_component(spec_name "${spec_path}" NAME_WE)
    set(output_stem "${diagnostic_output_dir}/${spec_name}")

    execute_process(
        COMMAND "${CSTC_BINARY}" "${RESOLVED_SOURCE_PATH}" -o "${output_stem}" --emit exe
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_stdout
        ERROR_VARIABLE compile_stderr
    )

    if(compile_result EQUAL 0)
        message(FATAL_ERROR
            "Diagnostic evidence expected compilation to fail for ${PATTERN_SPEC_RULE}: "
            "${PATTERN_SPEC_SOURCE}")
    endif()

    assert_ordered_patterns("${spec_path}" "${compile_stderr}" ${PATTERN_SPEC_PATTERNS})
    math(EXPR passed_specs "${passed_specs} + 1")
endforeach()

if(total_specs EQUAL 0)
    message(FATAL_ERROR "Availability evidence suite has no pattern specs")
endif()

message("Availability evidence summary\n  Specs: ${passed_specs}/${total_specs} passed")
