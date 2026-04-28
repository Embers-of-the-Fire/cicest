add_test(NAME availability_evidence
    COMMAND ${CMAKE_COMMAND}
        -DAVAILABILITY_EVIDENCE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DAVAILABILITY_EVIDENCE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DCICEST_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DCSTC_BINARY=$<TARGET_FILE:cstc>
        -DCSTC_INSPECT_BINARY=$<TARGET_FILE:cstc_inspect>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_availability_evidence.cmake
)

set_tests_properties(availability_evidence PROPERTIES
    LABELS "availability;evidence"
)
