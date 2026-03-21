add_executable(cstc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

target_include_directories(cstc
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_definitions(cstc PRIVATE
    CICEST_STD_PATH="${CMAKE_SOURCE_DIR}/libraries/std"
    CICEST_RT_PATH="$<TARGET_FILE:cicest_rt>"
)

target_link_libraries(cstc
    PRIVATE
        cstc_cli_support
        cstc_resource_path
        cstc_codegen
        cstc_lir_builder
        cstc_parser
        cstc_span
        cstc_symbol
        cstc_tyir_builder
        cicest_options
        cicest_warnings
)

if(CICEST_BUILD_TESTS)
    add_executable(cstc_linker_selection
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/linker_selection.cpp
    )

    target_include_directories(cstc_linker_selection
        PRIVATE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )

    target_link_libraries(cstc_linker_selection
        PRIVATE
            cicest_options
            cicest_warnings
    )

    add_test(NAME cstc_linker_selection COMMAND cstc_linker_selection)
endif()
