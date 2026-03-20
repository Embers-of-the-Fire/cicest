add_executable(cstc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
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
