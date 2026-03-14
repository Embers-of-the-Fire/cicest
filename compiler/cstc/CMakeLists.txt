add_executable(cstc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

target_link_libraries(cstc
    PRIVATE
        cstc_codegen
        cstc_lir_builder
        cstc_parser
        cstc_span
        cstc_symbol
        cstc_tyir_builder
        cicest_options
        cicest_warnings
)
