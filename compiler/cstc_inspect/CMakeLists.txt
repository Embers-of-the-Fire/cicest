add_executable(cstc_inspect
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

target_compile_definitions(cstc_inspect PRIVATE
    CICEST_STD_PATH="${CMAKE_SOURCE_DIR}/libraries/std"
)

target_link_libraries(cstc_inspect
    PRIVATE
        cstc_ast
        cstc_cli_support
        cstc_codegen
        cstc_lexer
        cstc_lir
        cstc_lir_builder
        cstc_parser
        cstc_resource_path
        cstc_tyir
        cstc_tyir_builder
        cicest_options
        cicest_warnings
)

install(TARGETS cstc_inspect
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
