add_executable(cstc_inspect
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

target_link_libraries(cstc_inspect
    PRIVATE
        cstc_ast
        cstc_lexer
        cstc_parser
        cstc_tyir
        cstc_tyir_builder
        cicest_options
        cicest_warnings
)
